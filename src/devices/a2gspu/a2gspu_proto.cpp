/*
 *   A2GSPU Mosaic Slot I/O Protocol Implementation
 *
 *   Handles slot I/O register reads/writes for the Mosaic tile protocol.
 *   Same protocol drives both HW (USB serial) and EMU (local render) backends.
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#include "computer.hpp"
#include "a2gspu.hpp"
#include "a2gspu_proto.hpp"
#include "a2gspu_emu.hpp"
#include "mmus/mmu_ii.hpp"

// ── VT100 parser — C header wrapped for C++ translation unit ──────
// vt100.h is a plain-C header (no C++ overloads, no extern "C" guards
// of its own).  The extern "C" block suppresses C++ name-mangling so
// the linker resolves vt100_init / vt100_process to the symbols emitted
// by vt100.c (compiled as C).  Without this wrapper the linker would
// look for mangled names like _Z10vt100_initP12vt100_state_tPFv... and
// fail to find them.
extern "C" {
#include "vt100.h"
}

// ── telnet_vt100_callback forward declaration ─────────────────────
// Defined in a2gspu_emu.cpp as a non-static function specifically so it
// can be referenced from this translation unit.  It is the bridge between
// the VT100 parser and the emulated TERM renderer:
//
//   vt100_process() → parser FSM → vt100_emit_fn callback
//                                       │
//                                       ▼
//                             telnet_vt100_callback()
//                             (a2gspu_emu.cpp)
//                                       │
//                                       ▼
//                             a2gspu_emu_term_execute()
//                             (maps vt100_cmd_t → A2GSPU_CMD_TERM_*)
//
// On the real card the equivalent chain is:
//   USB CDC byte → vt100_process() → card callback → terminal renderer
// making emulator and hardware execution paths structurally identical.
extern void telnet_vt100_callback(void *context, vt100_cmd_t cmd, int p1, int p2);

// ── Command execution ──────────────────────────────────────────────
// Triggered when DATA_HI is written (command + 16-bit data ready).

static void execute_with_data(a2gspu_data *ad, uint16_t data16) {
    a2gspu_proto_state *ps = &ad->proto;

    switch (ps->cmd) {
        case A2GSPU_CMD_NOP:
            break;

        case A2GSPU_CMD_SET_MODE:
            a2gspu_emu_set_mode(&ad->emu, (a2gspu_video_mode_t)(data16 & 0xFF));
            break;

        case A2GSPU_CMD_SET_CURSOR:
            ps->cursor_x = data16 & 0xFF;
            ps->cursor_y = (data16 >> 8) & 0xFF;
            break;

        case A2GSPU_CMD_SET_PAL_IDX:
            ps->pal_idx = data16 & 0xFF;
            break;

        case A2GSPU_CMD_WRITE_PAL:
            if (ps->pal_idx < 256) {
                ad->emu.palette[ps->pal_idx] = data16;
                ps->pal_idx++;
                ad->emu.frame_dirty = true;
            }
            break;

        case A2GSPU_CMD_SCROLL: {
            // dy is a signed 8-bit vertical scroll delta (positive = scroll down = content moves up)
            int8_t dy = (int8_t)((data16 >> 8) & 0xFF);
            if (ad->emu.uhr_fb) {
                if (dy > 0) {
                    // Scroll content up: move rows toward lower addresses, blank at bottom
                    memmove(ad->emu.uhr_fb, ad->emu.uhr_fb + dy * A2GSPU_UHR_W,
                            (A2GSPU_UHR_H - dy) * A2GSPU_UHR_W);
                    memset(ad->emu.uhr_fb + (A2GSPU_UHR_H - dy) * A2GSPU_UHR_W, 0,
                           dy * A2GSPU_UHR_W);
                } else if (dy < 0) {
                    int ady = -dy;
                    // Scroll content down: move rows toward higher addresses, blank at top
                    memmove(ad->emu.uhr_fb + ady * A2GSPU_UHR_W, ad->emu.uhr_fb,
                            (A2GSPU_UHR_H - ady) * A2GSPU_UHR_W);
                    memset(ad->emu.uhr_fb, 0, ady * A2GSPU_UHR_W);
                }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_INVALIDATE:
            ad->emu.frame_dirty = true;
            break;

        case A2GSPU_CMD_SELECT_SHEET:
            // TODO: implement sheet selection
            break;

        case A2GSPU_CMD_VERSION:
            // Read-only command, handled in read path
            break;

        case A2GSPU_CMD_SET_DRAW_POS:
            ps->cursor_x = data16;
            break;

        case A2GSPU_CMD_SET_DRAW_Y:
            ps->cursor_y = data16;
            break;

        case A2GSPU_CMD_SET_DRAW_COLOR:
            ps->draw_color = data16 & 0xFF;
            break;

        case A2GSPU_CMD_WRITE_PIXEL:
            if (ad->emu.uhr_fb && ps->cursor_x < A2GSPU_UHR_W && ps->cursor_y < A2GSPU_UHR_H) {
                ad->emu.uhr_fb[ps->cursor_y * A2GSPU_UHR_PITCH + ps->cursor_x] = data16 & 0xFF;
                ps->cursor_x++;
                if (ps->cursor_x >= A2GSPU_UHR_W) { ps->cursor_x = 0; ps->cursor_y++; }
                if (ps->cursor_y >= A2GSPU_UHR_H) ps->cursor_y = 0;
                ad->emu.frame_dirty = true;
            }
            break;

        case A2GSPU_CMD_FILL_RECT: {
            uint16_t w = data16 & 0xFF;
            uint16_t h = (data16 >> 8) & 0xFF;
            if (!ad->emu.uhr_fb || w == 0 || h == 0) break;
            for (uint16_t row = 0; row < h; row++) {
                uint16_t y = ps->cursor_y + row;
                if (y >= A2GSPU_UHR_H) break;
                uint16_t x_end = ps->cursor_x + w;
                if (x_end > A2GSPU_UHR_W) x_end = A2GSPU_UHR_W;
                if (ps->cursor_x < A2GSPU_UHR_W)
                    memset(ad->emu.uhr_fb + y * A2GSPU_UHR_PITCH + ps->cursor_x,
                           ps->draw_color, x_end - ps->cursor_x);
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_FILL_SCREEN:
            if (ad->emu.uhr_fb)
                memset(ad->emu.uhr_fb, data16 & 0xFF, A2GSPU_UHR_SIZE);
            ad->emu.frame_dirty = true;
            break;

        case A2GSPU_CMD_HLINE: {
            uint16_t len = data16;
            if (!ad->emu.uhr_fb || len == 0 || ps->cursor_y >= A2GSPU_UHR_H) break;
            uint16_t x_end = ps->cursor_x + len;
            if (x_end > A2GSPU_UHR_W) x_end = A2GSPU_UHR_W;
            if (ps->cursor_x < A2GSPU_UHR_W)
                memset(ad->emu.uhr_fb + ps->cursor_y * A2GSPU_UHR_PITCH + ps->cursor_x,
                       ps->draw_color, x_end - ps->cursor_x);
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_VLINE: {
            uint16_t len = data16;
            if (!ad->emu.uhr_fb || len == 0 || ps->cursor_x >= A2GSPU_UHR_W) break;
            for (uint16_t i = 0; i < len && (ps->cursor_y + i) < A2GSPU_UHR_H; i++)
                ad->emu.uhr_fb[(ps->cursor_y + i) * A2GSPU_UHR_PITCH + ps->cursor_x] = ps->draw_color;
            ad->emu.frame_dirty = true;
            break;
        }

        // ── Extended drawing primitives (0x11-0x1F) ─────────────────────────
        case A2GSPU_CMD_LINE_TO_X:
            ps->line_to_x = data16;
            break;

        case A2GSPU_CMD_LINE_TO_Y: {
            // Bresenham line from cursor to (line_to_x, data16)
            if (!ad->emu.uhr_fb) break;
            int16_t x0 = ps->cursor_x, y0 = ps->cursor_y;
            int16_t x1 = ps->line_to_x, y1 = data16;
            // Clip to framebuffer
            int16_t dx = abs(x1 - x0), dy = -abs(y1 - y0);
            int16_t sx = (x0 < x1) ? 1 : -1;
            int16_t sy = (y0 < y1) ? 1 : -1;
            int16_t err = dx + dy;
            for (;;) {
                if (x0 >= 0 && x0 < A2GSPU_UHR_W && y0 >= 0 && y0 < A2GSPU_UHR_H)
                    ad->emu.uhr_fb[y0 * A2GSPU_UHR_PITCH + x0] = ps->draw_color;
                if (x0 == x1 && y0 == y1) break;
                int16_t e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
            ps->cursor_x = ps->line_to_x;
            ps->cursor_y = data16;
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_DRAW_CIRCLE: {
            // Midpoint circle outline
            if (!ad->emu.uhr_fb) break;
            int16_t cx = ps->cursor_x, cy = ps->cursor_y, r = data16;
            if (r <= 0) break;
            int16_t x = r, y = 0, d = 1 - r;
            uint8_t col = ps->draw_color;
            while (x >= y) {
                auto px = [&](int16_t px, int16_t py) {
                    if (px >= 0 && px < A2GSPU_UHR_W && py >= 0 && py < A2GSPU_UHR_H)
                        ad->emu.uhr_fb[py * A2GSPU_UHR_PITCH + px] = col;
                };
                px(cx+x,cy+y); px(cx-x,cy+y); px(cx+x,cy-y); px(cx-x,cy-y);
                px(cx+y,cy+x); px(cx-y,cy+x); px(cx+y,cy-x); px(cx-y,cy-x);
                y++;
                if (d < 0) d += 2*y + 1;
                else { x--; d += 2*(y - x) + 1; }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_FILL_CIRCLE: {
            // Midpoint circle filled
            if (!ad->emu.uhr_fb) break;
            int16_t cx = ps->cursor_x, cy = ps->cursor_y, r = data16;
            if (r <= 0) break;
            int16_t x = r, y = 0, d = 1 - r;
            uint8_t col = ps->draw_color;
            auto hspan = [&](int16_t sx, int16_t sy, int16_t len) {
                if (sy < 0 || sy >= A2GSPU_UHR_H || len <= 0) return;
                if (sx < 0) { len += sx; sx = 0; }
                if (sx + len > A2GSPU_UHR_W) len = A2GSPU_UHR_W - sx;
                if (len > 0) memset(ad->emu.uhr_fb + sy * A2GSPU_UHR_PITCH + sx, col, len);
            };
            while (x >= y) {
                hspan(cx-x, cy+y, 2*x+1); hspan(cx-x, cy-y, 2*x+1);
                hspan(cx-y, cy+x, 2*y+1); hspan(cx-y, cy-x, 2*y+1);
                y++;
                if (d < 0) d += 2*y + 1;
                else { x--; d += 2*(y - x) + 1; }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_DRAW_ELLIPSE: {
            if (!ad->emu.uhr_fb) break;
            int16_t cx = ps->cursor_x, cy = ps->cursor_y;
            int16_t rx = data16 & 0xFF, ry = (data16 >> 8) & 0xFF;
            if (rx <= 0 || ry <= 0) break;
            uint8_t col = ps->draw_color;
            auto px = [&](int16_t px, int16_t py) {
                if (px >= 0 && px < A2GSPU_UHR_W && py >= 0 && py < A2GSPU_UHR_H)
                    ad->emu.uhr_fb[py * A2GSPU_UHR_PITCH + px] = col;
            };
            int32_t rx2 = (int32_t)rx*rx, ry2 = (int32_t)ry*ry;
            int16_t x = 0, y = ry;
            int32_t d1 = ry2 - rx2*ry + rx2/4;
            while (ry2*x < rx2*y) {
                px(cx+x,cy+y); px(cx-x,cy+y); px(cx+x,cy-y); px(cx-x,cy-y);
                x++;
                if (d1 < 0) d1 += 2*ry2*x + ry2;
                else { y--; d1 += 2*ry2*x - 2*rx2*y + ry2; }
            }
            int32_t d2 = ry2*((int32_t)(2*x+1)*(2*x+1))/4 + rx2*((int32_t)(y-1)*(y-1)) - rx2*ry2;
            while (y >= 0) {
                px(cx+x,cy+y); px(cx-x,cy+y); px(cx+x,cy-y); px(cx-x,cy-y);
                y--;
                if (d2 > 0) d2 += rx2 - 2*rx2*y;
                else { x++; d2 += 2*ry2*x - 2*rx2*y + rx2; }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_FILL_ELLIPSE: {
            if (!ad->emu.uhr_fb) break;
            int16_t cx = ps->cursor_x, cy = ps->cursor_y;
            int16_t rx = data16 & 0xFF, ry = (data16 >> 8) & 0xFF;
            if (rx <= 0 || ry <= 0) break;
            uint8_t col = ps->draw_color;
            auto hspan = [&](int16_t sx, int16_t sy, int16_t len) {
                if (sy < 0 || sy >= A2GSPU_UHR_H || len <= 0) return;
                if (sx < 0) { len += sx; sx = 0; }
                if (sx + len > A2GSPU_UHR_W) len = A2GSPU_UHR_W - sx;
                if (len > 0) memset(ad->emu.uhr_fb + sy * A2GSPU_UHR_PITCH + sx, col, len);
            };
            int32_t rx2 = (int32_t)rx*rx, ry2 = (int32_t)ry*ry;
            int16_t x = 0, y = ry;
            int32_t d1 = ry2 - rx2*ry + rx2/4;
            while (ry2*x < rx2*y) {
                hspan(cx-x, cy+y, 2*x+1); hspan(cx-x, cy-y, 2*x+1);
                x++;
                if (d1 < 0) d1 += 2*ry2*x + ry2;
                else { y--; d1 += 2*ry2*x - 2*rx2*y + ry2; }
            }
            int32_t d2 = ry2*((int32_t)(2*x+1)*(2*x+1))/4 + rx2*((int32_t)(y-1)*(y-1)) - rx2*ry2;
            while (y >= 0) {
                hspan(cx-x, cy+y, 2*x+1); hspan(cx-x, cy-y, 2*x+1);
                y--;
                if (d2 > 0) d2 += rx2 - 2*rx2*y;
                else { x++; d2 += 2*ry2*x - 2*rx2*y + rx2; }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_DRAW_ARC: {
            // Arc with angle filtering using latched radius
            if (!ad->emu.uhr_fb) break;
            int16_t cx = ps->cursor_x, cy = ps->cursor_y;
            int16_t r = ps->arc_radius;
            int16_t start_deg = data16 & 0xFF, end_deg = (data16 >> 8) & 0xFF;
            if (r <= 0) break;
            uint8_t col = ps->draw_color;
            auto px = [&](int16_t px, int16_t py) {
                if (px >= 0 && px < A2GSPU_UHR_W && py >= 0 && py < A2GSPU_UHR_H)
                    ad->emu.uhr_fb[py * A2GSPU_UHR_PITCH + px] = col;
            };
            auto pixel_angle = [](int16_t dx, int16_t dy) -> int16_t {
                if (dx == 0 && dy == 0) return 0;
                int16_t ax = abs(dx), ay = abs(dy);
                int16_t angle;
                if (ax >= ay) angle = (int16_t)((int32_t)45 * ay / (ax + 1));
                else angle = 90 - (int16_t)((int32_t)45 * ax / (ay + 1));
                if (dx < 0 && dy >= 0) angle = 180 - angle;
                else if (dx < 0 && dy < 0) angle = 180 + angle;
                else if (dx >= 0 && dy < 0) angle = 360 - angle;
                return angle % 360;
            };
            auto in_range = [](int16_t angle, int16_t s, int16_t e) -> bool {
                s = ((s % 360) + 360) % 360;
                e = ((e % 360) + 360) % 360;
                return (s <= e) ? (angle >= s && angle <= e) : (angle >= s || angle <= e);
            };
            auto arc_px = [&](int16_t dx, int16_t dy) {
                if (in_range(pixel_angle(dx, -dy), start_deg, end_deg))
                    px(cx + dx, cy + dy);
            };
            int16_t x = r, y = 0, d = 1 - r;
            while (x >= y) {
                arc_px(x,y); arc_px(-x,y); arc_px(x,-y); arc_px(-x,-y);
                arc_px(y,x); arc_px(-y,x); arc_px(y,-x); arc_px(-y,-x);
                y++;
                if (d < 0) d += 2*y + 1;
                else { x--; d += 2*(y - x) + 1; }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_FLOOD_FILL: {
            // Scanline flood fill
            if (!ad->emu.uhr_fb) break;
            int16_t sx = ps->cursor_x, sy = ps->cursor_y;
            if (sx < 0 || sx >= A2GSPU_UHR_W || sy < 0 || sy >= A2GSPU_UHR_H) break;
            uint8_t border = data16 & 0xFF;
            uint8_t fill = ps->draw_color;
            uint8_t seed = ad->emu.uhr_fb[sy * A2GSPU_UHR_PITCH + sx];
            if (seed == border || seed == fill) break;
            struct { int16_t x, y; } stack[1024];
            int top = 0;
            stack[top++] = {sx, sy};
            uint8_t *fb = ad->emu.uhr_fb;
            while (top > 0) {
                auto e = stack[--top];
                int16_t px = e.x, py = e.y;
                uint8_t c = fb[py * A2GSPU_UHR_PITCH + px];
                if (c == border || c == fill) continue;
                int16_t lx = px;
                while (lx > 0 && fb[py * A2GSPU_UHR_PITCH + (lx-1)] != border &&
                       fb[py * A2GSPU_UHR_PITCH + (lx-1)] != fill) lx--;
                int16_t rx = lx;
                bool above_started = false, below_started = false;
                while (rx < A2GSPU_UHR_W) {
                    c = fb[py * A2GSPU_UHR_PITCH + rx];
                    if (c == border || c == fill) break;
                    fb[py * A2GSPU_UHR_PITCH + rx] = fill;
                    if (py > 0) {
                        uint8_t ca = fb[(py-1) * A2GSPU_UHR_PITCH + rx];
                        if (ca != border && ca != fill) {
                            if (!above_started && top < 1024) {
                                stack[top++] = {rx, (int16_t)(py-1)};
                                above_started = true;
                            }
                        } else above_started = false;
                    }
                    if (py < A2GSPU_UHR_H - 1) {
                        uint8_t cb = fb[(py+1) * A2GSPU_UHR_PITCH + rx];
                        if (cb != border && cb != fill) {
                            if (!below_started && top < 1024) {
                                stack[top++] = {rx, (int16_t)(py+1)};
                                below_started = true;
                            }
                        } else below_started = false;
                    }
                    rx++;
                }
            }
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_SET_CLIP_X1:
            ps->clip_x1 = data16;
            break;

        case A2GSPU_CMD_SET_CLIP_Y1:
            // Activates clip rect from (cursor_x, cursor_y) to (clip_x1, data16)
            // For now, store but drawing uses bounds checking directly
            // TODO: integrate clip rect into all drawing operations
            break;

        case A2GSPU_CMD_RESET_CLIP:
            // Reset clip to full framebuffer
            break;

        case A2GSPU_CMD_SET_LINE_STYLE:
            ps->line_pattern = data16 & 0xFF;
            ps->line_thickness = (data16 >> 8) & 0xFF;
            if (ps->line_thickness == 0) ps->line_thickness = 1;
            break;

        case A2GSPU_CMD_SET_FILL_STYLE:
            ps->fill_pattern = data16 & 0xFF;
            ps->fill_color = (data16 >> 8) & 0xFF;
            break;

        case A2GSPU_CMD_DRAW_RECT: {
            // Outline rectangle
            if (!ad->emu.uhr_fb) break;
            uint16_t w = data16 & 0xFF, h = (data16 >> 8) & 0xFF;
            if (w == 0 || h == 0) break;
            int16_t x = ps->cursor_x, y = ps->cursor_y;
            uint8_t col = ps->draw_color;
            // Top edge — guard the lower bound (x+i >= 0) too. cursor_x is set from a raw
            // 16-bit guest value (SET_DRAW_POS), so x can be negative once read as int16_t;
            // without the >= 0 guard a negative x+i indexes far below uhr_fb (heap OOB write).
            for (int16_t i = 0; i < w && x+i < A2GSPU_UHR_W; i++)
                if (x+i >= 0 && y >= 0 && y < A2GSPU_UHR_H)
                    ad->emu.uhr_fb[y * A2GSPU_UHR_PITCH + x + i] = col;
            // Bottom edge
            int16_t yb = y + h - 1;
            for (int16_t i = 0; i < w && x+i < A2GSPU_UHR_W; i++)
                if (x+i >= 0 && yb >= 0 && yb < A2GSPU_UHR_H)
                    ad->emu.uhr_fb[yb * A2GSPU_UHR_PITCH + x + i] = col;
            // Left edge — guard y+i >= 0 (cursor_y may read back negative as int16_t)
            for (int16_t i = 0; i < h && y+i < A2GSPU_UHR_H; i++)
                if (y+i >= 0 && x >= 0 && x < A2GSPU_UHR_W)
                    ad->emu.uhr_fb[(y+i) * A2GSPU_UHR_PITCH + x] = col;
            // Right edge
            int16_t xr = x + w - 1;
            for (int16_t i = 0; i < h && y+i < A2GSPU_UHR_H; i++)
                if (y+i >= 0 && xr >= 0 && xr < A2GSPU_UHR_W)
                    ad->emu.uhr_fb[(y+i) * A2GSPU_UHR_PITCH + xr] = col;
            ad->emu.frame_dirty = true;
            break;
        }

        case A2GSPU_CMD_SET_ARC_RADIUS:
            ps->arc_radius = data16;
            break;

        // ── Terminal commands (0x20-0x2D) ─────────────────────────────
        //
        // All 14 discrete TERM commands route to a single dispatcher,
        // a2gspu_emu_term_execute(), rather than having individual case
        // bodies here.  This mirrors exactly what the real card firmware
        // does: handle_mosaic_command() (card/terminal.c) has an
        // analogous fall-through block that dispatches 0x20-0x2D to a
        // single term_execute() handler.
        //
        // The design rationale is layering.  These 14 commands are
        // semantically terminal rendering primitives (cursor, color,
        // scroll, clear, etc.) — they belong to the TERM renderer, not
        // to this slot I/O glue layer.  Keeping command semantics in
        // a2gspu_emu_term_execute() means:
        //
        //   1. The emulator and hardware paths share a single definition
        //      of what each command means.
        //   2. Future changes to terminal behavior touch one site only.
        //   3. The protocol handler here remains a thin mechanical layer
        //      (latch DATA_LO, trigger on DATA_HI, route to subsystem).
        //
        // The cmd byte is passed through unchanged so term_execute() can
        // use a second switch on the same A2GSPU_CMD_TERM_* values, which
        // also equal the BUS_MOSAIC_CMD_TERM_* values forwarded over USB.
        // frame_dirty is set here (not inside term_execute) to keep the
        // dirty-flag contract at the protocol layer where all other
        // commands set it.
        case A2GSPU_CMD_TERM_PUTCHAR:
        case A2GSPU_CMD_TERM_SET_CURSOR:
        case A2GSPU_CMD_TERM_SET_FG:
        case A2GSPU_CMD_TERM_SET_BG:
        case A2GSPU_CMD_TERM_SET_ATTR:
        case A2GSPU_CMD_TERM_SCROLL_UP:
        case A2GSPU_CMD_TERM_SCROLL_DOWN:
        case A2GSPU_CMD_TERM_CLEAR_LINE:
        case A2GSPU_CMD_TERM_CLEAR_SCREEN:
        case A2GSPU_CMD_TERM_SET_REGION:
        case A2GSPU_CMD_TERM_SET_FONT:
        case A2GSPU_CMD_TERM_CURSOR_STYLE:
        case A2GSPU_CMD_TERM_SAVE_CURSOR:
        case A2GSPU_CMD_TERM_RESTORE_CURSOR:
        case A2GSPU_CMD_TERM_INSERT_CHARS:
        case A2GSPU_CMD_TERM_DELETE_CHARS:
        case A2GSPU_CMD_TERM_ERASE_CHARS:
        case A2GSPU_CMD_TERM_ALT_SCREEN:
            a2gspu_emu_term_execute(&ad->emu, ps->cmd, data16);
            ad->emu.frame_dirty = true;
            break;

        // ── TERM_WRITE_RAW (0x2E) ──────────────────────────────────────
        //
        // The IIgs application is acting as a transparent byte pump: it
        // has received a raw byte from a serial port or TCP socket (via
        // Marinetti or WiModem232) and wants the card to parse and render
        // it without any IIgs-side terminal interpretation.
        //
        // This command exists because:
        //   - ProDOS / GS/OS serial drivers emit raw bytes; they have no
        //     ANSI escape sequence awareness.
        //   - A 65816 VT100 parser is non-trivial to implement and
        //     consumes scarce IIgs RAM.
        //   - The RP2350 has a 384 MHz ARM M33 core and 512 KB SRAM;
        //     parsing ANSI sequences is essentially free on the card.
        //
        // Execution path:
        //   IIgs POKE slot+$02 (DATA_HI)
        //     → a2gspu_slot_write() latches REG_DATA_HI
        //       → execute_with_data() dispatches here
        //         → vt100_process(parser, byte)        [vt100.c]
        //           → parser FSM accumulates byte; on complete sequence:
        //             → telnet_vt100_callback(emu, vt_cmd, p1, p2)  [a2gspu_emu.cpp]
        //               → a2gspu_emu_term_execute(emu, cmd, data)
        //                 → renders to emu.term_grid[]
        //
        // The real card's equivalent: handle_mosaic_command() receives
        // the raw [REG_CMD=0x2E, REG_DATA_LO=byte] pair, calls the same
        // vt100_process() function and the same callback chain.  The
        // emulator path is intentionally isomorphic.
        //
        // Lazy initialization: the vt100_state_t is allocated on first
        // WRITE_RAW rather than at card/emu init time.  This avoids
        // allocating and zeroing a ~200-byte parser state block when TERM
        // mode might never be used, and it matches the card firmware
        // behavior where the VT100 parser is initialized on first use
        // rather than at power-on.  Once allocated the parser persists
        // for the lifetime of the emu state (freed in a2gspu_emu_shutdown).
        // The check is intentionally unsynchronized: execute_with_data()
        // is always called from the IIgs CPU thread, so no lock is needed.
        case A2GSPU_CMD_TERM_WRITE_RAW:
            /* Feed raw byte through emulator-side VT100 parser.
             * This mirrors what the real card does: IIgs POKEs a raw byte,
             * card parses VT100 and renders. Works for both emulator and
             * real hardware paths. */
            if (!ad->emu.telnet_vt100) {
                // Lazy-init the VT100 parser for WRITE_RAW mode
                extern void a2gspu_emu_telnet_connect(a2gspu_emu_state *emu, const char *host, int port);
                vt100_state_t *vt = new vt100_state_t();
                extern void telnet_vt100_callback(void *, vt100_cmd_t, int, int);
                vt100_init(vt, telnet_vt100_callback, &ad->emu);
                ad->emu.telnet_vt100 = vt;
            }
            vt100_process((vt100_state_t *)ad->emu.telnet_vt100, data16 & 0xFF);
            ad->emu.frame_dirty = true;
            break;

        case A2GSPU_CMD_SET_PROTOCOL:
            // Protocol switching is card-side only (13 protocol parsers).
            // In EMU mode, the VT100 parser handles everything.
            // Store for HW forwarding but don't act locally.
            break;

        case A2GSPU_CMD_COMP_WRITE_RAW:
            // Raw byte through compositor's active protocol — card-side only.
            // EMU doesn't have the 13 protocol parsers; use TERM_WRITE_RAW for VT100.
            // Forward to card via mosaic_cmds for HW path.
            break;
    }
}

