/*
 * utf8_decode.c — UTF-8 state machine decoder + Unicode-to-CP437 mapper
 *
 * Covers the ranges that modern TUI apps (ratatui, bubbletea, btop) emit:
 *   - U+0080..U+00FF  Latin-1 Supplement
 *   - U+2500..U+257F  Box Drawing
 *   - U+2580..U+259F  Block Elements
 *
 * All other codepoints fall back to 0xFE (CP437 "■").
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#include "utf8_decode.h"

/* ── state machine ─────────────────────────────────────────────────────── */

void utf8_init(utf8_state_t *s)
{
    s->codepoint      = 0;
    s->bytes_remaining = 0;
}

int utf8_decode(utf8_state_t *s, uint8_t byte, uint32_t *out)
{
    if (s->bytes_remaining == 0) {
        /* Start of a new sequence. */
        if ((byte & 0x80) == 0x00) {
            /* ASCII — caller handles directly, should not reach here */
            *out = byte;
            return 1;
        } else if ((byte & 0xE0) == 0xC0) {
            s->codepoint       = byte & 0x1F;
            s->bytes_remaining = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            s->codepoint       = byte & 0x0F;
            s->bytes_remaining = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            s->codepoint       = byte & 0x07;
            s->bytes_remaining = 3;
        } else {
            /* Invalid lead byte — reset and signal error */
            utf8_init(s);
            return -1;
        }
        return 0;
    }

    /* Continuation byte */
    if ((byte & 0xC0) != 0x80) {
        /* Expected continuation but got something else — reset */
        utf8_init(s);
        return -1;
    }

    s->codepoint = (s->codepoint << 6) | (byte & 0x3F);
    s->bytes_remaining--;

    if (s->bytes_remaining == 0) {
        *out = s->codepoint;
        s->codepoint = 0;
        return 1;
    }
    return 0;
}

/* ── Unicode-to-CP437 mapping tables ───────────────────────────────────── */

/*
 * Box Drawing U+2500..U+257F (128 entries).
 * Sparse — most entries are 0xFE (unmapped).
 * Only the characters that live in the standard CP437 font are mapped.
 */
