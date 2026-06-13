/*
 * utf8_decode.h — UTF-8 state machine decoder + Unicode-to-CP437 mapper
 *
 * Intercepts multi-byte UTF-8 sequences before they reach the terminal grid,
 * mapping common Unicode ranges to their CP437 equivalents.  Unknown codepoints
 * fall back to 0xFE (CP437 "■" block).
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#ifndef UTF8_DECODE_H
#define UTF8_DECODE_H

#include <stdint.h>

/* Decoder state — ~4 bytes of SRAM. */
typedef struct {
    uint32_t codepoint;      /* accumulates decoded bits          */
    uint8_t  bytes_remaining;/* continuation bytes still expected */
} utf8_state_t;

/* Initialise (or reset) decoder state. */
void utf8_init(utf8_state_t *s);

/*
 * Feed one byte into the decoder.
 *
 * Returns:
 *   1  — codepoint complete; *out is valid
 *   0  — need more bytes
 *  -1  — invalid sequence (state reset); caller should treat byte as CP437
 */
int utf8_decode(utf8_state_t *s, uint8_t byte, uint32_t *out);

/*
 * Map a Unicode codepoint to its CP437 equivalent.
 * Returns 0xFE if no mapping exists.
 */
uint8_t unicode_to_cp437(uint32_t codepoint);

#endif /* UTF8_DECODE_H */
