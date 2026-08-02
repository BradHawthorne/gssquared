# Double Hi-Res (DHGR) — contract for gssquared + art tools

This is the **named oracle policy** for DHGR in this fork. a2tile and a2engine
align against it. If you change colour math, update this file and re-export profiles.

## 1. Bitstream (ground truth)

Per scanline, 40 byte-columns, each producing **14 dots**:

```
for each column 0..39:
  AUX  byte @ $2000+addr  → 7 dots, bit 0 first, bit 7 discarded
  MAIN byte @ $2000+addr  → 7 dots, bit 0 first, bit 7 discarded
```

Addressing uses the standard HGR triple interleave (shared with HGR). Full page
dump for tools: **8K AUX then 8K MAIN** (`vram` CTRL verb).

Softswitch gate for scanner `VM_DHIRES`:

```
graphics && hires && dblres && 80col
```

(80STORE is **not** required; a2engine keeps PAGE2 free for double-buffering via RAMWRT.)

## 2. Named colour profiles

| Name | Path | Use |
|------|------|-----|
| **mono** | `png` CTRL verb | Bit/layout QA only |
| **4dot** / **a2engine** / **gssquared** | `pngc` + RGB discrete LUT | **Art oracle** for seams and LORES solids |
| **ntsc560** | Interactive `NTSC560` + `dhgr-export` LUT | Composite finish look; FIR Y/I/Q |

### Discrete 4-dot model (art oracle)

```
window = state | (bit << 3)     // bit3 = CURRENT, bit0 = oldest
rgb    = palette[window][phase]
state  = ((state >> 1) | (bit << 2)) & 7
phase  = (phase_offset + x) & 3
```

DHGR uses **phase_offset = 1** (90° vs HGR) in Comp and RGB generators.

Palette RGB = a2engine hardware-order LORES (`APPLE2_16`), rotated into phases the
same way ii-pix / a2tile build four-dot tables. **Do not** use the HGR 11-bit RGB
LUT for DHGR (that path was wrong by construction; fixed in `VideoScanGenerator_RGB`).

### Composite NTSC560

- Finite FIR window: `NUM_TAPS` (currently 7) → 15-bit bit window × 4 phases
- Separate luma/chroma bandwidths in `filters.cpp`
- LUT: `g_hgr_LUT[phase][bits]`
- Bit-order of the LUT builder was historically uncertain; always re-run
  `dhgr-calibrate` after changes. Discrete 4-dot has an explicit solid gate.

## 3. CTRL verbs (agent rails)

| Verb | Meaning |
|------|---------|
| `png <file> [page] [scale] [auto\|hgr\|dhgr]` | Mono dots from RAM |
| `pngc <file> [page] [scale] [4dot\|mono]` | **Named colour** PNG (discrete 4-dot default) |
| `vram <file> [page]` | Raw 8K AUX + 8K MAIN |
| `dhgr-export <dir>` | Write `gssquared_4dot.bin` + `gssquared_ntsc.bin` for a2tile |
| `dhgr-calibrate` | Solid LORES round-trip on discrete table (0 fails = PASS) |

## 4. Profile file formats (shared with a2tile)

### GSDHGR4D (`gssquared_4dot.bin`)

```
"GSDHGR4D"          8 bytes
version u32 le = 1
depth   u32 le = 4
RGB u8[16][4][3]    window × phase × channels
```

### GSNTSC01 (`gssquared_ntsc.bin`)

```
"GSNTSC01"          8 bytes
version u32 le = 1
taps    u32 le = NUM_TAPS
RGBA u8[4][2^(2*taps+1)][4]
```

a2tile: `a2tile encode … --gssquared-4dot path --auto-extras`

## 5. What is NOT the art oracle

- SDL `shot` BMP (host scaling, GPU path)
- RGB HGR LUT applied to DHGR bits (**removed**)
- Unnamed “whatever the window shows” without a profile string

## 6. Full-screen binary layout

Some tools store `AUX $2000–$3FFF` then `MAIN $2000–$3FFF` as a 16K blob.
a2engine runtime tiles are **row-plane banks** (different packing) — convert via a2tile.

## 7. Calibration policy

After any change to DHGR colour:

1. `dhgr-calibrate` → PASS  
2. `dhgr-export build/dhgr_profiles`  
3. `a2tile calibrate --gssquared-4dot …` → PASS  
4. Solid LORES fixtures through a2engine DHGR build + `pngc` visual spot-check  

## References

- a2engine `tools/palette.py`, `tools/calibrate.py`, `tools/dhgrview.py`
- a2tile `docs/TILE_AND_RENDER.md`, `src/a2tile/profiles.py`
- ii-pix `docs/dhr.md` (physics; not tile packing)
- DisplayNG.md (bitstream architecture notes)