// ── Tile write (fast path) ─────────────────────────────────────────
// Writing TILE_HI triggers tile entry write at cursor + auto-increment.

static void write_tile_at_cursor(a2gspu_data *ad, uint16_t entry) {
    a2gspu_proto_state *ps = &ad->proto;

    // Bounds check
    if (ps->cursor_x >= 80 || ps->cursor_y >= 50) return;

    // TODO: Write to mosaic scene tile map
    // gpu_mosaic_set_tile(&ad->emu.scene, ps->cursor_x, ps->cursor_y, entry);

    // Auto-increment cursor (row-major)
    ps->cursor_x++;
    if (ps->cursor_x >= 80) {
        ps->cursor_x = 0;
        ps->cursor_y++;
        // cursor_y can exceed 50 — next write will be bounds-rejected
    }

    ad->emu.frame_dirty = true;
}

// ── Slot I/O write handlers ────────────────────────────────────────

static void a2gspu_slot_write(void *context, uint32_t address, uint8_t value) {
    a2gspu_data *ad = (a2gspu_data *)context;
    a2gspu_proto_state *ps = &ad->proto;
    uint8_t reg = address & 0x0F;

    // Execute locally for EMU backend
    switch (reg) {
        case A2GSPU_REG_CMD:
            ps->cmd = value;
            break;

        case A2GSPU_REG_DATA_LO:
            ps->data_lo = value;
            break;

        case A2GSPU_REG_DATA_HI: {
            uint16_t data16 = (uint16_t)ps->data_lo | ((uint16_t)value << 8);
            execute_with_data(ad, data16);
            break;
        }

        case A2GSPU_REG_TILE_LO:
            ps->tile_lo = value;
            break;

        case A2GSPU_REG_TILE_HI: {
            uint16_t entry = (uint16_t)ps->tile_lo | ((uint16_t)value << 8);
            write_tile_at_cursor(ad, entry);
            break;
        }

        case A2GSPU_REG_RAW_BYTE:
            // Single-write raw terminal byte — 3x more efficient than WRITE_RAW.
            // Feeds directly through the VT100 parser, same as the card firmware.
            if (!ad->emu.telnet_vt100) {
                vt100_state_t *vt = new vt100_state_t();
                vt100_init(vt, telnet_vt100_callback, &ad->emu);
                ad->emu.telnet_vt100 = vt;
            }
            if (ad->emu.mode != A2GSPU_MODE_TERM) {
                a2gspu_emu_set_mode(&ad->emu, A2GSPU_MODE_TERM);
            }
            vt100_process((vt100_state_t *)ad->emu.telnet_vt100, value);
            ad->emu.frame_dirty = true;
            break;

        default:
            break;
    }

    // Record raw write for HW forwarding.
    // The card replays these through handle_mosaic_command() — identical to
    // how PIO bus snooping delivers them on real IIgs hardware.
    // O4: buffer-full is rare (only hits if 1000+ slot writes arrive in one frame).
    if (__builtin_expect(ps->mosaic_cmd_count < BUS_MOSAIC_MAX_CMDS, 1)) {
        // O2: maintain a running byte offset alongside mosaic_cmd_count to
        // avoid recomputing count * BUS_MOSAIC_CMD_SIZE on every slot write.
        // Called on every IIgs slot I/O write — hot path at 60fps×writes/frame.
        uint32_t idx = ps->mosaic_cmd_count * BUS_MOSAIC_CMD_SIZE;
        ps->mosaic_cmds[idx + 0] = reg;
        ps->mosaic_cmds[idx + 1] = value;
        ps->mosaic_cmds[idx + 2] = 0;  // reserved
        ps->mosaic_cmd_count++;
    }
}

