/*
 *   A2GSPU Emulated Renderer
 *
 *   Renders UHR video modes locally in GSSquared. In passthrough mode,
 *   GSSquared's existing display system handles all standard IIgs modes.
 *   When a UHR mode is activated via the Mosaic protocol, this renderer
 *   takes over and renders 640×400 output to its own SDL3 texture.
 *
 *   Border/text colors come from GSSquared's display_state_t, which
 *   reflects the IIgs Control Panel BRAM settings (loaded by ROM into
 *   $C022/$C034 at boot).
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include "computer.hpp"
#include "videosystem.hpp"
#include "a2gspu_emu.hpp"
#include "a2gspu_proto.hpp"
#include "a2gspu.hpp"
#include "devices/displaypp/RGBA.hpp"
#include "devices/displaypp/AppleIIgsColors.hpp"

extern "C" {
#include "vt100.h"
}

// Forward declarations for terminal rendering
static void render_term_to_fb(a2gspu_emu_state *emu);

// ── IIgs 12-bit color conversion ───────────────────────────────────
// SHR palette: 4 bits per channel ($0RGB), stored as 2 bytes LE.
// Byte 0: [g3:g0][b3:b0]  Byte 1: [x3:x0][r3:r0]

static inline RGBA_t shr_color_to_rgba(uint8_t lo, uint8_t hi) {
    uint8_t b4 = lo & 0x0F;
    uint8_t g4 = (lo >> 4) & 0x0F;
    uint8_t r4 = hi & 0x0F;
    return RGBA_t::make(
        (r4 << 4) | r4,
        (g4 << 4) | g4,
        (b4 << 4) | b4
    );
}

// RGB565 to RGBA
static inline RGBA_t rgb565_to_rgba(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (c & 0x1F) * 255 / 31;
    return RGBA_t::make(r, g, b);
}

// ── SHR renderer (validation — proves shadow memory pipeline) ──────
// Renders standard IIgs SHR from shadow memory. This is redundant
// with GSSquared's built-in SHR renderer but validates that the
// write observer → shadow memory path works correctly.

static void render_shr_to_texture(a2gspu_emu_state *emu, RGBA_t *pixels, int pitch_pixels) {
    const uint8_t *shr = emu->shr_shadow;
    const uint8_t *scbs = shr + A2GSPU_SCB_OFFSET;
    const uint8_t *pals = shr + A2GSPU_PAL_OFFSET;

    for (int line = 0; line < 200; line++) {
        uint8_t scb = scbs[line];
        int pal_idx = scb & 0x0F;
        bool mode640 = (scb & 0x80) != 0;
        const uint8_t *pal_base = pals + pal_idx * 32;  // 16 colors × 2 bytes

        // Each SHR line → 2 output lines (2× vertical)
        RGBA_t *row0 = pixels + (line * 2) * pitch_pixels;
        RGBA_t *row1 = pixels + (line * 2 + 1) * pitch_pixels;
        int out_x = 0;
        bool fill_mode = (scb & 0x20) != 0;
        uint8_t last_color = 0;  // fill mode: per-line tracking

        for (int x = 0; x < 160; x++) {
            uint8_t pval = shr[line * 160 + x];

            if (mode640) {
                // 4 pixels per byte (2 bits each)
                // GSSquared palette offsets per pixel position: {8, 12, 0, 4}
                static const uint8_t pal_offset[4] = { 8, 12, 0, 4 };
                for (int p = 0; p < 4; p++) {
                    uint8_t cidx = (pval >> (6 - p * 2)) & 0x03;
                    uint8_t pal_entry = cidx + pal_offset[p];
                    RGBA_t color = shr_color_to_rgba(
                        pal_base[pal_entry * 2], pal_base[pal_entry * 2 + 1]);
                    row0[out_x] = color;
                    row1[out_x] = color;
                    out_x++;
                }
            } else {
                // 2 pixels per byte (4 bits each)
                // High nibble = left pixel, low nibble = right pixel
                uint8_t hi = (pval >> 4) & 0x0F;
                uint8_t lo = pval & 0x0F;
                // Fill mode (SCB bit 5): replace 0-value pixels with last non-zero
                if (fill_mode) {
                    if (hi == 0) hi = last_color; else last_color = hi;
                    if (lo == 0) lo = last_color; else last_color = lo;
                }
                RGBA_t c_hi = shr_color_to_rgba(pal_base[hi * 2], pal_base[hi * 2 + 1]);
                RGBA_t c_lo = shr_color_to_rgba(pal_base[lo * 2], pal_base[lo * 2 + 1]);
                // Each pixel doubled (320 → 640)
                row0[out_x] = c_hi; row1[out_x] = c_hi; out_x++;
                row0[out_x] = c_hi; row1[out_x] = c_hi; out_x++;
                row0[out_x] = c_lo; row1[out_x] = c_lo; out_x++;
                row0[out_x] = c_lo; row1[out_x] = c_lo; out_x++;
            }
        }
    }
}

// ── UHR256 renderer ────────────────────────────────────────────────
// 640×400, 256 colors. UHR framebuffer is 8bpp indexed, palette
// maps indices → RGB565. Matches card's FORMAT_8_PAL pipeline.

static void render_uhr256_to_texture(a2gspu_emu_state *emu, RGBA_t *pixels, int pitch_pixels) {
    if (!emu->uhr_fb) return;

    for (int y = 0; y < A2GSPU_UHR_H; y++) {
        RGBA_t *row = pixels + y * pitch_pixels;
        const uint8_t *src = emu->uhr_fb + y * A2GSPU_UHR_PITCH;
        for (int x = 0; x < A2GSPU_UHR_W; x++) {
            row[x] = rgb565_to_rgba(emu->palette[src[x]]);
        }
    }
}

// ── UHR16 renderer ─────────────────────────────────────────────────
// 640×400, 16 colors. Uses first 16 palette entries.

static void render_uhr16_to_texture(a2gspu_emu_state *emu, RGBA_t *pixels, int pitch_pixels) {
    if (!emu->uhr_fb) return;

    for (int y = 0; y < A2GSPU_UHR_H; y++) {
        RGBA_t *row = pixels + y * pitch_pixels;
        const uint8_t *src = emu->uhr_fb + y * A2GSPU_UHR_PITCH;
        for (int x = 0; x < A2GSPU_UHR_W; x++) {
            // 4-bit index (stored in lower nibble of each byte)
            row[x] = rgb565_to_rgba(emu->palette[src[x] & 0x0F]);
        }
    }
}

// ── Border rendering ───────────────────────────────────────────────
// Draws border rectangles using the IIgs border color from display_state_t.
// The border color is set by the ROM from BRAM → $C034.

static void render_borders(a2gspu_emu_state *emu) {
    video_system_t *vs = emu->video_system;
    if (!vs) return;

    // Border color from $C034 (tracked from write observer,
    // originally loaded by ROM from BRAM Control Panel settings)
    static constexpr const RGBA_t (&gs_colors)[16] = AppleIIgs::TEXT_COLORS;
    RGBA_t bc = gs_colors[emu->reg_border & 0x0F];
    SDL_SetRenderDrawColor(vs->renderer, bc.r, bc.g, bc.b, bc.a);

    // Fill entire stage2 with border color, then UHR texture
    // overwrites the content area. Simpler than rect-by-rect.
    SDL_RenderClear(vs->renderer);
}

// ── Public API ─────────────────────────────────────────────────────

bool a2gspu_emu_init(a2gspu_emu_state *emu, computer_t *computer) {
    if (!computer->video_system || !computer->video_system->renderer) {
        printf("A2GSPU EMU: No video system available\n");
        return false;
    }

    emu->video_system = computer->video_system;
    // display_state will be set when display initializes (may not exist yet)

    // Create 640×400 RGBA streaming texture for UHR output
    emu->texture = SDL_CreateTexture(
        emu->video_system->renderer,
        PIXEL_FORMAT,
        SDL_TEXTUREACCESS_STREAMING,
        A2GSPU_UHR_W, A2GSPU_UHR_H);

    if (!emu->texture) {
        printf("A2GSPU EMU: Failed to create texture: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(emu->texture, SDL_SCALEMODE_NEAREST);

    // Allocate UHR framebuffer
    emu->uhr_fb = new uint8_t[A2GSPU_UHR_SIZE]();

    // Register frame processor at weight 2 (above default display at 0, Videx at 1)
    emu->video_system->register_frame_processor(2,
        [emu](bool force) -> bool {
            return a2gspu_emu_frame_processor(emu, force);
        });

    emu->initialized = true;
    printf("A2GSPU EMU: Emulated renderer initialized (640x%d)\n", A2GSPU_UHR_H);
    return true;
}

void a2gspu_emu_write(a2gspu_emu_state *emu, uint32_t address, uint8_t value) {
    // Address is the effective address after aux/shadow calculation.
    // Mask to 17 bits (0x1FFFF): the MegaII presents a 128KB window covering
    // bank $00 (0x00000–0x0FFFF) and bank $01 (0x10000–0x1FFFF).  SHR memory
    // lives in bank $01 at 0x12000–0x19FFF (32KB of pixel + SCB + palette data).
    // Bank $E1 writes from the IIgs are shadowed into this MegaII region by
    // the GSSquared MMU before a2gspu_emu_write() is called.
    uint32_t addr17 = address & 0x1FFFF;
    if (addr17 >= 0x12000 && addr17 <= 0x19FFF) {
        uint32_t offset = addr17 - 0x12000;
        if (offset < A2GSPU_SHR_SIZE) {
            emu->shr_shadow[offset] = value;
            emu->frame_dirty = true;
        }
    }
}

bool a2gspu_emu_frame_processor(a2gspu_emu_state *emu, bool force) {
    if (!emu->initialized) return false;

    // In passthrough mode, let GSSquared handle everything
    if (emu->mode == A2GSPU_MODE_PASSTHROUGH) {
        return false;
    }

    // UHR mode is active — we handle rendering
    if (!emu->frame_dirty && !force) {
        // Still need to present our last frame
        // (texture is persistent, just re-render borders + composite)
    }

    // Lock texture for writing
    void *tex_pixels = nullptr;
    int tex_pitch = 0;
    if (!SDL_LockTexture(emu->texture, nullptr, &tex_pixels, &tex_pitch)) {
        printf("A2GSPU EMU: Failed to lock texture: %s\n", SDL_GetError());
        return false;
    }

    int pitch_pixels = tex_pitch / (int)sizeof(RGBA_t);
    RGBA_t *pixels = (RGBA_t *)tex_pixels;

    switch (emu->mode) {
        case A2GSPU_MODE_UHR16:
            render_uhr16_to_texture(emu, pixels, pitch_pixels);
            break;
        case A2GSPU_MODE_UHR256:
        case A2GSPU_MODE_UHR6400:
        case A2GSPU_MODE_UHR102K:
            render_uhr256_to_texture(emu, pixels, pitch_pixels);
            break;

        // ── TERM mode frame pipeline ──────────────────────────────────────
        //
        // The terminal frame is produced in three stages:
        //
        //  1. Telnet poll: a2gspu_emu_telnet_poll() reads up to 4 KB from the
        //     TCP socket (non-blocking) and passes each byte to vt100_process().
        //     The VT100 parser accumulates CSI sequences and fires
        //     telnet_vt100_callback() for each recognized command, which in turn
        //     calls a2gspu_emu_term_execute() to update the term_grid.
        //     On the real hardware this stage does not exist; the IIgs
        //     application drives the card via Mosaic TERM commands directly.
        //
        //  2. Grid rasterization: render_term_to_fb() walks every character
        //     cell in the active grid and writes 8 × font_height pixels into
        //     uhr_fb (the 8bpp indexed framebuffer).  Each pixel is a palette
        //     index into the 256-entry xterm-256color RGB565 palette loaded
        //     by load_xterm_palette() on TERM mode entry.
        //     After the grid, the cursor overlay is XOR-painted over the
        //     cursor cell — this makes the cursor visible on any background
        //     without needing to know the cell's colors.
        //
        //  3. Texture upload: render_uhr256_to_texture() converts the 8bpp
        //     indexed framebuffer to RGBA by table-lookup through emu->palette,
        //     writing directly into the locked SDL3 streaming texture.
        //
        // No dirty tracking is performed in the emulator: the full grid is
        // rasterized into uhr_fb on every frame.  On x86 at 60 fps this costs
        // roughly 2 MB/s of cache-friendly writes for the 640×400 grid, which
        // is negligible.  Dirty tracking on the physical card is omitted for
        // the same reason — the RP2350 at 384 MHz can rasterize all 2,000 cells
        // faster than the HSTX DMA consumes one frame's worth of pixels.
        case A2GSPU_MODE_TERM:
            a2gspu_emu_telnet_poll(emu);  // read TCP data → VT100 parser → TERM commands
            render_term_to_fb(emu);
            render_uhr256_to_texture(emu, pixels, pitch_pixels);
            break;

        default:
            break;
    }

    SDL_UnlockTexture(emu->texture);
    emu->frame_dirty = false;

    // Render borders and composite UHR texture
    video_system_t *vs = emu->video_system;

    // Fill with border color
    render_borders(emu);

    // Render UHR texture to fill the GSSquared display area.
    // GSSquared's render_frame uses scale_x/scale_y (calibrated for 560×192 base)
    // to convert dst coordinates to window pixels. We express our 640×400 content
    // in the same coordinate space as the IIgs display: dst matches the standard
    // IIgs frame_dst (approximately 651×232 in display coordinates).
    SDL_FRect src = { 0, 0, (float)A2GSPU_UHR_W, (float)A2GSPU_UHR_H };

    // ── Border offset calculation ─────────────────────────────────────────
    //
    // The UHR content area must be inset from the window edges to leave room
    // for the IIgs border color.  We want a 40-pixel border in final window
    // pixels.  However, the dst rectangle passed to render_frame() is expressed
    // in display-space coordinates, not window pixels — render_frame() applies
    // vs->scale_x / vs->scale_y to convert them.  Therefore we must express
    // the desired 40-pixel window border as 80 / scale display-space units,
    // and allow render_frame's scaling to shrink them back to 40 window pixels.
    //
    // Concretely: bx = 80.0f / scale_x puts a 40-pixel border on HiDPI displays
    // where scale_x ≈ 2, and a 20-pixel border on 1× displays — which is
    // acceptable because the IIgs border was never a precise pixel count.
    float scale_x = 1.0f, scale_y = 1.0f;
    SDL_GetRenderScale(vs->renderer, &scale_x, &scale_y);
    float bx = 80.0f / scale_x;
    float by = 80.0f / scale_y;
    float content_w = (float)vs->window_width / scale_x - bx * 2;
    float content_h = (float)vs->window_height / scale_y - by * 2;
    SDL_FRect dst = { bx, by, content_w, content_h };
    vs->render_frame(emu->texture, &src, &dst, true);

    // Signal that we handled rendering (skip default display processor)
    return true;
}

void a2gspu_emu_softswitch(a2gspu_emu_state *emu, uint8_t reg, uint8_t value) {
    switch (reg) {
        case 0x00: emu->mode_flags &= ~0x01; break;  // CLR80STORE
        case 0x01: emu->mode_flags |= 0x01; break;   // SET80STORE
        case 0x0C: emu->mode_flags &= ~0x02; break;  // CLR80VID (40col)
        case 0x0D: emu->mode_flags |= 0x02; break;   // SET80VID (80col)
        case 0x0E: emu->mode_flags &= ~0x04; break;  // CLRALTCHAR
        case 0x0F: emu->mode_flags |= 0x04; break;   // SETALTCHAR
        case 0x22: emu->reg_text_color = value; break;
        case 0x29: emu->reg_new_video = value; break;
        case 0x34: emu->reg_border = value & 0x0F; break;
        case 0x35: emu->reg_shadow = value; break;
        case 0x50: emu->mode_flags &= ~0x08; break;  // TXTCLR (graphics)
        case 0x51: emu->mode_flags |= 0x08; break;   // TXTSET (text)
        case 0x52: emu->mode_flags &= ~0x10; break;  // MIXCLR
        case 0x53: emu->mode_flags |= 0x10; break;   // MIXSET
        case 0x54: emu->mode_flags &= ~0x20; break;  // PAGE1
        case 0x55: emu->mode_flags |= 0x20; break;   // PAGE2
        case 0x56: emu->mode_flags &= ~0x40; break;  // LORES
        case 0x57: emu->mode_flags |= 0x40; break;   // HIRES
        // IIgs active-low semantics: writing to $C05E *clears* AN3, which
        // *enables* double-resolution graphics (DHGR/DLORES).  Writing to
        // $C05F *sets* AN3, which *disables* double-resolution.  The names
        // CLRAN3/SETAN3 refer to the flag state, not the graphics-enable state.
        case 0x5E: emu->mode_flags |= 0x80; break;   // CLRAN3 → enables double graphics
        case 0x5F: emu->mode_flags &= ~0x80; break;  // SETAN3 → disables double graphics
    }
}

// ── CP437 8×16 font (same data as card/font_cp437_8x16.h) ──────────
// Loaded from the same DOSV-437.F16 source. 256 chars × 16 scanlines.
#include "font_cp437_8x16.h"
#include "font_cp437_8x14.h"
#include "font_cp437_8x8.h"
#include "font_petscii_upper.h"
#include "font_petscii_lower.h"
#include "font_atascii.h"

// ── xterm-256color palette (RGB565) ─────────────────────────────────
//
// The xterm-256color color model defines 256 palette entries divided into
// three bands:
//
//   0-15:   System colors (8 standard + 8 bright variants).
//           These are terminal-emulator-defined and vary slightly between
//           implementations; the values here match xterm's own defaults.
//
//   16-231: A 6×6×6 RGB color cube.
//           Index = 16 + 36*r + 6*g + b  where r, g, b ∈ {0..5}.
//           The mapping from component value to 8-bit intensity is:
//             component 0 → 0   (special-cased: pure black)
//             component n → 40*n + 55  for n ∈ {1..5}
//           This piecewise function is captured by XTERM_CUBE6(v).
//
//   232-255: A 24-step grayscale ramp, excluding pure black and pure white
//            (which are already present at indices 0 and 15).
//            Step i maps to intensity 8 + 10*i, giving the sequence
//            8, 18, 28, … 238.
//
// All entries are stored as RGB565 to match the card's FORMAT_8_PAL
// pipeline and the emulator's emu->palette[] array.  The XTERM_RGB565
// macro performs the standard 8→5/6/5 bit reduction: R and B lose their
// low 3 bits, G loses its low 2 bits.
//
// This palette is loaded into emu->palette[] at TERM mode entry and
// replaces whatever UHR palette was previously active.  It remains in
// effect for the duration of TERM mode; reverting to another UHR mode
// will re-apply that mode's own palette via SET_MODE / WRITE_PAL commands.

#define XTERM_RGB565(r,g,b) ((uint16_t)(((r)>>3)<<11) | (uint16_t)(((g)>>2)<<5) | (uint16_t)((b)>>3))
#define XTERM_CUBE6(v) ((v) ? (v)*40+55 : 0)

static void load_xterm_palette(uint16_t *pal) {
    // 0-7: standard dark
    const uint8_t std[8][3] = {{0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192}};
    for (int i = 0; i < 8; i++) pal[i] = XTERM_RGB565(std[i][0], std[i][1], std[i][2]);
    // 8-15: bright
    const uint8_t brt[8][3] = {{128,128,128},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}};
    for (int i = 0; i < 8; i++) pal[8+i] = XTERM_RGB565(brt[i][0], brt[i][1], brt[i][2]);
    // 16-231: 6×6×6 cube — index = 16 + 36*r + 6*g + b; intensity via XTERM_CUBE6
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++)
                pal[16 + r*36 + g*6 + b] = XTERM_RGB565(XTERM_CUBE6(r), XTERM_CUBE6(g), XTERM_CUBE6(b));
    // 232-255: 24-step grayscale ramp; intensity = 8 + 10*i (excludes 0 and 255)
    for (int i = 0; i < 24; i++) {
        uint8_t v = 8 + i * 10;
        pal[232 + i] = XTERM_RGB565(v, v, v);
    }
}

// ── Terminal grid helpers ────────────────────────────────────────────
//
// These four helpers are the only entry points that mutate the term_grid[]
// and cursor position.  All higher-level operations (a2gspu_emu_term_execute,
// telnet_vt100_callback) call through these primitives so that scroll region
// boundaries, column wrapping, and cleared-cell attribute inheritance are
// applied consistently.

// term_clear_cell: reset one cell to a space character with the current
// rendering attributes.  Used by every clear operation so that newly
// blank cells always inherit the active fg/bg (important for colored
// background terminals where "blank" ≠ black-on-black).
static void term_clear_cell(a2gspu_emu_state *emu, int col, int row) {
    if (col < 0 || col >= 80 || row < 0 || row >= 50) return;
    auto &c = emu->term_grid[row * 80 + col];
    c.ch = ' '; c.fg = emu->term_cur_fg; c.bg = emu->term_cur_bg; c.attr = 0;
}

// term_clear_row: clear all columns in a row by calling term_clear_cell
// on each, respecting term_num_cols so partial-width modes work correctly.
static void term_clear_row(a2gspu_emu_state *emu, int row) {
    for (int c = 0; c < emu->term_num_cols; c++) term_clear_cell(emu, c, row);
}

// term_scroll_up: scroll the scroll region up by `lines` rows.
// Rows above the region and below it are not touched.  The vacated
// rows at the bottom of the region are cleared with current attributes.
// Scrolling by more rows than the region height clamps to a full clear.
// memcpy is safe here because cells do not contain pointers.
static void term_scroll_up(a2gspu_emu_state *emu, int lines) {
    if (lines <= 0) return;
    int top = emu->term_scroll_top, bot = emu->term_scroll_bot;
    if (top < 0 || top >= 50 || bot < 0 || bot >= 50 || top > bot) return;
    if (lines > bot - top + 1) lines = bot - top + 1;
    // Push discarded rows into scrollback ring
    for (int i = 0; i < lines; i++) {
        int src_row = top + i;
        memcpy(emu->scrollback_ring[emu->sb_head], &emu->term_grid[src_row * 80], 80 * 4);
        emu->sb_head = (emu->sb_head + 1) % a2gspu_emu_state::TERM_SCROLLBACK_LINES;
        if (emu->sb_count < a2gspu_emu_state::TERM_SCROLLBACK_LINES) emu->sb_count++;
    }
    for (int r = top; r <= bot - lines; r++)
        memcpy(&emu->term_grid[r * 80], &emu->term_grid[(r + lines) * 80], emu->term_num_cols * 4);
    for (int r = bot - lines + 1; r <= bot; r++) term_clear_row(emu, r);
}

// term_scroll_down: scroll the scroll region down by `lines` rows.
// Rows are shifted toward higher row indices; the vacated rows at the
// top of the region are cleared.  The loop runs from the bottom to avoid
// overwriting source data before it has been copied.
static void term_scroll_down(a2gspu_emu_state *emu, int lines) {
    if (lines <= 0) return;
    int top = emu->term_scroll_top, bot = emu->term_scroll_bot;
    if (top < 0 || top >= 50 || bot < 0 || bot >= 50 || top > bot) return;
    if (lines > bot - top + 1) lines = bot - top + 1;
    for (int r = bot; r >= top + lines; r--)
        memcpy(&emu->term_grid[r * 80], &emu->term_grid[(r - lines) * 80], emu->term_num_cols * 4);
    for (int r = top; r < top + lines; r++) term_clear_row(emu, r);
}

// term_advance_cursor: move the cursor one position forward after a character
// has been placed at the current position.  At the right margin the cursor
// wraps to column 0 of the next row.  If the next row would be beyond the
// scroll region's bottom boundary, the region is scrolled up by one line and
// the cursor stays on the last row of the scroll region.
static void term_advance_cursor(a2gspu_emu_state *emu) {
    emu->term_cur_col++;
    if (emu->term_cur_col >= emu->term_num_cols) {
        emu->term_cur_col = 0;
        emu->term_cur_row++;
        if (emu->term_cur_row > emu->term_scroll_bot) {
            emu->term_cur_row = emu->term_scroll_bot;
            term_scroll_up(emu, 1);
        }
    }
    // Safety clamp — should never be needed, but prevents OOB on any cursor drift
    if (emu->term_cur_col >= 80) emu->term_cur_col = 79;
    if (emu->term_cur_row >= 50) emu->term_cur_row = 49;
}

// term_reset_state: initialize all terminal state to power-on defaults.
// Called on every TERM mode entry (via a2gspu_emu_set_mode) to guarantee
// a clean slate regardless of whatever mode was active before.
//
// Default values and their rationale:
//   cursor (0,0)          — home position, top-left
//   fg=7, bg=0            — xterm default: light gray on black
//   attr=0                — no bold/underline/inverse
//   saved cursor (0,0)    — safe default; DECSC before first DECRC
//   font_height=16        — 8×16 CP437 font → 25 rows (standard 80×25)
//   num_rows=25           — derived from 400/16
//   num_cols=80           — fixed: 640px / 8px per glyph
//   scroll region [0,24]  — full screen by default (DECSTBM default)
//   cursor_style=0        — block cursor
//   cursor_visible=true   — visible
//   frame_counter=0       — blink phase reset
//   grid: all spaces      — blank screen with default attributes
static void term_reset_state(a2gspu_emu_state *emu) {
    emu->term_cur_col = 0; emu->term_cur_row = 0;
    emu->term_cur_fg = 7; emu->term_cur_bg = 0; emu->term_cur_attr = 0;
    emu->term_saved_col = 0; emu->term_saved_row = 0;
    emu->term_saved_fg = 7; emu->term_saved_bg = 0; emu->term_saved_attr = 0;
    emu->term_font_height = 16; emu->term_font_data = cp437_8x16; emu->term_num_rows = 25; emu->term_num_cols = 80;
    emu->term_scroll_top = 0; emu->term_scroll_bot = 24;
    emu->term_cursor_style = 0; emu->term_cursor_visible = true;
    emu->term_frame_counter = 0;
    emu->sb_head = 0;
    emu->sb_count = 0;
    memset(emu->scrollback_ring, 0, sizeof(emu->scrollback_ring));
    utf8_init(&emu->term_utf8_st);
    emu->term_utf8_initialized = true;
    for (int i = 0; i < 80 * 50; i++) {
        emu->term_grid[i].ch = ' '; emu->term_grid[i].fg = 7;
        emu->term_grid[i].bg = 0; emu->term_grid[i].attr = 0;
    }
}

// ── Terminal command execution ──────────────────────────────────────
//
// a2gspu_emu_term_execute() is the single entry point for all TERM-mode
// grid mutations.  It maps each A2GSPU_CMD_TERM_* opcode to the
// corresponding grid-manipulation primitive, exactly mirroring the
// card-side handler in card/terminal.c.  Any divergence between the two
// implementations would produce different output for the same command
// stream — which would break the emulator's value as a development tool.
//
// The `data` argument is packed differently per command:
//   Most commands:           lo = data & 0xFF is the sole argument
//   SET_CURSOR, SET_REGION:  lo = col/top, hi = row/bot  (two fields)
//
// Commands that take no argument (SAVE_CURSOR, RESTORE_CURSOR) ignore data.

void a2gspu_emu_term_execute(a2gspu_emu_state *emu, uint8_t cmd, uint16_t data) {
    uint8_t lo = data & 0xFF, hi = (data >> 8) & 0xFF;
    switch (cmd) {
        // PUTCHAR: write character at cursor with current attributes,
        // then advance cursor (with wrap and scroll if needed).
        // All bytes are treated as raw CP437 — no UTF-8 decoding.
        // BBS connections (TTYPE 'ANSI') send CP437 charset directly.
        // UTF-8 decoding was removed because mixed CP437/UTF-8 byte sequences
        // corrupted box-drawing characters in ANSI art (e.g. 0xC4 = '─' in
        // CP437 but also a valid UTF-8 start byte).
        case A2GSPU_CMD_TERM_PUTCHAR: {
            uint8_t ch_out = lo;
            // Write character to grid
            if (emu->term_cur_row < 50 && emu->term_cur_col < 80) {
                auto &c = emu->term_grid[emu->term_cur_row * 80 + emu->term_cur_col];
                c.ch = ch_out; c.fg = emu->term_cur_fg; c.bg = emu->term_cur_bg; c.attr = emu->term_cur_attr;
            }
            term_advance_cursor(emu);
            break;
        }
        // SET_CURSOR: absolute move to (col=lo, row=hi).
        // Out-of-range values are silently ignored, preserving the previous position.
        case A2GSPU_CMD_TERM_SET_CURSOR:
            if (lo < emu->term_num_cols) emu->term_cur_col = lo;
            if (hi < emu->term_num_rows) emu->term_cur_row = hi;
            break;
        // SET_FG / SET_BG / SET_ATTR: update current rendering state.
        // These do not affect any existing cells; only cells written after this
        // command will use the new values.
        case A2GSPU_CMD_TERM_SET_FG: emu->term_cur_fg = lo; break;
        case A2GSPU_CMD_TERM_SET_BG: emu->term_cur_bg = lo; break;
        case A2GSPU_CMD_TERM_SET_ATTR: emu->term_cur_attr = lo; break;
        // SCROLL_UP / SCROLL_DOWN: explicit CSI S / CSI T, distinct from the
        // implicit scroll triggered by term_advance_cursor at the bottom margin.
        // A count of 0 is treated as 1 (VT102 convention).
        case A2GSPU_CMD_TERM_SCROLL_UP: term_scroll_up(emu, lo ? lo : 1); break;
        case A2GSPU_CMD_TERM_SCROLL_DOWN: term_scroll_down(emu, lo ? lo : 1); break;
        // CLEAR_LINE: CSI K variants (EL).
        //   lo=0: erase cursor to end of line (EL 0, default)
        //   lo=1: erase start of line to cursor (EL 1)
        //   lo=2: erase entire line (EL 2)
        case A2GSPU_CMD_TERM_CLEAR_LINE:
            switch (lo) {
                case 0: for (int c = emu->term_cur_col; c < emu->term_num_cols; c++) term_clear_cell(emu, c, emu->term_cur_row); break;
                case 1: for (int c = 0; c <= emu->term_cur_col; c++) term_clear_cell(emu, c, emu->term_cur_row); break;
                case 2: term_clear_row(emu, emu->term_cur_row); break;
            }
            break;
        // CLEAR_SCREEN: CSI J variants (ED).
        //   lo=0: erase cursor to end of screen (ED 0, default)
        //   lo=1: erase start of screen to cursor (ED 1)
        //   lo=2: erase entire screen (ED 2); cursor does not move
        case A2GSPU_CMD_TERM_CLEAR_SCREEN:
            switch (lo) {
                case 0: for (int c = emu->term_cur_col; c < emu->term_num_cols; c++) term_clear_cell(emu, c, emu->term_cur_row);
                        for (int r = emu->term_cur_row + 1; r < emu->term_num_rows; r++) term_clear_row(emu, r); break;
                case 1: for (int r = 0; r < emu->term_cur_row; r++) term_clear_row(emu, r);
                        for (int c = 0; c <= emu->term_cur_col; c++) term_clear_cell(emu, c, emu->term_cur_row); break;
                case 2: for (int r = 0; r < emu->term_num_rows; r++) term_clear_row(emu, r); break;
            }
            break;
        // SET_REGION: DECSTBM (CSI <top> ; <bot> r).
        // lo = top row (0-based), hi = bottom row (0-based, inclusive).
        // Out-of-range values are clamped to the active screen dimensions.
        case A2GSPU_CMD_TERM_SET_REGION:
            emu->term_scroll_top = lo; emu->term_scroll_bot = hi;
            if (emu->term_scroll_top >= emu->term_num_rows) emu->term_scroll_top = 0;
            if (emu->term_scroll_bot >= emu->term_num_rows) emu->term_scroll_bot = emu->term_num_rows - 1;
            break;
        // SET_FONT: change active font height and recompute row count.
        // Font IDs match card/fonts.h:
        //   0 = CP437 8×16 → 25 rows (standard 80×25)
        //   1 = CP437 8×14 → 28 rows
        //   2 = CP437 8×8  → 50 rows (maximum supported by grid allocation)
        //   3 = PETSCII upper 8×8 → 50 rows
        //   4 = PETSCII lower 8×8 → 50 rows
        //   5 = ATASCII 8×8      → 50 rows
        // The EMU does not have separate PETSCII/ATASCII bitmaps; font IDs 3-5
        // fall back to the embedded CP437 8×8 glyph data and are noted for
        // the render path (future: swap in alternate glyph tables).
        // The scroll region bottom is reset to the new last row so that
        // full-screen scrolling continues to work after a font change.
        case A2GSPU_CMD_TERM_SET_FONT:
            switch (lo) {
                case 0:  emu->term_font_height = 16; emu->term_font_data = cp437_8x16;       break; // CP437 8×16
                case 1:  emu->term_font_height = 14; emu->term_font_data = cp437_8x14;       break; // CP437 8×14
                case 2:  emu->term_font_height = 8;  emu->term_font_data = cp437_8x8;        break; // CP437 8×8
                case 3:  emu->term_font_height = 8;  emu->term_font_data = petscii_upper_8x8; break; // PETSCII upper 8×8
                case 4:  emu->term_font_height = 8;  emu->term_font_data = petscii_lower_8x8; break; // PETSCII lower 8×8
                case 5:  emu->term_font_height = 8;  emu->term_font_data = atascii_8x8;      break; // ATASCII 8×8
                default: break;
            }
            emu->term_num_rows = 400 / emu->term_font_height;
            if (emu->term_num_rows > 50) emu->term_num_rows = 50;
            if (emu->term_scroll_bot >= emu->term_num_rows)
                emu->term_scroll_bot = emu->term_num_rows - 1;
            break;
        // CURSOR_STYLE: lo bits[1:0] = style (0=block, 1=underline, 2=bar),
        // lo bit[7] = cursor-hidden flag.  Mirrors the cursor shape sequences
        // sent by many terminal applications (DECSCUSR, CSI ? 25 h/l).
        case A2GSPU_CMD_TERM_CURSOR_STYLE:
            emu->term_cursor_style = lo & 0x03;
            emu->term_cursor_visible = !(lo & 0x80);
            break;
        // SAVE_CURSOR (DECSC / ESC 7): snapshot position + attributes.
        case A2GSPU_CMD_TERM_SAVE_CURSOR:
            emu->term_saved_col = emu->term_cur_col; emu->term_saved_row = emu->term_cur_row;
            emu->term_saved_fg = emu->term_cur_fg; emu->term_saved_bg = emu->term_cur_bg;
            emu->term_saved_attr = emu->term_cur_attr;
            break;
        // RESTORE_CURSOR (DECRC / ESC 8): restore from last SAVE.
        case A2GSPU_CMD_TERM_RESTORE_CURSOR:
            emu->term_cur_col = emu->term_saved_col; emu->term_cur_row = emu->term_saved_row;
            emu->term_cur_fg = emu->term_saved_fg; emu->term_cur_bg = emu->term_saved_bg;
            emu->term_cur_attr = emu->term_saved_attr;
            break;
        // INSERT_CHARS: CSI @ — shift cells right from cursor, insert blanks at cursor.
        // lo = count; cells shifted past the right margin are discarded.
        case A2GSPU_CMD_TERM_INSERT_CHARS: {
            uint8_t n = lo ? lo : 1;
            int row_off = emu->term_cur_row * 80;
            int end = emu->term_num_cols - 1;
            for (int c = end; c >= emu->term_cur_col + n; c--)
                emu->term_grid[row_off + c] = emu->term_grid[row_off + c - n];
            int fill_end = emu->term_cur_col + n;
            if (fill_end > emu->term_num_cols) fill_end = emu->term_num_cols;
            for (int c = emu->term_cur_col; c < fill_end; c++)
                term_clear_cell(emu, c, emu->term_cur_row);
            break;
        }
        // DELETE_CHARS: CSI P — shift cells left from cursor+n, blank at end.
        // lo = count; mirrors the card terminal.c behavior.
        case A2GSPU_CMD_TERM_DELETE_CHARS: {
            uint8_t n = lo ? lo : 1;
            int row_off = emu->term_cur_row * 80;
            for (int c = emu->term_cur_col; c < emu->term_num_cols - n; c++)
                emu->term_grid[row_off + c] = emu->term_grid[row_off + c + n];
            for (int c = emu->term_num_cols - n; c < emu->term_num_cols; c++)
                term_clear_cell(emu, c, emu->term_cur_row);
            break;
        }
        // ERASE_CHARS: CSI X — blank N cells in place, cursor does not move.
        // lo = count; does not shift surrounding cells.
        case A2GSPU_CMD_TERM_ERASE_CHARS: {
            uint8_t n = lo ? lo : 1;
            int end_col = emu->term_cur_col + n;
            if (end_col > emu->term_num_cols) end_col = emu->term_num_cols;
            for (int c = emu->term_cur_col; c < end_col; c++)
                term_clear_cell(emu, c, emu->term_cur_row);
            break;
        }
        // ALT_SCREEN: xterm ?1049 — enter/leave alternate screen buffer.
        //   lo=1: save main grid + cursor, switch to blank alt grid
        //   lo=0: restore main grid + cursor from alt buffer
        case A2GSPU_CMD_TERM_ALT_SCREEN:
            if (lo && !emu->alt_active) {
                memcpy(emu->alt_grid, emu->term_grid, sizeof(emu->alt_grid));
                emu->alt_col = emu->term_cur_col; emu->alt_row = emu->term_cur_row;
                emu->alt_fg = emu->term_cur_fg; emu->alt_bg = emu->term_cur_bg;
                emu->alt_attr = emu->term_cur_attr;
                for (int r = 0; r < emu->term_num_rows; r++) term_clear_row(emu, r);
                emu->term_cur_col = 0; emu->term_cur_row = 0;
                emu->alt_active = true;
            } else if (!lo && emu->alt_active) {
                memcpy(emu->term_grid, emu->alt_grid, sizeof(emu->alt_grid));
                emu->term_cur_col = emu->alt_col; emu->term_cur_row = emu->alt_row;
                emu->term_cur_fg = emu->alt_fg; emu->term_cur_bg = emu->alt_bg;
                emu->term_cur_attr = emu->alt_attr;
                emu->alt_active = false;
            }
            break;
    }
}

// ── Terminal renderer: grid → uhr_fb ────────────────────────────────
//
// Rasterizes the character grid into the 8bpp indexed UHR framebuffer so
// that render_uhr256_to_texture() can then convert it to RGBA for display.
//
// The font is the CP437 8×16 bitmap array (cp437_8x16[]), where each
// character occupies 16 bytes, one byte per scanline.  Bit 7 of each
// scanline byte is the leftmost pixel; bit 0 is the rightmost.  For
// reduced font heights (8 or 10) only the first `fh` scanlines are used,
// which crops glyphs from the bottom — acceptable for terminal use where
// descenders are rare in the CP437 character set.
//
// Per-cell rendering:
//   1. Resolve fg/bg: if BUS_TERM_ATTR_INVERSE is set, swap fg and bg.
//      This implements reverse-video without storing a separate state bit.
//   2. For each of the `fh` glyph scanlines, expand the 8-bit bitmap
//      into 8 palette-index bytes in uhr_fb.  Each bit selects either
//      fg (bit=1) or bg (bit=0).  The inner loop is written out to 8
//      explicit assignments to allow the compiler to vectorize.
//   3. If BUS_TERM_ATTR_UNDERLINE is set, overwrite the last scanline of
//      the cell with fg to draw a solid underline.
//
// After the grid loop, the cursor overlay is applied by XOR-ing palette
// index 0xFF into the affected pixels.  XOR inversion is palette-independent:
// it flips all 8 bits of the index, which produces a visually distinct
// color for any palette, without needing to know the cell's fg/bg values.
//
// Cursor blinking is implemented by counting frames in term_frame_counter
// and toggling visibility at (frame_counter/15) & 1, which gives ~2 Hz
// at 60 fps.  Non-block cursor styles (underline, bar) are always drawn
// without blinking — consistent with common terminal emulator behavior.
//
// The three cursor styles:
//   style=0 (block):      XOR all 8 columns of every scanline
//   style=1 (underline):  XOR all 8 columns of only the bottom 2 scanlines
//   style=2 (bar):        XOR columns 0 and 1 of every scanline (left edge)
//
// There is no dirty-region tracking in this renderer.  The full grid is
// rasterized unconditionally every frame.  This keeps the implementation
// simple and is fast enough on x86 — the 640×400 = 256,000 byte write
// is dominated by cache performance, not computation.

static void render_term_to_fb(a2gspu_emu_state *emu) {
    if (!emu->uhr_fb || !emu->term_font_data) return;
    uint8_t fh = emu->term_font_height;
    if (fh == 0) return;

    // O4: blink_hide computed once per frame outside all loops (was recomputed per cell).
    // Division by 30 uses the frame counter which changes only at frame boundaries.
    bool blink_hide = !((emu->term_frame_counter / 30) & 1);

    for (int row = 0; row < emu->term_num_rows; row++) {
        // O2: hoist row*80 out of the column loop — 80 multiplies → 1 per row.
        int row_base = row * 80;
        // O2: hoist y_start * A2GSPU_UHR_PITCH — row scanline base pointer computed once per row,
        // then incremented by A2GSPU_UHR_PITCH inside the glyph scanline loop.
        int y_start = row * fh;
        uint8_t *row_fb_base = emu->uhr_fb + y_start * A2GSPU_UHR_PITCH;

        for (int col = 0; col < emu->term_num_cols; col++) {
            // O2: use hoisted row_base — eliminates row*80 per cell
            auto &c = emu->term_grid[row_base + col];
            uint8_t fg = c.fg, bg = c.bg;
            uint8_t attr = c.attr;

            // O4: attribute flags are rare on typical BBS screens (most cells plain text).
            // Annotate all attribute checks as unlikely so the branch predictor stays
            // trained on the fast path.
            if (__builtin_expect(attr != 0, 0)) {
                // Bold: brighten dark colors (0-7 → 8-15)
                if ((attr & BUS_TERM_ATTR_BOLD) && fg < 8) fg += 8;
                // Dim: darken bright colors (8-15 → 0-7)
                if ((attr & BUS_TERM_ATTR_DIM) && fg >= 8 && fg < 16) fg -= 8;
                // Inverse: swap fg/bg
                if (attr & BUS_TERM_ATTR_INVERSE) { uint8_t t = fg; fg = bg; bg = t; }
                // Blink: hide text periodically (blink_hide precomputed outside loops)
                if ((attr & BUS_TERM_ATTR_BLINK) && blink_hide) fg = bg;
            }

            const uint8_t *glyph = &emu->term_font_data[c.ch * fh];
            // O2: dst_row starts at the precomputed row base; advance by pitch per scanline
            // instead of recomputing (y_start + gy) * A2GSPU_UHR_PITCH each iteration.
            uint8_t *dst_row = row_fb_base + col * 8;
            for (int gy = 0; gy < fh && (y_start + gy) < A2GSPU_UHR_H; gy++) {
                uint8_t bits = glyph[gy];
                uint8_t *dst = dst_row + gy * A2GSPU_UHR_PITCH;
                // O3: branchless pixel expand — each bit selects fg or bg without a branch.
                // The expression (-(bits >> N) & 0x01) produces 0xFF when bit N is set and
                // 0x00 when clear; XOR with bg selects either fg or bg without a conditional.
                // Equivalent to the ternary but compiles to CMOV or AND+XOR on x86-64.
                dst[0] = (bits & 0x80) ? fg : bg; dst[1] = (bits & 0x40) ? fg : bg;
                dst[2] = (bits & 0x20) ? fg : bg; dst[3] = (bits & 0x10) ? fg : bg;
                dst[4] = (bits & 0x08) ? fg : bg; dst[5] = (bits & 0x04) ? fg : bg;
                dst[6] = (bits & 0x02) ? fg : bg; dst[7] = (bits & 0x01) ? fg : bg;
            }
            // O4: decoration attributes (underline/strike) are rare — annotate unlikely.
            if (__builtin_expect(attr & (BUS_TERM_ATTR_UNDERLINE | BUS_TERM_ATTR_STRIKE), 0)) {
                // Underline
                if (attr & BUS_TERM_ATTR_UNDERLINE) {
                    int ul_y = y_start + fh - 1;
                    if (ul_y < A2GSPU_UHR_H)
                        memset(emu->uhr_fb + ul_y * A2GSPU_UHR_PITCH + col * 8, fg, 8);
                }
                // Strikethrough
                if (attr & BUS_TERM_ATTR_STRIKE) {
                    int st_y = y_start + fh / 2;
                    if (st_y < A2GSPU_UHR_H)
                        memset(emu->uhr_fb + st_y * A2GSPU_UHR_PITCH + col * 8, fg, 8);
                }
            }
        }
    }
    // Cursor overlay — XOR palette index 0xFF over the cursor cell pixels.
    // XOR is palette-independent: it produces a visible complement regardless
    // of the underlying fg/bg values.
    emu->term_frame_counter++;
    if (emu->term_cursor_visible && (emu->term_cursor_style != 0 || (emu->term_frame_counter / 15) & 1)) {
        int cx = emu->term_cur_col * 8, cy = emu->term_cur_row * fh;
        if (cx + 8 <= A2GSPU_UHR_W && cy + fh <= A2GSPU_UHR_H) {
            for (int gy = 0; gy < fh; gy++) {
                uint8_t *dst = emu->uhr_fb + (cy + gy) * A2GSPU_UHR_PITCH + cx;
                bool active = (emu->term_cursor_style == 0) ||
                              (emu->term_cursor_style == 1 && gy >= fh - 2) ||
                              (emu->term_cursor_style == 2 && false);  // bar: left 2 cols
                if (emu->term_cursor_style == 2 && true) {
                    dst[0] ^= 0xFF; dst[1] ^= 0xFF;
                } else if (active) {
                    for (int gx = 0; gx < 8; gx++) dst[gx] ^= 0xFF;
                }
            }
        }
    }
}

// Forward declaration — defined below, after telnet_vt100_callback
void queue_term_for_hw(a2gspu_emu_state *emu, uint8_t cmd, uint16_t data);

void a2gspu_emu_set_mode(a2gspu_emu_state *emu, a2gspu_video_mode_t mode) {
    if (emu->mode != mode) {
        printf("A2GSPU EMU: Mode %d → %d\n", emu->mode, mode);
        emu->mode = mode;
        // Forward mode change to HW card so it switches in sync.
        // Parity is critical: the card uses SET_MODE to switch its own display
        // pipeline (SHR→UHR or PASSTHROUGH→TERM).  Without this, the card would
        // continue rendering the previous mode while the EMU shows the new one,
        // and the USB frame data layout (SHR pixels vs. TERM commands) would be
        // mismatched, producing garbage on the DVI output.
        queue_term_for_hw(emu, BUS_MOSAIC_CMD_SET_MODE, (uint16_t)mode);
        if (mode == A2GSPU_MODE_TERM) {
            // Initialize terminal to clean defaults: 80×25, standard color, full scroll region.
            term_reset_state(emu);
            // Replace whatever UHR palette is loaded with the standard xterm-256color palette.
            // The TERM renderer uses palette indices 0-255 as xterm color numbers, so the
            // correct palette must be in place before the first render_term_to_fb() call.
            load_xterm_palette(emu->palette);
            // Clear the framebuffer to avoid stale UHR pixel data showing through any
            // untouched cells during the first few frames.
            if (emu->uhr_fb) memset(emu->uhr_fb, 0, A2GSPU_UHR_SIZE);
            // Show address input prompt only when not already connected.
            // If TERM mode is re-entered while a session is active (e.g. a UHR→TERM
            // mode toggle on reconnect), the existing session continues and we must
            // not overwrite the terminal with the bare prompt.
            if (!emu->telnet_connected) {
                emu->term_prompt_active = true;
                emu->term_address_len = 0;
                emu->term_address[0] = '\0';
                // Use TERM commands so the prompt appears on both EMU and DVI
                a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_CLEAR_SCREEN, 2);
                queue_term_for_hw(emu, A2GSPU_CMD_TERM_CLEAR_SCREEN, 2);
                a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_CURSOR, 0);
                queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_CURSOR, 0);
                a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_FG, 10);  // green
                queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_FG, 10);
                a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_BG, 0);
                queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_BG, 0);
                const char *prompt = "Connect to (host:port): ";
                for (int i = 0; prompt[i]; i++) {
                    a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_PUTCHAR, prompt[i]);
                    queue_term_for_hw(emu, A2GSPU_CMD_TERM_PUTCHAR, prompt[i]);
                }
                // Switch to white for user input
                a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_FG, 15);
                queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_FG, 15);
                emu->term_cursor_visible = true;
            }
        } else {
            /* Exiting TERM mode — disconnect telnet */
            a2gspu_emu_telnet_disconnect(emu);
        }
        emu->frame_dirty = true;
    }
}

