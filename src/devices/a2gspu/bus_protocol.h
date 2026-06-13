/*
 * A2GSPU Bus Protocol — shared between GSSquared and card firmware.
 *
 * Variable-length frame with sync word for reliable USB CDC transport.
 *
 * Frame format:
 *   [SYNC:4][C029:1][C022:1][C034:1][C035:1][FLAGS:1][MODE:1][FLAGS2:1]
 *   [TEXT1_MAIN:1024][TEXT1_AUX:1024]
 *   [HGR_MAIN:8192][HGR_AUX:8192]     — if FLAGS has HGR/HGR_AUX bits
 *   [SHR:32768]                         — if FLAGS has SHR bit (full frame)
 *   [SHR_DIRTY:count+bitmap+scb+pal+lines] — if FLAGS has SHR_DIRTY bit
 *   [SCANLINE:800]                      — if FLAGS has SCANLINE bit (per-line state)
 *   [MOSAIC:count+entries]              — if FLAGS2 has MOSAIC bit (slot I/O commands)
 *   [CSUM:2]
 *
 * SYNC = 0x7E 0x41 0x32 0x47 ("~A2G")
 * FLAGS bit 0: text page 1 main valid
 * FLAGS bit 1: text page 1 aux valid (for TEXT80)
 * FLAGS bit 2: HGR main valid ($2000-$3FFF)
 * FLAGS bit 3: HGR aux valid ($2000-$3FFF aux, for DHGR)
 * FLAGS bit 4: SHR full (32KB pixels+SCBs+palettes)
 * FLAGS bit 5: SHR dirty (incremental: bitmap + changed lines only)
 * FLAGS bit 6: per-scanline mode+border (800 bytes: 4 per active line)
 * FLAGS bit 7: IIgs monochrome mode ($C021 bit 7)
 * FLAGS2 bit 0: Mosaic command buffer present (slot I/O writes for UHR)
 *
 * SHR, SHR_DIRTY, and HGR are mutually exclusive.
 */

#ifndef BUS_PROTOCOL_H
#define BUS_PROTOCOL_H

#include <stdint.h>

#define BUS_SYNC_0       0x7E
#define BUS_SYNC_1       0x41
#define BUS_SYNC_2       0x32
#define BUS_SYNC_3       0x47

/* Header: [SYNC:4][C029:1][C022:1][C034:1][C035:1][FLAGS:1][MODE:1][FLAGS2:1] = 11 bytes.
 * FLAGS2 was added to support Mosaic command forwarding without consuming
 * the fully-allocated FLAGS byte. All data offsets derive from BUS_HEADER_SIZE,
 * so expanding from 10→11 automatically shifts text/HGR/SHR sections. */
#define BUS_HEADER_SIZE  11
#define BUS_FLAGS2_OFFSET 10   /* frame[10] = FLAGS2 byte */

#define BUS_TEXT1_SIZE   1024
#define BUS_HGR_SIZE     8192
#define BUS_SHR_SIZE     32768     /* $2000-$9FFF = pixels + SCBs + palettes */
#define BUS_SHR_LINES    200
#define BUS_SHR_LINE_BYTES 160
#define BUS_SHR_SCB_SIZE 200
#define BUS_SHR_PAL_SIZE 512
#define BUS_SHR_BITMAP_SIZE 25    /* ceil(200/8) = 25 bytes */
#define BUS_TEXT1_OFFSET BUS_HEADER_SIZE                               /* main text page = 11 */
#define BUS_AUX1_OFFSET  (BUS_TEXT1_OFFSET + BUS_TEXT1_SIZE)           /* aux text page = 1035 */
#define BUS_HGR_OFFSET   (BUS_AUX1_OFFSET + BUS_TEXT1_SIZE)           /* HGR main = 2059 */
#define BUS_HGR_AUX_OFFSET (BUS_HGR_OFFSET + BUS_HGR_SIZE)           /* HGR aux = 10251 */
#define BUS_SHR_OFFSET   BUS_HGR_OFFSET                               /* SHR at same offset (replaces HGR) */

/* SHR dirty frame layout (starting at BUS_SHR_OFFSET):
 *   [COUNT:1]          — number of dirty lines (0-200)
 *   [BITMAP:25]        — 200-bit dirty mask (bit N = line N dirty)
 *   [SCB:200]          — all SCBs (always included)
 *   [PAL:512]          — all palettes (always included)
 *   [pixels: count×160] — only dirty line pixel data, in order
 */
#define BUS_SHR_DIRTY_HEADER (1 + BUS_SHR_BITMAP_SIZE + BUS_SHR_SCB_SIZE + BUS_SHR_PAL_SIZE)  /* 738 bytes */

#define BUS_FLAG_TEXT1      0x01
#define BUS_FLAG_TEXT1_AUX  0x02
#define BUS_FLAG_HGR        0x04
#define BUS_FLAG_HGR_AUX    0x08
#define BUS_FLAG_SHR        0x10   /* SHR full frame (mutually exclusive with HGR/SHR_DIRTY) */
#define BUS_FLAG_SHR_DIRTY  0x20   /* SHR incremental (bitmap + dirty lines only) */
#define BUS_FLAG_SCANLINE   0x40   /* Per-scanline state (800 bytes: 4 per active line) */
#define BUS_FLAG_MONO       0x80   /* IIgs monochrome mode ($C021 bit 7) */

/* FLAGS2 byte (frame[10]) — extended flags for features beyond the 8-bit FLAGS. */
#define BUS_FLAG2_MOSAIC    0x01   /* Mosaic command buffer present (UHR slot I/O writes) */

/* Per-scanline state block: 200 lines × 4 bytes = 800 bytes.
 * Each entry: [video_mode:1][border_color:1][mode_flags:1][scb:1]
 *   video_mode:   BUS_VM_* renderer selector (TEXT40, SHR, HIRES, etc.)
 *   border_color: IIgs color index (0-15) for border during this line
 *   mode_flags:   BUS_MODE_* flags (80COL, DGR, etc.) for this line
 *   scb:          SHR Screen Control Byte (0 if not SHR mode)
 * Only sent when video_mode varies across scanlines (split-screen). */
