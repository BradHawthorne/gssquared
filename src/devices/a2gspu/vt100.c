/*
 * vt100.c — Portable ANSI/VT100 escape sequence parser
 *
 * State machine parses incoming bytes and emits terminal commands via
 * callback. Modeled on VersaTerm's terminal.c but simplified for the
 * A2GSPU command set.
 *
 * States:
 *   NORMAL    → printable chars, C0 control codes (CR, LF, BS, TAB, BEL, ESC)
 *   ESC       → received ESC (0x1B), waiting for next character
 *   CSI       → received ESC[, accumulating numeric parameters
 *   CSI_PARAM → reading digits/semicolons after ESC[ (or ESC[? prefix)
 *   CHARSET   → received ESC( or ESC), reading charset designator
 *   OSC       → received ESC], reading operating system command (ignored)
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#include "vt100.h"
#include <string.h>

/* Parser states */
#define ST_NORMAL    0
#define ST_ESC       1
#define ST_CSI       2
#define ST_CHARSET   3
#define ST_OSC       4
#define ST_DCS_ENTRY 5   /* DCS parameter collection (ESC P ...) */
#define ST_DCS_PASS  6   /* DCS passthrough to sub-protocol (Sixel/ReGIS) */

/* Attribute flags (must match BUS_TERM_ATTR_*) */
#define ATTR_BOLD      0x01
#define ATTR_UNDERLINE 0x02
#define ATTR_INVERSE   0x04
#define ATTR_BLINK     0x08
#define ATTR_DIM       0x20
#define ATTR_STRIKE    0x10
#define ATTR_ITALIC    0x40

/* ── Helpers ─────────────────────────────────────────────────────────*/

static void emit(vt100_state_t *s, vt100_cmd_t cmd, int p1, int p2) {
    s->emit(s->emit_context, cmd, p1, p2);
}

static int param(vt100_state_t *s, int idx, int def) {
    if (idx >= s->num_params) return def;
    return s->params[idx] ? s->params[idx] : def;
}

/* Map 24-bit RGB to nearest xterm-256color index.
 * Used by SGR 38;2;r;g;b and 48;2;r;g;b (ISO 8613-6 truecolor).
 * Checks both the 6x6x6 cube (indices 16-231) and the 24-step
 * grayscale ramp (232-255), returning whichever has lower squared
 * Euclidean distance. Cost: ~50 ARM cycles, called only for
 * truecolor sequences (rare in typical BBS traffic). */