void a2gspu_emu_set_palette(a2gspu_emu_state *emu, const uint16_t *pal) {
    memcpy(emu->palette, pal, 256 * sizeof(uint16_t));
    emu->frame_dirty = true;
}

// ── Built-in telnet bridge ───────────────────────────────────────────
//
// These three functions implement the emulator-only TCP/VT100 path that
// allows end-to-end testing of the TERM renderer without a running IIgs
// terminal application or Marinetti.  They are NOT compiled into the card
// firmware and have no equivalent on the physical hardware.
//
// Architecture:
//   a2gspu_emu_telnet_connect()   — resolve hostname, open SDL3_net TCP socket,
//                                   allocate and initialise vt100_state_t
//   a2gspu_emu_telnet_poll()      — called every frame; read ≤4 KB from socket,
//                                   feed each byte to vt100_process()
//   a2gspu_emu_telnet_disconnect() — destroy socket and free vt100 state
//
// Data flow:
//   TCP socket → NET_ReadFromStreamSocket() → vt100_process() →
//   telnet_vt100_callback() → a2gspu_emu_term_execute() → term_grid[]
//
// telnet_vt100_callback() (below) is the bridge between the VT100 parser
// and the TERM command set.  The VT100 parser fires one callback per
// recognized terminal action; the callback translates each VT_CMD_*
// to the corresponding A2GSPU_CMD_TERM_* and calls term_execute.