#define BUS_SCANLINE_SIZE   800    /* 200 × 4 bytes per active scanline */
#define BUS_SCANLINE_LINES  200    /* number of active scanlines */

/* Video mode values for scanline state (matches GSSquared video_mode_t) */
#define BUS_VM_TEXT40       0
#define BUS_VM_LORES        1
#define BUS_VM_HIRES        2
#define BUS_VM_TEXT80       4
#define BUS_VM_DLORES       5
#define BUS_VM_DHIRES       6
#define BUS_VM_SHR          7

/* ── Mosaic Protocol ──────────────────────────────────────────────────
 * Slot I/O register writes forwarded from GSSquared (or captured by PIO
 * on real hardware). Appended after SCANLINE section, before checksum.
 *
 * Layout: [COUNT:2 (uint16_t LE)][ENTRIES: count × 3 bytes]
 * Each entry: [REG:1][VALUE:1][PAD:1]
 *
 * REG is the slot register offset (0x0-0xF). The card replays these
 * through handle_mosaic_command() — identical code path whether commands
 * arrive via USB frame or PIO bus snooping on real IIgs hardware.
 *
 * Why replay raw writes instead of high-level commands: preserves exact
 * sequencing (DATA_LO latch before DATA_HI trigger, TILE_LO before
 * TILE_HI). The card maintains its own protocol state machine. */
#define BUS_MOSAIC_CMD_SIZE   3      /* bytes per command entry */
#define BUS_MOSAIC_MAX_CMDS   8192   /* max entries per frame (~24KB) */
#define BUS_MOSAIC_COUNT_SIZE 2      /* uint16_t count prefix */

/* Mosaic slot I/O register offsets (relative to slot base $C080 + slot*$10).
 * Shared between GSSquared proto handler and card-side command handler. */
#define BUS_MOSAIC_REG_CMD       0x00  /* W: Command byte */
#define BUS_MOSAIC_REG_DATA_LO   0x01  /* W: Data low (latched) */
#define BUS_MOSAIC_REG_DATA_HI   0x02  /* W: Data high (triggers command) */
#define BUS_MOSAIC_REG_STATUS    0x03  /* R: Status register */
#define BUS_MOSAIC_REG_TILE_LO   0x04  /* W: Tile entry low (latched) */
#define BUS_MOSAIC_REG_TILE_HI   0x05  /* W: Tile entry high (triggers write) */
#define BUS_MOSAIC_REG_RAW_BYTE  0x06  /* W: Raw terminal byte — single-write trigger.
                                        * Every write feeds one byte through the card-side
                                        * VT100 parser. No CMD/DATA_LO/DATA_HI sequence
                                        * needed — one STA per byte. This is 3x more
                                        * bus-efficient than TERM_WRITE_RAW (0x2E).
                                        *
                                        * IIgs usage (65816):
                                        *   .loop  LDA (serial_port)
                                        *          STA CARD_RAW_REG   ; $C0B6 for slot 3
                                        *          BRA .loop
                                        *
                                        * ~5 cycles per byte at 2.8 MHz = 560K bytes/sec max.
                                        * Far exceeds any serial/TCP data rate. */
#define BUS_MOSAIC_REG_IDENT     0x0F  /* R: Identity byte ($A2) */

/* Mosaic command IDs (written to REG_CMD, executed when REG_DATA_HI written) */
#define BUS_MOSAIC_CMD_NOP           0x00
#define BUS_MOSAIC_CMD_SET_MODE      0x01  /* data[7:0] = video mode */
#define BUS_MOSAIC_CMD_SET_CURSOR    0x02  /* data[7:0]=X, data[15:8]=Y */
#define BUS_MOSAIC_CMD_SET_PAL_IDX   0x03  /* data[7:0] = palette start index */
#define BUS_MOSAIC_CMD_WRITE_PAL     0x04  /* data[15:0] = RGB565 color, auto-incr */
#define BUS_MOSAIC_CMD_SCROLL        0x05  /* data[7:0]=dx, data[15:8]=dy (signed) */
#define BUS_MOSAIC_CMD_INVALIDATE    0x06  /* Force full redraw */
#define BUS_MOSAIC_CMD_SELECT_SHEET  0x07  /* data[7:0] = sheet index (0-3) */
#define BUS_MOSAIC_CMD_SET_DRAW_POS  0x08  /* data[15:0] = X pixel position (0-639) */
#define BUS_MOSAIC_CMD_SET_DRAW_Y    0x09  /* data[15:0] = Y pixel position (0-399) */
#define BUS_MOSAIC_CMD_SET_DRAW_COLOR 0x0A /* data[7:0] = palette index for fill/line ops */
#define BUS_MOSAIC_CMD_WRITE_PIXEL   0x0B  /* data[7:0] = color at cursor, X auto-advance */
#define BUS_MOSAIC_CMD_FILL_RECT     0x0C  /* data[7:0]=W, data[15:8]=H, uses cursor+color */
#define BUS_MOSAIC_CMD_FILL_SCREEN   0x0D  /* data[7:0] = color, fills entire framebuffer */
#define BUS_MOSAIC_CMD_HLINE         0x0E  /* data[15:0] = length, horiz span at cursor */
#define BUS_MOSAIC_CMD_VLINE         0x0F  /* data[15:0] = length, vert span at cursor */
#define BUS_MOSAIC_CMD_VERSION       0x10  /* Read: firmware version */