static const uint8_t box_drawing_table[128] = {
    /* U+2500 */ 0xC4, /* ─ BOX DRAWINGS LIGHT HORIZONTAL          */
    /* U+2501 */ 0xFE, /* ━ heavy horizontal                        */
    /* U+2502 */ 0xB3, /* │ BOX DRAWINGS LIGHT VERTICAL             */
    /* U+2503 */ 0xFE, /* ┃ heavy vertical                          */
    /* U+2504 */ 0xFE, /* ┄ */
    /* U+2505 */ 0xFE, /* ┅ */
    /* U+2506 */ 0xFE, /* ┆ */
    /* U+2507 */ 0xFE, /* ┇ */
    /* U+2508 */ 0xFE, /* ┈ */
    /* U+2509 */ 0xFE, /* ┉ */
    /* U+250A */ 0xFE, /* ┊ */
    /* U+250B */ 0xFE, /* ┋ */
    /* U+250C */ 0xDA, /* ┌ BOX DRAWINGS LIGHT DOWN AND RIGHT       */
    /* U+250D */ 0xFE, /* ┍ */
    /* U+250E */ 0xFE, /* ┎ */
    /* U+250F */ 0xFE, /* ┏ */
    /* U+2510 */ 0xBF, /* ┐ BOX DRAWINGS LIGHT DOWN AND LEFT        */
    /* U+2511 */ 0xFE, /* ┑ */
    /* U+2512 */ 0xFE, /* ┒ */
    /* U+2513 */ 0xFE, /* ┓ */
    /* U+2514 */ 0xC0, /* └ BOX DRAWINGS LIGHT UP AND RIGHT         */
    /* U+2515 */ 0xFE, /* ┕ */
    /* U+2516 */ 0xFE, /* ┖ */
    /* U+2517 */ 0xFE, /* ┗ */
    /* U+2518 */ 0xD9, /* ┘ BOX DRAWINGS LIGHT UP AND LEFT          */
    /* U+2519 */ 0xFE, /* ┙ */
    /* U+251A */ 0xFE, /* ┚ */
    /* U+251B */ 0xFE, /* ┛ */
    /* U+251C */ 0xC3, /* ├ BOX DRAWINGS LIGHT VERTICAL AND RIGHT   */
    /* U+251D */ 0xFE, /* ┝ */
    /* U+251E */ 0xFE, /* ┞ */
    /* U+251F */ 0xFE, /* ┟ */
    /* U+2520 */ 0xFE, /* ┠ */
    /* U+2521 */ 0xFE, /* ┡ */
    /* U+2522 */ 0xFE, /* ┢ */
    /* U+2523 */ 0xFE, /* ┣ */
    /* U+2524 */ 0xB4, /* ┤ BOX DRAWINGS LIGHT VERTICAL AND LEFT    */
    /* U+2525 */ 0xFE, /* ┥ */
    /* U+2526 */ 0xFE, /* ┦ */
    /* U+2527 */ 0xFE, /* ┧ */
    /* U+2528 */ 0xFE, /* ┨ */
    /* U+2529 */ 0xFE, /* ┩ */
    /* U+252A */ 0xFE, /* ┪ */
    /* U+252B */ 0xFE, /* ┫ */
    /* U+252C */ 0xC2, /* ┬ BOX DRAWINGS LIGHT DOWN AND HORIZONTAL  */
    /* U+252D */ 0xFE, /* ┭ */
    /* U+252E */ 0xFE, /* ┮ */
    /* U+252F */ 0xFE, /* ┯ */
    /* U+2530 */ 0xFE, /* ┰ */
    /* U+2531 */ 0xFE, /* ┱ */
    /* U+2532 */ 0xFE, /* ┲ */
    /* U+2533 */ 0xFE, /* ┳ */
    /* U+2534 */ 0xC1, /* ┴ BOX DRAWINGS LIGHT UP AND HORIZONTAL    */
    /* U+2535 */ 0xFE, /* ┵ */
    /* U+2536 */ 0xFE, /* ┶ */
    /* U+2537 */ 0xFE, /* ┷ */
    /* U+2538 */ 0xFE, /* ┸ */
    /* U+2539 */ 0xFE, /* ┹ */
    /* U+253A */ 0xFE, /* ┺ */
    /* U+253B */ 0xFE, /* ┻ */
    /* U+253C */ 0xC5, /* ┼ BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL */
    /* U+253D */ 0xFE, /* ┽ */
    /* U+253E */ 0xFE, /* ┾ */
    /* U+253F */ 0xFE, /* ┿ */
    /* U+2540 */ 0xFE, /* ╀ */
    /* U+2541 */ 0xFE, /* ╁ */
    /* U+2542 */ 0xFE, /* ╂ */
    /* U+2543 */ 0xFE, /* ╃ */
    /* U+2544 */ 0xFE, /* ╄ */
    /* U+2545 */ 0xFE, /* ╅ */
    /* U+2546 */ 0xFE, /* ╆ */
    /* U+2547 */ 0xFE, /* ╇ */
    /* U+2548 */ 0xFE, /* ╈ */
    /* U+2549 */ 0xFE, /* ╉ */
    /* U+254A */ 0xFE, /* ╊ */
    /* U+254B */ 0xFE, /* ╋ */
    /* U+254C */ 0xFE, /* ╌ */
    /* U+254D */ 0xFE, /* ╍ */
    /* U+254E */ 0xFE, /* ╎ */
    /* U+254F */ 0xFE, /* ╏ */
    /* U+2550 */ 0xCD, /* ═ BOX DRAWINGS DOUBLE HORIZONTAL          */
    /* U+2551 */ 0xBA, /* ║ BOX DRAWINGS DOUBLE VERTICAL            */
    /* U+2552 */ 0xD5, /* ╒ BOX DRAWINGS DOWN SINGLE AND RIGHT DOUBLE */
    /* U+2553 */ 0xD6, /* ╓ BOX DRAWINGS DOWN DOUBLE AND RIGHT SINGLE */
    /* U+2554 */ 0xC9, /* ╔ BOX DRAWINGS DOUBLE DOWN AND RIGHT      */
    /* U+2555 */ 0xB8, /* ╕ BOX DRAWINGS DOWN SINGLE AND LEFT DOUBLE */
    /* U+2556 */ 0xB7, /* ╖ BOX DRAWINGS DOWN DOUBLE AND LEFT SINGLE */
    /* U+2557 */ 0xBB, /* ╗ BOX DRAWINGS DOUBLE DOWN AND LEFT       */
    /* U+2558 */ 0xD4, /* ╘ BOX DRAWINGS UP SINGLE AND RIGHT DOUBLE */
    /* U+2559 */ 0xD3, /* ╙ BOX DRAWINGS UP DOUBLE AND RIGHT SINGLE */
    /* U+255A */ 0xC8, /* ╚ BOX DRAWINGS DOUBLE UP AND RIGHT        */
    /* U+255B */ 0xBE, /* ╛ BOX DRAWINGS UP SINGLE AND LEFT DOUBLE  */
    /* U+255C */ 0xBD, /* ╜ BOX DRAWINGS UP DOUBLE AND LEFT SINGLE  */
    /* U+255D */ 0xBC, /* ╝ BOX DRAWINGS DOUBLE UP AND LEFT         */
    /* U+255E */ 0xC6, /* ╞ BOX DRAWINGS VERTICAL SINGLE AND RIGHT DOUBLE */
    /* U+255F */ 0xC7, /* ╟ BOX DRAWINGS VERTICAL DOUBLE AND RIGHT SINGLE */
    /* U+2560 */ 0xCC, /* ╠ BOX DRAWINGS DOUBLE VERTICAL AND RIGHT  */
    /* U+2561 */ 0xB5, /* ╡ BOX DRAWINGS VERTICAL SINGLE AND LEFT DOUBLE */
    /* U+2562 */ 0xB6, /* ╢ BOX DRAWINGS VERTICAL DOUBLE AND LEFT SINGLE */
    /* U+2563 */ 0xB9, /* ╣ BOX DRAWINGS DOUBLE VERTICAL AND LEFT   */
    /* U+2564 */ 0xD1, /* ╤ BOX DRAWINGS DOWN SINGLE AND HORIZONTAL DOUBLE */
    /* U+2565 */ 0xD2, /* ╥ BOX DRAWINGS DOWN DOUBLE AND HORIZONTAL SINGLE */
    /* U+2566 */ 0xCB, /* ╦ BOX DRAWINGS DOUBLE DOWN AND HORIZONTAL */
    /* U+2567 */ 0xCF, /* ╧ BOX DRAWINGS UP SINGLE AND HORIZONTAL DOUBLE */
    /* U+2568 */ 0xD0, /* ╨ BOX DRAWINGS UP DOUBLE AND HORIZONTAL SINGLE */
    /* U+2569 */ 0xCA, /* ╩ BOX DRAWINGS DOUBLE UP AND HORIZONTAL   */
    /* U+256A */ 0xD8, /* ╪ BOX DRAWINGS VERTICAL SINGLE AND HORIZONTAL DOUBLE */
    /* U+256B */ 0xD7, /* ╫ BOX DRAWINGS VERTICAL DOUBLE AND HORIZONTAL SINGLE */
    /* U+256C */ 0xCE, /* ╬ BOX DRAWINGS DOUBLE VERTICAL AND HORIZONTAL */
    /* U+256D */ 0xFE, /* ╭ */
    /* U+256E */ 0xFE, /* ╮ */
    /* U+256F */ 0xFE, /* ╯ */
    /* U+2570 */ 0xFE, /* ╰ */
    /* U+2571 */ 0xFE, /* ╱ */
    /* U+2572 */ 0xFE, /* ╲ */
    /* U+2573 */ 0xFE, /* ╳ */
    /* U+2574 */ 0xFE, /* ╴ */
    /* U+2575 */ 0xFE, /* ╵ */
    /* U+2576 */ 0xFE, /* ╶ */
    /* U+2577 */ 0xFE, /* ╷ */
    /* U+2578 */ 0xFE, /* ╸ */
    /* U+2579 */ 0xFE, /* ╹ */
    /* U+257A */ 0xFE, /* ╺ */
    /* U+257B */ 0xFE, /* ╻ */
    /* U+257C */ 0xFE, /* ╼ */
    /* U+257D */ 0xFE, /* ╽ */
    /* U+257E */ 0xFE, /* ╾ */
    /* U+257F */ 0xFE, /* ╿ */
};