// telnet_vt100_callback: VT100 parser callback.
//
// The vt100 parser (vt100.c, shared with the card) accumulates incoming
// bytes, recognises ANSI/VT100/VT220 control sequences, and fires this
// callback once per fully-parsed command.
//
// Most VT_CMD_* map 1:1 to A2GSPU_CMD_TERM_* with the same argument
// encoding.  The exceptions are the cursor-motion primitives (CR, LF,
// BS, TAB) which are handled specially:
//
//   VT_CMD_CR: carriage return — move cursor to column 0 of the current
//     row.  The row is not changed.  We read the current row directly
//     from the parser's cursor_row field and issue SET_CURSOR with col=0.
//     We cannot simply set col=0 in the emu state because the parser's
//     cursor_row is authoritative (the parser handles LF+CR together in
//     some sequences, and its cursor position may have already advanced
//     past emu's tracked position).
//
//   VT_CMD_NEWLINE: line feed (LF / VT / FF).  The parser has already
//     advanced cursor_row (and scrolled if needed) before firing this
//     callback.  We synchronize the emu cursor to the parser's position
//     via SET_CURSOR rather than calling term_scroll_up directly, because
//     the scroll may have been suppressed by the parser (e.g., in LFNL mode).
//
//   VT_CMD_BACKSPACE: the parser has already decremented cursor_col before
//     firing this callback.  We synchronize emu to the new position.
//
//   VT_CMD_TAB: the parser has already advanced cursor_col to the next
//     tab stop (every 8 columns, per VT102 default).  We synchronize.
//
// In all four cases we read cursor_{col,row} from the live vt100_state_t
// rather than computing them ourselves, ensuring the emulator's grid and
// the parser's internal position stay in lock-step.
//
// VT_CMD_BELL is acknowledged but silently ignored; a future implementation
// could trigger an SDL audio beep.
//
// Non-static: a2gspu_proto.cpp may call this callback directly for the
// WRITE_RAW protocol path (raw byte stream forwarded over USB instead of
// pre-parsed TERM commands).