static uint8_t rgb_to_xterm256(uint8_t r, uint8_t g, uint8_t b) {
    static const uint8_t cube[] = {0, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
    int ri = 0, gi = 0, bi = 0;
    int rd = 256, gd = 256, bd = 256;
    for (int i = 0; i < 6; i++) {
        int d;
        d = (int)r - cube[i]; if (d < 0) d = -d;
        if (d < rd) { rd = d; ri = i; }
        d = (int)g - cube[i]; if (d < 0) d = -d;
        if (d < gd) { gd = d; gi = i; }
        d = (int)b - cube[i]; if (d < 0) d = -d;
        if (d < bd) { bd = d; bi = i; }
    }
    int cube_err = rd * rd + gd * gd + bd * bd;

    /* Check grayscale ramp (232-255): values 8, 18, 28, ..., 238 */
    int gray = ((int)r + g + b) / 3;
    int gi_idx = (gray - 8 + 5) / 10;  /* round to nearest */
    if (gi_idx < 0) gi_idx = 0;
    if (gi_idx > 23) gi_idx = 23;
    int gray_val = 8 + 10 * gi_idx;
    int gr = (int)r - gray_val, gg = (int)g - gray_val, gb = (int)b - gray_val;
    int gray_err = gr * gr + gg * gg + gb * gb;

    return (gray_err < cube_err) ? (uint8_t)(232 + gi_idx)
                                 : (uint8_t)(16 + 36 * ri + 6 * gi + bi);
}

static void clamp_cursor(vt100_state_t *s) {
    if (s->cursor_col < 0) s->cursor_col = 0;
    if (s->cursor_col >= s->num_cols) s->cursor_col = s->num_cols - 1;
    if (s->cursor_row < 0) s->cursor_row = 0;
    if (s->cursor_row >= s->num_rows) s->cursor_row = s->num_rows - 1;
}

static void move_cursor(vt100_state_t *s) {
    clamp_cursor(s);
    emit(s, VT_CMD_SET_CURSOR, s->cursor_col, s->cursor_row);
}

/* ── SGR (Select Graphic Rendition) ──────────────────────────────────*/

static void process_sgr(vt100_state_t *s) {
    /* SGR with no params = reset */
    if (s->num_params == 0) {
        s->fg = 7; s->bg = 0; s->attr = 0;
        emit(s, VT_CMD_SET_FG, 7, 0);
        emit(s, VT_CMD_SET_BG, 0, 0);
        emit(s, VT_CMD_SET_ATTR, 0, 0);
        return;
    }

    for (int i = 0; i < s->num_params; i++) {
        int p = s->params[i];

        if (p == 0) {
            /* Reset */
            s->fg = 7; s->bg = 0; s->attr = 0;
        } else if (p == 1) {
            s->attr |= ATTR_BOLD;
        } else if (p == 2) {
            s->attr |= ATTR_DIM;
        } else if (p == 4) {
            s->attr |= ATTR_UNDERLINE;
        } else if (p == 5 || p == 6) {
            s->attr |= ATTR_BLINK;
        } else if (p == 3) {
            s->attr |= ATTR_ITALIC;
        } else if (p == 7) {
            s->attr |= ATTR_INVERSE;
        } else if (p == 9) {
            s->attr |= ATTR_STRIKE;
        } else if (p == 22) {
            s->attr &= ~(ATTR_BOLD | ATTR_DIM);
        } else if (p == 24) {
            s->attr &= ~ATTR_UNDERLINE;
        } else if (p == 25) {
            s->attr &= ~ATTR_BLINK;
        } else if (p == 23) {
            s->attr &= ~ATTR_ITALIC;
        } else if (p == 27) {
            s->attr &= ~ATTR_INVERSE;
        } else if (p == 29) {
            s->attr &= ~ATTR_STRIKE;
        } else if (p >= 30 && p <= 37) {
            /* Standard foreground (0-7) */
            s->fg = p - 30;
            if (s->attr & ATTR_BOLD) s->fg += 8;  /* bold = bright */
        } else if (p == 38 && i + 2 < s->num_params && s->params[i+1] == 5) {
            /* Extended foreground: ESC[38;5;Nm */
            s->fg = s->params[i+2];
            i += 2;
        } else if (p == 38 && i + 4 < s->num_params && s->params[i+1] == 2) {
            /* 24-bit truecolor foreground: ESC[38;2;R;G;Bm */
            s->fg = rgb_to_xterm256(s->params[i+2], s->params[i+3], s->params[i+4]);
            i += 4;
        } else if (p == 39) {
            /* Default foreground */
            s->fg = 7;
        } else if (p >= 40 && p <= 47) {
            /* Standard background (0-7) */
            s->bg = p - 40;
        } else if (p == 48 && i + 2 < s->num_params && s->params[i+1] == 5) {
            /* Extended background: ESC[48;5;Nm */
            s->bg = s->params[i+2];
            i += 2;
        } else if (p == 48 && i + 4 < s->num_params && s->params[i+1] == 2) {
            /* 24-bit truecolor background: ESC[48;2;R;G;Bm */
            s->bg = rgb_to_xterm256(s->params[i+2], s->params[i+3], s->params[i+4]);
            i += 4;
        } else if (p == 49) {
            /* Default background */
            s->bg = 0;
        } else if (p >= 90 && p <= 97) {
            /* Bright foreground (8-15) */
            s->fg = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            /* Bright background (8-15) */
            s->bg = p - 100 + 8;
        }
    }

    emit(s, VT_CMD_SET_FG, s->fg, 0);
    emit(s, VT_CMD_SET_BG, s->bg, 0);
    emit(s, VT_CMD_SET_ATTR, s->attr, 0);
}

/* ── CSI command dispatch ────────────────────────────────────────────*/

static void process_csi(vt100_state_t *s, uint8_t ch) {
    int p1, p2;

    /* Finalize the last parameter being accumulated */
    if (s->num_params < VT100_MAX_PARAMS) {
        s->params[s->num_params] = s->current_param;
        s->num_params++;
    }

    switch (ch) {
        case 'A': /* CUU — Cursor Up */
            s->cursor_row -= param(s, 0, 1);
            move_cursor(s);
            break;

        case 'B': /* CUD — Cursor Down */
            s->cursor_row += param(s, 0, 1);
            move_cursor(s);
            break;

        case 'C': /* CUF — Cursor Forward */
            s->cursor_col += param(s, 0, 1);
            move_cursor(s);
            break;

        case 'D': /* CUB — Cursor Backward */
            s->cursor_col -= param(s, 0, 1);
            move_cursor(s);
            break;

        case 'E': /* CNL — Cursor Next Line */
            s->cursor_row += param(s, 0, 1);
            s->cursor_col = 0;
            move_cursor(s);
            break;

        case 'F': /* CPL — Cursor Previous Line */
            s->cursor_row -= param(s, 0, 1);
            s->cursor_col = 0;
            move_cursor(s);
            break;

        case 'G': /* CHA — Cursor Horizontal Absolute */
            s->cursor_col = param(s, 0, 1) - 1;
            move_cursor(s);
            break;

        case 'H': /* CUP — Cursor Position */
        case 'f': /* HVP — same as CUP */
            s->cursor_row = param(s, 0, 1) - 1;
            s->cursor_col = param(s, 1, 1) - 1;
            move_cursor(s);
            break;

        case 'J': /* ED — Erase in Display */
            emit(s, VT_CMD_CLEAR_SCREEN, param(s, 0, 0), 0);
            break;

        case 'K': /* EL — Erase in Line */
            emit(s, VT_CMD_CLEAR_LINE, param(s, 0, 0), 0);
            break;

        case 'L': /* IL — Insert Lines */
            emit(s, VT_CMD_SCROLL_DOWN, param(s, 0, 1), 0);
            break;

        case 'M': /* DL — Delete Lines */
            emit(s, VT_CMD_SCROLL_UP, param(s, 0, 1), 0);
            break;

        case 'S': /* SU — Scroll Up */
            emit(s, VT_CMD_SCROLL_UP, param(s, 0, 1), 0);
            break;

        case 'T': /* SD — Scroll Down */
            emit(s, VT_CMD_SCROLL_DOWN, param(s, 0, 1), 0);
            break;

        case 'd': /* VPA — Vertical Position Absolute */
            s->cursor_row = param(s, 0, 1) - 1;
            move_cursor(s);
            break;

        case 'm': /* SGR — Select Graphic Rendition */
            process_sgr(s);
            break;

        case 'r': /* DECSTBM — Set Scrolling Region */
            p1 = param(s, 0, 1) - 1;
            p2 = param(s, 1, s->num_rows) - 1;
            s->scroll_top = p1;
            s->scroll_bot = p2;
            emit(s, VT_CMD_SET_REGION, p1, p2);
            /* Home cursor after setting region */
            s->cursor_col = 0;
            s->cursor_row = 0;
            move_cursor(s);
            break;

        case 's': /* SCP — Save Cursor Position */
            emit(s, VT_CMD_SAVE_CURSOR, 0, 0);
            break;

        case 'u': /* RCP — Restore Cursor Position */
            emit(s, VT_CMD_RESTORE_CURSOR, 0, 0);
            break;

        case '@': /* ICH — Insert Characters */
            emit(s, VT_CMD_INSERT_CHARS, param(s, 0, 1), 0);
            break;

        case 'P': /* DCH — Delete Characters */
            emit(s, VT_CMD_DELETE_CHARS, param(s, 0, 1), 0);
            break;

        case 'X': /* ECH — Erase Characters */
            emit(s, VT_CMD_ERASE_CHARS, param(s, 0, 1), 0);
            break;

        case 'b': /* REP — Repeat preceding graphic character */
            if (s->last_char) {
                int count = param(s, 0, 1);
                for (int i = 0; i < count; i++)
                    emit(s, VT_CMD_PUTCHAR, s->last_char, 0);
                s->cursor_col += count;
                if (s->cursor_col >= s->num_cols)
                    s->cursor_col = s->num_cols - 1;
            }
            break;

        case 'I': /* CHT — Cursor Forward Tabulation */
        {
            int count = param(s, 0, 1);
            for (int i = 0; i < count; i++) {
                s->cursor_col = (s->cursor_col + 8) & ~7;
                if (s->cursor_col >= s->num_cols)
                    s->cursor_col = s->num_cols - 1;
            }
            move_cursor(s);
            break;
        }

        case 'Z': /* CBT — Cursor Backward Tabulation */
        {
            int count = param(s, 0, 1);
            for (int i = 0; i < count; i++) {
                if (s->cursor_col > 0) {
                    s->cursor_col = ((s->cursor_col - 1) & ~7);
                }
            }
            move_cursor(s);
            break;
        }

        case 'g': /* TBC — Tabulation Clear (no-op with default 8-col tabs) */
            break;

        case 'n': /* DSR — Device Status Report */
            if (param(s, 0, 0) == 6) {
                /* CPR — Cursor Position Report: ESC[row;colR */
                int r = s->cursor_row + 1, c = s->cursor_col + 1;
                int len = 0;
                s->response[len++] = 0x1B;
                s->response[len++] = '[';
                if (r >= 100) s->response[len++] = '0' + (r / 100);
                if (r >= 10)  s->response[len++] = '0' + ((r / 10) % 10);
                s->response[len++] = '0' + (r % 10);
                s->response[len++] = ';';
                if (c >= 100) s->response[len++] = '0' + (c / 100);
                if (c >= 10)  s->response[len++] = '0' + ((c / 10) % 10);
                s->response[len++] = '0' + (c % 10);
                s->response[len++] = 'R';
                s->response_len = len;
                emit(s, VT_CMD_RESPONSE, 0, 0);
            } else if (param(s, 0, 0) == 5) {
                /* Status Report: respond "OK" = ESC[0n */
                s->response[0] = 0x1B;
                s->response[1] = '[';
                s->response[2] = '0';
                s->response[3] = 'n';
                s->response_len = 4;
                emit(s, VT_CMD_RESPONSE, 0, 0);
            }
            break;

        case 'c': /* DA — Device Attributes */
            if (s->csi_prefix == 0 && param(s, 0, 0) == 0) {
                /* Primary DA: respond as VT220 with ANSI color */
                /* ESC[?62;1;2;6;7;8;9c */
                const char *da = "\x1b[?62;1;2;6;7;8;9c";
                int len = 0;
                while (da[len] && len < 15) {
                    s->response[len] = da[len];
                    len++;
                }
                s->response_len = len;
                emit(s, VT_CMD_RESPONSE, 0, 0);
            }
            break;

        case 'h': /* SM — Set Mode */
        case 'l': /* RM — Reset Mode */
            if (s->csi_prefix == '?') {
                int mode = param(s, 0, 0);
                int set = (ch == 'h');
                switch (mode) {
                    case 7:  /* DECAWM — Auto-Wrap Mode */
                        s->autowrap = set;
                        break;
                    case 25: /* DECTCEM — Text Cursor Enable Mode */
                        emit(s, VT_CMD_CURSOR_VISIBLE, set, 0);
                        break;
                    case 1049: /* Alternate Screen Buffer (xterm) */
                        emit(s, VT_CMD_ALT_SCREEN, set, 0);
                        break;
                    case 1000: /* X11 mouse tracking (normal) */
                        s->mouse_mode = set ? 1 : 0;
                        emit(s, VT_CMD_MOUSE_MODE, s->mouse_mode, s->mouse_sgr);
                        break;
                    case 1002: /* Button-event mouse tracking */
                        s->mouse_mode = set ? 2 : 0;
                        emit(s, VT_CMD_MOUSE_MODE, s->mouse_mode, s->mouse_sgr);
                        break;
                    case 1003: /* Any-event mouse tracking */
                        s->mouse_mode = set ? 3 : 0;
                        emit(s, VT_CMD_MOUSE_MODE, s->mouse_mode, s->mouse_sgr);
                        break;
                    case 1006: /* SGR extended mouse format */
                        s->mouse_sgr = set;
                        emit(s, VT_CMD_MOUSE_MODE, s->mouse_mode, s->mouse_sgr);
                        break;
                }
            }
            break;

        default:
            /* Unknown CSI sequence — ignore */
            break;
    }
}

/* ── Main byte processor ─────────────────────────────────────────────*/

void vt100_process(vt100_state_t *s, uint8_t ch) {
    switch (s->state) {

    case ST_NORMAL:
        if (ch >= 0x20 && ch != 0x7F) {
            /* Printable character */
            s->last_char = ch;
            emit(s, VT_CMD_PUTCHAR, ch, 0);
            s->cursor_col++;
            if (s->cursor_col >= s->num_cols) {
                if (s->autowrap) {
                    s->cursor_col = 0;
                    s->cursor_row++;
                    if (s->cursor_row > s->scroll_bot) {
                        s->cursor_row = s->scroll_bot;
                    }
                } else {
                    s->cursor_col = s->num_cols - 1;
                }
            }
        } else switch (ch) {
            case 0x1B: /* ESC */
                s->state = ST_ESC;
                break;
            case 0x0D: /* CR */
                s->cursor_col = 0;
                emit(s, VT_CMD_CR, 0, 0);
                break;
            case 0x0A: /* LF */
            case 0x0B: /* VT */
            case 0x0C: /* FF */
                s->cursor_row++;
                if (s->cursor_row > s->scroll_bot) {
                    s->cursor_row = s->scroll_bot;
                    emit(s, VT_CMD_SCROLL_UP, 1, 0);
                } else {
                    emit(s, VT_CMD_NEWLINE, 0, 0);
                }
                break;
            case 0x08: /* BS */
                if (s->cursor_col > 0) s->cursor_col--;
                emit(s, VT_CMD_BACKSPACE, 0, 0);
                break;
            case 0x09: /* TAB */
                emit(s, VT_CMD_TAB, 0, 0);
                s->cursor_col = (s->cursor_col + 8) & ~7;
                if (s->cursor_col >= s->num_cols) s->cursor_col = s->num_cols - 1;
                break;
            case 0x07: /* BEL */
                emit(s, VT_CMD_BELL, 0, 0);
                break;
            case 0x0E: /* SO — Shift Out: activate G1 charset */
                s->active_charset = 1;
                break;
            case 0x0F: /* SI — Shift In: activate G0 charset (default) */
                s->active_charset = 0;
                break;
            default:
                /* Ignore other control characters */
                break;
        }
        break;

    case ST_ESC:
        switch (ch) {
            case '[': /* CSI introducer */
                s->state = ST_CSI;
                s->num_params = 0;
                s->current_param = 0;
                s->csi_prefix = 0;
                memset(s->params, 0, sizeof(s->params));
                break;
            case '(': /* G0 charset select */
            case ')': /* G1 charset select */
                s->state = ST_CHARSET;
                break;
            case ']': /* OSC */
                s->state = ST_OSC;
                break;
            case 'P': /* DCS — Device Control String (Sixel, ReGIS) */
                s->state = ST_DCS_ENTRY;
                s->num_params = 0;
                s->current_param = 0;
                memset(s->params, 0, sizeof(s->params));
                break;
            case '7': /* DECSC — Save Cursor */
                emit(s, VT_CMD_SAVE_CURSOR, 0, 0);
                s->state = ST_NORMAL;
                break;
            case '8': /* DECRC — Restore Cursor */
                emit(s, VT_CMD_RESTORE_CURSOR, 0, 0);
                s->state = ST_NORMAL;
                break;
            case 'D': /* IND — Index (move cursor down, scroll if at bottom) */
                s->cursor_row++;
                if (s->cursor_row > s->scroll_bot) {
                    s->cursor_row = s->scroll_bot;
                    emit(s, VT_CMD_SCROLL_UP, 1, 0);
                }
                move_cursor(s);
                s->state = ST_NORMAL;
                break;
            case 'M': /* RI — Reverse Index (move cursor up, scroll if at top) */
                s->cursor_row--;
                if (s->cursor_row < s->scroll_top) {
                    s->cursor_row = s->scroll_top;
                    emit(s, VT_CMD_SCROLL_DOWN, 1, 0);
                }
                move_cursor(s);
                s->state = ST_NORMAL;
                break;
            case 'E': /* NEL — Next Line */
                s->cursor_col = 0;
                s->cursor_row++;
                if (s->cursor_row > s->scroll_bot) {
                    s->cursor_row = s->scroll_bot;
                    emit(s, VT_CMD_SCROLL_UP, 1, 0);
                }
                move_cursor(s);
                s->state = ST_NORMAL;
                break;
            case 'c': /* RIS — Full Reset */
                vt100_reset(s);
                emit(s, VT_CMD_CLEAR_SCREEN, 2, 0);
                emit(s, VT_CMD_SET_CURSOR, 0, 0);
                s->state = ST_NORMAL;
                break;
            default:
                /* Unknown ESC sequence — back to normal */
                s->state = ST_NORMAL;
                break;
        }
        break;

    case ST_CSI:
        if (ch == '?' || ch == '>' || ch == '!') {
            /* Private mode prefix */
            s->csi_prefix = ch;
            break;  /* Stay in ST_CSI, wait for params or final char */
        }
        /* Fall through to parameter parsing */
        s->state = ST_CSI;  /* Redundant but clear */
        /* FALLTHROUGH */

        if (ch >= '0' && ch <= '9') {
            s->current_param = s->current_param * 10 + (ch - '0');
        } else if (ch == ';') {
            if (s->num_params < VT100_MAX_PARAMS) {
                s->params[s->num_params] = s->current_param;
                s->num_params++;
            }
            s->current_param = 0;
        } else if (ch >= 0x40 && ch <= 0x7E) {
            /* Final byte — dispatch command */
            process_csi(s, ch);
            s->state = ST_NORMAL;
        } else {
            /* Intermediate byte or unknown — ignore but stay in CSI */
        }
        break;

    case ST_CHARSET:
        /* G0/G1 charset designation. 'B'=ASCII, '0'=DEC Special Graphics.
         * The charset_g0/g1 fields are stored but not currently used to
         * remap characters — DEC line drawing is rare in BBS ANSI art.
         * Stored for future completeness. */
        /* Previous ESC ( or ESC ) set a flag we'd need to track;
         * for now just consume the designator byte. */
        s->state = ST_NORMAL;
        break;

    case ST_OSC:
        /* OSC is terminated by ST (ESC\) or BEL (0x07) */
        if (ch == 0x07 || ch == 0x1B) {
            s->state = ST_NORMAL;
        }
        break;

    case ST_DCS_ENTRY:
        /* Collecting DCS parameters: ESC P Ps1;Ps2;Ps3 <final>
         * Parameters are digits and semicolons.
         * Final byte (0x40-0x7E) determines sub-protocol. */
        if (ch >= '0' && ch <= '9') {
            s->current_param = s->current_param * 10 + (ch - '0');
        } else if (ch == ';') {
            if (s->num_params < VT100_MAX_PARAMS)
                s->params[s->num_params++] = s->current_param;
            s->current_param = 0;
        } else if (ch >= 0x40 && ch <= 0x7E) {
            /* Final byte — store last param */
            if (s->num_params < VT100_MAX_PARAMS)
                s->params[s->num_params++] = s->current_param;
            s->dcs_final = ch;

            if (ch == 'q' && s->dcs_handler) {
                /* Sixel: hand off to DCS passthrough */
                s->state = ST_DCS_PASS;
            } else {
                /* Unknown DCS — consume until ST */
                s->state = ST_DCS_PASS;
            }
        } else if (ch == 0x1B) {
            /* Premature ESC — might be ST */
            s->state = ST_NORMAL;
        }
        break;

    case ST_DCS_PASS:
        /* Passthrough: feed bytes to the DCS handler until it returns true (ST). */
        if (s->dcs_handler) {
            if (s->dcs_handler(s->dcs_context, ch)) {
                /* Handler received ST — return to normal */
                s->dcs_handler = NULL;
                s->dcs_context = NULL;
                s->state = ST_NORMAL;
            }
        } else {
            /* No handler — just consume until ST (ESC \) */
            if (ch == 0x1B) {
                /* Could be ST — peek next byte */
                s->state = ST_DCS_ENTRY; /* reuse to detect \ */
            }
            /* Actually, without handler we need simple ST detection */
            if (ch == 0x9C) { /* C1 ST */
                s->state = ST_NORMAL;
            }
        }
        break;
    }
}

/* ── Init / Reset ────────────────────────────────────────────────────*/

void vt100_init(vt100_state_t *s, vt100_emit_fn emit, void *context) {
    memset(s, 0, sizeof(*s));
    s->emit = emit;
    s->emit_context = context;
    s->num_cols = 80;
    s->num_rows = 25;
    s->scroll_bot = 24;
    s->fg = 7;
    s->bg = 0;
    s->autowrap = 1;
    s->last_char = 0;
    s->charset_g0 = 0;
    s->charset_g1 = 0;
    s->active_charset = 0;
    s->response_len = 0;
}

void vt100_reset(vt100_state_t *s) {
    vt100_emit_fn fn = s->emit;
    void *ctx = s->emit_context;
    vt100_init(s, fn, ctx);
}
