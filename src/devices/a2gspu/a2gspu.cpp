/*
 *   A2GSPU - Apple IIgs Graphics and Sound Processing Unit
 *   Emulator device plugin for GSSquared
 *
 *   Unified write observer feeds both backends:
 *   - HW: streams bus writes to physical RP2350 card over USB serial
 *   - EMU: shadows SHR memory for local UHR rendering via SDL3
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#include "computer.hpp"
#include "cpu.hpp"
#include "a2gspu.hpp"
#include "mmus/mmu_iigs.hpp"
#include "display/display.hpp"
#include "videosystem.hpp"
#include "devices/adb/keygloo.hpp"
#include "devices/displaypp/VideoScannerII.hpp"
#include "Module_ID.hpp"
#include "bus_protocol.h"
#include <SDL3_net/SDL_net.h>

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Serial port helpers ────────────────────────────────────────────

static void *serial_open(const char *port) {
#ifdef _WIN32
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\%s", port);
    // FILE_FLAG_OVERLAPPED: required for non-blocking WriteFile calls via
    // serial_write_async(). Without it, WriteFile blocks until the OS drains
    // the CDC TX buffer, stalling the entire emulation thread for up to 16ms
    // per frame on a saturated USB link.
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return nullptr;  // silent — reconnect loop would spam console otherwise
    }
    // Configure: 115200 baud (ignored by CDC, but required by API)
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    // DTR_CONTROL_ENABLE / RTS_CONTROL_ENABLE: some CDC-ACM firmware (including
    // TinyUSB defaults) gates the data path on these modem control signals.
    // Asserting both ensures the RP2350 CDC driver considers the port open and
    // ready to receive before the first frame is sent.
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(h, &dcb);
    // Set timeouts to match pyserial defaults
    COMMTIMEOUTS timeouts = {};
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &timeouts);
    // PurgeComm: clears stale bytes left in the OS and device buffers from any
    // previous session. Without this, the card's frame parser would see leftover
    // data prepended to the first sync header (0x7E 0x41 0x32 0x47), causing it
    // to reject the first several frames as corrupt.
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    printf("A2GSPU: Opened %s for streaming\n", port);
    return (void *)h;
#else
    (void)port;
    return nullptr;
#endif
}

// Async serial write using Windows overlapped I/O.
// Returns: 1=sent/queued, 0=skipped (busy), -1=error (caller should close handle).
//
// Why tri-state instead of bool: returning false for both "busy" and "error"
// caused the caller to close the handle on busy, triggering infinite reconnect
// loops (close → reopen → still pending from old handle → "error" → close...).
// Tri-state lets the caller distinguish: skip (0) preserves connection state,
// only real errors (-1) trigger reconnect.
//
// Why skip instead of block: the frame callback runs on the main emulation
// thread. Blocking on USB write would stall the entire emulator. Skipping
// keeps the emulator responsive — dirty tracking ensures the skipped frame's
// changes are re-detected and sent next time.
static int serial_write_async(void *handle, const uint8_t *data, uint32_t len) {
    if (!handle || len == 0) return -1;
#ifdef _WIN32
    // Static OVERLAPPED: reused across calls to avoid heap allocation per frame.
    // ov_handle tracks which HANDLE currently owns the OVERLAPPED. If the handle
    // changes (reconnect after hot-unplug), write_pending is cleared immediately.
    // Failure mode without this guard: after reconnect, ov_handle still refers to
    // the old (now closed) HANDLE. GetOverlappedResult on a closed handle is
    // undefined behavior — in practice it returns ERROR_INVALID_HANDLE, which
    // triggers a spurious -1 return and closes the brand-new handle too, causing
    // an infinite connect → error → close → reconnect loop.
    static OVERLAPPED ov = {};
    static bool ov_init = false;
    static bool write_pending = false;
    static void *ov_handle = nullptr;  // track which handle owns the OVERLAPPED
    if (!ov_init) {
        ov.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
        ov_init = true;
    }
    // Reset pending state if handle changed (reconnect)
    if (handle != ov_handle) {
        write_pending = false;
        ov_handle = handle;
        ResetEvent(ov.hEvent);
    }
    if (write_pending) {
        if (WaitForSingleObject(ov.hEvent, 0) == WAIT_TIMEOUT) return 0; // busy — skip
        // Check if completed write succeeded
        DWORD completed = 0;
        if (!GetOverlappedResult((HANDLE)handle, &ov, &completed, FALSE)) {
            write_pending = false;
            return -1; // real I/O error
        }
        write_pending = false;
    }
    ResetEvent(ov.hEvent);
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    DWORD written = 0;
    if (!WriteFile((HANDLE)handle, data, len, &written, &ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            write_pending = true;
        } else {
            write_pending = false;
            return -1;
        }
    }
    return 1; // sent/queued
#else
    return -1;
#endif
}

static void serial_close(void *handle) {
    if (!handle) return;
#ifdef _WIN32
    CloseHandle((HANDLE)handle);
#endif
}

// Build mode_flags byte from display_state_t — the authoritative source.
// $C050-$C05F are "any access" switches (reads toggle them too).
// The MegaII updates display_state_t on both reads and writes, making it
// the ground truth. MMU_IIgs fields may lag behind.
static uint8_t build_mode_flags(display_state_t *ds, MMU_IIgs *mmu) {
    if (!ds) return BUS_MODE_TEXT;
    uint8_t flags = 0;
    if (ds->display_mode == TEXT_MODE)           flags |= BUS_MODE_TEXT;
    if (ds->display_graphics_mode == HIRES_MODE) flags |= BUS_MODE_HIRES;
    if (ds->display_split_mode == SPLIT_SCREEN)  flags |= BUS_MODE_MIXED;
    if (ds->display_page_num == DISPLAY_PAGE_2)  flags |= BUS_MODE_PAGE2;
    if (ds->f_80col)                             flags |= BUS_MODE_80COL;
    if (ds->f_altcharset)                        flags |= BUS_MODE_ALTCHAR;
    if (ds->f_double_graphics)                   flags |= BUS_MODE_DGR;
    if (mmu && mmu->is_80store())                flags |= BUS_MODE_80STORE;
    return flags;
}

// ── Key injection: poll command file for keyboard input ────────────
// Injects directly into KeyGloo's ADB buffer — does NOT touch the
// user's real keyboard or mouse. File is consumed after reading.
// Keys are queued and drained a few per frame to avoid overflowing
// the 16-entry ADB key buffer.
static const char *CMD_FILE = "a2gspu_keys.txt";
#define KEYS_PER_FRAME 4  // drain rate — ADB buffer is 16 deep

static void inject_keys(a2gspu_data *ad) {
    // Lazy-init KeyGloo: it may not be registered yet when the A2GSPU device
    // initializes (module registration order is not guaranteed). We retry on
    // every frame callback until it appears.
    //
    // static logged guard: without it this printf fires 60 times per second
    // whenever KeyGloo hasn't registered yet, flooding the console and making
    // other diagnostic output unreadable.
    //
    // kg->kg double-dereference: get_module_state() returns a keygloo_state_t*
    // (the opaque state blob registered by the ADB module). keygloo_state_t is
    // a thin wrapper that holds a KeyGloo* as its inner kg member. The actual
    // key-injection API lives on KeyGloo, not on keygloo_state_t.
    if (!ad->keygloo) {
        keygloo_state_t *kg = (keygloo_state_t *)ad->computer->get_module_state(MODULE_KEYGLOO);
        static bool logged = false;
        if (!logged) {
            printf("A2GSPU: KeyGloo lookup: kg=%p kg->kg=%p\n", (void*)kg, kg ? (void*)kg->kg : nullptr);
            fflush(stdout);
            logged = true;
        }
        if (kg) ad->keygloo = kg->kg;
        if (!ad->keygloo) return;
    }

    // Load new commands from file into queue.
    // Lines ending with \n get RETURN appended (for BASIC commands).
    // Raw text without \n is injected as-is (for single-key game input).
    // Only poll file every 4 frames (~15Hz) to reduce I/O when idle.
    if (ad->key_queue.empty()) {
        static int key_poll_counter = 0;
        if (++key_poll_counter < 4) return;
        key_poll_counter = 0;
        FILE *f = fopen(CMD_FILE, "r");
        if (f) {
            // Read entire file
            char buf[1024];
            std::string content;
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                content.append(buf, n);
            fclose(f);

            if (!content.empty()) {
                bool has_newline = content.find('\n') != std::string::npos;
                if (has_newline) {
                    // Line-by-line mode: each line gets RETURN
                    size_t pos = 0;
                    while (pos < content.size()) {
                        size_t nl = content.find('\n', pos);
                        if (nl == std::string::npos) nl = content.size();
                        for (size_t i = pos; i < nl; i++) {
                            uint8_t ch = (uint8_t)content[i];
                            if (ch == '\r') continue;
                            if (ch >= 'a' && ch <= 'z') ch -= 0x20;
                            ad->key_queue += (char)ch;
                        }
                        ad->key_queue += '\r';
                        pos = nl + 1;
                    }
                } else {
                    // Raw mode: inject chars as-is (no RETURN)
                    for (size_t i = 0; i < content.size(); i++) {
                        uint8_t ch = (uint8_t)content[i];
                        if (ch >= 'a' && ch <= 'z') ch -= 0x20;
                        ad->key_queue += (char)ch;
                    }
                }
                // Consume the file
                f = fopen(CMD_FILE, "w");
                if (f) fclose(f);
            }
        }
    }

    // Drain a few keys per frame.
    // When TERM mode is active with a telnet connection, forward keys to the
    // telnet socket instead of the IIgs keyboard so the user can interact
    // with the remote BBS.
    if (!ad->key_queue.empty()) {
        // Both telnet_connected and telnet_socket are checked independently.
        // They can diverge transiently during disconnect cleanup: telnet_socket
        // may be non-null for one or two frames after telnet_connected is cleared
        // (the close is async), or vice versa. Requiring both prevents writing to
        // a socket that is mid-teardown or sending to the IIgs keyboard when we
        // still think we're connected.
        bool to_telnet = (ad->emu.mode == A2GSPU_MODE_TERM &&
                          ad->emu.telnet_connected && ad->emu.telnet_socket);
        int count = 0;
        while (!ad->key_queue.empty() && count < KEYS_PER_FRAME) {
            uint8_t ch = (uint8_t)ad->key_queue[0];
            ad->key_queue.erase(0, 1);
            if (to_telnet) {
                NET_WriteToStreamSocket((NET_StreamSocket *)ad->emu.telnet_socket, &ch, 1);
            } else {
                ad->keygloo->store_key_to_buffer(ch, 0);
            }
            count++;
        }
    }
}

// ── Mouse injection: poll command file for mouse input ────────────
// Injects directly into ADB_Mouse — does NOT touch the user's real
// mouse. File format: one command per line:
//   M dx dy   — relative motion (pixels)
//   D         — left button down
//   U         — left button up
//   C         — click (down + up next frame)
//   RD        — right button down
//   RU        — right button up
static const char *MOUSE_CMD_FILE = "a2gspu_mouse.txt";

// Encode relative motion into ADB mouse register format
static void encode_mouse_motion(int dx, int dy, bool btn_left, bool btn_right,
                                 uint8_t *data0, uint8_t *data1) {
    // Clamp to ±63
    if (dx > 63) dx = 63; if (dx < -63) dx = -63;
    if (dy > 63) dy = 63; if (dy < -63) dy = -63;
    // ADB register 0 format:
    // data[0] = Y: [button0_up][moved_up][value 5:0]
    // data[1] = X: [button1_up][moved_right][value 5:0]
    uint8_t btn0 = btn_left ? 0x00 : 0x80;
    uint8_t btn1 = btn_right ? 0x00 : 0x80;
    uint8_t yabs = (dy < 0) ? (uint8_t)(-dy) : (uint8_t)dy;
    uint8_t xabs = (dx < 0) ? (uint8_t)(-dx) : (uint8_t)dx;
    *data0 = btn0 | (dy < 0 ? 0x40 : 0x00) | (yabs & 0x3F);
    *data1 = btn1 | (dx < 0 ? 0x40 : 0x00) | (xabs & 0x3F);
}

static bool mouse_btn_left = false;
static bool mouse_btn_right = false;

static void inject_mouse(a2gspu_data *ad) {
    if (!ad->keygloo) return;

    // Only poll file every 4 frames (~15Hz) to reduce I/O when idle.
    static int mouse_poll_counter = 0;
    if (++mouse_poll_counter < 4) return;
    mouse_poll_counter = 0;

    FILE *f = fopen(MOUSE_CMD_FILE, "r");
    if (!f) return;

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return;
    buf[n] = '\0';

    // Consume the file
    f = fopen(MOUSE_CMD_FILE, "w");
    if (f) fclose(f);

    // Parse commands (one per line)
    char *line = strtok(buf, "\n\r");
    while (line) {
        while (*line == ' ') line++;
        uint8_t d0, d1;
        if (line[0] == 'M' || line[0] == 'm') {
            int dx = 0, dy = 0;
            sscanf(line + 1, "%d %d", &dx, &dy);
            encode_mouse_motion(dx, dy, mouse_btn_left, mouse_btn_right, &d0, &d1);
            ad->keygloo->inject_mouse_data(d0, d1);
        } else if (line[0] == 'D' || line[0] == 'd') {
            mouse_btn_left = true;
            encode_mouse_motion(0, 0, mouse_btn_left, mouse_btn_right, &d0, &d1);
            ad->keygloo->inject_mouse_data(d0, d1);
        } else if (line[0] == 'U' || line[0] == 'u') {
            mouse_btn_left = false;
            encode_mouse_motion(0, 0, mouse_btn_left, mouse_btn_right, &d0, &d1);
            ad->keygloo->inject_mouse_data(d0, d1);
        } else if (line[0] == 'C' || line[0] == 'c') {
            mouse_btn_left = true;
            encode_mouse_motion(0, 0, mouse_btn_left, mouse_btn_right, &d0, &d1);
            ad->keygloo->inject_mouse_data(d0, d1);
        } else if (line[0] == 'R' && (line[1] == 'D' || line[1] == 'd')) {
            mouse_btn_right = true;
            encode_mouse_motion(0, 0, mouse_btn_left, mouse_btn_right, &d0, &d1);
            ad->keygloo->inject_mouse_data(d0, d1);
        } else if (line[0] == 'R' && (line[1] == 'U' || line[1] == 'u')) {
            mouse_btn_right = false;
            encode_mouse_motion(0, 0, mouse_btn_left, mouse_btn_right, &d0, &d1);
            ad->keygloo->inject_mouse_data(d0, d1);
        }
        line = strtok(nullptr, "\n\r");
    }
}

// ── Unified bus write observer ─────────────────────────────────────
// Called by MMU_IIgs for every memory write (via set_write_observer on megaii).
// Routes C0XX soft switch writes to the EMU backend's mode tracker, and data
// writes ($2000-$9FFF) to the EMU's SHR shadow memory.
// Also tracks $C021 (MONOCOLOR) for the HW backend's BUS_FLAG_MONO flag.
// Why a single observer: both backends need identical state. One interception
// point guarantees they always agree, avoiding subtle desync bugs.

static void a2gspu_write_observer(void *context, uint32_t address, uint8_t value) {
    a2gspu_data *ad = (a2gspu_data *)context;
    uint32_t page = (address >> 8) & 0xFF;

    if (page >= 0xC0 && page <= 0xCF) {
        uint8_t reg = address & 0xFF;
        if (reg == 0x21) ad->reg_c021 = value;  // track $C021 MONOCOLOR
        a2gspu_emu_softswitch(&ad->emu, reg, value);
    } else {
        a2gspu_emu_write(&ad->emu, address, value);
    }
}

// ── HW backend: frame callback (drains serial buffer) ──────────────

static void a2gspu_hw_frame_callback(a2gspu_data *ad) {
    static bool cb_logged = false;
    if (!cb_logged) { printf("A2GSPU: frame callback running\n"); fflush(stdout); cb_logged = true; }

    // Poll for injected keyboard and mouse input (file-based, independent of user's devices)
    inject_keys(ad);
    inject_mouse(ad);

    // Terminal keyboard is handled by SDL event handlers registered at init,
    // not here. The IIgs CPU runs thousands of cycles between VSYNC and the
    // point this frame callback fires, so by the time we get here the keyboard
    // latch ($C000) has already been read and cleared by the running program.
    // Polling the latch here would find it empty nearly every frame and we
    // would lose keystrokes. The SDL key-down event is the only reliable hook.

    // Auto-reconnect: try to open serial port periodically if disconnected.
    // Uses exponential backoff: starts at ~1s, backs off to ~5s if no card found.
    // This keeps reconnect fast after a hot-unplug while minimizing overhead
    // when running in EMU-only mode (no card will ever appear).
    if (!ad->serial_handle) {
        static int reconnect_counter = 0;
        static int reconnect_interval = 60;   // ~1s at 60fps, backs off to 300 (~5s)
        static bool ever_connected = false;
        if (++reconnect_counter >= reconnect_interval) {
            reconnect_counter = 0;
            const char *ports[] = { "COM7", "COM6", "COM8", "COM5", nullptr };
            for (int i = 0; ports[i]; i++) {
                ad->serial_handle = serial_open(ports[i]);
                if (ad->serial_handle) {
                    ad->prev_valid = false;  // force full resend
                    ever_connected = true;
                    reconnect_interval = 60;  // reset to fast reconnect
                    printf("A2GSPU: Connected via %s\n", ports[i]);
                    break;
                }
            }
            // Backoff asymmetry by ever_connected:
            // - ever_connected==true: interval stays pinned at 60 frames (~1s)
            //   permanently, even after the current disconnect. Hot-unplug is the
            //   normal workflow (reprogramming, cable swap) and the user expects
            //   sub-second reconnect as soon as the card reappears.
            // - ever_connected==false: interval doubles each miss (60→120→240→300)
            //   and caps at 300 frames (~5s). No card has ever appeared, so we are
            //   almost certainly running in EMU-only mode; the overhead of scanning
            //   four COM ports at 60fps is wasted work.
            if (!ad->serial_handle && reconnect_interval < 300) {
                reconnect_interval = ever_connected ? 60 : reconnect_interval * 2;
                if (reconnect_interval > 300) reconnect_interval = 300;
            }
        }
        if (!ad->serial_handle) return;
    }

    // Send every frame (60fps). Dirty tracking keeps most frames small.
    // Previous 30fps (skip every other) added up to 33ms latency on cursor/menus.

    {
        uint8_t *ram = ad->computer->mmu->get_memory_base();
        if (!ram) return;

        // Lazy-init display_state_t (not available during device init)
        if (!ad->ds) ad->ds = (display_state_t *)ad->computer->cached_display_state;

        // Read actual video mode from display_state_t (ground truth)
        uint8_t modes = build_mode_flags(ad->ds, ad->mmu_gs);

        // Select active text page: page 2 when PAGE2 set and 80STORE off.
        // IIgs behavior: when 80STORE ($C001) is set, the PAGE2 soft switch
        // ($C055) controls auxiliary-memory banking for the text and HGR pages,
        // NOT which display page is visible. Only when 80STORE is clear does
        // PAGE2 select the page-2 display buffer ($0800/$0C00 vs $0400/$0800).
        // Without this exclusion, a program using 80-column text (which requires
        // 80STORE+PAGE2) would cause us to read $0800 as the active text page,
        // sending wrong data to the card and showing garbage on the display.
        bool page2_display = (modes & BUS_MODE_PAGE2) && !(modes & BUS_MODE_80STORE);
        uint8_t *text = page2_display ? &ram[0x0800] : &ram[0x0400];
        uint8_t *aux_text = page2_display ? &ram[0x10800] : &ram[0x10400];

        // Read IIgs registers (pointers cached at init)
        MMU_IIgs *mmu_gs = ad->mmu_gs;
        display_state_t *ds = ad->ds;
        uint8_t cur_regs[4] = {
            mmu_gs ? mmu_gs->new_video_register() : (uint8_t)0,
            ds ? ds->text_color : (uint8_t)0xF0,
            ds ? ds->border_color : (uint8_t)0,
            mmu_gs ? mmu_gs->shadow_register() : (uint8_t)0
        };

        // Determine which graphics data to include
        // $C029 bit 7 (NEW_VIDEO): when set the IIgs enters Super Hi-Res mode,
        // which completely replaces the legacy video pipeline including HGR.
        // On real hardware SHR and HGR are mutually exclusive at the hardware
        // level; the card's frame parser relies on BUS_FLAG_SHR and BUS_FLAG_HGR
        // never appearing together in the same frame header.
        bool need_shr = (cur_regs[0] & 0x80) != 0;  // $C029 bit 7 = SHR mode

        // need_hgr is only evaluated when SHR is off, enforcing the invariant above.
        bool need_hgr = !need_shr && (modes & BUS_MODE_HIRES) && !(modes & BUS_MODE_TEXT);
        bool need_hgr_aux = need_hgr && (modes & BUS_MODE_80COL) && (modes & BUS_MODE_DGR);

        // Select HGR page based on PAGE2 flag (page flipping for animation)
        bool page2 = (modes & BUS_MODE_PAGE2) != 0;
        uint8_t *hgr = page2 ? &ram[0x4000] : &ram[0x2000];
        uint8_t *hgr_aux = page2 ? &ram[0x14000] : &ram[0x12000];

        // SHR data lives in the MegaII's shadow of bank $E1 ($2000-$9FFF).
        // The IIgs hardware automatically shadows writes to $E1:2000-9FFF into
        // the MegaII's address space at offset 0x12000 (0x10000 bank + 0x2000).
        // We read from the MegaII because that's where the VideoScanner reads —
        // it's the canonical copy. computer->mmu->get_memory_base() is the MegaII's
        // 128KB buffer (bank $00 + $01), NOT the IIgs's full RAM.
        uint8_t *megaii_ram = ad->mmu_gs ? ad->mmu_gs->get_megaii_memory_base() : nullptr;
        uint8_t *shr = megaii_ram ? &megaii_ram[0x12000] : &ram[0x12000];

        // Skip if nothing changed since last SUCCESSFUL send.
        // Why "successful": if the previous send was skipped (busy), prev state
        // was NOT updated, so the dirty comparison will re-detect those changes.
        // This prevents menu element dropouts that occurred when skipped frames
        // updated prev state — the card never received the data but we thought
        // it had, so subsequent frames didn't re-send the missing changes.
        static bool force_next = true;
        bool text_dirty = !ad->prev_valid || memcmp(text, ad->prev_text1, 1024) != 0;
        bool hgr_dirty = need_hgr && memcmp(hgr, ad->prev_hgr, 8192) != 0;
        bool hgr_aux_dirty = need_hgr_aux && memcmp(hgr_aux, ad->prev_hgr_aux, 8192) != 0;
        bool mode_dirty = (modes != ad->prev_mode_flags) ||
                          memcmp(cur_regs, ad->prev_regs, 4) != 0;

        // SHR dirty-line tracking: compare each of 200 lines against previous frame.
        // Why per-line: SHR is 32KB — sending it every frame at 60fps saturates USB CDC.
        // Typical desktop activity (cursor, menu highlights) touches 2-10 lines, reducing
        // frame size from 32KB to 1-3KB. The 25-byte bitmap (200 bits) tells the card
        // which lines to update. SCBs+palettes (712 bytes) are always included because
        // palette changes affect all lines and the cost is small vs per-line savings.
        uint8_t shr_dirty_bitmap[BUS_SHR_BITMAP_SIZE] = {};
        uint8_t shr_dirty_count = 0;
        bool shr_need_full = false;  // true = send full 32KB, false = incremental
        bool shr_dirty = false;

        if (need_shr) {
            if (!ad->prev_valid || !ad->prev_shr) {
                // First frame or no prev data — must send full
                shr_need_full = true;
                shr_dirty = true;
            } else {
                // Per-line comparison: build dirty bitmap.
                // O2: walk pointers rather than recomputing line * BUS_SHR_LINE_BYTES per
                // iteration — eliminates 200 multiplications in this 60fps loop.
                // O2: replace line/8 and line%8 with explicit bit index variables that
                // are maintained by incrementing and resetting, avoiding division entirely.
                const uint8_t *shr_pixels = shr;
                const uint8_t *prev_pixels = ad->prev_shr;
                const uint8_t *sp = shr_pixels;
                const uint8_t *pp = prev_pixels;
                int byte_idx = 0;   // shr_dirty_bitmap byte index (line / 8)
                int bit_idx  = 0;   // bit position within that byte (line % 8)
                for (int line = 0; line < BUS_SHR_LINES; line++, sp += BUS_SHR_LINE_BYTES, pp += BUS_SHR_LINE_BYTES) {
                    if (memcmp(sp, pp, BUS_SHR_LINE_BYTES) != 0) {
                        shr_dirty_bitmap[byte_idx] |= (uint8_t)(1 << bit_idx);
                        shr_dirty_count++;
                    }
                    // Advance bit index; roll byte index every 8 lines
                    if (++bit_idx == 8) { bit_idx = 0; byte_idx++; }
                }
                // Also check SCBs and palettes
                if (memcmp(shr + 0x7D00, ad->prev_shr + 0x7D00, BUS_SHR_SCB_SIZE + BUS_SHR_PAL_SIZE) != 0) {
                    shr_dirty = true;  // metadata changed — sends SHR_DIRTY with count=0 (SCBs+palettes only)
                }
                if (shr_dirty_count > 0) shr_dirty = true;
                // If >75% lines dirty, send full frame (less overhead than bitmap)
                if (shr_dirty_count > 150) shr_need_full = true;
            }
        }

        bool mosaic_dirty = (ad->proto.mosaic_cmd_count > 0);

        if (!force_next && !text_dirty && !hgr_dirty && !hgr_aux_dirty && !shr_dirty && !mode_dirty && !mosaic_dirty) return;

        force_next = false;
        static uint8_t frame[BUS_FRAME_MAX];

        frame[0] = 0x7E; frame[1] = 0x41;
        frame[2] = 0x32; frame[3] = 0x47;
        frame[4] = cur_regs[0];
        frame[5] = cur_regs[1];
        frame[6] = cur_regs[2];
        frame[7] = cur_regs[3];
        uint8_t flags = BUS_FLAG_TEXT1 | BUS_FLAG_TEXT1_AUX;
        uint8_t flags2 = 0;
        if (ad->reg_c021 & 0x80) flags |= BUS_FLAG_MONO;
        if (need_shr) {
            if (shr_need_full) {
                flags |= BUS_FLAG_SHR;
            } else {
                flags |= BUS_FLAG_SHR_DIRTY;
            }
        } else {
            if (need_hgr) flags |= BUS_FLAG_HGR;
            if (need_hgr_aux) flags |= BUS_FLAG_HGR_AUX;
        }
        frame[8] = flags;
        frame[9] = modes;
        frame[10] = flags2;  // FLAGS2: extended flags (Mosaic, future)
        memcpy(&frame[BUS_TEXT1_OFFSET], text, BUS_TEXT1_SIZE);
        memcpy(&frame[BUS_AUX1_OFFSET], aux_text, BUS_TEXT1_SIZE);

        if (flags & BUS_FLAG_SHR) {
            memcpy(&frame[BUS_SHR_OFFSET], shr, BUS_SHR_SIZE);
        } else if (flags & BUS_FLAG_SHR_DIRTY) {
            // Pack dirty frame: count + bitmap + SCBs + palettes + dirty pixel lines
            uint32_t pos = BUS_SHR_OFFSET;
            frame[pos++] = shr_dirty_count;
            memcpy(&frame[pos], shr_dirty_bitmap, BUS_SHR_BITMAP_SIZE); pos += BUS_SHR_BITMAP_SIZE;
            memcpy(&frame[pos], shr + 0x7D00, BUS_SHR_SCB_SIZE); pos += BUS_SHR_SCB_SIZE;
            memcpy(&frame[pos], shr + 0x7E00, BUS_SHR_PAL_SIZE); pos += BUS_SHR_PAL_SIZE;
            // O2: replace line/8 and line%8 with maintained bit-index counters —
            // avoids 200 divisions and 200 modulo ops in the frame pack path.
            {
                int byte_idx = 0, bit_idx = 0;
                const uint8_t *src_line = shr;
                for (int line = 0; line < BUS_SHR_LINES; line++, src_line += BUS_SHR_LINE_BYTES) {
                    if (shr_dirty_bitmap[byte_idx] & (uint8_t)(1 << bit_idx)) {
                        memcpy(&frame[pos], src_line, BUS_SHR_LINE_BYTES);
                        pos += BUS_SHR_LINE_BYTES;
                    }
                    if (++bit_idx == 8) { bit_idx = 0; byte_idx++; }
                }
            }
        } else {
            if (need_hgr) memcpy(&frame[BUS_HGR_OFFSET], hgr, BUS_HGR_SIZE);
            if (need_hgr_aux) memcpy(&frame[BUS_HGR_AUX_OFFSET], hgr_aux, BUS_HGR_SIZE);
        }

        // Per-scanline mode: detect split-screen and append 800 bytes if mode varies
        // Read video_scanner fresh each frame — display system may recreate it on mode change
        VideoScannerII *vs = ad->ds ? ad->ds->video_scanner : nullptr;
        if (vs) {
            const scanline_state_t *sl = vs->get_scanline_state();
            // Check if display video_mode varies across active scanlines (0-199).
            // Only consider real display modes (0-7: TEXT40..SHR). Ignore VBL/border
            // modes (>=8: VM_SHR_MODE, VM_SHR_PALETTE, VM_BORDER_COLOR, VM_VSYNC, etc.)
            // which appear in blanking lines and would falsely trigger mode_varies.
            uint8_t first_mode = 0xFF;  // unset
            bool mode_varies = false;
            for (int i = 0; i < BUS_SCANLINE_LINES; i++) {
                uint8_t vm = sl[i].video_mode;
                if (vm > BUS_VM_SHR) continue;  // skip non-display modes
                if (first_mode == 0xFF) {
                    first_mode = vm;
                } else if (vm != first_mode) {
                    mode_varies = true;
                    break;
                }
            }
            if (mode_varies) {
                flags |= BUS_FLAG_SCANLINE;
                frame[8] = flags;  // update flags byte in header
                // Compute where scanline data starts (after pixel data)
                uint32_t sl_off = BUS_HEADER_SIZE + BUS_TEXT1_SIZE + BUS_TEXT1_SIZE;
                if (flags & BUS_FLAG_SHR) {
                    sl_off += BUS_SHR_SIZE;
                } else if (flags & BUS_FLAG_SHR_DIRTY) {
                    sl_off += BUS_SHR_DIRTY_HEADER + (uint32_t)shr_dirty_count * BUS_SHR_LINE_BYTES;
                } else {
                    if (flags & BUS_FLAG_HGR) sl_off += BUS_HGR_SIZE;
                    if (flags & BUS_FLAG_HGR_AUX) sl_off += BUS_HGR_SIZE;
                }
                // Pack 200 × 4 bytes: [video_mode, border_color, mode_flags, scb]
                // Clamp non-display modes (VBL/border) to first_mode for safe card rendering
                for (int i = 0; i < BUS_SCANLINE_LINES; i++) {
                    uint8_t vm = sl[i].video_mode;
                    frame[sl_off + i * 4 + 0] = (vm <= BUS_VM_SHR) ? vm : first_mode;
                    frame[sl_off + i * 4 + 1] = sl[i].border_color;
                    frame[sl_off + i * 4 + 2] = sl[i].mode_flags;
                    frame[sl_off + i * 4 + 3] = sl[i].scb;
                }
            }
        }

        // Pack Mosaic command buffer (slot I/O writes accumulated since last send).
        // Appended after all IIgs data sections, before checksum.
        uint16_t mosaic_count = ad->proto.mosaic_cmd_count;
        if (mosaic_count > 0) {
            // Debug: uncomment for mosaic command tracing (fires 60/sec in UHR mode)
            // printf("A2GSPU: Packing %d Mosaic commands into frame\n", mosaic_count);
            flags2 |= BUS_FLAG2_MOSAIC;
            frame[10] = flags2;  // update FLAGS2 in header
            // Compute mosaic section offset (after all preceding variable sections)
            uint32_t m_off = BUS_HEADER_SIZE + BUS_TEXT1_SIZE + BUS_TEXT1_SIZE;
            if (flags & BUS_FLAG_SHR) {
                m_off += BUS_SHR_SIZE;
            } else if (flags & BUS_FLAG_SHR_DIRTY) {
                m_off += BUS_SHR_DIRTY_HEADER + (uint32_t)shr_dirty_count * BUS_SHR_LINE_BYTES;
            } else {
                if (flags & BUS_FLAG_HGR) m_off += BUS_HGR_SIZE;
                if (flags & BUS_FLAG_HGR_AUX) m_off += BUS_HGR_SIZE;
            }
            if (flags & BUS_FLAG_SCANLINE) m_off += BUS_SCANLINE_SIZE;
            // Pack [count:2][entries: count × 3]
            frame[m_off + 0] = mosaic_count & 0xFF;
            frame[m_off + 1] = (mosaic_count >> 8) & 0xFF;
            memcpy(&frame[m_off + 2], ad->proto.mosaic_cmds,
                   mosaic_count * BUS_MOSAIC_CMD_SIZE);
        }

        uint32_t frame_sz = bus_frame_size_ex2(flags, flags2, shr_dirty_count, mosaic_count);
        uint32_t csum_off = frame_sz - 2;
        uint16_t csum = 0;
        for (uint32_t i = 4; i < csum_off; i++) csum ^= frame[i];
        frame[csum_off] = csum & 0xFF;
        frame[csum_off + 1] = (csum >> 8) & 0xFF;

        int wr = serial_write_async(ad->serial_handle, frame, frame_sz);
        if (wr < 0) {
            // Write error — device may be disconnected. Close and let reconnect logic retry.
            serial_close(ad->serial_handle);
            ad->serial_handle = nullptr;
            ad->prev_valid = false;
            force_next = true;
            printf("A2GSPU: Serial write failed, will auto-reconnect\n");
        }

        // Only update prev state if frame was actually sent (wr==1).
        // If skipped (wr==0), keep prev state so dirty lines are re-detected next frame.
        if (wr == 1 && !force_next) {
            memcpy(ad->prev_text1, text, 1024);
            if (need_hgr) memcpy(ad->prev_hgr, hgr, 8192);
            if (need_hgr_aux) memcpy(ad->prev_hgr_aux, hgr_aux, 8192);
            if (need_shr) {
                if (!ad->prev_shr) ad->prev_shr = new uint8_t[BUS_SHR_SIZE];
                memcpy(ad->prev_shr, shr, BUS_SHR_SIZE);
            }
            ad->prev_mode_flags = modes;
            memcpy(ad->prev_regs, cur_regs, 4);
            ad->prev_valid = true;
            ad->proto.mosaic_cmd_count = 0;  // commands delivered, clear buffer
        }
    }
}

// ── Terminal key handler (SDL event) ──────────────────────────────
// Processes a single ASCII key for the terminal: either building the
// address string (prompt mode) or forwarding to the telnet socket.
// Returns true if the key was consumed (prevents IIgs from seeing it).
static bool term_handle_key(a2gspu_data *ad, uint8_t ascii) {
    if (ad->emu.mode != A2GSPU_MODE_TERM) return false;

    if (ad->emu.term_prompt_active) {
        if (ascii == 0x0D || ascii == 0x0A) {
            // Enter — connect
            ad->emu.term_address[ad->emu.term_address_len] = '\0';
            ad->emu.term_prompt_active = false;

            char host[128];
            int port = 23;
            strncpy(host, ad->emu.term_address, sizeof(host));
            host[sizeof(host)-1] = '\0';
            char *colon = strrchr(host, ':');
            if (colon) {
                *colon = '\0';
                port = atoi(colon + 1);
                if (port <= 0 || port > 65535) port = 23;
            }

            // Clear grid and connect
            memset(ad->emu.term_grid, 0, sizeof(ad->emu.term_grid));
            ad->emu.term_cur_col = 0;
            ad->emu.term_cur_row = 0;
            if (ad->emu.uhr_fb) memset(ad->emu.uhr_fb, 0, A2GSPU_UHR_SIZE);
            ad->emu.frame_dirty = true;
            a2gspu_emu_telnet_connect(&ad->emu, host, port);
        } else if (ascii == 0x08 || ascii == 0x7F) {
            if (ad->emu.term_address_len > 0) {
                ad->emu.term_address_len--;
                // Destructive backspace idiom: move cursor left one column,
                // overwrite the character with a space (erase it), then move
                // left again so the next typed character lands on the erased cell.
                // Three commands, not one, because the terminal protocol has no
                // combined "erase-and-retreat" primitive.
                //
                // Each command is issued as a paired call: a2gspu_emu_term_execute
                // updates the SDL EMU framebuffer immediately (so the user sees it),
                // while queue_term_for_hw enqueues the same command for delivery to
                // the physical HW card on the next USB frame. The two backends must
                // see identical command sequences to stay in sync; a missed call to
                // either one causes permanent divergence between EMU and HW display.
                uint16_t cpos = ((ad->emu.term_cur_col - 1) & 0xFF) | ((uint16_t)ad->emu.term_cur_row << 8);
                a2gspu_emu_term_execute(&ad->emu, A2GSPU_CMD_TERM_SET_CURSOR, cpos);
                queue_term_for_hw(&ad->emu, A2GSPU_CMD_TERM_SET_CURSOR, cpos);
                a2gspu_emu_term_execute(&ad->emu, A2GSPU_CMD_TERM_PUTCHAR, ' ');
                queue_term_for_hw(&ad->emu, A2GSPU_CMD_TERM_PUTCHAR, ' ');
                a2gspu_emu_term_execute(&ad->emu, A2GSPU_CMD_TERM_SET_CURSOR, cpos);
                queue_term_for_hw(&ad->emu, A2GSPU_CMD_TERM_SET_CURSOR, cpos);
                ad->emu.frame_dirty = true;
            }
        } else if (ascii == 0x1B) {
            a2gspu_emu_set_mode(&ad->emu, A2GSPU_MODE_PASSTHROUGH);
        } else if (ascii >= 0x20 && ascii < 0x7F && ad->emu.term_address_len < 126) {
            ad->emu.term_address[ad->emu.term_address_len++] = ascii;
            a2gspu_emu_term_execute(&ad->emu, A2GSPU_CMD_TERM_PUTCHAR, ascii);
            queue_term_for_hw(&ad->emu, A2GSPU_CMD_TERM_PUTCHAR, ascii);
            ad->emu.frame_dirty = true;
        }
        return true;
    } else if (ad->emu.telnet_connected && ad->emu.telnet_socket) {
        NET_StreamSocket *sock = (NET_StreamSocket *)ad->emu.telnet_socket;
        // Translate Apple II arrow keycodes to VT100 escape sequences.
        // The IIgs reuses C0 control characters for arrow keys: 0x0B (VT/Ctrl-K)
        // maps to Up, 0x0A (LF/Ctrl-J) maps to Down. These mappings only apply
        // here, inside term_handle_key, when TERM mode is active with a live
        // telnet connection. In PASSTHROUGH mode these same byte values reach
        // the IIgs with their standard C0 meanings (line-feed, vertical-tab,
        // etc.), so this translation must not be applied globally.
        // IIgs: Up=0x0B Down=0x0A Left=0x08 Right=0x15
        // VT100: Up=ESC[A Down=ESC[B Right=ESC[C Left=ESC[D
        switch (ascii) {
            case 0x0B: { uint8_t seq[] = {0x1B,'[','A'}; NET_WriteToStreamSocket(sock, seq, 3); break; }
            case 0x0A: { uint8_t seq[] = {0x1B,'[','B'}; NET_WriteToStreamSocket(sock, seq, 3); break; }
            case 0x15: { uint8_t seq[] = {0x1B,'[','C'}; NET_WriteToStreamSocket(sock, seq, 3); break; }
            case 0x08: { uint8_t seq[] = {0x1B,'[','D'}; NET_WriteToStreamSocket(sock, seq, 3); break; }
            default:   NET_WriteToStreamSocket(sock, &ascii, 1); break;
        }
        return true;
    }
    return false;
}

// ── Debug display ──────────────────────────────────────────────────

static const char *mode_name(a2gspu_video_mode_t mode) {
    switch (mode) {
        case A2GSPU_MODE_PASSTHROUGH: return "PASSTHROUGH";
        case A2GSPU_MODE_UHR16:      return "UHR16";
        case A2GSPU_MODE_UHR256:     return "UHR256";
        case A2GSPU_MODE_UHR6400:    return "UHR6400";
        case A2GSPU_MODE_UHR102K:    return "UHR102K";
        case A2GSPU_MODE_TERM:       return "TERM";
        default:                     return "UNKNOWN";
    }
}

static DebugFormatter *a2gspu_debug_display(a2gspu_data *ad) {
    DebugFormatter *df = new DebugFormatter();
    df->addLine("Mode: %s", mode_name(ad->emu.mode));
    df->addLine("HW: Serial %s", ad->serial_handle ? "connected" : "disconnected");
    df->addLine("EMU: %s", ad->emu.initialized ? "ready" : "not initialized");
    df->addLine("Cursor: (%d,%d)  Pal idx: %d",
        ad->proto.cursor_x, ad->proto.cursor_y, ad->proto.pal_idx);
    return df;
}

// ── Device initialization ──────────────────────────────────────────

void init_slot_a2gspu(computer_t *computer, SlotType_t slot) {
    a2gspu_data *ad = new a2gspu_data();
    ad->computer = computer;
    ad->cpu = computer->cpu;
    ad->_slot = slot;

    // Cache pointers for frame callback (avoids dynamic_cast per frame)
    ad->mmu_gs = dynamic_cast<MMU_IIgs *>(computer->cpu->mmu);
    ad->ds = (display_state_t *)computer->cached_display_state;
    // KeyGloo for keyboard injection (may be null if not yet initialized)
    keygloo_state_t *kg_state = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
    if (kg_state) ad->keygloo = kg_state->kg;

    // Register unified write observer on MMU_IIgs (the FPI layer).
    // NOTE: computer->mmu is the MegaII (MMU_IIe), not the FPI (MMU_IIgs).
    // set_write_observer must be called on mmu_gs (FPI) rather than on the
    // MegaII because SHR writes target bank $E1 ($E1:2000-$E1:9FFF), which
    // are routed through the FPI's shadow logic. The MegaII observer only sees
    // the 128KB MegaII address space (banks $00/$01); it would miss all $E1
    // bank writes entirely, so SHR pixel data would never reach the observer.
    if (ad->mmu_gs) {
        ad->mmu_gs->set_write_observer(a2gspu_write_observer, ad);

        // Register Mosaic slot I/O protocol on megaii (where IOLC dispatches)
        uint16_t slot_base = 0xC080 + (slot * 0x10);
        a2gspu_proto_register(ad, ad->mmu_gs->megaii, slot_base);
    }

    // Initialize EMU backend (creates SDL3 texture, registers frame processor)
    a2gspu_emu_init(&ad->emu, computer);
    ad->emu.owner = ad;  // back-pointer for HW forwarding from telnet bridge

    // Register HW backend frame callback (drains serial buffer)
    if (computer->device_frame_dispatcher) {
        computer->device_frame_dispatcher->registerHandler([ad]() -> bool {
            a2gspu_hw_frame_callback(ad);
            return true;
        });
        printf("A2GSPU: Frame callback registered\n");
    } else {
        printf("A2GSPU: WARNING — device_frame_dispatcher is NULL!\n");
    }

    // Register terminal keyboard intercept on KeyGloo.
    // When TERM mode is active, keystrokes are consumed before reaching the
    // IIgs keyboard buffer and routed to the address prompt or telnet socket.
    //
    // Lambda lifetime: the ad pointer captured here is safe for the entire
    // lifetime of the lambda. The shutdown handler (registered just below)
    // calls a2gspu_emu_shutdown(), clears the serial handle, and then deletes
    // ad. KeyGloo itself is destroyed as part of the same computer teardown
    // sequence, which runs the shutdown handlers before destroying device
    // modules. The lambda is therefore guaranteed to be unregistered (along
    // with KeyGloo) before ad is freed — it cannot fire after ad is deleted.
    if (ad->keygloo) {
        ad->keygloo->key_intercept = [ad](uint8_t keycode, uint8_t keymods) -> bool {
            if (ad->emu.mode != A2GSPU_MODE_TERM) return false;
            return term_handle_key(ad, keycode);
        };
    }

    // Register debug display
    computer->register_debug_display_handler("A2GSPU", (uint64_t)ad,
        [ad]() -> DebugFormatter * { return a2gspu_debug_display(ad); });

    // Register shutdown handler
    computer->register_shutdown_handler([ad]() -> bool {
        a2gspu_emu_shutdown(&ad->emu);
        if (ad->serial_handle) {
            serial_close(ad->serial_handle);
            ad->serial_handle = nullptr;
        }
        delete ad;
        return true;
    });

    // Auto-open serial port for HW backend (try COM7, common for RP2350 CDC)
    const char *serial_ports[] = { "COM7", "COM6", "COM8", "COM5", nullptr };
    for (int i = 0; serial_ports[i]; i++) {
        ad->serial_handle = serial_open(serial_ports[i]);
        if (ad->serial_handle) break;
    }

    printf("A2GSPU: Device initialized in slot %d (HW %s, EMU %s)\n",
           slot,
           ad->serial_handle ? "connected" : "no serial",
           ad->emu.initialized ? "ready" : "failed");
}
