/*
 * vt100.h — Portable ANSI/VT100 escape sequence parser
 *
 * Parses a byte stream containing ANSI/VT100 escape sequences and emits
 * high-level terminal commands via a callback interface. The parser is
 * CPU-agnostic: it compiles for x86 (GSSquared emulator testing) and
 * 65816 (IIgs CDA target) without modification.
 *
 * Usage:
 *   vt100_state_t state;
 *   vt100_init(&state, my_callback, my_context);
 *   // For each byte received from TCP:
 *   vt100_process(&state, byte);
 *
 * The callback receives terminal commands (PUTCHAR, SET_CURSOR, etc.)
 * which the caller translates to Mosaic slot I/O writes on the IIgs,
 * or directly to a2gspu_emu_term_execute() in the emulator.
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Maximum CSI parameters (ESC[p1;p2;...pN X) */
#define VT100_MAX_PARAMS 16

/* Terminal commands emitted by the parser via callback */
typedef enum {
    VT_CMD_PUTCHAR,        /* ch = character to display */
    VT_CMD_SET_CURSOR,     /* p1 = col, p2 = row */
    VT_CMD_SET_FG,         /* p1 = foreground palette index (0-255) */
    VT_CMD_SET_BG,         /* p1 = background palette index (0-255) */
    VT_CMD_SET_ATTR,       /* p1 = attribute flags */
    VT_CMD_SCROLL_UP,      /* p1 = lines */
    VT_CMD_SCROLL_DOWN,    /* p1 = lines */
    VT_CMD_CLEAR_LINE,     /* p1 = mode (0=to-end, 1=to-start, 2=whole) */
    VT_CMD_CLEAR_SCREEN,   /* p1 = mode (0=to-end, 1=to-start, 2=whole) */
    VT_CMD_SET_REGION,     /* p1 = top, p2 = bottom */
    VT_CMD_SAVE_CURSOR,
    VT_CMD_RESTORE_CURSOR,
    VT_CMD_BELL,           /* audible bell */
    VT_CMD_TAB,            /* advance to next tab stop */
    VT_CMD_NEWLINE,        /* line feed (may trigger scroll) */
    VT_CMD_CR,             /* carriage return */
    VT_CMD_BACKSPACE,      /* move cursor left 1 */
    VT_CMD_INSERT_CHARS,   /* p1 = count; insert blanks, shift chars right */
    VT_CMD_DELETE_CHARS,   /* p1 = count; delete chars, shift left */
    VT_CMD_ERASE_CHARS,    /* p1 = count; blank N chars at cursor (no shift) */
    VT_CMD_CURSOR_VISIBLE, /* p1 = 1=visible, 0=hidden (DECTCEM) */
    VT_CMD_ALT_SCREEN,     /* p1 = 1=enter alt screen, 0=leave alt screen */
    VT_CMD_RESPONSE,       /* Response data in s->response[0..response_len-1] */
    VT_CMD_MOUSE_MODE,    /* p1 = mode (0=off, 1=normal, 2=button, 3=any), p2 = sgr_format */
} vt100_cmd_t;

/* Callback: called for each terminal command the parser emits.
 * context: user-provided pointer (e.g., slot I/O state, emu state)
 * cmd: which command
 * p1, p2: command-specific parameters */
typedef void (*vt100_emit_fn)(void *context, vt100_cmd_t cmd, int p1, int p2);

/* Parser state */
typedef struct {
    /* State machine */
    uint8_t state;

    /* CSI parameter accumulation */
    int params[VT100_MAX_PARAMS];
    int num_params;
    int current_param;
    uint8_t csi_prefix;     /* '?' or '>' prefix after ESC[ */

    /* Shadow terminal state (for relative cursor movement) */
    int cursor_col, cursor_row;
    int num_cols, num_rows;
    int scroll_top, scroll_bot;

    /* Current SGR attributes (tracked to emit only changes) */
    uint8_t fg, bg, attr;

    /* Auto-wrap mode (DECAWM, CSI ?7 h/l). Default: on (1).
     * When on, printing past column 79 wraps to next line.
     * When off, cursor stays at column 79 and overwrites. */
    uint8_t autowrap;

    /* Last printed character, for CSI b (REP - repeat last char) */
    uint8_t last_char;

    /* Charset state for SI/SO (0x0E/0x0F) and ESC ( / ESC ) */
    uint8_t charset_g0;     /* 0=ASCII, 1=DEC special graphics */
    uint8_t charset_g1;     /* 0=ASCII, 1=DEC special graphics */
    uint8_t active_charset; /* 0=G0, 1=G1 (toggled by SI/SO) */

    /* DSR/DA response buffer. Filled by parser, read by callback. */
    uint8_t response[16];
    uint8_t response_len;

    /* Callback */
    vt100_emit_fn emit;
    void *emit_context;

    /* Mouse tracking mode (xterm) */
    uint8_t mouse_mode;     /* 0=off, 1=normal(1000), 2=button(1002), 3=any(1003) */
    bool    mouse_sgr;      /* true=SGR format(1006), false=X10 format */

    /* DCS passthrough: when non-NULL, bytes in DCS_PASS state are fed
     * to this callback instead of the normal parser. Set by the host
     * when a DCS final byte ('q' for Sixel, 'p' for ReGIS) is detected.
     * Returns true when ST is received (DCS complete). */
    bool (*dcs_handler)(void *dcs_ctx, uint8_t byte);
    void *dcs_context;
    uint8_t dcs_final;  /* Final byte that triggered DCS ('q', 'p', etc.) */
} vt100_state_t;

/* Initialize parser state */
void vt100_init(vt100_state_t *s, vt100_emit_fn emit, void *context);

/* Process one byte through the parser */
void vt100_process(vt100_state_t *s, uint8_t ch);

/* Reset parser to initial state (e.g., on new connection) */
void vt100_reset(vt100_state_t *s);
