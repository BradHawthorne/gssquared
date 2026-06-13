/*
 *   A2GSPU Mosaic Slot I/O Protocol
 *
 *   Command interface for IIgs software to drive UHR content on the
 *   A2GSPU card (or emulated equivalent). Uses the slot's I/O space
 *   ($C0n0-$C0nF where n = slot + 8) for a command/data register pair.
 *
 *   Protocol design prioritizes tile map streaming throughput:
 *   setting cursor once, then streaming tile entries at 2 bytes each.
 *   At 2.8 MHz, a full 80×50 tile map update takes ~2.9ms.
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#pragma once

#include <cstdint>
#include "bus_protocol.h"

// ── Slot I/O Register Map ──────────────────────────────────────────
// Base address: $C080 + (slot * $10)
//
// Offset  R/W  Name         Description
// ------  ---  -----------  ------------------------------------------
//  $0     W    CMD          Command register
//  $1     W    DATA_LO      Data low byte
//  $2     W    DATA_HI      Data high byte (triggers command execution)
//  $3     R    STATUS       Status register
//  $4     W    TILE_LO      Tile entry low (for fast tile streaming)
//  $5     W    TILE_HI      Tile entry high (triggers tile write + auto-increment)
//  $F     R    IDENT        Identity byte ($A2 = A2GSPU present)

#define A2GSPU_REG_CMD       0x00
#define A2GSPU_REG_DATA_LO   0x01
#define A2GSPU_REG_DATA_HI   0x02
#define A2GSPU_REG_STATUS    0x03
#define A2GSPU_REG_TILE_LO   0x04
#define A2GSPU_REG_TILE_HI   0x05
#define A2GSPU_REG_RAW_BYTE  0x06  // W: Raw terminal byte (single-write trigger, 3x faster than WRITE_RAW)
#define A2GSPU_REG_IDENT     0x0F

// ── Commands ───────────────────────────────────────────────────────
// Write command byte to CMD, then DATA_LO + DATA_HI.
// DATA_HI write triggers execution.

#define A2GSPU_CMD_NOP           0x00  // No operation
#define A2GSPU_CMD_SET_MODE      0x01  // data[7:0] = a2gspu_video_mode_t
#define A2GSPU_CMD_SET_CURSOR    0x02  // data[7:0] = X, data[15:8] = Y
#define A2GSPU_CMD_SET_PAL_IDX   0x03  // data[7:0] = palette start index
#define A2GSPU_CMD_WRITE_PAL     0x04  // data[15:0] = RGB565 color, auto-increment
#define A2GSPU_CMD_SCROLL        0x05  // data[7:0] = dx (signed), data[15:8] = dy
#define A2GSPU_CMD_INVALIDATE    0x06  // Force full scene redraw
#define A2GSPU_CMD_SELECT_SHEET  0x07  // data[7:0] = sheet index (0-3)
#define A2GSPU_CMD_SET_DRAW_POS  0x08  // data[15:0] = X pixel position (0-639)
#define A2GSPU_CMD_SET_DRAW_Y    0x09  // data[15:0] = Y pixel position (0-399)
#define A2GSPU_CMD_SET_DRAW_COLOR 0x0A // data[7:0] = palette index for fill/line
#define A2GSPU_CMD_WRITE_PIXEL   0x0B  // data[7:0] = color at cursor, X auto-advance
#define A2GSPU_CMD_FILL_RECT     0x0C  // data[7:0]=W, data[15:8]=H, uses cursor+color
#define A2GSPU_CMD_FILL_SCREEN   0x0D  // data[7:0] = color, fills entire framebuffer
#define A2GSPU_CMD_HLINE         0x0E  // data[15:0] = length, horiz span at cursor
#define A2GSPU_CMD_VLINE         0x0F  // data[15:0] = length, vert span at cursor
#define A2GSPU_CMD_VERSION       0x10  // Read: data = firmware version

// ── Extended Drawing Primitives (0x11-0x1F) ──────────────────────
// Vector drawing: lines, circles, ellipses, arcs, flood fill.
// Processed by card-side drawing.c. Emulator forwards via USB CDC.
#define A2GSPU_CMD_LINE_TO_X     0x11  // data[15:0] = target X, latched
#define A2GSPU_CMD_LINE_TO_Y     0x12  // data[15:0] = target Y → Bresenham line, update cursor
#define A2GSPU_CMD_DRAW_CIRCLE   0x13  // data[15:0] = radius, outline
#define A2GSPU_CMD_FILL_CIRCLE   0x14  // data[15:0] = radius, filled
#define A2GSPU_CMD_DRAW_ELLIPSE  0x15  // data[7:0]=rx, [15:8]=ry, outline
#define A2GSPU_CMD_FILL_ELLIPSE  0x16  // data[7:0]=rx, [15:8]=ry, filled
#define A2GSPU_CMD_DRAW_ARC      0x17  // data[7:0]=start°, [15:8]=end° (uses latched radius)
#define A2GSPU_CMD_FLOOD_FILL    0x18  // data[7:0]=border_color, scanline fill
#define A2GSPU_CMD_SET_CLIP_X1   0x19  // data[15:0] = clip right edge, latched
#define A2GSPU_CMD_SET_CLIP_Y1   0x1A  // data[15:0] = clip bottom → activate clip rect
#define A2GSPU_CMD_RESET_CLIP    0x1B  // restore full-screen clipping
#define A2GSPU_CMD_SET_LINE_STYLE 0x1C // data[7:0]=pattern, [15:8]=thickness
#define A2GSPU_CMD_SET_FILL_STYLE 0x1D // data[7:0]=pattern_id, [15:8]=fill_color
#define A2GSPU_CMD_DRAW_RECT     0x1E  // data[7:0]=W, [15:8]=H, outline rect
#define A2GSPU_CMD_SET_ARC_RADIUS 0x1F // data[15:0] = radius, latched for DRAW_ARC

// ── Terminal Commands (0x20-0x2E) ─────────────────────────────────
//
// These 15 command codes constitute the TERM mode command layer.  Their
// numerical values are an exact 1:1 mirror of the BUS_MOSAIC_CMD_TERM_*
// constants in bus_protocol.h (0x20-0x2E), which are in turn the values
// packed into USB frames by the GSSquared emulator and replayed by the
// card firmware's handle_mosaic_command().  Keeping the two sets
// numerically identical is intentional: it means a raw command byte
// captured from slot I/O can be forwarded over USB without translation,
// and the card side requires no lookup table.
//
// The architectural split is:
//
//   IIgs application        Card / emulator renderer
//   ─────────────────       ────────────────────────
//   Sends pre-parsed        Receives discrete
//   discrete commands       PUTCHAR / SET_CURSOR /
//   (0x20-0x2D path)        SET_FG / … commands
//                           → a2gspu_emu_term_execute()
//
//     — OR —
//
//   Sends raw bytes         Card-side or emu-side VT100
//   (0x2E path)             parser decodes them and emits
//                           the same 0x20-0x2D commands
//
// Two host-side usage modes therefore exist:
//
//   A) IIgs app includes an ANSI/VT100 parser and issues discrete
//      0x20-0x2D commands.  Requires 65816 code on the IIgs.
//      Minimal bandwidth (typically 3 bytes per terminal action).
//
//   B) IIgs app (or ProDOS/GS/OS driver) simply pumps raw bytes
//      from a serial port or TCP socket into TERM_WRITE_RAW (0x2E).
//      The card's built-in VT100 parser handles decoding.  The IIgs
//      becomes a transparent byte relay — no terminal awareness needed.
//      This is the mode used by the emulator's built-in telnet bridge.
//
// Both modes ultimately arrive at the same renderer in a2gspu_emu.cpp
// (or card/terminal.c on real hardware).

// ── 0x20-0x2D: Discrete terminal commands ─────────────────────────
// The IIgs application has already parsed ANSI/VT100 sequences and
// issues these high-level commands directly.  Each uses the standard
// 3-register write sequence: CMD → DATA_LO → DATA_HI.
//
// Mirrors: BUS_MOSAIC_CMD_TERM_PUTCHAR … BUS_MOSAIC_CMD_TERM_RESTORE_CURSOR

#define A2GSPU_CMD_TERM_PUTCHAR       0x20  // data[7:0] = char; write at cursor, advance X
#define A2GSPU_CMD_TERM_SET_CURSOR    0x21  // data[7:0] = col,  data[15:8] = row
#define A2GSPU_CMD_TERM_SET_FG        0x22  // data[7:0] = foreground palette index (0-255)
#define A2GSPU_CMD_TERM_SET_BG        0x23  // data[7:0] = background palette index (0-255)
#define A2GSPU_CMD_TERM_SET_ATTR      0x24  // data[7:0] = BUS_TERM_ATTR_* flags (bold|uline|inverse|blink|strike|dim)
#define A2GSPU_CMD_TERM_SCROLL_UP     0x25  // data[7:0] = number of lines to scroll up within scroll region
#define A2GSPU_CMD_TERM_SCROLL_DOWN   0x26  // data[7:0] = number of lines to scroll down within scroll region
#define A2GSPU_CMD_TERM_CLEAR_LINE    0x27  // data[7:0] = mode: 0=to-end, 1=to-start, 2=whole line (VT100 EL)
#define A2GSPU_CMD_TERM_CLEAR_SCREEN  0x28  // data[7:0] = mode: 0=to-end, 1=to-start, 2=whole screen (VT100 ED)
#define A2GSPU_CMD_TERM_SET_REGION    0x29  // data[7:0] = scroll-region top row, data[15:8] = bottom row (VT100 DECSTBM)
#define A2GSPU_CMD_TERM_SET_FONT      0x2A  // data[7:0] = font_id: 0=CP437 8×16, 1=8×10, 2=8×8
#define A2GSPU_CMD_TERM_CURSOR_STYLE  0x2B  // data[7:0]: bits[1:0]=shape (0=block,1=uline,2=bar), bit7=visible
#define A2GSPU_CMD_TERM_SAVE_CURSOR   0x2C  // Save cursor col, row, fg, bg, attr (VT100 DECSC); data ignored
#define A2GSPU_CMD_TERM_RESTORE_CURSOR 0x2D // Restore state saved by SAVE_CURSOR (VT100 DECRC); data ignored

// ── 0x2E: Raw byte passthrough ─────────────────────────────────────
// TERM_WRITE_RAW is distinct from the 14 discrete commands above.
// Rather than encoding a pre-parsed terminal action, it delivers a
// single raw byte from the host byte stream to the card's built-in
// VT100 parser.
//
// Rationale: the IIgs has limited RAM for a 65816 VT100 parser, and
// ProDOS / GS/OS serial drivers emit raw bytes with no terminal
// interpretation.  By delegating parsing to the card (RP2350, 384 MHz,
// 512 KB SRAM), the IIgs application becomes a simple byte pump:
//   POKE slot+0, $2E        ; CMD = TERM_WRITE_RAW
//   POKE slot+1, next_byte  ; DATA_LO = raw byte
//   POKE slot+2, $00        ; DATA_HI = 0, triggers execution
// The card (or emulator) feeds the byte into its vt100_state_t and
// emits discrete TERM commands (0x20-0x2D) internally.
//
// In the emulator the VT100 parser is vt100_state_t (vt100.h).  The
// callback is telnet_vt100_callback() (a2gspu_emu.cpp), which maps each
// vt100_cmd_t → A2GSPU_CMD_TERM_* and calls a2gspu_emu_term_execute().
//
// Mirrors: BUS_MOSAIC_CMD_TERM_WRITE_RAW (0x2E) in bus_protocol.h
#define A2GSPU_CMD_TERM_WRITE_RAW     0x2E  // data[7:0] = raw byte; feed through card-side VT100 parser
#define A2GSPU_CMD_TERM_INSERT_CHARS  0x2F  // data[7:0]=count; insert blanks, shift right (CSI @)
#define A2GSPU_CMD_TERM_DELETE_CHARS  0x30  // data[7:0]=count; delete at cursor, shift left (CSI P)
#define A2GSPU_CMD_TERM_ERASE_CHARS   0x31  // data[7:0]=count; blank N chars, no shift (CSI X)
#define A2GSPU_CMD_TERM_ALT_SCREEN    0x32  // data[0]=1: enter alt screen; 0: leave

#define A2GSPU_CMD_SET_PROTOCOL       0x33  // data[7:0] = protocol_id (0x00-0x0C)
#define A2GSPU_CMD_COMP_WRITE_RAW     0x34  // data[7:0] = raw byte through active protocol

// ── Status Register Bits ───────────────────────────────────────────
#define A2GSPU_STATUS_BUSY       0x80  // Rendering in progress
#define A2GSPU_STATUS_UHR        0x40  // UHR mode active (not passthrough)
#define A2GSPU_STATUS_HW         0x20  // Hardware card connected (USB serial)
#define A2GSPU_STATUS_MODE_MASK  0x0F  // Current mode (low nibble)

// ── Identity ───────────────────────────────────────────────────────
#define A2GSPU_IDENT_BYTE        0xA2  // "A2" — A2GSPU card present

// ── Protocol State ─────────────────────────────────────────────────

struct a2gspu_proto_state {
    uint8_t cmd;            // Current command
    uint8_t data_lo;        // Data low byte (latched)
    uint8_t tile_lo;        // Tile entry low byte (latched)

    // Pixel/tile cursor (used by draw commands and tile writes)
    uint16_t cursor_x;      // 0-639 for pixel ops, 0-79 for tile ops
    uint16_t cursor_y;      // 0-399 for pixel ops, 0-49 for tile ops

    // Palette write cursor
    uint8_t pal_idx;        // 0-255, auto-increments

    // Latched draw color for fill/line operations
    uint8_t draw_color;     // palette index (0-255)

    // Extended drawing state (latched values for vector commands)
    uint16_t line_to_x;     // latched target X for LINE_TO_Y
    uint16_t clip_x1;       // latched clip right edge for SET_CLIP_Y1
    uint16_t arc_radius;    // latched radius for DRAW_ARC
    uint8_t  line_pattern;  // line dash pattern (0xFF = solid)
    uint8_t  line_thickness; // line thickness in pixels
    uint8_t  fill_pattern;  // fill pattern index (0 = solid)
    uint8_t  fill_color;    // fill pattern secondary color
    uint8_t  write_mode;    // 0=COPY, 1=XOR, 2=OR

    // Accumulated slot I/O writes for HW forwarding.
    // Each frame, raw register writes are recorded here and packed into the
    // USB frame. The card replays them through handle_mosaic_command() —
    // identical to how PIO bus snooping delivers them on real hardware.
    // Buffer is only cleared after successful USB send (wr==1), so skipped
    // frames re-send accumulated commands.
    uint8_t mosaic_cmds[BUS_MOSAIC_MAX_CMDS * BUS_MOSAIC_CMD_SIZE];
    uint16_t mosaic_cmd_count;

    a2gspu_proto_state() :
        cmd(0), data_lo(0), tile_lo(0),
        cursor_x(0), cursor_y(0), pal_idx(0), draw_color(0),
        line_to_x(0), clip_x1(0), arc_radius(0),
        line_pattern(0xFF), line_thickness(0), fill_pattern(0), fill_color(0), write_mode(0),
        mosaic_cmd_count(0) {}
};

// Forward declarations
struct a2gspu_data;

// Register slot I/O handlers for the Mosaic protocol
void a2gspu_proto_register(a2gspu_data *ad, void *mmu, uint16_t slot_base);