/* ── Extended Drawing Primitives (0x11-0x1F) ─────────────────────────────────
 *
 * Vector drawing commands for RIPscrip, ReGIS, NAPLPS, and general graphics.
 * All use the latched draw position (SET_DRAW_POS/SET_DRAW_Y) and draw color
 * (SET_DRAW_COLOR). Coordinates are clipped to the active clip rectangle.
 *
 * Multi-command sequences:
 *   Line:   SET_DRAW_POS(x0) → SET_DRAW_Y(y0) → LINE_TO(x1 | y1<<16) [two cmds]
 *   Circle: SET_DRAW_POS(cx) → SET_DRAW_Y(cy) → DRAW_CIRCLE(r)
 *   Flood:  SET_DRAW_POS(x)  → SET_DRAW_Y(y)  → FLOOD_FILL(border) */
#define BUS_MOSAIC_CMD_LINE_TO_X     0x11  /* data[15:0] = target X, latched */
#define BUS_MOSAIC_CMD_LINE_TO_Y     0x12  /* data[15:0] = target Y → draw Bresenham line from cursor to (X,Y), update cursor */
#define BUS_MOSAIC_CMD_DRAW_CIRCLE   0x13  /* data[15:0] = radius, outline circle at cursor */
#define BUS_MOSAIC_CMD_FILL_CIRCLE   0x14  /* data[15:0] = radius, filled circle at cursor */
#define BUS_MOSAIC_CMD_DRAW_ELLIPSE  0x15  /* data[7:0] = rx, data[15:8] = ry, outline */
#define BUS_MOSAIC_CMD_FILL_ELLIPSE  0x16  /* data[7:0] = rx, data[15:8] = ry, filled */
#define BUS_MOSAIC_CMD_DRAW_ARC      0x17  /* data[7:0] = start°, data[15:8] = end° (uses latched radius) */
#define BUS_MOSAIC_CMD_FLOOD_FILL    0x18  /* data[7:0] = border_color, scanline fill from cursor */
#define BUS_MOSAIC_CMD_SET_CLIP_X1   0x19  /* data[15:0] = clip right edge, latched */
#define BUS_MOSAIC_CMD_SET_CLIP_Y1   0x1A  /* data[15:0] = clip bottom → activates clip rect from cursor pos */
#define BUS_MOSAIC_CMD_RESET_CLIP    0x1B  /* — restore full-screen clipping */
#define BUS_MOSAIC_CMD_SET_LINE_STYLE 0x1C /* data[7:0] = pattern, data[15:8] = thickness */
#define BUS_MOSAIC_CMD_SET_FILL_STYLE 0x1D /* data[7:0] = pattern_id, data[15:8] = fill_color */
#define BUS_MOSAIC_CMD_DRAW_RECT     0x1E  /* data[7:0] = W, data[15:8] = H, outline rect at cursor */
#define BUS_MOSAIC_CMD_SET_ARC_RADIUS 0x1F /* data[15:0] = radius, latched for DRAW_ARC */

