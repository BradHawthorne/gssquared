# Video cards and card-side graphics protocols: the ideal shape

Sources: fork `ed2c00fc`, `upstream/main` (read with `git show`). "U:" is an upstream path and "F:" a fork path, both under `src/devices/` unless the path says otherwise.

## 0. What a slot card can physically do (this bounds every protocol below)

- **It can watch writes.** A IIgs slot card sees "shadowed video writes, any accesses to internal I/O or expansion card slots, and accesses to banks $E0 and $E1", and M2B0 (main vs aux) is "only available on slot 3". [doc: Apple IIGS TN #68, *Tips for I/O Expansion Slot Card Design*] The real Second Sight "watches the IIGS I/O bus for writes to the IIGS video memory", manages "15 full-screen updates per second", and needs slot 3 on ROM 01 (slots 1–6 on ROM 03). [doc: *Second Sight VGA Manual v1*] The fork already models this signal view (F:`slot_bus/slot_bus.hpp:23-40`, `src/mmus/mmu_iigs.hpp:166-183`).
- **DMA is real, but it costs the CPU time.** /DMA "temporarily halt[s] the CPU"; software must load the bank register `$C037` first, at 1 MHz with interrupts off. [doc: TN #68] VidHD's DMA pause "interferes with" floppy-write timing. [doc: *VidHD Manual 1.2*, "Accelerators and DMA"] A card can read host RAM only by stopping the CPU for about one bus cycle per byte, and only if it drives the bus.
- **Classic Second Sight bulk data is PIO, not bus DMA.** The 1995 host library pushes every byte, and with M=0 it writes 16-bit words through `STA $B1` with D=`$C000`, so a single store hits both `$C0B1` and `$C0B2`. [test program: `VGA.Lib.s`, 1995 SS host library] "DMA" in the SS API names the Z180's own transfer, which U:`Docs/SecondSight.md:416` says correctly.
- **Card timing does not follow the host's.** "Mega II-VGC registers and interrupts are not synchronous to the ... Video Overlay Card video." [doc: Apple IIGS TN #70, *Fast Graphics Hints*] Any card with its own crystal (VGA at 70 Hz, a VideoTerm at 17.430 MHz [doc: *Videx VideoTerm Installation and Operation Manual*]) has a VBL that the host cannot see unless the card exposes it.

**Rule the ideal emulation follows:** card state changes only because of (a) bus cycles in emulated order, or (b) events on the card's own clock, expressed in emulated cycles. A card never reads host RAM for free, and its state never advances because the host rendered a frame.

## 1. Per card and mode

### 1.1 Second Sight, classic VGA API

- **Real card:** Z180 plus Oak VGA, up to 1 MB VRAM. Commands go to `$C0B0`, data to `$C0B1`/`$C0B2`, and `$C0B8` is the handshake: `$01`/`$00` for a transfer phase, `$A5`/`$A6` for a long command. The host polls in CMP loops; only `GetStatus` has a timeout. [test program: `VGA.Lib.s`]
- **Fork (classic only):** handshakes complete on emulated cycles (`LONGRUN_WAIT_CYCLES=60`, F:`secondsight/secondsight.hpp:176-180, 732, 1113-1122`), which is deterministic. The version byte is `$14` (`:672`).
- **Upstream:** completion depends on how many times `$C0B8` has been read (`HANDSHAKE_HOLD_READS=3`, U:`secondsight/secondsight.hpp:265-267, 1976-2015`). A card that only advances when polled is not physical. A debugger peek at `$C0B8` changes card state, and a host that waits on a timer instead of polling hangs.

**Defects (both):**
- `ClearScreen` and `ScrollScreen` use guest address and length with no bounds check, a host heap overrun (F:`:771, 958-966`; U:`:1457, 1637-1645`).
- `GetVGAReg` step 4 reuses a stale `dma_length_remaining` from the previous upload (U:`:1576-1586`).
- `dma_address` is a raw host pointer, and no video card has a snapshot block (F:`src/a2gspu_snapshot.cpp:168-220`).
- Every command costs zero card time: a 1 MB clear finishes in 60 cycles (fork) or 3 polls (upstream).

**Fork only:**
- `SetTextFont $00-$02` does nothing after `$03`. The user font stays because `font_ready` latches (F:`:154-170`; F:`vga_render_text_9x16.cpp:91-113`).

**Upstream only:**
- A host setting, not card state, gates the Apple II text overlay, which pre-empts VGA and GPU output whenever the Mega II is in text mode (U:`:2127-2141`). Its flash counter is a function-static that writes `ds->flash_state` (U:`ss_a2_text_sync.cpp:172-180`).
- Glyph and palette tables are global statics, not per-card (U:`vga_render_text_9x16.cpp:25-45`), and the VGA attribute blink bit is never rendered.
- `GetStatus` `size_rec` is `$05` in code (U:`:1232`) but `$06` in `SecondSight_GPU.md` (U:`Docs/SecondSight_GPU.md:371`).

**Neither covers:** Z180 code (`RunCode`), `SetShadow`, the Oak extended registers, border and overscan, or the real card's own emulation-mode scanout of shadowed Apple video.

### 1.2 VGA fence and version/capabilities (upstream)

- **Design:** per-mode tables return `$A6` for fenced opcodes (U:`:1888-1949`); `GetStatus` reports version `$20`; `GetCapabilities` (`$10`) returns TLV mode records (U:`:1248-1283`), sent only when the version is `$20` or higher. [design: upstream spec, `SecondSight_GPU.md` §7.5]
- **Assessment:** sound. It is one bounded response over the existing handshake, and hosts skip unknown records by `len`.
- **Defects:** the catalog does not encode which engine supports which mode, and has no per-engine feature bitmap, so hosts learn features from `$A6` failures.

### 1.3 Host Text (`$04`, upstream)

- **Design:** a 32-byte control block plus cells in shadowed RAM, armed by `SetTextCtrl` (`$50`), latched "at VBL", addressed as 16 bits plus an aux bit. [design: upstream spec, `SecondSight_Text.md` §5–8]
- **Addressing is physically honest:** 16 bits plus M2B0 is exactly what TN #68 allows, and the test program writes `$E0` directly (U:`testdev/hosttext/hosttext.s:125-354`).
- **Shortcut:** `frame_host_text` copies host RAM at host render time (U:`:623-668`, U:`ss_host_text.cpp:84-150`). The content matches a write-shadow, because `$E0`/`$E1` hold exactly the slot-visible writes; the timing does not.
- **Atomicity defect:** with no commit strobe and no card-VBL status, a real card can latch a half-written block. The test syncs to the IIgs VBL (`$C019`, `hosttext.s:98-104`), which works only because the emulator makes card VBL equal the IIgs frame. The cursor blink counts rendered frames (U:`:561`).

### 1.4 GPU Text (`$05`, upstream)

- **Design:** a tagged 16-bit word stream on `$C0B1`/`$C0B2` (putc when bit 15 = 0, else 128 opcodes); card-side scroll, IL/DL, ICH/DCH and erase; a clamp-advance cursor with policy on the host; VT100 and IIe COUT mappings; Yield `$95`; ring-empty on `$C0B8`. [design: upstream spec, `SecondSight_GPUText.md`, `Specs/SSConsole.md` §13]
- **Assessment:** the most physically plausible new mode: one-way, bounded ops, and a bandwidth bound argued from Mega II cycles (§3). `ReadCells` is rightly left out.
- **Defects:** ring overflow silently drops the newest word (U:`ss_gpu_text.cpp:89-91`); byte-to-word pairing has no resync, so one lost byte misaligns the stream forever (`:68-83`); `$C0B8` means both handshake and ring idle, resolved by read-consumed latches that give reads side effects (U:`:2027-2041`); the VBL drain runs only in `frame()` (U:`:770-773`).

### 1.5 GPU mode (`$03`, upstream)

- **Design:** host-assigned modes, card-assigned texture handles, CSB upload over the handshake, and run-to-completion with `$A5`/`$A6`. [design: upstream spec, `SecondSight_GPU.md` §5–9]
- **Implementation:**
  - Surfaces are SDL renderer textures (U:`ss_gpu.cpp:54-85, 136-150`): not snapshot-able, not readable without side effects, and backend-dependent in pixels.
  - Only `End`, `Clear`, `Present` and `DrawTexture` exist (`:270-319`), though `SecondSight_GPU.md` marks `$03 $05 $06 $10-$15 $20-$23` as required.
  - IDX8 uploads become grey, not palette colours (`:112-113`).
  - The whole CSB runs inside the I/O write that completes it, in zero card time (U:`:1846-1858`).
- **Hang bugs:** a Present with the VBL flag publishes `$A5` only from `frame()` (U:`:2151-2155`). `ScreenOff` returns earlier (`:2146-2148`), the text overlay branch returns earlier, and a higher-priority frame handler stops the loop (F:`src/videosystem.cpp:418-426`). Any of these means `WaitHSDone` never returns.

### 1.6 PPU mode (upstream; the fork dropped it in `3fbb454d`)

- **Implementation:** fixed addresses in host RAM, read at render time (U:`:797-830`). Scroll is documented as "latched at VBL" but read live; split and behind-background are not implemented (U:`ppu_render.cpp:97-100`).
- **Assessment:** the same physics as Host Text (a write-shadow is plausible), but with no arming and no commit. `Docs/PPU.md` is a sketch, not a protocol. Treat it as a design exercise.

### 1.7 Video Overlay Card (upstream only)

- **Real card:** "a duplicate of the Apple IIGS video circuitry". RAM page select chooses aux `$E1`, main `$E0`, or interlace. Main-page linearization is controlled separately. The 400-line interlace mode takes "odd 200 lines from bank $E1 and ... even 200 lines from bank $E0". [doc: Apple II Misc TN #17, *Buried Treasures of the VOC*]
  - It can be in a different mode from the main board (dual monitors).
  - Disabling the GG bus "inhibits writes to the VOC display memory", and the VOC screen stays "frozen". [doc: TN #17]
  - It uses M2B0 "to detect writes to main and auxiliary RAM". [doc: TN #68]
  - Its timing is independent of the main board. [doc: TN #70]
  - It also works on an enhanced IIe, giving SHR there. [doc: TN #17]
- **Upstream:** register bits come from KEGS [emulator: KEGS, via U:`Docs/VOC_SPEC.md:7-9`]. It reads host `$E0`/`$E1` at render time, only when the host itself is in SHR, and replaces the main display (U:`voc/voc.cpp:133-170`). VBL is the host scanner's (`:81-87`); field parity flips per rendered frame (`:168`); `$C0B7`, `$C0B8` and `$C0BD` read `$00` (`:109-110`).
- **Defects:**
  - `VOC_SPEC.md` says SHR updates "work regardless of GG bus state" (`VOC_SPEC.md:117-121`); TN #17 contradicts this.
  - With no card-side display RAM, neither the freeze nor a decoupled mode can be modelled, and there is no second output.
  - Upstream takes even lines from `$E1`; TN #17 says odd lines. Whether this is only 0-based vs 1-based counting is unverified.
- **Neither covers:** genlock, key colour, dissolve, VBL and line IRQs, or VOC on a IIe.

### 1.8 Videx VideoTerm (fork and upstream identical)

- **Real card:** a 6845 CRTC, 2 K of RAM windowed at `$CC00-$CDFF` in four 512-byte banks, firmware at `$C800`, and a 7×9 matrix in a 9-line cell. The manual says "the full matrix is actually 9 by 9". [doc: VideoTerm manual] The four banks are selected by A3–A2 on DEVSEL accesses, with even and odd addresses as the CRTC address and data registers. Whether *writes* also select a bank is [recollection — unverified; one secondary reimplementation article agrees]. The R10 bit-6/bit-5 cursor modes are [recollection — unverified: 6845 datasheet].
- **Defects:**
  - Writes never select a bank (U:`videx/videx.cpp:101-135`); every register reads back (`:89-91`; the chip variant is unverified); page-select reads return `$00`, not the floating bus (`:98`).
  - Geometry is hard-wired to 80×24×9, ignoring R1, R6 and R9 (`videx_80x24.cpp:40, 61-83`); the blink counts host frames (`:104-122`).
  - An additive double-draw (`:146-150`) means screenshots are not the card's pixels; `video_system->clear()` runs even when the card does not own the frame (`videx.cpp:157`).
  - There is no snapshot.

### 1.9 VidHD

- **Real card:** slot 3 is required on IIgs ROM 0/1 (ROM 3 allows slots 1–6). It uses DMA at startup and for `AUX_MOVE`, has text firmware for 80×45, 120×67 and 240×135, and gives the II/IIe the IIgs display registers (`$C021`, `$C022`, `$C029`, `$C02B`, `$C034`, `$C068`) plus a `$C06E` pause register. [doc: *VidHD Manual 1.2*]
- **Both trees:** only the ROM bytes `$24 $EA $4C` exist (F:`vidhd/vidhd.cpp:24-35`). The fork's comment (`:7-23`) states the problem honestly: the card advertises a capability that is absent. The signature bytes themselves are [recollection — unverified].

### 1.10 A2GSPU card model (fork)

- **Function:** the card watches the slot bus and never drives it. Every read floats; there is no read handler (F:`a2gspu/a2gspu.cpp:1173-1178`). The one host channel is write-only `$C0nF`, framed as `[SYNC $A2][seq u16][op][len ≤ 32][payload]`, with skip and oversize resync and gap and duplicate counters (F:`a2gspu/a2gspu_cmdreg.hpp:5-29, 111-139`). Only NOP and HELLO have meaning. Records go to `A2GSPU_CARD_CMDLOG` (`Docs/A2GSPU_RAILS.md:140`). The physical card receives the forwarded bus stream (`a2gspu.cpp:61-77`).
- **Defects:**
  - The "16 commands per video frame" budget resets in the host device dispatcher (`:569`). Free-run frames follow host time (F:`src/gs2.cpp:528-559`), so `overflow` is nondeterministic, and the host cannot observe or act on it anyway.
  - The assembler is not in any snapshot.
  - The legacy model still exposes a readable ident `$A2` and a status register (F:`a2gspu/a2gspu_proto.hpp:32-41, 178-184`), which contradicts the card model; it is correctly kept behind `A2GSPU_CARD_MODEL=legacy`.

## 2. Upstream protocols assessed as card protocols

| | Classic/fence/caps | Host Text | GPU Text | GPU | PPU |
|---|---|---|---|---|---|
| Transport | PIO on `$C0B0-2`, `$C0B8` handshake | shadowed writes plus a `$50` pointer | word FIFO on `$C0B1`/`$C0B2` | PIO over the handshake (8-byte header, pixels, CSB) | shadowed writes |
| Host RAM by DMA? | no | emulated as a free render-time read; a physical card would need a write-shadow | no | no (a paravirtual pointer is an open question, §15.6; that would be real DMA with CPU halt plus `$C037`) | as Host Text |
| Physically possible on a IIgs | yes | yes via a write-shadow (TN #68), `$E0`/`$E1` or shadowed pages only | yes | yes, but multi-KB/s PIO costs real bus time | yes via shadow |
| Completion | `$A5`/`$A6` after polls (upstream) or cycles (fork) | none; "next VBL" | ring-idle `$01` | `$A5`/`$A6`; VBL Present only via `frame()` | none |
| Errors | `$A6`; unknown commands ignored | blank screen on a bad block | reserved ops = ignore one word; overflow dropped silently | `$A6`, no rollback | none |
| Negotiation | version `$20` plus TLV modes | shared | shared | plus `GetGpuInfo` | none |
| Determinism | read-count (upstream) or cycles (fork) | host-frame latch | host-frame VBL drain | SDL backend pixels; host-frame VBL | host-frame |
| Must snapshot | 1 MB VRAM, 128 K SRAM, CRTC/mode/palette, command step, buffers, DMA *offset*, timers | armed, ctrl addr/aux, latch, raster, blink phase, palette, font | cells, cursor, margins, attr, style, sync, yield, ring, `have_lo`/`lo_byte`, pending/count | handle table plus guest-format pixels, palette, both display buffers, front index, policy, pending VBL, partial CSB/upload | tilesets, FB pages, latched registers |

**Verdict:**
- **Sound as card designs:** GPU Text (one-way, bounded, bus-rate argued) and the version/capabilities handshake.
- **Sound, but missing an atomic commit:** Host Text (a write-shadow is physically honest).
- **Plausible ISA, but its emulation shape is wrong:** GPU mode (zero-time execution, SDL-owned state).

## 3. The ideal shape for the fork's integration

### (a) Fidelity
1. **Card core as a pure state machine:** `bus_write(addr16, data, ctl, cycle)` fed from the existing slot-bus tap (M2B0, DEVSEL), `devsel_read/write`, and `advance_to(cycle)`. The card owns its memory (a write-shadow of the windows it decodes, plus VRAM, textures and cells, in plain byte arrays) and never touches `mmu->get_memory_base()`.
2. **Real card time:** each command has a card-time cost (work multiplied by a calibrated rate; the SS manual's 15 full screens per second is one anchor), and `$A5`/`$A6` is an event on the emulator's cycle timer.
3. **The card's own scanout clock:** card VBL every N emulated cycles at the card's rate. It latches Host Text, drains VBL-sync GPU Text, flips GPU swaps and advances blink. The IIgs VBL is a different clock, so a protocol that needs sync must expose card VBL or offer a commit op.
4. **Presentation separated from state:** `frame()` only chooses which card framebuffer to show. Card state keeps evolving whether or not its handler wins the frame, which fixes §1.5 and the VOC field bug.
5. **DMA only as real DMA:** it halts the CPU for its bus cycles, needs `$C037`, and exists only on models that drive the bus.

### (b) Determinism
- Every card effect is a function of (bus-ordered writes, emulated cycle).
- The free-run loop must not define card VBL. Derive it from emulated cycles, never from SDL time.
- Reads with side effects, such as a handshake read that advances a state machine or a FIFO pop, happen only on guest bus cycles. Debug readers go through separate functions with no side effects.

### (c) Instrumentation
- **`devstate <card>` and snapshot blocks:** every field in the §2 snapshot row plus the state machine and timers, in one versioned block per card (the existing `a2gspu_snap_dev_begin` shape), with pointers stored as (buffer, offset).
- **Rail records per command:** `{cycle, card, mode, cmd, args digest, result, card-time cost}`, generalising `A2GSPU_CARD_CMDLOG` to every card; drops and overflows are records too.
- **Render goldens:** an FNV hash of the card framebuffer (native format plus palette) taken before SDL, for every mode.
- **Side-effect-free agent reads:** `text_cells(card)` returns the char/attr grid; `framebuffer(card)` returns pixels with no SDL readback.
- **GPU mode:** a software reference rasterizer is ground truth; any SDL acceleration is optional and golden-checked against it.

## 4. Mapping SecondSight ideas onto the A2GSPU card model

The constraint is that the host writes `$C0nF` and nothing flows back to the CPU: no read, no handshake, no DMA, no IRQ.

| Idea | Fits? | What the frame format must adopt |
|---|---|---|
| Text word stream (VT100/IIe mapping, card-side scroll/IL/DL, clamp cursor) | **Yes, best fit;** already one-way. | Op `TEXT` carrying up to 16 LE words (37 bytes on the wire); framing supplies the resync upstream lacks. Drop `ReadCells` and `$C0B8` idle. Rule: each op finishes within the card's per-byte budget, so no ring can overflow even at the bus maximum (~17,030 slow cycles per frame). |
| Display yield | **Yes.** | Op `OUTPUT{source}`: card surface, card text, or passthrough of the snooped Apple video. |
| Host text control block | **Yes, and more faithful than on SS**, because this card already shadows the bus. | `TEXT_CTRL{addr16, aux}` plus `COMMIT`: the card latches its shadow at the commit frame's last byte (bus order, deterministic), making updates atomic without a readable VBL. Data must sit in the forwarded windows (`a2gspu.cpp:76`: `$0400-$0BFF`, `$2000-$9FFF`) or the whitelist must grow. |
| Command sequence buffer ISA | **Opcodes fit;** run-to-completion with `$A5`/`$A6` does not. | Inline ops (len ≤ 32, seq-chained continuation, a gap aborts), or `EXEC{addr16, aux, len16}` naming a display list in shadowed RAM, captured at the frame. Errors go to telemetry and a visible error state; the host never waits. |
| Texture handles | **Only as host-assigned IDs.** | `TEX_DEFINE{id, w, h, fmt, src addr16/aux}` copies from the shadow; `TEX_FREE{id}`. Capacity is a published profile contract the host tracks; on failure the id is invalidated and recorded. |
| Version/capabilities handshake | **Contradicts** (needs a read). | HELLO already carries the host version one way; capabilities come from a configuration profile keyed by card model; the card records HELLO/profile mismatches. Do not add a readable byte. |
| `$C0B8` completion / VBL Present wait | **Contradicts.** | The host paces itself on IIgs VBL (`$C019`) with a known per-frame budget; `PRESENT` takes effect at card VBL and never blocks the host. |

**Frame-format changes, in summary:**
- Op-class ranges: session `$00-$0F`, text `$10-$2F`, CSB `$40-$7F`, resources `$80-$9F`.
- A seq-chained continuation for more than 32 bytes, with "a gap aborts the transaction".
- A shadow-reference tuple `{addr16, side, len16}` with capture-at-last-byte semantics.
- `COMMIT`/`PRESENT` ops.
- The per-frame budget redefined in emulated bus time, or removed by sizing card buffers to the physical bus maximum.

## 5. Implementation steps (by value)

| # | Step | Size | Risk |
|---|---|---|---|
| 1 | Harden fork SS classic: bounds-check `ClearScreen`, `ScrollScreen` and uploads; fix the stale `GetVGAReg` remainder; fix the `$00-$02` font revert. | S | low |
| 2 | Side-effect-free `devstate`, `text_cells` and `framebuffer` readers for SS classic, Videx, and the A2GSPU assembler; FNV goldens before SDL. | S–M | low |
| 3 | Snapshot blocks for SS classic (VRAM, SRAM, regs, step, DMA offset, timers), Videx, and the A2GSPU assembler. | M | low–med |
| 4 | A card clock service: per-card VBL events in emulated cycles; move blink, latch and budget logic off `frame()` and the device dispatcher; make `frame()` presentation-only. | M | med (touches shared loops; run the gates) |
| 5 | Port the upstream SS modes onto steps 1–4 (the port is owed anyway): cycle-cost handshakes, Host Text write-shadow plus commit, GPU Text ring with resync and snapshot, GPU mode on a software rasterizer with the full v0.1 op set, no host-setting text override. | L | med–high |
| 6 | A2GSPU frame format v2 (§4): spec, emulator model with reference renderer, and physical card together. | L | high (cross-artifact) |
| 7 | VOC: card-side display RAM fed from the slot bus, GG-bus freeze, latched card mode, second output view; check TN #17 line parity on hardware. | M–L | med |
| 8 | Videx: bank select on every DEVSEL access (verify against the manual), CRTC-driven geometry, per-field blink, floating reads, goldens without the double-draw. | S–M | low |
| 9 | VidHD: detection stub opt-in and declared in the manifest; a behavioural model is deferred (proprietary firmware). | S | low |

**Validation:**
- **Programs:**
  - `testdev/gputest`: texture upload, CSB and VBL Present; run it again with ScreenOff to expose the hang.
  - `testdev/hosttext` and `hosttext43`: wrap scroll, freeze rows, palette-from-block, page-flip.
  - A GPU Text IIe driver. `SSConsole.md` names `testdev/ss80`, but that directory is not in `upstream/main`, so a test must be written.
  - `VGA.Lib.s`-based classic software for the handshake.
  - TN #17's IIe SHR loader and interlace sequence for VOC.
  - Videx firmware 2.4 `PR#3` with CRTC pokes.
- **Gates:** identical framebuffer hashes across two runs, across normal vs free-run at the same emulated cycle, and after a mid-stream snapshot/restore; every per-mode golden holds; card state is unchanged when frame presentation is suppressed.