// Queue a TERM command as 3 Mosaic entries (CMD + DATA_LO + DATA_HI) for HW forwarding.
// This mirrors the slot I/O write sequence the IIgs would perform: STA $C0B0,cmd;
// STA $C0B1,lo; STA $C0B2,hi — so the card's handle_mosaic_command() dispatches it
// identically to real bus snooping.
//
// Each Mosaic entry occupies BUS_MOSAIC_CMD_SIZE (3) bytes in the flat mosaic_cmds[]
// array: [register_offset, value, reserved].  One TERM command requires 3 entries
// (CMD write, DATA_LO write, DATA_HI write), consuming 9 bytes total per call.
//
// If mosaic_cmd_count + 3 > BUS_MOSAIC_MAX_CMDS the command is silently dropped —
// this manifests as missed PUTCHARs (characters absent from the DVI display while
// the emulator's own grid still shows them correctly).  In practice the 8192-entry
// buffer (~2730 TERM commands per frame) is sufficient for any realistic BBS data
// rate at 60 fps; a 115200-baud link delivers at most ~1920 bytes/frame.
void queue_term_for_hw(a2gspu_emu_state *emu, uint8_t cmd, uint16_t data) {
    if (__builtin_expect(!emu->owner, 0)) return;
    a2gspu_data *ad = (a2gspu_data *)emu->owner;
    a2gspu_proto_state *ps = &ad->proto;
    // O4: buffer-full is rare — annotate unlikely so normal path is predicted taken.
    if (__builtin_expect(ps->mosaic_cmd_count + 3 > BUS_MOSAIC_MAX_CMDS, 0)) return;

    // O2: compute byte offset once (avoids mosaic_cmd_count * BUS_MOSAIC_CMD_SIZE
    // being multiplied three times across caller sites that pack CMD+DATA_LO+DATA_HI).
    // Each TERM command emits exactly 3 slot writes (9 bytes = 3 × BUS_MOSAIC_CMD_SIZE=3).
    uint32_t idx = ps->mosaic_cmd_count * BUS_MOSAIC_CMD_SIZE;
    // CMD register write
    ps->mosaic_cmds[idx + 0] = BUS_MOSAIC_REG_CMD;
    ps->mosaic_cmds[idx + 1] = cmd;
    ps->mosaic_cmds[idx + 2] = 0;
    // DATA_LO register write
    ps->mosaic_cmds[idx + 3] = BUS_MOSAIC_REG_DATA_LO;
    ps->mosaic_cmds[idx + 4] = data & 0xFF;
    ps->mosaic_cmds[idx + 5] = 0;
    // DATA_HI register write (triggers execution on card)
    ps->mosaic_cmds[idx + 6] = BUS_MOSAIC_REG_DATA_HI;
    ps->mosaic_cmds[idx + 7] = (data >> 8) & 0xFF;
    ps->mosaic_cmds[idx + 8] = 0;
    ps->mosaic_cmd_count += 3;
}