/* ── Terminal (TERM) Mode ─────────────────────────────────────────────────────
 *
 * MOTIVATION — BBS terminal rendering on a DVI card
 * ─────────────────────────────────────────────────
 * A2GSPU is installed in the IIgs video slot and has direct HSTX DVI output.
 * Running a BBS terminal application on the IIgs (e.g., connecting via
 * Uthernet II + Marinetti or a WiModem232 serial adapter) requires rendering
 * VT100/ANSI escape sequences at interactive speeds. The native IIgs soft-font
 * renderer in SHR 640 mode is fast enough, but using the card's dedicated
 * RP2350 renders completely independently of the 65C816 CPU, freeing the host
 * entirely for application logic (protocol parsing, network I/O, etc.).
 *
 * TERM mode (BUS_MOSAIC_MODE_TERM = 5) activates an 80×25 character-cell
 * terminal on the card. The card owns:
 *   • A character grid: 80 columns × 25 rows = 2,000 cells
 *   • Per-cell state: Unicode/CP437 codepoint, fg color index, bg color index,
 *     attribute flags (bold, underline, inverse, blink, strikethrough, dim)
 *   • A hardware cursor with configurable style (block/underline/bar) and
 *     blink period
 *   • A scroll region (top/bottom row pair, defaulting to 0/24)
 *   • A saved-cursor register (VT100 DECSC/DECRC)
 *   • A CP437 8×16 bitmap font (glyphs rendered into the HSTX framebuffer)
 *
 * The card renders the terminal into its 640×400 HSTX framebuffer using the
 * active UHR palette for color mapping. Character cells are 8×16 pixels
 * (80×25 → 640×400, exactly filling the active area with no letterboxing).
 *
 *
 * TWO TERMINAL COMMAND PATHS
 * ──────────────────────────
 * The IIgs has two ways to drive the TERM mode renderer:
 *
 *   Path A — Structured commands (0x20-0x2D):
 *     The IIgs-side terminal application parses ANSI/VT100 escape sequences
 *     itself and translates them into discrete card commands: SET_CURSOR,
 *     SET_FG, SET_BG, SET_ATTR, PUTCHAR, etc. This path gives the IIgs full
 *     control and is suited for GS/OS applications that use Marinetti TCP/IP
 *     or other high-level I/O layers.
 *
 *   Path B — TERM_WRITE_RAW (0x2E):
 *     The IIgs pumps raw bytes directly to the card. The card contains its
 *     own VT100/ANSI state machine and processes escape sequences
 *     autonomously. This path requires essentially zero IIgs CPU — the host
 *     only needs to read bytes from SCC (serial) or Uthernet II (network) and
 *     forward them unmodified. It works identically from ProDOS 8 assembly,
 *     ProDOS 16, GS/OS, or bare-metal 65C816 code with no dependency on any
 *     IIgs system software beyond the slot I/O write mechanism.
 *
 *
 * THE 3-BYTE COMMAND PROTOCOL CONSTRAINT
 * ───────────────────────────────────────
 * All Mosaic commands share the slot I/O register interface:
 *
 *   STA CMD_REG       ; write command opcode
 *   STA DATA_LO_REG   ; write low byte of 16-bit data word (latched)
 *   STA DATA_HI_REG   ; write high byte → triggers execution
 *
 * Each command carries exactly 16 bits of payload — one byte in DATA_LO,
 * one in DATA_HI. This is a hard constraint imposed by the 8-bit Apple II
 * slot I/O bus: each register write is one byte, and the card must trigger
 * on a single "commit" write to avoid partial-state execution. The DATA_HI
 * write serves as that atomic trigger.
 *
 * For UHR drawing commands (0x08-0x0F) this 16-bit payload is sufficient:
 * pixel positions fit in 10 bits, colors in 8 bits, etc. The TERM commands
 * (0x20-0x2E) are designed around the same constraint and exploit latched
 * card state to avoid needing more payload bits per command.
 *
 *
 * WHY SEPARATE SET_FG / SET_BG / SET_ATTR COMMANDS
 * ─────────────────────────────────────────────────
 * xterm-256 color requires 8 bits per color channel (fg or bg). If fg and
 * bg were packed into a single 16-bit data word (8 bits each), that would
 * consume the entire payload, leaving no room for the attribute flags byte
 * (bold, underline, inverse, blink, strike, dim). A hypothetical packed
 * "SET_COLORS_AND_ATTR" command would require 8+8+8 = 24 bits of payload,
 * which exceeds the 16-bit protocol limit by 50%.
 *
 * The solution is to split attribute state into three separate latching
 * commands — SET_FG, SET_BG, SET_ATTR — each using only the DATA_LO byte.
 * The card stores these in a "current cell attribute" register. PUTCHAR then
 * applies the latched fg/bg/attr when writing the character cell, without
 * any additional data bytes needed at write time.
 *
 * This also matches how VT100 escape sequences actually work: SGR parameters
 * (CSI ... m) arrive as a sequence of numeric codes that modify the current
 * attribute state, not as a single atomic color+attribute tuple. The
 * structured TERM commands mirror this naturally.
 *
 *
 * LATCHED-STATE PUTCHAR PARADIGM
 * ────────────────────────────────
 * TERM_PUTCHAR (0x20) writes a single character at the current cursor
 * position using the current latched fg, bg, and attr state, then advances
 * the cursor. This makes streaming a run of characters with the same
 * attributes maximally efficient: set fg/bg/attr once, then issue one
 * PUTCHAR per character with no repeated attribute data.
 *
 * Typical usage for a line of text in color:
 *
 *   SET_FG   (bright_white)      ; 3 cycles
 *   SET_BG   (blue)              ; 3 cycles
 *   SET_ATTR (bold)              ; 3 cycles
 *   PUTCHAR  ('H')               ; 3 cycles  → writes cell with latched attrs
 *   PUTCHAR  ('e')               ; 3 cycles  → same attrs, cursor at +1
 *   PUTCHAR  ('l')               ; 3 cycles
 *   ...
 *
 * Without latching, each PUTCHAR would need to carry fg+bg+attr inline,
 * requiring either multi-command sequences per cell or a wider protocol.
 * With latching, the attribute overhead is amortized across all characters
 * in a run — exactly what BBS ANSI art requires (long runs of same-color
 * characters interrupted by occasional SGR changes).
 *
 *
 * SCROLL REGION (TERM_SET_REGION) AND VT100 DECSTBM
 * ──────────────────────────────────────────────────
 * VT100's DECSTBM escape (CSI Pt ; Pb r) defines a scrolling region
 * bounded by a top row (Pt) and bottom row (Pb). Scroll-up and scroll-down
 * operations affect only rows within this region; rows outside it are
 * preserved. This is essential for full-screen BBS applications such as
 * door games (Legend of the Red Dragon, TradeWars) that maintain a status
 * line at the bottom while scrolling a message area above it.
 *
 * TERM_SET_REGION maps directly to DECSTBM:
 *   data[7:0]  = top row index (0-based, inclusive)
 *   data[15:8] = bottom row index (0-based, inclusive)
 *
 * Both TERM_SCROLL_UP and TERM_SCROLL_DOWN respect this region: lines
 * scrolled out of the region are discarded and new blank lines (filled with
 * current bg color) are inserted at the opposite boundary. The region
 * defaults to rows 0-24 (full screen) on mode entry and on reset.
 *
 * On Path B (TERM_WRITE_RAW), the card's internal VT100 parser handles
 * DECSTBM automatically. On Path A, the IIgs application must call
 * TERM_SET_REGION whenever it processes a CSI ... r sequence.
 *
 *
 * CLEAR_LINE AND CLEAR_SCREEN MODE PARAMETERS (VT100 ED/EL)
 * ──────────────────────────────────────────────────────────
 * VT100 defines two erase families:
 *
 *   ED  (Erase in Display, CSI J / CSI n J):
 *     n=0: erase from cursor to end of screen
 *     n=1: erase from start of screen to cursor
 *     n=2: erase entire screen (cursor unchanged in strict VT100; many
 *          implementations also home the cursor, but that is non-standard)
 *
 *   EL  (Erase in Line, CSI K / CSI n K):
 *     n=0: erase from cursor to end of current line
 *     n=1: erase from start of current line to cursor
 *     n=2: erase entire current line
 *
 * TERM_CLEAR_SCREEN and TERM_CLEAR_LINE both accept the same mode byte
 * matching this 0/1/2 encoding, making the IIgs-side VT100 parser (Path A)
 * trivially simple: extract the CSI numeric parameter and write it directly
 * as the DATA_LO byte. On Path B, the card's VT100 parser performs the
 * parameter extraction internally. All erase operations fill cleared cells
 * with the current background color index (latched via SET_BG) so that
 * colored backgrounds (e.g., reverse-video status bars) clear correctly.
 *
 *
 * TERM_WRITE_RAW — RAW BYTE INJECTION FOR CARD-SIDE VT100 PARSING
 * ────────────────────────────────────────────────────────────────
 * TERM_WRITE_RAW (0x2E) is qualitatively different from all other TERM
 * commands. Where commands 0x20-0x2D represent high-level terminal
 * operations (each equivalent to a decoded escape sequence), TERM_WRITE_RAW
 * feeds a single undecoded raw byte into the card's internal VT100/ANSI
 * state machine.
 *
 * The card's parser implements:
 *   • Printable ASCII and CP437 extended characters → PUTCHAR to grid
 *   • C0 control characters: BS (0x08), HT (0x09), LF (0x0A), CR (0x0D),
 *     BEL (0x07), DEL (0x7F)
 *   • CSI sequences: cursor movement (CUU/CUD/CUF/CUB/CUP), erase
 *     (ED/EL), SGR (color + attributes), scroll (SU/SD), cursor
 *     style (DECTCEM), DECSTBM scroll region, DECSC/DECRC
 *   • OSC sequences (title set, ignored gracefully)
 *
 * From the IIgs application's perspective, using TERM_WRITE_RAW reduces
 * the terminal driver to a trivial byte pump:
 *
 *   ; 65C816 pseudocode — forward one byte from SCC to card
 *   LDA  #BUS_MOSAIC_CMD_TERM_WRITE_RAW
 *   STA  SLOT_CMD_REG
 *   LDA  received_byte
 *   STA  SLOT_DATA_LO_REG
 *   STZ  SLOT_DATA_HI_REG          ; trigger execution (hi byte unused)
 *
 * This three-instruction loop is the complete terminal driver. It works
 * identically whether the IIgs is running ProDOS 8, ProDOS 16, GS/OS, or
 * bare metal. It requires no knowledge of escape sequences, no state
 * machine, no screen-coordinate arithmetic, and no font rendering. The
 * 65C816's entire bandwidth is available for the I/O path (serial or TCP).
 *
 * The distinction between 0x2E and 0x20-0x2D is intentional: applications
 * that need precise control (e.g., a GS/OS desktop terminal that also wants
 * to overlay native graphics) should use the structured commands. Applications
 * that just need a fast, simple path to a BBS should use TERM_WRITE_RAW.
 * Both paths update the same internal terminal state and produce identical
 * visual output for equivalent input sequences.
 */

