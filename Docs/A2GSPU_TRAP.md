# A2GSPU_TRAP — PC traps from configuration

> **Where this came from.** The fork had two kinds of PC trap and they were
> opposite shapes. `A2GSPU_PCTRAP` is a generic trigger with a fixed payload:
> first entry to a range, dump the registers and the PC ring, once. The three
> `WIZ5_*_OUT` traps in `src/wiz5_config.hpp` are the reverse — hard-coded
> triggers with bespoke payloads: a decode table at `$0D00` on driver entry, the
> output window on driver exit, the eval stack when a p-code sub-opcode is 6,
> disk blocks when `X==1`. Neither could express the other, so every new
> "dump X when the guest reaches Y" was a C++ change, in a fork that is meant
> to serve every project without carrying each project's code.
>
> `A2GSPU_TRAP` is the general form of both. A trap is a **trigger**, a
> **condition**, a **policy** and a **payload**, and all four come from the
> environment. The three wiz5 traps are written out as specs at the end of this
> page; that is the proof that nothing was lost.

Source: `src/a2gspu_trap.hpp`. Hook: one gated call in the CPU execute loop
(`base_6502.cpp`), beside `A2GSPU_TAP` and `A2GSPU_PCTRAP`. Armed at spike
start and, idempotently, from `run_one_frame`, so CTRL and windowed runs get
it too.

## Rails

| Rail | Effect |
|------|--------|
| `A2GSPU_TRAP="<spec>[;<spec>…]"` | one or more specs, `;`-separated |
| `A2GSPU_TRAP_FILE=<file>` | specs one per line, `#` comments; combines with the above |
| `A2GSPU_TRAP_OUT=<file>` | default NDJSON sink for specs without `out=`; else the global record sink `A2GSPU_RECORDS`; otherwise records go to stdout as `IIGS TRAP: {…}` |

## Live, from the CTRL rail

The same grammar, armed mid-session, so a trap an agent discovers it needs costs a
command instead of a relaunch that throws the session away.

| Verb | Effect |
|------|--------|
| `trap` | status: every armed trap with `hits`, `records`, `entries`, `exits` (`traps=0` when none) |
| `trap add <spec>` | parse and arm one spec now. A malformed spec **fails the ack** (`status=FAIL trap-spec <why> -- nothing armed`) instead of ending the session; at launch the same error is still fatal. A name already armed is refused |
| `trap list` | every armed spec, as the arm echo describes it |
| `trap clear [<name>]` | disarm one trap, or all; each prints its close-out summary to the session log, and `store=`/`read=` funnel costs go away with the last trap that needed them |

Specs from `A2GSPU_TRAP`/`A2GSPU_TRAP_FILE` arm first; live specs join them.

## A spec

Space-separated `key=value` tokens. **No spaces inside a value** — the spec is
split on spaces, which is why the set operator is `~{…}` and not `in {…}`.

| Token | Meaning |
|-------|---------|
| `name=<id>` | tag carried on every record (default `trapN`) |
| `at=<pc>[-<pc>]` | entry trigger, PC or range. `HHHH` matches that 16-bit PC in **any** bank (interpreter traps are bank-agnostic); `BB:HHHH` matches one bank |
| `store=<addr>[-<addr>]` | trigger: a byte is **written** into the range (same bank syntax). The record's `pc`/`sym` name the storing instruction, and `store:{addr,value}` says what landed where. Real stores only, not the dummy write of a read-modify-write. The any-bank form matches that offset in **every** bank, RAM included — measured: `store=C03D` on the Dealer Demo caught 199,978 writes through `$E1` and a handful of plain-RAM writes to `$02:C03D` and friends — so name the bank for an I/O port: `store=E1:C03D` is "who feeds the sound GLU, with registers and ring" in one spec |
| `read=<addr>[-<addr>]` | trigger: a byte is **read** from the range as operand data, through the CPU's data-read funnel (`bus_read`). Opcode and operand fetches are `at=`'s job, and vector pulls and the dummy read of a read-modify-write are not reads a program asked for, so neither fires. Same bank syntax as `store=`, and the same caution: the bank is where the CPU's **data bank register** points, so a IIgs program running with `DBR=$E1` reads `$E1:7000`, not `$00:7000` — use the any-bank form when unsure. `read:{addr,value}`; `pc`/`sym` name the reading instruction |
| `sp=<lo>-<hi>` | trigger: the **effective** stack address (page 1 for an 8-bit or emulation-mode S, so `01F0-01FF` means the same on every machine) **enters** the range — an edge, one record per entry, not one per instruction spent inside. STACKTRAP's job with payloads; the record carries `sp` |
| `exit=<pc>` | paired exit trigger (`at=` traps only). The exit record carries the entry's `n`. One outstanding entry at a time; a second entry before the exit replaces it |
| `when=<cond>[,<cond>]` | all must hold at entry. `<expr>OP<expr>` with `OP` one of `== != < > <= >=`, or `<expr>~{v,v,…}` ("is one of") |
| `once` | record the first qualifying hit only — the default, and `A2GSPU_PCTRAP`'s behaviour |
| `every` | record every qualifying hit |
| `first=N[/<expr>]` | record every hit, but attach the `heavy=` payload only to the first `N` hits per distinct value of `<expr>` (or the first `N` overall) |
| `cap=N` | stop recording after `N` records (default 20000). Hits are still counted for the close-out line |
| `do=<payload>[,…]` | on every entry record |
| `heavy=<payload>[,…]` | on entry, when the policy allows |
| `xdo=` / `xheavy=` | the same, for exit records (heavy when the paired entry was heavy) |
| `out=<file>` | this spec's own NDJSON sink |