/*
 * Block Elements U+2580..U+259F (32 entries).
 */
static const uint8_t block_elements_table[32] = {
    /* U+2580 */ 0xDF, /* ▀ UPPER HALF BLOCK    */
    /* U+2581 */ 0xFE, /* ▁ LOWER ONE EIGHTH    */
    /* U+2582 */ 0xFE, /* ▂ LOWER ONE QUARTER   */
    /* U+2583 */ 0xFE, /* ▃ LOWER THREE EIGHTHS */
    /* U+2584 */ 0xDC, /* ▄ LOWER HALF BLOCK    */
    /* U+2585 */ 0xFE, /* ▅ LOWER FIVE EIGHTHS  */
    /* U+2586 */ 0xFE, /* ▆ LOWER THREE QUARTERS*/
    /* U+2587 */ 0xFE, /* ▇ LOWER SEVEN EIGHTHS */
    /* U+2588 */ 0xDB, /* █ FULL BLOCK          */
    /* U+2589 */ 0xFE, /* ▉ */
    /* U+258A */ 0xFE, /* ▊ */
    /* U+258B */ 0xFE, /* ▋ */
    /* U+258C */ 0xDD, /* ▌ LEFT HALF BLOCK     */
    /* U+258D */ 0xFE, /* ▍ */
    /* U+258E */ 0xFE, /* ▎ */
    /* U+258F */ 0xFE, /* ▏ */
    /* U+2590 */ 0xDE, /* ▐ RIGHT HALF BLOCK    */
    /* U+2591 */ 0xB0, /* ░ LIGHT SHADE         */
    /* U+2592 */ 0xB1, /* ▒ MEDIUM SHADE        */
    /* U+2593 */ 0xB2, /* ▓ DARK SHADE          */
    /* U+2594 */ 0xFE, /* ▔ UPPER ONE EIGHTH    */
    /* U+2595 */ 0xFE, /* ▕ RIGHT ONE EIGHTH    */
    /* U+2596 */ 0xFE, /* ▖ */
    /* U+2597 */ 0xFE, /* ▗ */
    /* U+2598 */ 0xFE, /* ▘ */
    /* U+2599 */ 0xFE, /* ▙ */
    /* U+259A */ 0xFE, /* ▚ */
    /* U+259B */ 0xFE, /* ▛ */
    /* U+259C */ 0xFE, /* ▜ */
    /* U+259D */ 0xFE, /* ▝ */
    /* U+259E */ 0xFE, /* ▞ */
    /* U+259F */ 0xFE, /* ▟ */
};