/* ── Terminal commands (0x20-0x2E) ──────────────────────────────────
 * Used in TERM mode (BUS_MOSAIC_MODE_TERM). The card maintains an
 * 80×25 character grid with per-cell fg/bg/attr. The IIgs-side ANSI/VT100
 * parser converts escape sequences to these commands.
 *
 * Each command uses the standard 3-byte protocol:
 *   write CMD → write DATA_LO → write DATA_HI (triggers execution)
 * The 16-bit data word is encoded as [DATA_HI:DATA_LO] big-endian. */

/* TERM_PUTCHAR — write character at cursor, then advance cursor right.
 * Uses the current latched fg color, bg color, and attribute flags (set
 * via SET_FG / SET_BG / SET_ATTR). DATA_LO carries the CP437 codepoint
 * (0x00-0xFF); DATA_HI is reserved and must be zero. When the cursor
 * reaches column 79, the next PUTCHAR wraps to column 0 of the next row;
 * if the cursor is on the last row of the scroll region, the region scrolls
 * up by one line before the character is placed. */
#define BUS_MOSAIC_CMD_TERM_PUTCHAR       0x20  /* data[7:0]=char; write at cursor, advance */

/* TERM_SET_CURSOR — absolute cursor positioning.
 * data[7:0] = column (0-79), data[15:8] = row (0-24).
 * Equivalent to VT100 CUP (CSI row ; col H) with 0-based indices.
 * Does not affect the latched fg/bg/attr state. */
#define BUS_MOSAIC_CMD_TERM_SET_CURSOR    0x21  /* data[7:0]=col, data[15:8]=row */

/* TERM_SET_FG — set foreground color latch for subsequent PUTCHAR calls.
 * data[7:0] = xterm-256 palette index (0-255); DATA_HI is reserved.
 * Indices 0-15:   standard + bright ANSI colors (matches xterm-256 block 0)
 * Indices 16-231: 6×6×6 color cube
 * Indices 232-255: grayscale ramp
 * The card maps this index through the active UHR palette to an RGB565 value
 * at render time, so the palette can be freely customized without changing
 * the logical color assignment.
 *
 * DESIGN NOTE — why not pack fg+bg into one command:
 *   xterm-256 requires 8 bits per color. Packing fg (8) + bg (8) + attr (8)
 *   would require 24 bits of payload — 50% more than the 16-bit protocol
 *   limit. Separate latching commands keep each within the DATA_LO byte. */
#define BUS_MOSAIC_CMD_TERM_SET_FG        0x22  /* data[7:0]=fg palette index (0-255) */

/* TERM_SET_BG — set background color latch for subsequent PUTCHAR calls.
 * data[7:0] = xterm-256 palette index (0-255); DATA_HI is reserved.
 * Semantics are identical to TERM_SET_FG but for the cell background.
 * The background color is also used to fill cleared cells in CLEAR_LINE
 * and CLEAR_SCREEN operations, which is important for reverse-video status
 * bars and colored panel backgrounds common in BBS door games. */
#define BUS_MOSAIC_CMD_TERM_SET_BG        0x23  /* data[7:0]=bg palette index (0-255) */

/* TERM_SET_ATTR — set attribute flag latch for subsequent PUTCHAR calls.
 * data[7:0] = BUS_TERM_ATTR_* bitmask (see below); DATA_HI is reserved.
 * Writing 0x00 clears all attributes (normal video). Attributes combine
 * additively with bitwise OR. The latched value persists until overwritten.
 * Equivalent to VT100 SGR (CSI ... m) parameters 0-9, 21-29. */
#define BUS_MOSAIC_CMD_TERM_SET_ATTR      0x24  /* data[7:0]=flags (bold|uline|inverse|blink) */

/* TERM_SCROLL_UP — scroll lines within the scroll region upward.
 * data[7:0] = number of lines to scroll (1-25); DATA_HI is reserved.
 * Lines scrolled past the top of the region are discarded. New blank lines
 * inserted at the bottom of the region are filled with the current bg color.
 * Equivalent to VT100 SU (CSI n S).
 * Only the rows defined by TERM_SET_REGION are affected; rows outside the
 * region are undisturbed. This is the fundamental scroll for incoming text:
 * the IIgs application calls this when the cursor is at the bottom of the
 * region and a newline is received. */