Counts (`first`, `cap`, lengths) are **decimal**. Addresses and values inside
expressions are **hex**.

## Payloads

| Payload | Record field | What |
|---------|--------------|------|
| `regs` | `regs` | `a x y s d dbr pbr p e`, decimal |
| `ring[=N]` | `ring` | the last `N` instruction PCs, oldest first, max 256. The trap keeps its own ring; it does not arm `BRKMEM` |
| `zp[=LO-HI]` | `zp` | bank-0 bytes `LO..HI` as hex (default `00-FF`, max 4096 bytes) |
| `stack[=N]` | `stack` | `N` bytes above S (default 32). Page-1 wrap in emulation mode or when S is 8-bit; 16-bit otherwise |
| `mem=<expr>:<len>` | `mem` | `len` bytes at the 24-bit address `<expr>`, with the resolved address and the expression text, so a reader can tell where the bytes came from |
| `field=<name>:<expr>` | `f.<name>` | one scalar |
| `text` | `text` | the 40×24 text page as one string |
| `scan=<zp\|stack>:<lo>-<hi>:<n>:<max>` | `scan` | for every little-endian 16-bit value in the configured `zp` or `stack` window that lands in `[lo,hi]`, dump `n` bytes at it, at most `max` entries. This is the CSP6 trap's "chase every pointer" |

If a spec names no `do=` and no `heavy=`, it records `regs`. A trap never
records a bare "it fired".

## Expressions

Hex numbers; a leading `$` or `0x` forces a number, which matters for `a`, `d`
and `e` — bare they are registers, `$a` `$d` `$e` are digits.

| Form | Value |
|------|-------|
| `a x y s d dbr pbr pc fpc p e` | registers; `pc` is 16-bit, `fpc` 24-bit, `e` the emulation flag |
| `m8(e) m16(e) m24(e)` | byte / word / 24-bit value at 24-bit address `e` |
| `z8(e) z16(e)` | byte / word at bank-0 address `e` — zero page, or any 16-bit address |
| `s8(e) s16(e)` | byte / word at `S+e`, same wrap rule as `stack` |
| `e + e`, `e - e`, `( … )` | arithmetic, 24-bit |

They nest. `m8(s16(1)+1)` is "the byte after the return address on the stack",
which after a `JSR $BF00` is the ProDOS MLI command number.
`m16(m16(s16(1)+2)+1)` is the pathname pointer inside that call's parameter
block.

## Records

One JSON object per line:

```
{"event":"trap.entry","frame":900,"cycles":15306622,"trap":"mli","t":"e","n":12,"pc":"00:BF00","sym":"MLI","incall":null,"heavy":1,
 "regs":{…},"f":{"cmd":200},"mem":{"at":8448,"expr":"m16(m16(s16(1)+2)+1)","d":"0b2f5344312f42494e2f4541474c45…"}}
```

`t` is `e` for entry, `x` for exit; `n` is the entry's sequence number and the
exit that pairs with it carries the same `n`. `frame` is the headless frame
counter and `cycles` the clock's cycle count at the trap. `sym` names the trap
PC — an application symbol when `A2GSPU_SYMBOLS` is loaded, else a documented
firmware entry point (`COUT`, `RDKEY`, `MLI`, `ToolDisp`…), else `null`. `incall`
names the toolbox call in progress when the trap fired (`WindowMgr.fn15`), or
`null` outside any call.

## Guarantees

- **Off by default.** With no `A2GSPU_TRAP*` set, `g_trap_on` is false and the
  CPU loop pays one untaken branch. Stock behaviour is byte-identical (the
  unarmed SHR hash is part of the gate).
- **Observe-don't-disturb.** Every read goes through the MMU probe
  (`probe_peek`): no soft switch is ever thrown (`$C0xx` reads as floating
  bus), banks `$00/$01` resolve through the CPU's own aux and language-card
  mapping, `$E0/$E1` come from the Mega II image. A trap never writes. (An
  earlier revision of this page said the probe returned floating bus for
  banks `$00/$01`; that was once true of the fork and is not — see the
  resolution in `mmu_iigs.hpp`.)
