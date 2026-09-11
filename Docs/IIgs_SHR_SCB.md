# Super Hi-Res SCB bits and fill mode (oracle)

This is hardware/oracle documentation, not a game feature.

## Hardware Scan-line Control Byte

Apple IIgs Hardware Reference Table 4-19 / Figure 4-19 (also CiderPress, Heineman 1992, MAME):

| Bit | Mask | Meaning |
|-----|------|---------|
| 7 | `$80` | 640 mode (1=640, 0=320) |
| 6 | `$40` | Scanline interrupt enable |
| 5 | `$20` | Fill mode (320 only) |
| 4 | `$10` | Reserved |
| 3-0 | `$0F` | Palette number |

A line SCB of `$40` is **IRQ on, fill off, 320**. It must fire a scanline IRQ (if the VGC scanline-IRQ enable is also set) and must **not** expand fill. Fill is `$20`.

GSSquared already decoded those bits this way in `SHRMode` (`src/devices/displaypp/generate/AppleIIgs.hpp`), the scanline IRQ check (`VideoScannerIIgs.cpp`, `current_scb & 0x40`), and the 320 fill expand (`VideoScanGenerator*.cpp`, `mode.fill`). A2GSPU's validation SHR blit (`a2gspu_emu.cpp`) also uses bit 5 (`0x20`) for fill. Those paths were not in conflict.

## Fill-mode pixel rule

In 320 mode with SCB bit 5 set, a **0 nibble** repeats the previous color on that scanline (left-to-right, high nibble first). Color 0 is therefore not a drawable index on a fill line. The first nibble of a fill line must not be 0; on hardware that case is undefined leftover VGC state. MAME / John Brooks: leftover index = `fillmode_init[SCB & 0x1F]`.

## What was wrong

1. **`iigs_shr::histogram` / `idxN` ignored fill.** Stored zero nibbles were counted as color 0, so a fill-mode sky/ground (byte0 = color, rest 0) under-counted the expanded color. The video map had the same gap. The scan *renderer* already expanded fill; only the diagnostic decode did not.
2. **Fill leftover used `vcount`, not SCB.** The generators copied MAME's `fillmode_init` table but indexed it with `vcount & 0x1F` at HSYNC, before that line's SCB was latched. Hardware/MAME index with `SCB & 0x1F` when the SCB is read.
3. **Incomplete SCB comments** (video summary listed only bit7=640 + palette) invited swapping fill vs IRQ. Bit 6 is IRQ, not fill.

## What changed

- `src/iigs_shr_layout.hpp`: named SCB masks; fill-aware `decode_line_indices` / `histogram`.
- `src/iigs_video_summary.hpp` / `src/iigs_diag.hpp`: map + `idxN` use that decode; comments list all SCB bits.
- Scan generators: leftover fill color latched from `SCB & 0x1F` when the SCB is read (`VM_SHR_MODE`).
- `VideoScannerIIgs.cpp` / `AppleIIgs.hpp`: comments match Table 4-19.
- MMUTEST `vid_fill_mode`: `$20` expands, `$40` does not.