#define BUS_MOSAIC_CMD_TERM_SCROLL_UP     0x25  /* data[7:0]=lines to scroll up */

/* TERM_SCROLL_DOWN — scroll lines within the scroll region downward.
 * data[7:0] = number of lines to scroll (1-25); DATA_HI is reserved.
 * Lines scrolled past the bottom of the region are discarded. New blank
 * lines inserted at the top are filled with the current bg color.
 * Equivalent to VT100 SD (CSI n T).
 * Used by editors (e.g., nano-style BBS editors) to insert lines above
 * the cursor without redrawing the entire screen. */
#define BUS_MOSAIC_CMD_TERM_SCROLL_DOWN   0x26  /* data[7:0]=lines to scroll down */

/* TERM_CLEAR_LINE — erase cells within the current row.
 * data[7:0] = erase mode matching VT100 EL (CSI n K):
 *   0: erase from cursor column to end of line (inclusive) — CSI K / CSI 0 K
 *   1: erase from start of line to cursor column (inclusive) — CSI 1 K
 *   2: erase entire current line — CSI 2 K
 * DATA_HI is reserved. Erased cells are filled with a space (0x20) in the
 * current bg color; the cursor position is not changed.
 *
 * DESIGN NOTE — mode parameter mirrors VT100 directly:
 *   The IIgs VT100 parser (Path A) extracts the CSI numeric parameter and
 *   writes it verbatim as DATA_LO — no translation needed. This one-to-one
 *   mapping eliminates an entire class of parser bugs where re-encoding
 *   VT100 semantics into a different numbering scheme introduces off-by-one
 *   errors at the boundary between modes. */
#define BUS_MOSAIC_CMD_TERM_CLEAR_LINE    0x27  /* data[7:0]=mode: 0=to-end,1=to-start,2=whole */

/* TERM_CLEAR_SCREEN — erase cells across the entire terminal grid.
 * data[7:0] = erase mode matching VT100 ED (CSI n J):
 *   0: erase from cursor to end of screen (cursor row+col through row 24 col 79)
 *   1: erase from start of screen to cursor (row 0 col 0 through cursor row+col)
 *   2: erase entire screen — CSI 2 J
 * DATA_HI is reserved. Erased cells are filled with a space in the current
 * bg color. The cursor position is not changed (strict VT100 behavior;
 * applications that want to home the cursor after mode 2 should issue a
 * separate TERM_SET_CURSOR(0,0)).
 *
 * The mode 0/1/2 encoding is the same as TERM_CLEAR_LINE to keep the IIgs
 * VT100 parser symmetric: both ED and EL parameters map identically. */
#define BUS_MOSAIC_CMD_TERM_CLEAR_SCREEN  0x28  /* data[7:0]=mode: 0=to-end,1=to-start,2=whole */

/* TERM_SET_REGION — define the scroll region (maps to VT100 DECSTBM).
 * data[7:0]  = top row (0-based, inclusive; 0 = row 1 in VT100 1-based notation)
 * data[15:8] = bottom row (0-based, inclusive; 24 = row 25 in VT100 notation)
 * Resets cursor to the home position of the scroll region (top-left).
 *
 * VT100 DECSTBM reference: CSI Pt ; Pb r  (1-based; subtract 1 for this command)
 *
 * SCROLL REGION MODEL:
 *   Only rows [top, bottom] participate in scroll operations. Rows outside
 *   this range are locked in place during scrolling. This is critical for BBS
 *   applications that maintain:
 *     • A status line at row 24 (connection stats, menu bar)
 *     • A message area at rows 0-23 that scrolls independently
 *   Default on mode entry: top=0, bottom=24 (full-screen scrolling). */
#define BUS_MOSAIC_CMD_TERM_SET_REGION    0x29  /* data[7:0]=top, data[15:8]=bottom */

/* TERM_SET_FONT — select the active character font.
 * data[7:0] = font_id:
 *   0: 8×16 CP437 (default) — fills 640×400 exactly for 80×25
 *   1: 8×10 — allows 80×40 rows (future; requires grid resize)
 *   2: 8×8  — allows 80×50 rows (future; requires grid resize)
 * DATA_HI is reserved. The font bitmaps are stored in card firmware flash.
 * CP437 (IBM Code Page 437) is used for full BBS compatibility: it includes
 * the box-drawing characters (─│┌┐└┘├┤┬┴┼), block elements (█▓▒░), and
 * smiley/suit/arrow glyphs that ANSI BBS art relies on. */
#define BUS_MOSAIC_CMD_TERM_SET_FONT      0x2A  /* data[7:0]=font_id (0=8x16,1=8x10,2=8x8) */

/* TERM_CURSOR_STYLE — configure hardware cursor appearance.
 * data[7:0] encoding:
 *   bits [1:0]: shape — 0=block, 1=underline bar, 2=I-beam (vertical bar)
 *   bit 7:      visibility — 1=cursor visible, 0=cursor hidden (DECTCEM off)
 * DATA_HI is reserved.
 * The cursor blinks at approximately 500ms half-period, implemented in the
 * card's vsync callback. Hiding the cursor (bit 7=0) suppresses blinking
 * entirely — the cell renders as normal text with no cursor overlay.
 * VT100 reference: CSI ? 25 h (DECTCEM show), CSI ? 25 l (DECTCEM hide). */
#define BUS_MOSAIC_CMD_TERM_CURSOR_STYLE  0x2B  /* data[7:0]: 0=block,1=uline,2=bar; bit7=visible */

/* TERM_SAVE_CURSOR — save cursor position and current attribute state.
 * No payload used (DATA_LO and DATA_HI may be zero).
 * Saves: cursor column, cursor row, fg color latch, bg color latch,
 *        attribute flags latch.
 * Only one save slot exists (same as VT100 DECSC). A subsequent SAVE
 * overwrites the previous saved state.
 * VT100 reference: CSI s  or  ESC 7 */