/*
 * Latin-1 Supplement U+00A0..U+00FF (96 entries).
 * CP437 has approximate equivalents for most accented Latin characters.
 */
static const uint8_t latin1_table[96] = {
    /* U+00A0 */ 0xFF, /* NBSP → CP437 0xFF (non-breaking space glyph) */
    /* U+00A1 */ 0xAD, /* ¡ inverted exclamation */
    /* U+00A2 */ 0x9B, /* ¢ cent sign            */
    /* U+00A3 */ 0x9C, /* £ pound sign           */
    /* U+00A4 */ 0xFE, /* ¤ currency sign        */
    /* U+00A5 */ 0x9D, /* ¥ yen sign             */
    /* U+00A6 */ 0xFE, /* ¦ broken bar           */
    /* U+00A7 */ 0xFE, /* § section sign         */
    /* U+00A8 */ 0xFE, /* ¨ diaeresis            */
    /* U+00A9 */ 0xFE, /* © copyright            */
    /* U+00AA */ 0xA6, /* ª feminine ordinal     */
    /* U+00AB */ 0xAE, /* « left double angle    */
    /* U+00AC */ 0xAA, /* ¬ not sign             */
    /* U+00AD */ 0xFE, /* ­ soft hyphen          */
    /* U+00AE */ 0xFE, /* ® registered           */
    /* U+00AF */ 0xFE, /* ¯ macron               */
    /* U+00B0 */ 0xF8, /* ° degree sign          */
    /* U+00B1 */ 0xF1, /* ± plus-minus           */
    /* U+00B2 */ 0xFE, /* ² superscript 2        */
    /* U+00B3 */ 0xFE, /* ³ superscript 3        */
    /* U+00B4 */ 0xFE, /* ´ acute accent         */
    /* U+00B5 */ 0xE6, /* µ micro sign           */
    /* U+00B6 */ 0xFE, /* ¶ pilcrow              */
    /* U+00B7 */ 0xFA, /* · middle dot           */
    /* U+00B8 */ 0xFE, /* ¸ cedilla             */
    /* U+00B9 */ 0xFE, /* ¹ superscript 1       */
    /* U+00BA */ 0xA7, /* º masculine ordinal   */
    /* U+00BB */ 0xAF, /* » right double angle  */
    /* U+00BC */ 0xAC, /* ¼ one quarter         */
    /* U+00BD */ 0xAB, /* ½ one half            */
    /* U+00BE */ 0xFE, /* ¾ three quarters      */
    /* U+00BF */ 0xA8, /* ¿ inverted question   */
    /* U+00C0 */ 0xFE, /* À */
    /* U+00C1 */ 0xFE, /* Á */
    /* U+00C2 */ 0xFE, /* Â */
    /* U+00C3 */ 0xFE, /* Ã */
    /* U+00C4 */ 0x8E, /* Ä */
    /* U+00C5 */ 0x8F, /* Å */
    /* U+00C6 */ 0x92, /* Æ */
    /* U+00C7 */ 0x80, /* Ç */
    /* U+00C8 */ 0xFE, /* È */
    /* U+00C9 */ 0x90, /* É */
    /* U+00CA */ 0xFE, /* Ê */
    /* U+00CB */ 0xFE, /* Ë */
    /* U+00CC */ 0xFE, /* Ì */
    /* U+00CD */ 0xFE, /* Í */
    /* U+00CE */ 0xFE, /* Î */
    /* U+00CF */ 0xFE, /* Ï */
    /* U+00D0 */ 0xFE, /* Ð */
    /* U+00D1 */ 0xA5, /* Ñ */
    /* U+00D2 */ 0xFE, /* Ò */
    /* U+00D3 */ 0xFE, /* Ó */
    /* U+00D4 */ 0xFE, /* Ô */
    /* U+00D5 */ 0xFE, /* Õ */
    /* U+00D6 */ 0x99, /* Ö */
    /* U+00D7 */ 0xFE, /* × */
    /* U+00D8 */ 0xFE, /* Ø */
    /* U+00D9 */ 0xFE, /* Ù */
    /* U+00DA */ 0xFE, /* Ú */
    /* U+00DB */ 0xFE, /* Û */
    /* U+00DC */ 0x9A, /* Ü */
    /* U+00DD */ 0xFE, /* Ý */
    /* U+00DE */ 0xFE, /* Þ */
    /* U+00DF */ 0xE1, /* ß */
    /* U+00E0 */ 0x85, /* à */
    /* U+00E1 */ 0xA0, /* á */
    /* U+00E2 */ 0x83, /* â */
    /* U+00E3 */ 0xFE, /* ã */
    /* U+00E4 */ 0x84, /* ä */
    /* U+00E5 */ 0x86, /* å */
    /* U+00E6 */ 0x91, /* æ */
    /* U+00E7 */ 0x87, /* ç */
    /* U+00E8 */ 0x8A, /* è */
    /* U+00E9 */ 0x82, /* é */
    /* U+00EA */ 0x88, /* ê */
    /* U+00EB */ 0x89, /* ë */
    /* U+00EC */ 0x8D, /* ì */
    /* U+00ED */ 0xA1, /* í */
    /* U+00EE */ 0x8C, /* î */
    /* U+00EF */ 0x8B, /* ï */
    /* U+00F0 */ 0xFE, /* ð */
    /* U+00F1 */ 0xA4, /* ñ */
    /* U+00F2 */ 0x95, /* ò */
    /* U+00F3 */ 0xA2, /* ó */
    /* U+00F4 */ 0x93, /* ô */
    /* U+00F5 */ 0xFE, /* õ */
    /* U+00F6 */ 0x94, /* ö */
    /* U+00F7 */ 0xF6, /* ÷ */
    /* U+00F8 */ 0xFE, /* ø */
    /* U+00F9 */ 0x97, /* ù */
    /* U+00FA */ 0xA3, /* ú */
    /* U+00FB */ 0x96, /* û */
    /* U+00FC */ 0x81, /* ü */
    /* U+00FD */ 0xFE, /* ý */
    /* U+00FE */ 0xFE, /* þ */
    /* U+00FF */ 0x98, /* ÿ */
};

