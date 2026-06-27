/*
 *   A2GSPU Emulated Renderer
 *
 *   Renders UHR video modes locally in GSSquared without physical hardware.
 *   Shadow SHR memory is populated by the bus write observer. Video state
 *   (border color, text color, etc.) is read from GSSquared's display_state_t
 *   which is kept current by the IIgs ROM loading BRAM defaults into the
 *   soft switches at boot ($C022/$C034 from Control Panel settings).
 *
 *   Standard IIgs modes (TEXT, LORES, HIRES, SHR) pass through to
 *   GSSquared's existing display system. The A2GSPU emulated renderer
 *   only activates for UHR modes, driven by the Mosaic tile protocol.
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#pragma once

#include <cstdint>
#include <cstring>

extern "C" {
#include "utf8_decode.h"
}

struct SDL_Texture;
struct SDL_Renderer;
struct computer_t;
struct display_state_t;
struct video_system_t;

// ── A2GSPU video modes ─────────────────────────────────────────────
//
// The mode enum controls which renderer is active for the current frame.
// PASSTHROUGH is the normal, quiescent state: the emulated renderer is
// completely inactive and GSSquared's own display pipeline handles all
// standard IIgs modes (TEXT, LORES, HIRES, DHGR, SHR 320/640).
//
// The remaining modes are UHR (Ultra-High Resolution) variants that
// exceed what the native IIgs hardware can produce.  They are entered
// by the IIgs application via the Mosaic slot-I/O protocol (SET_MODE
// command on slot 3).  On real hardware this drives a RP2350 card; in
// the emulator it switches this renderer into the matching mode.
//
// A2GSPU_MODE_TERM is the terminal mode.  It uses the same 640×400 UHR
// framebuffer as UHR256 but populates it from a character-cell grid
// (term_grid) rather than direct pixel commands.  This allows the card
// to function as a high-quality 80-column terminal for BBS and network
// connections made from IIgs applications via Marinetti / WiModem232.
// In the emulator the grid is populated either by the Mosaic protocol
// handler (TERM commands forwarded over USB, same path as real hardware)
// or by the built-in telnet bridge (see telnet_* fields below).

enum a2gspu_video_mode_t {
    A2GSPU_MODE_PASSTHROUGH = 0,  // Let GSSquared handle rendering
    A2GSPU_MODE_UHR16,            // 640×400, 16 colors, global palette
    A2GSPU_MODE_UHR256,           // 640×400, 256 colors, fixed RGB332
    A2GSPU_MODE_UHR6400,          // 640×400, 16 colors × 400 per-line
    A2GSPU_MODE_UHR102K,          // 640×400, 256 colors × 400 per-line
    A2GSPU_MODE_TERM,             // 80×25 text terminal (CP437 8×16, xterm-256color)
};

// ── Dimensions ─────────────────────────────────────────────────────

#define A2GSPU_SHR_SIZE    0x8000   // $2000-$9FFF = 32KB
#define A2GSPU_SHR_BASE    0x2000   // offset within bank $E1

#define A2GSPU_SCB_OFFSET  0x7D00   // $9D00: SCBs (200 bytes)
#define A2GSPU_PAL_OFFSET  0x7E00   // $9E00: palettes (512 bytes)

#define A2GSPU_UHR_W       640
#define A2GSPU_UHR_H       400
#define A2GSPU_UHR_PITCH   A2GSPU_UHR_W
#define A2GSPU_UHR_SIZE    (A2GSPU_UHR_W * A2GSPU_UHR_H)

// ── Emulated renderer state ────────────────────────────────────────

struct a2gspu_emu_state {
    // Shadow SHR memory (bank $E1 $2000-$9FFF, populated by write observer)
    uint8_t shr_shadow[A2GSPU_SHR_SIZE];

    // UHR framebuffer (8bpp indexed, 640×400)
    uint8_t *uhr_fb;

    // UHR palette (256 entries, RGB565 — matches card's FORMAT_8_PAL)
    uint16_t palette[256];

    // Current video mode
    a2gspu_video_mode_t mode;

    // SDL3 output texture (640×400 RGBA, rendered each frame when UHR active)
    SDL_Texture *texture;

    // Video state — tracked from write observer.
    // These mirror the IIgs soft switch values, kept current by the
    // ROM loading BRAM defaults into $C022/$C034/$C029/$C035 at boot.
    uint8_t reg_new_video;   // $C029 — SHR enable, linear, etc.
    uint8_t reg_text_color;  // $C022 — text fg (high nibble) / bg (low nibble)
    uint8_t reg_border;      // $C034 — border color (low nibble)
    uint8_t reg_shadow;      // $C035 — shadow register
    uint8_t mode_flags;      // display mode flags (80col, text, hires, etc.)

    // References to GSSquared state (not owned)
    video_system_t *video_system;
    // Back-pointer to the enclosing a2gspu_data instance.
    // Required by queue_term_for_hw(), which needs to append a TERM command to
    // ad->proto.mosaic_cmds[] and increment ad->proto.mosaic_cmd_count so the
    // command is included in the next USB frame.  a2gspu_emu_state has no
    // direct link to a2gspu_proto_state, so this pointer is the only path from
    // the EMU layer back to the Mosaic protocol buffer.  Typed as void* to
    // avoid a circular header dependency (a2gspu.hpp includes a2gspu_emu.hpp).
    // The cast to a2gspu_data* in queue_term_for_hw() is always safe because
    // owner is set exactly once, in init_slot_a2gspu(), to &ad.
    void *owner;

    // ── Terminal grid state (TERM mode) ────────────────────────────────────
    //
    // The terminal model is a 2-D array of character cells, each holding a
    // Unicode code point (restricted to CP437 here), foreground and background
    // palette indices (0-255, xterm-256color convention), and an attribute
    // byte (bold, underline, inverse — see BUS_TERM_ATTR_* constants).
    //
    // The grid is fixed at 80 columns × up to 50 rows in memory; the active
    // row count (term_num_rows) is determined by the font height:
    //   400 / 16 = 25 rows  (8×16 font, standard 80×25)
    //   400 / 10 = 40 rows  (8×10 font)
    //   400 /  8 = 50 rows  (8×8  font)
    //
    // This mirrors the character grid maintained on the real hardware card in
    // card/terminal.c, so that the same TERM command stream produces identical
    // output whether it is processed by the emulator or the physical card.
    //
    // The grid is populated by a2gspu_emu_term_execute() — either called
    // directly from a2gspu_proto.cpp (Mosaic USB forwarding path, same as
    // real hardware) or from telnet_vt100_callback() (emulator-local telnet
    // bridge path, bypassing Marinetti entirely).
    struct {
        uint8_t ch;    // CP437 character code
        uint8_t fg;    // foreground palette index (0-255, xterm-256color)
        uint8_t bg;    // background palette index (0-255, xterm-256color)
        uint8_t attr;  // attribute flags: BUS_TERM_ATTR_BOLD | _UNDERLINE | _INVERSE
    } term_grid[80 * 50];           // character grid (80×25 default, 80×50 max)

    // Active cursor position (column and row, 0-based).
    // Both values are clipped to [0, term_num_cols-1] / [0, term_num_rows-1]
    // by A2GSPU_CMD_TERM_SET_CURSOR.  term_advance_cursor() wraps at the
    // right margin and scrolls when the cursor moves past the bottom of
    // the scroll region.
    uint8_t  term_cur_col, term_cur_row;

    // Current rendering attributes applied to the next PUTCHAR.
    // fg/bg are xterm-256color palette indices; attr carries BUS_TERM_ATTR_*
    // flag bits set via A2GSPU_CMD_TERM_SET_FG/BG/ATTR.
    uint8_t  term_cur_fg, term_cur_bg, term_cur_attr;

    // Saved cursor state (DECSC / ESC 7, restored by DECRC / ESC 8).
    // Holds the full rendering context so that save/restore round-trips
    // correctly preserve both position and color state.
    uint8_t  term_saved_col, term_saved_row;
    uint8_t  term_saved_fg, term_saved_bg, term_saved_attr;

    // Scroll region (DECSTBM, ESC[<top>;<bot>r).
    // All scrolling operations (term_scroll_up, term_scroll_down,
    // term_advance_cursor wrapping) are confined to [term_scroll_top,
    // term_scroll_bot] inclusive, defaulting to [0, term_num_rows-1].
    uint8_t  term_scroll_top, term_scroll_bot;

    // Active grid dimensions, computed from font height.
    // term_num_cols is always 80 (card hardware is 640px / 8px per glyph).
    // term_num_rows = 400 / term_font_height, capped at 50.
    uint8_t  term_num_cols, term_num_rows;

    // Active font scanline height (8, 10, or 16).  Controls both how many
    // scanlines are copied per character row and how many rows fit in 400px.
    // Set via A2GSPU_CMD_TERM_SET_FONT; default 16 → 25 rows.
    uint8_t  term_font_height;

    // Pointer to the active glyph bitmap array.  Each glyph is term_font_height
    // bytes wide (1 byte per scanline, 8 pixels per byte, MSB = leftmost pixel).
    // Set alongside term_font_height by A2GSPU_CMD_TERM_SET_FONT.
    const uint8_t *term_font_data = nullptr;

    // Cursor visual style, matching the TERM protocol encoding:
    //   0 = block (full-cell, default)
    //   1 = underline (bottom 2 scanlines)
    //   2 = bar (left 2 columns)
    // Bit 7 of the SET_CURSOR_STYLE argument sets term_cursor_visible = false
    // (cursor off, e.g. during drawing to suppress flicker).
    uint8_t  term_cursor_style;
    bool     term_cursor_visible;

    // Alternate screen buffer (xterm ?1049)
    struct {
        uint8_t ch;
        uint8_t fg;
        uint8_t bg;
        uint8_t attr;
    } alt_grid[80 * 50];
    uint8_t alt_col, alt_row, alt_fg, alt_bg, alt_attr;
    bool alt_active = false;

    // Scrollback ring buffer (mirrors card/terminal.c)
    static constexpr int TERM_SCROLLBACK_LINES = 50;
    struct { uint8_t ch, fg, bg, attr; } scrollback_ring[TERM_SCROLLBACK_LINES][80];
    uint16_t sb_head = 0;
    uint16_t sb_count = 0;

    // Frame counter incremented once per render_term_to_fb() call.
    // Used to drive the block-cursor blink: cursor is shown when
    // (term_frame_counter / 15) & 1 is true, giving ~2 Hz at 60 fps.
    // Non-block cursor styles are always displayed (no blink).
    uint32_t term_frame_counter;

    // ── Built-in telnet bridge (emulator testing only) ─────────────────────
    //
    // In normal production use, the IIgs application (a terminal program using
    // Marinetti TCP/IP or a WiModem232 serial bridge) sends bytes to the card
    // which parses VT100/ANSI sequences and issues TERM commands via the Mosaic
    // protocol.  The emulator faithfully replays those same TERM commands via
    // a2gspu_proto.cpp, so no telnet bridge is needed for protocol-level testing.
    //
    // However, during early development it is useful to verify the terminal
    // renderer end-to-end from raw TCP bytes without needing a running IIgs
    // terminal application, Marinetti stack, or physical card.  The telnet
    // bridge provides this shortcut: on TERM mode entry it opens a direct
    // SDL3_net TCP connection, feeds the received bytes through the embedded
    // vt100 parser (the same vt100.c library used on the card), and drives
    // a2gspu_emu_term_execute() with the resulting TERM commands.
    //
    // This path is EMULATOR-ONLY and is NOT present on the physical card,
    // which has no TCP stack.  The production path on real hardware is:
    //   IIgs app → SCC serial / Marinetti → Mosaic TERM commands → card renderer
    //
    // telnet_socket: opaque pointer to a NET_StreamSocket allocated by
    //   a2gspu_emu_telnet_connect().  Stored as void* to avoid pulling
    //   SDL3_net headers into this struct definition.
    //   Callers must cast to NET_StreamSocket* before use.  The cast is safe
    //   because a2gspu_emu_telnet_connect() is the only code path that assigns
    //   this pointer, so the dynamic type is always NET_StreamSocket at every
    //   use site (poll, disconnect).
    //
    // telnet_vt100: opaque pointer to a vt100_state_t allocated at connect
    //   time and freed at disconnect.  Stored as void* for the same reason.
    //   Callers must cast to vt100_state_t* before use; the cast is safe for
    //   the same reason — only a2gspu_emu_telnet_connect() sets this pointer.
    //
    // telnet_connected: true between a successful NET_WaitUntilConnected()
    //   and the first read error / explicit disconnect.  Guards all polling.
    void *telnet_socket;        // NET_StreamSocket* (cast to avoid header dep)
    void *telnet_vt100;         // vt100_state_t* (allocated on connect)
    bool  telnet_connected;

    // Telnet IAC negotiation state — per-connection, reset on connect AND
    // disconnect so a session that drops mid-IAC cannot bleed stale state into
    // the first bytes of the next session. (Formerly function-static locals
    // keyed on a pointer-compare, which failed when a new socket reused the
    // previous allocation's address.)
    int     iac_state = 0;      // 0=normal 1=IAC 2=verb+opt 3=SB 4=SB+IAC 5=SB option
    uint8_t iac_verb = 0;       // verb byte (WILL/WONT/DO/DONT)
    uint8_t iac_sb_option = 0;  // option byte for current SB
    uint8_t iac_sb_buf[64] = {0};
    int     iac_sb_len = 0;

    // UTF-8 decoder state for multi-byte characters arriving via PUTCHAR
    // (mirrors the utf8_state_t used in card/terminal.c)
    utf8_state_t term_utf8_st;
    bool         term_utf8_initialized = false;

    // ── Terminal address input ────────────────────────────────────────
    // When TERM mode is entered, if no telnet connection is active, the
    // terminal shows a "Connect to: " prompt and captures keystrokes
    // to build an address string.  On Enter, it connects to that address.
    //
    // Lifecycle (three-state cycle):
    //   PROMPT     → term_prompt_active = true
    //                Set on TERM mode entry when telnet_connected is false,
    //                and again after every disconnect.
    //   CONNECTED  → term_prompt_active = false
    //                Cleared when Enter is pressed and the connection attempt
    //                is initiated (a2gspu_emu_telnet_connect() is called).
    //   DISCONNECT → term_prompt_active = true
    //                Reset by a2gspu_emu_telnet_disconnect() so the prompt
    //                reappears, completing the cycle back to PROMPT.
    bool term_prompt_active = false;
    char term_address[128];
    int  term_address_len = 0;

    // Frame dirty flag (set by write observer, cleared after render)
    bool frame_dirty;
    bool initialized;

    // Constructor initialises soft switch shadows to the IIgs ROM BRAM defaults
    // as loaded by the ROM into the soft switches at boot:
    //   reg_new_video = 0x01  ($C029: bit 0 = SHR enable off; IIgs power-on default)
    //   reg_text_color = 0xF6 ($C022: high nibble = fg 0xF = white,
    //                                  low nibble = bg 0x6 = blue; Control Panel default)
    //   reg_border     = 0x06 ($C034: low nibble = border color 6 = blue; Control Panel default)
    //   reg_shadow     = 0x08 ($C035: bit 3 = inhibit bank $E1 SHR shadow; ROM default)
    // These values match what the write observer will see after the ROM's BRAM
    // restore routine runs, so the first frame is rendered correctly even if no
    // soft switch writes have been observed yet.
    a2gspu_emu_state() :
        uhr_fb(nullptr),
        mode(A2GSPU_MODE_PASSTHROUGH),
        reg_new_video(0x01),
        reg_text_color(0xF6),
        reg_border(0x06),
        reg_shadow(0x08),
        mode_flags(0x08),  // default: text mode on
        texture(nullptr),
        video_system(nullptr),
        owner(nullptr),
        frame_dirty(true),
        initialized(false)
    {
        memset(shr_shadow, 0, sizeof(shr_shadow));
        // Default RGB332 identity palette
        for (int i = 0; i < 256; i++) {
            int r3 = (i >> 5) & 0x7;
            int g3 = (i >> 2) & 0x7;
            int b2 = i & 0x3;
            palette[i] = (uint16_t)(((r3 * 31 + 3) / 7) << 11) |
                         (((g3 * 63 + 3) / 7) << 5) |
                         ((b2 * 31 + 1) / 3);
        }
    }
};

// ── API ────────────────────────────────────────────────────────────

// Initialize emulated renderer (creates SDL3 texture, registers frame processor)
bool a2gspu_emu_init(a2gspu_emu_state *emu, computer_t *computer);

// Handle a soft switch write (updates internal video state)
void a2gspu_emu_softswitch(a2gspu_emu_state *emu, uint8_t reg, uint8_t value);

// Handle a shadowed memory write to SHR region
void a2gspu_emu_write(a2gspu_emu_state *emu, uint32_t address, uint8_t value);

// Frame processor callback (registered at weight 2).
// Returns true when UHR mode is active (skips default renderer).
// Returns false for passthrough to GSSquared's existing display.
bool a2gspu_emu_frame_processor(a2gspu_emu_state *emu, bool force);

// Set video mode (called from Mosaic slot I/O protocol handler)
void a2gspu_emu_set_mode(a2gspu_emu_state *emu, a2gspu_video_mode_t mode);

// Load a custom palette (256 RGB565 entries)
void a2gspu_emu_set_palette(a2gspu_emu_state *emu, const uint16_t *pal);

// Process a terminal command (TERM mode).
// cmd is one of the A2GSPU_CMD_TERM_* constants from bus_protocol.h.
// data packing is command-specific: most commands use the low byte (lo = data &
// 0xFF) for their primary argument; SET_CURSOR and SET_REGION pack two fields
// as (col|row<<8) or (top|bot<<8) respectively.
void a2gspu_emu_term_execute(a2gspu_emu_state *emu, uint8_t cmd, uint16_t data);

// Queue a TERM command for HW forwarding via Mosaic USB.
// Defined in a2gspu_emu.cpp as a non-static free function so that a2gspu.cpp
// can also call it directly — specifically to forward prompt characters (typed
// address input) to the physical card without going through the TERM mode
// command handler.  Being non-static keeps it accessible across translation
// units without requiring a friend declaration or a separate helper header.
void queue_term_for_hw(a2gspu_emu_state *emu, uint8_t cmd, uint16_t data);

// Built-in telnet bridge — emulator development/testing only.
// Opens a TCP connection to host:port, initialises the VT100 parser,
// and begins feeding received bytes into a2gspu_emu_term_execute() each frame.
// Does NOT exist on the physical card; production flow goes through Marinetti.
void a2gspu_emu_telnet_connect(a2gspu_emu_state *emu, const char *host, int port);
void a2gspu_emu_telnet_poll(a2gspu_emu_state *emu);
void a2gspu_emu_telnet_disconnect(a2gspu_emu_state *emu);

// Cleanup
void a2gspu_emu_shutdown(a2gspu_emu_state *emu);