#define BUS_MOSAIC_CMD_TERM_SAVE_CURSOR   0x2C  /* Save cursor pos + fg/bg/attr */

/* TERM_RESTORE_CURSOR — restore cursor position and attribute state.
 * No payload used. Restores the state saved by TERM_SAVE_CURSOR.
 * If no save has been performed since mode entry, behavior is undefined
 * (card firmware may home the cursor and reset attributes to defaults).
 * VT100 reference: CSI u  or  ESC 8 */
#define BUS_MOSAIC_CMD_TERM_RESTORE_CURSOR 0x2D /* Restore saved state */

/* TERM_WRITE_RAW — feed a raw byte into the card-side VT100/ANSI parser.
 * data[7:0] = raw byte (0x00-0xFF); DATA_HI is reserved (must be zero).
 *
 * This command is the entry point for Path B operation (see block comment
 * above). Unlike commands 0x20-0x2D, which represent already-decoded
 * terminal operations, TERM_WRITE_RAW presents an undecoded byte to the
 * card's internal VT100 state machine. The card handles all parsing:
 *
 *   • Printable characters (0x20-0x7E, 0xA0-0xFF CP437): placed in grid
 *     at cursor position with current latched attributes; cursor advances
 *   • Control characters: BS, HT, LF, VT, FF, CR, BEL, DEL processed
 *   • ESC (0x1B): begins escape sequence; card buffers subsequent bytes
 *     until sequence is complete (CSI terminator 0x40-0x7E, OSC ST/BEL)
 *   • CSI sequences: CUU/CUD/CUF/CUB (cursor motion), CUP (position),
 *     ED (erase display), EL (erase line), SU/SD (scroll), SGR (attributes
 *     and color, including 256-color and truecolor coercion), DECTCEM,
 *     DECSTBM, DECSC/DECRC
 *   • OSC sequences: title set (ignored), color palette queries (ignored)
 *
 * TYPICAL USAGE (65C816, ProDOS 8 or bare metal):
 *
 *   ; Forward one byte from SCC UART receive register to card
 *   LDA  #BUS_MOSAIC_CMD_TERM_WRITE_RAW
 *   STA  CARD_CMD_REG           ; slot base + REG_CMD
 *   LDA  incoming_byte          ; byte from serial / Uthernet
 *   STA  CARD_DATA_LO_REG       ; slot base + REG_DATA_LO
 *   STZ  CARD_DATA_HI_REG       ; slot base + REG_DATA_HI — triggers execution
 *
 * Three stores per byte. No state machine, no font rendering, no coordinate
 * arithmetic on the IIgs side. The entire 65C816 bandwidth is available
 * for the I/O path.
 *
 * CONTRAST WITH STRUCTURED COMMANDS:
 *   TERM_WRITE_RAW and the structured commands (0x20-0x2D) update the same
 *   internal terminal state on the card. They are not separate rendering
 *   paths — the card has one character grid and one cursor. Applications may
 *   mix both: e.g., use TERM_WRITE_RAW for the incoming data stream but call
 *   TERM_SAVE_CURSOR / TERM_RESTORE_CURSOR directly to implement a local
 *   overlay without disturbing the parser's cursor tracking. */
#define BUS_MOSAIC_CMD_TERM_WRITE_RAW     0x2E /* data[7:0]=byte; feed through card-side VT100 parser */

/* ── Extended TERM commands (0x2F-0x33) ────────────────────────────────
 *
 * These commands support ANSI character editing operations that require
 * the card to shift or blank cells in the character grid. They cannot
 * be decomposed into simpler PUTCHAR sequences without N individual
 * writes, so dedicated commands avoid O(N) slot I/O round-trips.
 *
 * ICH/DCH/ECH are emitted by the VT100 parser for CSI @, CSI P, and
 * CSI X respectively. ALT_SCREEN implements the xterm alternate screen
 * buffer (CSI ?1049 h/l), used by full-screen TUI applications (vim,
 * htop, mc) to preserve the main scrollback on entry and restore on exit.
 */
#define BUS_MOSAIC_CMD_TERM_INSERT_CHARS  0x2F  /* data[7:0]=count; insert blanks at cursor, shift right */
#define BUS_MOSAIC_CMD_TERM_DELETE_CHARS  0x30  /* data[7:0]=count; delete at cursor, shift left */
#define BUS_MOSAIC_CMD_TERM_ERASE_CHARS   0x31  /* data[7:0]=count; blank N chars at cursor (no shift) */
#define BUS_MOSAIC_CMD_TERM_ALT_SCREEN    0x32  /* data[0]=1: enter alt screen; data[0]=0: leave */

/* ── Compositor commands (0x33-0x3F) ────────────────────────────────
 *
 * Protocol-level control. SET_PROTOCOL switches the active parser for
 * the default context. COMP_WRITE_RAW feeds a byte through the active
 * protocol (whatever was set by SET_PROTOCOL). This enables testing
 * any protocol from the host without needing the IIgs VT100 parser. */
#define BUS_MOSAIC_CMD_SET_PROTOCOL       0x33  /* data[7:0]=protocol_id (0x00-0x0C) */
#define BUS_MOSAIC_CMD_COMP_WRITE_RAW     0x34  /* data[7:0]=byte; feed through active protocol */