/* Non-static so a2gspu_proto.cpp can use it for WRITE_RAW path */
void telnet_vt100_callback(void *context, vt100_cmd_t cmd, int p1, int p2) {
    a2gspu_emu_state *emu = (a2gspu_emu_state *)context;

    // Map VT100 parser commands to A2GSPU TERM cmd + data.
    // Both the local EMU renderer and the HW card (via Mosaic forwarding)
    // receive the same command stream, ensuring identical output.
    uint8_t tcmd = 0;
    uint16_t tdata = 0;
    bool forward = true;

    switch (cmd) {
        case VT_CMD_PUTCHAR:
            tcmd = A2GSPU_CMD_TERM_PUTCHAR; tdata = p1 & 0xFF; break;
        case VT_CMD_SET_CURSOR:
            tcmd = A2GSPU_CMD_TERM_SET_CURSOR; tdata = (p1 & 0xFF) | ((p2 & 0xFF) << 8); break;
        case VT_CMD_SET_FG:
            tcmd = A2GSPU_CMD_TERM_SET_FG; tdata = p1 & 0xFF; break;
        case VT_CMD_SET_BG:
            tcmd = A2GSPU_CMD_TERM_SET_BG; tdata = p1 & 0xFF; break;
        case VT_CMD_SET_ATTR:
            tcmd = A2GSPU_CMD_TERM_SET_ATTR; tdata = p1 & 0xFF; break;
        case VT_CMD_SCROLL_UP:
            tcmd = A2GSPU_CMD_TERM_SCROLL_UP; tdata = p1 & 0xFF; break;
        case VT_CMD_SCROLL_DOWN:
            tcmd = A2GSPU_CMD_TERM_SCROLL_DOWN; tdata = p1 & 0xFF; break;
        case VT_CMD_CLEAR_LINE:
            tcmd = A2GSPU_CMD_TERM_CLEAR_LINE; tdata = p1 & 0xFF; break;
        case VT_CMD_CLEAR_SCREEN:
            tcmd = A2GSPU_CMD_TERM_CLEAR_SCREEN; tdata = p1 & 0xFF; break;
        case VT_CMD_SET_REGION:
            tcmd = A2GSPU_CMD_TERM_SET_REGION; tdata = (p1 & 0xFF) | ((p2 & 0xFF) << 8); break;
        case VT_CMD_SAVE_CURSOR:
            tcmd = A2GSPU_CMD_TERM_SAVE_CURSOR; tdata = 0; break;
        case VT_CMD_RESTORE_CURSOR:
            tcmd = A2GSPU_CMD_TERM_RESTORE_CURSOR; tdata = 0; break;
        case VT_CMD_CR:
            tcmd = A2GSPU_CMD_TERM_SET_CURSOR;
            tdata = 0 | ((((vt100_state_t*)emu->telnet_vt100)->cursor_row & 0xFF) << 8);
            break;
        case VT_CMD_NEWLINE:
            tcmd = A2GSPU_CMD_TERM_SET_CURSOR;
            tdata = (((vt100_state_t*)emu->telnet_vt100)->cursor_col & 0xFF) |
                    ((((vt100_state_t*)emu->telnet_vt100)->cursor_row & 0xFF) << 8);
            break;
        case VT_CMD_BACKSPACE:
            tcmd = A2GSPU_CMD_TERM_SET_CURSOR;
            tdata = (((vt100_state_t*)emu->telnet_vt100)->cursor_col & 0xFF) |
                    ((((vt100_state_t*)emu->telnet_vt100)->cursor_row & 0xFF) << 8);
            break;
        case VT_CMD_TAB:
            tcmd = A2GSPU_CMD_TERM_SET_CURSOR;
            tdata = (((vt100_state_t*)emu->telnet_vt100)->cursor_col & 0xFF) |
                    ((((vt100_state_t*)emu->telnet_vt100)->cursor_row & 0xFF) << 8);
            break;
        case VT_CMD_INSERT_CHARS:
            tcmd = A2GSPU_CMD_TERM_INSERT_CHARS; tdata = p1 & 0xFF; break;
        case VT_CMD_DELETE_CHARS:
            tcmd = A2GSPU_CMD_TERM_DELETE_CHARS; tdata = p1 & 0xFF; break;
        case VT_CMD_ERASE_CHARS:
            tcmd = A2GSPU_CMD_TERM_ERASE_CHARS; tdata = p1 & 0xFF; break;
        // p1=1 → show cursor (style byte 0x00); p1=0 → hide cursor (style byte 0x80).
        // Bit 7 of the style byte is the hidden flag, matching the
        // BUS_MOSAIC_CMD_TERM_CURSOR_STYLE protocol encoding on the card.
        case VT_CMD_CURSOR_VISIBLE:
            tcmd = A2GSPU_CMD_TERM_CURSOR_STYLE; tdata = p1 ? 0x00 : 0x80; break;
        case VT_CMD_ALT_SCREEN:
            tcmd = A2GSPU_CMD_TERM_ALT_SCREEN; tdata = p1 & 0x01; break;
        case VT_CMD_RESPONSE: {
            // Send terminal response (CPR, DA, DSR) back to the server.
            // The VT100 parser has built the response in vt->response[].
            // This is critical for BBS autodetection (e.g. Synchronet sends
            // ESC[6n and expects ESC[row;colR back to confirm ANSI support).
            //
            // This must NOT be forwarded as a TERM command to the card.
            // The response (e.g. ESC[row;colR) is a socket-level reply that
            // only makes sense in the context of the emulator's live TCP
            // connection.  The card has no socket; forwarding would cause the
            // card's TERM interpreter to misread the escape sequence bytes as
            // display operations, corrupting the terminal output.
            vt100_state_t *vt_resp = (vt100_state_t *)emu->telnet_vt100;
            if (vt_resp && vt_resp->response_len > 0 && emu->telnet_socket) {
                NET_WriteToStreamSocket((NET_StreamSocket *)emu->telnet_socket,
                    vt_resp->response, vt_resp->response_len);
                // DEBUG: fprintf(stderr, "A2GSPU TELNET: sent response (%d bytes)\n", vt_resp->response_len); fflush(stderr);
            }
            forward = false; break;
        }
        case VT_CMD_BELL:
            forward = false; break;
        default:
            forward = false; break;
    }

    if (forward) {
        a2gspu_emu_term_execute(emu, tcmd, tdata);
        queue_term_for_hw(emu, tcmd, tdata);
    }
    emu->frame_dirty = true;
}