/* ── public mapper ─────────────────────────────────────────────────────── */

uint8_t unicode_to_cp437(uint32_t cp)
{
    /* ASCII passthrough (should be handled upstream, but guard anyway) */
    if (cp < 0x80) {
        return (uint8_t)cp;
    }

    /* Latin-1 Supplement */
    if (cp >= 0x00A0 && cp <= 0x00FF) {
        return latin1_table[cp - 0x00A0];
    }

    /* Box Drawing */
    if (cp >= 0x2500 && cp <= 0x257F) {
        return box_drawing_table[cp - 0x2500];
    }

    /* Block Elements */
    if (cp >= 0x2580 && cp <= 0x259F) {
        return block_elements_table[cp - 0x2580];
    }

    /* Common symbols that have direct CP437 equivalents */
    switch (cp) {
        case 0x2022: return 0x07; /* • BULLET                  */
        case 0x25CF: return 0x07; /* ● BLACK CIRCLE            */
        case 0x25CB: return 0x09; /* ○ WHITE CIRCLE            */
        case 0x25A0: return 0xFE; /* ■ BLACK SQUARE (IS 0xFE)  */
        case 0x25AA: return 0xFE; /* ▪ small black square      */
        case 0x2190: return 0x1B; /* ← LEFTWARDS ARROW        */
        case 0x2191: return 0x18; /* ↑ UPWARDS ARROW           */
        case 0x2192: return 0x1A; /* → RIGHTWARDS ARROW        */
        case 0x2193: return 0x19; /* ↓ DOWNWARDS ARROW         */
        case 0x2194: return 0x1D; /* ↔ LEFT RIGHT ARROW        */
        case 0x2195: return 0x12; /* ↕ UP DOWN ARROW           */
        case 0x21B5: return 0x17; /* ↵ DOWNWARDS ARROW W/ CORNER */
        case 0x221E: return 0xEC; /* ∞ INFINITY               */
        case 0x2248: return 0xF7; /* ≈ ALMOST EQUAL TO         */
        case 0x2260: return 0xFE; /* ≠ NOT EQUAL — no CP437 */
        case 0x2264: return 0xF3; /* ≤ LESS-THAN OR EQUAL TO  */
        case 0x2265: return 0xF2; /* ≥ GREATER-THAN OR EQUAL  */
        case 0x00B7: return 0xFA; /* · middle dot (duplicate path) */
        default:     return 0xFE;
    }
}
