# hwtruth: hardware questions answered by a real IIgs

`Docs/fidelity/README.md` lists behaviours no document settles. `hwtruth.s` measures them. It
records raw bytes only, and `report.py` turns the bytes into answers with the rule for each
printed beside it. The same program runs on a real IIgs and on the emulator, so a hardware run
and an emulator run can be compared answer by answer.

| Record | Question |
|---|---|
| `SLOT6` | Does `$C02D` bit 6 ("Your Card") take `$C0E0-$C0EF` away from the IWM? Does the IWM's data register read `$FF` with the motor off? |
| `E0AUX` | Does bank `$E0` follow RAMWRT and RAMRD? |
| `MEGAII` | Do the Mega II VBL and quarter-second flags latch while their interrupts are disabled? |
| `SCANCLR` | Does reading `$C02E`/`$C02F` clear a pending scan-line interrupt? |
| `C01X` | What do bits 0-6 of `$C011-$C01F` read? |
| `GAME` | What do the buttons and paddle timers read with nothing attached? |
| `BLKMOVE` | Do MVN and MVP step the index registers in 8 or 16 bits when x=1? |
| `BANKS` | What do unpopulated banks read? Which ROM is this? |
| `IWMREG` | Does the IWM drive the bus at an odd address? What does an even read return with Q6 and Q7 both on? Do the handshake register's bits 0-5 read ones, and what is bit 6 outside the write state? |
| `RTC` | How many `$C034` polls does a clock transfer hold bit 7 for? Does `z010ab01` reach BRAM `$10-$13`, and `z1abcd01` BRAM `$00-$0F`? |
| `VDA` | Does the IIgs decode I/O on the 65816's invalid-address cycle (VDA=VPA=0)? An `STA abs,X` whose extra cycle addresses a soft switch says. |

Nothing is destructive. The IWM is read after a two-second wait for a boot drive's motor to
stop; its mode register is written once, with the value it already holds, because an odd
address with Q6 and Q7 on latches the bus. The clock chip is only read. Every soft switch
touched is restored. SHR is on for a few frames.

## Build the disk

Needs a2engine's `asmiigs` and `diskiigs` (found through `A2_ASMIIGS`/`A2_DISKIIGS` or
`ROSETTA_ROOT`):

    python tools/hwtruth/build.py

This writes `build/hwtruth/hwtruth.po`, an 800K bootable ProDOS volume. It also refreshes
`tools/hwtruth/hwtruth.bin`, the committed copy the gate uses. Commit that file whenever
`hwtruth.s` changes.

## Run it on a real IIgs

1. Write `hwtruth.po` to a 3.5" disk, or put it on a floppy or SmartPort emulator. Leave it
   write-enabled: the results are written back to the disk.
2. Leave the game port empty for the `GAME` record's no-hardware case, and leave slot 6's
   Control Panel setting as it is.
3. Boot it. The screen shows `HWTRUTH 1  DISK WRITE: 80` (`80` = written) and one hex line per
   record. If the write failed (`Cx`), photograph the screen: the bytes are all there.
4. Read the disk back and compare it with the emulator:

       tools\hwtruth\run_emu.ps1 -Image build\hwtruth\hwtruth.po -Out build\hwtruth\emu.bin
       python tools/hwtruth/report.py path\to\hardware.po build\hwtruth\emu.bin

   `report.py` accepts the disk image itself; it finds the RESULTS file.

Record which ROM and which slot 6 setting the machine had (the `BANKS` record carries the ROM
version).

## The gate

`gate.ps1` (in `tools/a2rail/gates.ps1` as `hwtruth`) runs the committed binary on the
emulator with no disk. It compares the answers, not the raw bytes, with
`emulator_verdicts.json`, because some raw bytes are floating bus and move with any cycle-cost
change. When a hardware result arrives and the emulator is changed to match it, re-record with
`gate.ps1 -Bless`, and name the hardware run in the commit.