// ── Slot I/O read handlers ─────────────────────────────────────────

static uint8_t a2gspu_slot_read(void *context, uint32_t address) {
    a2gspu_data *ad = (a2gspu_data *)context;
    uint8_t reg = address & 0x0F;

    switch (reg) {
        case A2GSPU_REG_STATUS: {
            uint8_t status = 0;
            if (ad->emu.mode != A2GSPU_MODE_PASSTHROUGH)
                status |= A2GSPU_STATUS_UHR;
            if (ad->serial_handle)
                status |= A2GSPU_STATUS_HW;
            status |= (ad->emu.mode & A2GSPU_STATUS_MODE_MASK);
            return status;
        }

        case A2GSPU_REG_IDENT:
            return A2GSPU_IDENT_BYTE;

        default:
            return 0;
    }
}

// ── Registration ───────────────────────────────────────────────────

void a2gspu_proto_register(a2gspu_data *ad, void *mmu_ptr, uint16_t slot_base) {
    MMU_II *mmu = (MMU_II *)mmu_ptr;

    // Register handlers for all 16 addresses in slot I/O space
    for (int i = 0; i < 16; i++) {
        mmu->set_C0XX_write_handler(slot_base + i, {a2gspu_slot_write, ad});
        mmu->set_C0XX_read_handler(slot_base + i, {a2gspu_slot_read, ad});
    }
}