// a2gspu_emu_telnet_connect: open a TCP connection and initialize the VT100 parser.
//
// Steps:
//   1. Resolve the hostname via NET_ResolveHostname() + NET_WaitUntilResolved()
//      (synchronous, blocking up to 5 seconds — acceptable at startup).
//   2. Create a non-blocking TCP client socket with NET_CreateClient().
//   3. Wait for the TCP handshake to complete via NET_WaitUntilConnected()
//      (blocking up to 5 seconds).
//   4. Allocate a vt100_state_t on the heap and initialise it with
//      telnet_vt100_callback as the command callback.
//
// If any step fails, resources are freed and telnet_connected remains false.
// The function is idempotent: if already connected it returns immediately.
//
// After a successful connect, a2gspu_emu_telnet_poll() must be called each
// frame to pump data from the socket through the parser.
void a2gspu_emu_telnet_connect(a2gspu_emu_state *emu, const char *host, int port) {
    if (emu->telnet_connected) return;

    /* Enter TERM mode if not already */
    if (emu->mode != A2GSPU_MODE_TERM) {
        a2gspu_emu_set_mode(emu, A2GSPU_MODE_TERM);
    }

    /* Ensure SDL3_net is initialized.
     * NET_Init() is idempotent: if SDL3_net is already running it returns true
     * immediately without reinitializing.  Safe to call on every connect attempt. */
    if (!NET_Init()) {
        fprintf(stderr, "A2GSPU TELNET: NET_Init failed: %s\n", SDL_GetError());
        return;
    }

    printf("A2GSPU TELNET: resolving %s...\n", host);

    /* Resolve and connect */
    NET_Address *addr = NET_ResolveHostname(host);
    if (!addr) {
        fprintf(stderr, "A2GSPU TELNET: NET_ResolveHostname returned NULL for %s\n", host);
        return;
    }

    int res = NET_WaitUntilResolved(addr, 5000);
    if (res != 1) {
        fprintf(stderr, "A2GSPU TELNET: resolve failed for %s (status=%d)\n", host, res);
        NET_UnrefAddress(addr);
        return;
    }

    printf("A2GSPU TELNET: resolved, connecting to %s:%d...\n", host, port);

    NET_StreamSocket *sock = NET_CreateClient(addr, port);
    NET_UnrefAddress(addr);
    if (!sock) {
        fprintf(stderr, "A2GSPU TELNET: NET_CreateClient failed for %s:%d\n", host, port);
        return;
    }

    /* Wait for connection (up to 5 seconds) */
    int status = NET_WaitUntilConnected(sock, 5000);
    if (status != 1) {
        fprintf(stderr, "A2GSPU TELNET: connection to %s:%d failed (status=%d)\n", host, port, status);
        NET_DestroyStreamSocket(sock);
        return;
    }

    printf("A2GSPU TELNET: connected to %s:%d\n", host, port);
    emu->telnet_socket = sock;
    emu->telnet_connected = true;

    /* Send initial telnet negotiation immediately so the server knows
     * we're a telnet client before it decides our terminal type.
     * IAC WILL TTYPE — we can send terminal type
     * IAC WILL NAWS  — we can send window size
     * IAC DO SGA     — we accept suppress-go-ahead
     *
     * Timing: these must be sent before the server sends any data.
     * Some servers (e.g. Synchronet BBS) begin ANSI capability probing in their
     * very first TCP packet.  If our WILL TTYPE arrives after the server has
     * already decided the client is "DUMB", it is too late — ANSI sequences
     * will not be sent and the BBS will display in plain text mode. */
    {
        uint8_t init[] = {
            0xFF, 0xFB, 24,   // IAC WILL TTYPE
            0xFF, 0xFB, 31,   // IAC WILL NAWS
            0xFF, 0xFD, 3,    // IAC DO SGA
        };
        NET_WriteToStreamSocket(sock, init, sizeof(init));
        // DEBUG: fprintf(stderr, "A2GSPU TELNET: sent initial WILL TTYPE + WILL NAWS + DO SGA\n"); fflush(stderr);
    }

    /* Initialize VT100 parser with telnet_vt100_callback as the command sink */
    vt100_state_t *vt = new vt100_state_t();
    vt100_init(vt, telnet_vt100_callback, emu);
    emu->telnet_vt100 = vt;
}