- **Loud.** A malformed spec is fatal at arm time, with the spec and the reason
  on stderr and exit code 2. Every armed spec is echoed with its resolved
  trigger and sink. Every spec prints a close-out line —
  `hits= records= entries= exits=`, with `(never reached)` when it never fired —
  so silence is never mistaken for success.
- **Stamped.** Every record carries frame and cycles.

## Not yet

- `exit=` pairing for `store=`, `read=` and `sp=` traps.
- `A2GSPU_PCTRAP` is untouched and still works; `once do=regs,ring` is its
  equivalent here.

## The three wiz5 traps, as specs

These reproduce the *information* the hard-coded traps capture. The JSON
shapes differ (the wiz5 files have their own consumers), which is why the
wiz5 rails stay until those consumers move.

**SCREENDEV driver trap** (`WIZ5_EC19_OUT`): log every call's sub-code and the
source/destination pointers; for the first four calls per sub-code also dump
192 bytes at the source and the 768-byte decode table at `$0D00`; at driver
exit log the destination pointer and output index, and for heavy calls 192
bytes at the destination.

```
name=ec19 at=EC19 exit=EC28 every first=4/z8(32)
  do=field=sub:z8(32),field=src:z16(30),field=dst:z16(34)
  heavy=mem=z16(30):192,mem=0D00:768
  xdo=field=dst:z16(34),field=outidx:z8(14)
  xheavy=mem=z16(34):192
```

(one line; broken here for reading)

**Disk block writes** (`WIZ5_DISKWR_OUT`): at the unit dispatcher, for disk
units, when `X==1`, dump the block being written.

```
name=blkwr at=DF09 every when=x==1,z8(2d)~{4,5,9,a,b,c} cap=40000
  do=field=unit:z8(2d),field=blk:z16(34),field=len:z16(32),mem=z16(30):512
```

The read side (`UNITREAD` logged at `$DF04`, first read per block) is
`name=blkrd at=DF09 exit=DF04 every when=x==0,z8(2d)~{4,5,9,a,b,c}
do=field=unit:z8(2d),field=blk:z16(34) xdo=mem=z16(30):512` — the
"first read per block" dedup is a consumer-side filter on `f.blk`.

**CSP-6 eval-stack trap** (`WIZ5_CSP6_OUT`): at the CSP handler, when the
sub-opcode after the interpreter PC is 6, dump 40 bytes of page-1 stack,
zero page `$00–$5F`, and 40 bytes at every pointer in either that lands in
`$0800–$BFFF`.

```
name=csp6 at=D681 every when=m8(z16(9e)+1)==6 cap=4000
  do=field=ipc:z16(9e),stack=40,zp=00-5f,scan=stack:0800-bfff:40:24,scan=zp:0800-bfff:40:24
```

## Worked example: which file did ProDOS fail to open?

The Apple IIgs Dealer Demo with only its first disk mounted stops at "file not
found". The MLI is reached by `JSR $BF00` followed by a command byte and a
parameter-block pointer, so:

```
A2GSPU_TRAP="name=mli at=BF00 every do=field=cmd:m8(s16(1)+1),mem=m16(m16(s16(1)+2)+1):40"
A2GSPU_TRAP_OUT=mli.ndjson
```

Every MLI call lands as a record with its command number, and for `OPEN`
(`cmd=200`, `$C8`) the 40 bytes at the pathname pointer are the
length-prefixed pathname. The last `OPEN` before the error screen is the file
it wanted — no disassembly, no symbol table, no C++.

What it actually returned, 1,150 records over 1,000 frames, sixteen distinct
`OPEN` targets:

```
frame   84  BASIC.SYSTEM          frame  313  Hello.sound
frame   91  /SD1/STARTUP          frame  331  Bonjour.sound
frame  119  EAGLE                 frame  348  Hola.sound
frame  121  PARAMS                frame  357  Bonjorno.sound
frame  122  ANIMATE               frame  377  Screen.6
frame  124  CAST                  frame  392  Screen.11
frame  259  Intro.sound           frame  406  Attract.sound
frame  810  SceneControl          frame  848  zz.Score.P   <- and every 4 frames after
```

The last name arrives with its high bits set (`7a 7a ae d3 e3 ef f2 e5 ae d0`
— the engine builds pathnames in high-ASCII), it follows a `SET_PREFIX`
(`$C6`) to `/sd2/act1/sc1/`, and the error screen's *Hit any key* loop
retries it every four frames. The second disk is `/SD2`. One env variable,
one run, and the trap's close-out line says how many times it fired.