/* ── Terminal attribute flags (BUS_TERM_ATTR_*) ────────────────────────────
 * Used as a bitmask in TERM_SET_ATTR. Each flag corresponds to a distinct
 * visual rendering modification applied when drawing a character cell.
 * Multiple flags may be combined with bitwise OR.
 *
 * FLAG LAYOUT AND RATIONALE:
 *
 *   BOLD       (0x01): render the glyph with increased stroke weight or,
 *                      on hardware without a bold font variant, by rendering
 *                      the glyph twice with a 1-pixel horizontal offset
 *                      (double-strike). Also maps to SGR 1 in VT100.
 *                      In xterm-256 mode, bold on colors 0-7 selects the
 *                      corresponding bright color (8-15) unless the fg has
 *                      been set explicitly to an index ≥ 8.
 *
 *   UNDERLINE  (0x02): draw a horizontal line across the bottom pixel row
 *                      of the cell in the fg color. Maps to SGR 4.
 *                      Used extensively in BBS navigation menus to indicate
 *                      hotkeys (e.g., "[R]ead messages").
 *
 *   INVERSE    (0x04): swap fg and bg colors for this cell. Maps to SGR 7
 *                      (reverse video). The most frequently used attribute
 *                      in BBS ANSI art for highlighting and menu selection
 *                      bars. Implemented as a swap at render time — the
 *                      stored fg/bg values in the cell are not modified.
 *
 *   BLINK      (0x08): cause the character to alternate visibility at
 *                      approximately 1-2 Hz (slow blink, per VT100 spec).
 *                      Maps to SGR 5. Many BBS screens use blink for
 *                      attention indicators and animated art. Implemented
 *                      in the card's vsync callback — the glyph renders
 *                      normally on even blink periods and is replaced with
 *                      the bg color on odd periods.
 *
 *   STRIKE     (0x10): draw a horizontal line across the vertical midpoint
 *                      of the cell (strikethrough). Maps to SGR 9.
 *                      Less common in BBS art but present in some VT220
 *                      and xterm-256 sequences used by modern BBS software.
 *
 *   DIM        (0x20): render the glyph at reduced intensity by blending
 *                      the fg color toward the bg color (approximately 50%).
 *                      Maps to SGR 2 (faint). Used for de-emphasized text
 *                      and "ghosted" menu items.
 *
 * ALL SIX FLAGS fit in the low 6 bits of a single byte, leaving bits 6-7
 * available for future attributes (e.g., ITALIC=0x40, OVERLINE=0x80)
 * without protocol changes. The DATA_LO byte capacity (8 bits) matches
 * exactly the 8-attribute space defined here. */
#define BUS_TERM_ATTR_BOLD       0x01
#define BUS_TERM_ATTR_UNDERLINE  0x02
#define BUS_TERM_ATTR_INVERSE    0x04
#define BUS_TERM_ATTR_BLINK      0x08
#define BUS_TERM_ATTR_STRIKE     0x10
#define BUS_TERM_ATTR_DIM        0x20
#define BUS_TERM_ATTR_ITALIC     0x40
#define BUS_TERM_ATTR_DBL_HEIGHT 0x80  /* Double-height: top half renders rows 0-7, bottom renders 8-15 */

/* Mosaic video modes (written via SET_MODE command) */
#define BUS_MOSAIC_MODE_PASSTHROUGH  0   /* IIgs native rendering */
#define BUS_MOSAIC_MODE_UHR16        1   /* 640x400, 16 colors */
#define BUS_MOSAIC_MODE_UHR256       2   /* 640x400, 256 colors */
#define BUS_MOSAIC_MODE_UHR6400      3   /* 640x400, 16 × 400 per-line */
#define BUS_MOSAIC_MODE_UHR102K      4   /* 640x400, 256 × 400 per-line */
#define BUS_MOSAIC_MODE_TERM         5   /* 80×25 text terminal (CP437 8×16, xterm-256color) */

/* Mosaic identity byte — returned by REG_IDENT read ($A2 = "A2"GSPU) */
#define BUS_MOSAIC_IDENT             0xA2

/* Variable-length frames: size determined by FLAGS + FLAGS2 bytes.
 * Checksum is always the last 2 bytes of the frame.
 * Max frame size accommodates header + text + SHR full + scanline + mosaic + csum. */
#define BUS_FRAME_MAX    37000

/* Compute total frame size from flags, flags2, dirty count, and mosaic command count. */
static inline uint32_t bus_frame_size_ex2(uint8_t flags, uint8_t flags2,
                                           uint8_t dirty_count, uint16_t mosaic_count) {
    uint32_t sz = BUS_HEADER_SIZE + BUS_TEXT1_SIZE + BUS_TEXT1_SIZE;
    if (flags & BUS_FLAG_SHR) {
        sz += BUS_SHR_SIZE;
    } else if (flags & BUS_FLAG_SHR_DIRTY) {
        sz += BUS_SHR_DIRTY_HEADER + (uint32_t)dirty_count * BUS_SHR_LINE_BYTES;
    } else {
        if (flags & BUS_FLAG_HGR)     sz += BUS_HGR_SIZE;
        if (flags & BUS_FLAG_HGR_AUX) sz += BUS_HGR_SIZE;
    }
    if (flags & BUS_FLAG_SCANLINE)    sz += BUS_SCANLINE_SIZE;
    if (flags2 & BUS_FLAG2_MOSAIC)    sz += BUS_MOSAIC_COUNT_SIZE + (uint32_t)mosaic_count * BUS_MOSAIC_CMD_SIZE;
    return sz + 2;  /* +2 for checksum */
}

/* Backward-compatible wrappers */
static inline uint32_t bus_frame_size_ex(uint8_t flags, uint8_t dirty_count) {
    return bus_frame_size_ex2(flags, 0, dirty_count, 0);
}
static inline uint32_t bus_frame_size(uint8_t flags) {
    return bus_frame_size_ex2(flags, 0, 0, 0);
}

/* Mode byte (frame[9]) — matches a2gspu_emu_state::mode_flags encoding. */
#define BUS_MODE_80STORE   0x01
#define BUS_MODE_80COL     0x02
#define BUS_MODE_ALTCHAR   0x04
#define BUS_MODE_TEXT      0x08
#define BUS_MODE_MIXED     0x10
#define BUS_MODE_PAGE2     0x20
#define BUS_MODE_HIRES     0x40
#define BUS_MODE_DGR       0x80  /* double-res / AN3 */

#endif /* BUS_PROTOCOL_H */