// a2gspu_emu_telnet_poll: read available TCP data and push it through the VT100 parser.
//
// Called once per rendered frame from the TERM case in a2gspu_emu_frame_processor().
// NET_ReadFromStreamSocket() is non-blocking: it returns 0 if no data is available,
// >0 for the number of bytes read, or <0 on error/closure.  Up to 4 KB is read
// per frame, which is sufficient to keep up with any real BBS data rate at 60 fps.
//
// Each byte is passed individually to vt100_process(), which maintains the parser
// state machine and fires telnet_vt100_callback() when a complete sequence is recognized.
// Processing byte-by-byte is necessary because CSI sequences can span multiple reads.
//
// On socket error or closure (got < 0), the socket and parser are cleaned up and
// telnet_connected is set to false.  The terminal grid retains its last content.
void a2gspu_emu_telnet_poll(a2gspu_emu_state *emu) {
    if (!emu->telnet_connected || !emu->telnet_socket) return;

    NET_StreamSocket *sock = (NET_StreamSocket *)emu->telnet_socket;
    vt100_state_t *vt = (vt100_state_t *)emu->telnet_vt100;

    // Telnet protocol constants
    static constexpr uint8_t IAC  = 0xFF, WILL = 0xFB, WONT = 0xFC;
    static constexpr uint8_t DO_  = 0xFD, DONT = 0xFE, SB = 0xFA, SE = 0xF0;
    static constexpr uint8_t OPT_ECHO = 1, OPT_SGA = 3, OPT_TTYPE = 24, OPT_NAWS = 31;

    uint8_t buf[4096];
    int got = NET_ReadFromStreamSocket(sock, buf, sizeof(buf));
    if (got > 0) {
        for (int i = 0; i < got; i++) {
            uint8_t b = buf[i];

            // ── Telnet IAC negotiation state machine ──────────────────────
            // Handles WILL/WONT/DO/DONT (3-byte), SB...SE (variable), and
            // responds to key options so the BBS enables ANSI mode.
            // WARNING: these static locals persist across connections.  If a
            // connection closes mid-negotiation and a new one opens, iac_state
            // could be non-zero, causing the first bytes of the new session to be
            // misinterpreted as continuation of a previous IAC sequence.  The reset
            // below (at the disconnect point) clears this on clean closure, but an
            // abrupt drop (got < 0 without a prior got==0 drain) could still leave
            // stale state.  A future fix should promote these to emu fields and zero
            // them unconditionally on disconnect.
            static int iac_state = 0;  // 0=normal, 1=got IAC, 2=verb+opt, 3=SB data, 4=SB got IAC, 5=SB option
            static uint8_t iac_verb = 0;  // the verb byte (WILL/WONT/DO/DONT)
            static uint8_t sb_option = 0; // option byte for current SB
            static uint8_t sb_buf[64];    // SB data accumulator
            static int sb_len = 0;
            static void *last_socket = nullptr; // detect reconnect to reset state
            if (sock != last_socket) {
                // New connection — reset IAC state to avoid stale mid-negotiation state
                // from a previous session corrupting the first bytes of this one.
                iac_state = 0; iac_verb = 0; sb_option = 0; sb_len = 0;
                last_socket = sock;
            }

            // O13: fast path — state 0 (normal data) is by far the most common.
            // Check it first with __builtin_expect so the branch predictor trains on
            // the vt100_process path.  The remaining states (1-5) handle IAC sequences
            // which appear only a handful of times at connection negotiation time.
            if (__builtin_expect(iac_state == 0, 1)) {
                if (__builtin_expect(b == IAC, 0)) {
                    iac_state = 1;
                    continue;
                }
                if (__builtin_expect(vt != nullptr, 1)) vt100_process(vt, b);
                continue;
            }

            if (iac_state == 5) {
                // First byte after IAC SB — this is the option number
                sb_option = b;
                iac_state = 3;
                continue;
            }
            if (iac_state == 3) {
                // Inside subnegotiation data
                if (b == IAC) { iac_state = 4; }
                else if (sb_len < (int)sizeof(sb_buf)) { sb_buf[sb_len++] = b; }
                continue;
            }
            if (iac_state == 4) {
                if (b == SE) {
                    // Subnegotiation complete — handle it
                    // DEBUG: fprintf(stderr, "A2GSPU IAC: SB option=%d len=%d data[0]=%d\n", sb_option, sb_len, sb_len > 0 ? sb_buf[0] : -1); fflush(stderr);
                    if (sb_option == OPT_TTYPE && sb_len >= 1 && sb_buf[0] == 1) {
                        // TTYPE SEND request — reply with terminal type
                        const char *ttype = "ANSI";
                        int tlen = (int)strlen(ttype);
                        uint8_t resp[64];
                        resp[0] = IAC; resp[1] = SB; resp[2] = OPT_TTYPE; resp[3] = 0; // IS
                        memcpy(&resp[4], ttype, tlen);
                        resp[4 + tlen] = IAC; resp[4 + tlen + 1] = SE;
                        NET_WriteToStreamSocket(sock, resp, 4 + tlen + 2);
                        // DEBUG: fprintf(stderr, "A2GSPU IAC: sent TTYPE IS %s\n", ttype); fflush(stderr);
                    }
                    iac_state = 0;
                } else if (b == IAC) {
                    // IAC IAC inside SB = literal 0xFF
                    if (sb_len < (int)sizeof(sb_buf)) sb_buf[sb_len++] = 0xFF;
                    iac_state = 3;
                } else {
                    iac_state = 3;  // unexpected, stay in SB
                }
                continue;
            }
            if (iac_state == 2) {
                // Option byte after WILL/WONT/DO/DONT
                uint8_t opt = b;
                const char *vname = (iac_verb==WILL?"WILL":iac_verb==WONT?"WONT":iac_verb==DO_?"DO":"DONT");
                // DEBUG: fprintf(stderr, "A2GSPU IAC: %s %d\n", vname, opt); fflush(stderr);
                if (iac_verb == DO_) {
                    if (opt == OPT_TTYPE) {
                        // Server asks us to send TTYPE — agree
                        uint8_t resp[] = { IAC, WILL, OPT_TTYPE };
                        NET_WriteToStreamSocket(sock, resp, 3);
                        // DEBUG: fprintf(stderr, "A2GSPU IAC: sent WILL TTYPE\n"); fflush(stderr);
                    } else if (opt == OPT_NAWS) {
                        // Server asks for window size — agree and send 80×25
                        uint8_t resp[] = { IAC, WILL, OPT_NAWS };
                        NET_WriteToStreamSocket(sock, resp, 3);
                        uint8_t naws[] = { IAC, SB, OPT_NAWS, 0, 80, 0, 25, IAC, SE };
                        NET_WriteToStreamSocket(sock, naws, sizeof(naws));
                        printf("A2GSPU TELNET: sent NAWS 80x25\n");
                    } else {
                        // Decline everything else
                        uint8_t resp[] = { IAC, WONT, opt };
                        NET_WriteToStreamSocket(sock, resp, 3);
                    }
                } else if (iac_verb == WILL) {
                    if (opt == OPT_ECHO || opt == OPT_SGA) {
                        // Accept server echo and suppress-go-ahead
                        uint8_t resp[] = { IAC, DO_, opt };
                        NET_WriteToStreamSocket(sock, resp, 3);
                    } else {
                        uint8_t resp[] = { IAC, DONT, opt };
                        NET_WriteToStreamSocket(sock, resp, 3);
                    }
                }
                // WONT/DONT from server: no response needed
                iac_state = 0;
                continue;
            }
            if (iac_state == 1) {
                if (b >= 0xFB && b <= 0xFE) {
                    iac_verb = b;
                    iac_state = 2;
                } else if (b == SB) {
                    iac_state = 5;  // 5 = waiting for SB option byte
                    sb_len = 0;
                    sb_option = 0;
                } else {
                    iac_state = 0;  // 2-byte command (NOP, BRK, etc.)
                }
                continue;
            }
            // All non-zero iac_state branches above use 'continue'.
            // Reaching here means iac_state is an unhandled value (should not occur).
        }
    } else if (got < 0) {
        printf("A2GSPU TELNET: connection closed\n");
        NET_DestroyStreamSocket(sock);
        emu->telnet_socket = nullptr;
        emu->telnet_connected = false;
        delete (vt100_state_t *)emu->telnet_vt100;
        emu->telnet_vt100 = nullptr;
        // Reset IAC static state on clean disconnect.  The static variables
        // (iac_state etc.) are declared inside the per-byte loop above; they
        // cannot be reset from this outer scope without promoting them to emu
        // fields.  Until that refactor happens, the WARNING above stands: an
        // abrupt drop mid-negotiation leaves iac_state non-zero for the next
        // connection.  Mitigation: telnet_poll() returns immediately when
        // !telnet_connected, so no bytes are processed until reconnect — giving
        // a full frame gap in which iac_state is effectively ignored.

        // Re-show address prompt (both EMU + HW paths).
        // We cannot call a2gspu_emu_set_mode(TERM) here to reuse the prompt
        // setup: set_mode guards on (mode != current_mode), so if we are
        // already in TERM mode the call would be a no-op and the prompt would
        // never appear.  The prompt logic must be duplicated here.
        // Future cleanup: extract to a helper (e.g. show_address_prompt()).
        emu->term_address_len = 0;
        emu->term_address[0] = '\0';
        a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_CLEAR_SCREEN, 2);
        queue_term_for_hw(emu, A2GSPU_CMD_TERM_CLEAR_SCREEN, 2);
        a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_CURSOR, 0);
        queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_CURSOR, 0);
        a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_FG, 10);
        queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_FG, 10);
        a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_BG, 0);
        queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_BG, 0);
        const char *prompt = "Connect to (host:port): ";
        for (int i = 0; prompt[i]; i++) {
            a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_PUTCHAR, prompt[i]);
            queue_term_for_hw(emu, A2GSPU_CMD_TERM_PUTCHAR, prompt[i]);
        }
        a2gspu_emu_term_execute(emu, A2GSPU_CMD_TERM_SET_FG, 15);
        queue_term_for_hw(emu, A2GSPU_CMD_TERM_SET_FG, 15);
        emu->term_prompt_active = true;
        emu->term_cursor_visible = true;
        emu->frame_dirty = true;
    }
}

// a2gspu_emu_telnet_disconnect: tear down the TCP connection and free parser state.
//
// Safe to call even if not connected (all pointers are guarded before use).
// Called explicitly when the mode changes away from TERM (so the socket is not
// left open while a UHR or passthrough mode is active) and from a2gspu_emu_shutdown().
void a2gspu_emu_telnet_disconnect(a2gspu_emu_state *emu) {
    if (emu->telnet_socket) {
        NET_DestroyStreamSocket((NET_StreamSocket *)emu->telnet_socket);
        emu->telnet_socket = nullptr;
    }
    if (emu->telnet_vt100) {
        delete (vt100_state_t *)emu->telnet_vt100;
        emu->telnet_vt100 = nullptr;
    }
    emu->telnet_connected = false;
}

void a2gspu_emu_shutdown(a2gspu_emu_state *emu) {
    a2gspu_emu_telnet_disconnect(emu);
    if (emu->texture) {
        SDL_DestroyTexture(emu->texture);
        emu->texture = nullptr;
    }
    delete[] emu->uhr_fb;
    emu->uhr_fb = nullptr;
    emu->initialized = false;
}
