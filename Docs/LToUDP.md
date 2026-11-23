# LocalTalk-over-UDP (LToUDP)

Layer 2 of gsquared-localtalk in this fork. Additive: the layer-1
Z8530 SDLC / hunt / `$C044`/`$C045` idle path is unchanged when no peer is
present. This is a cable model, not instrumentation — it is on by default,
bound to loopback only, and turned off with `A2GSPU_LTOUDP=0`.

## Wire format

Compatible with the common emulator LToUDP convention (Mini vMac, TashTalk,
Snow, POM68K; confirm MAME when comparing):

- UDP multicast group `239.192.76.84`, port `1954` (override with
  `A2GSPU_LTOUDP_PORT`).
- Datagram: 4-byte little-endian sender id, then raw LLAP with no SDLC flags
  and no FCS: `dest`, `src`, `type`, 0..600 data bytes.
- Drop datagrams that carry our own sender id (pid xor a high-resolution
  clock, never zero).

### LTG1 guest-time extension (optional)

A plain LToUDP peer keeps the classic 4+LLAP datagram and schedules in host
time. That mode is supported and is non-deterministic: the peer's CTS wait,
IDG backoff, ATP retries and PAP tickle run in wall time, which cannot
agree with the guest-time cable.

LTG1 clocks the peer from the cable. The emulator stamps every LLAP it
sends with guest C14M time, and emits idle time beacons about every 1 ms of
guest time (or replies when the peer asks). Each peer datagram may carry
its intended guest delivery time; the cable delivers at
`max(that time, line idle)`. At most one peer frame is on the cable at a
time (half duplex), so the inbox cannot pile up and no frame is scheduled
from a stale eof.

Wire forms, ignored by a peer that only handles known LLAP types:

- Time control: `sid` + `dest=$00` `src=$00` `type=$C0` + `"LTG1"` +
  `cmd` (0 = request, 1 = beacon/reply, 2 = delivered-at, 3 = no-reply) +
  `now_u64le` + `idle_u64le` + optional `delivered_u64le` +
  `grant_u64le` + `accounted_u64le` + optional `seq_u64le`. Command 2 reports the last-byte RX
  tick of a peer frame that just finished clocking in; beacons may
  carry the same word so a missed command 2 is still visible. Command 3
  tells the cable the peer will not answer this solicited dialog.
  Command 4 is a NACK: `now` is the first missing per-sender sequence
  number, and the addressee resends that datagram and every later one
  still in its send window, with the original guest-time stamps.
  `grant` is "nothing from me before T." `accounted` is the eof tick of
  the last guest frame the grant promises silence for (see Conservative
  lockstep). `seq` is the sender's monotonic datagram sequence (time
  controls and LLAP share one counter per sender). Older 2-, 3-, 4-, and
  5-word controls still parse; a missing `accounted` or `seq` is 0
  (unsequenced; no gap detection). dest 0 is not a LocalTalk node; this is never
  injected into the SCC.
- Extended LLAP, only after a peer has advertised LTG1 (by sending a time
  control or an extended frame): `sid` + `"LTG1"` + `now_u64le` +
  `due_u64le` + llap. A sequenced speaker inserts `"SEQ1"` + `seq_u64le`
  between `due` and the llap (`sid` + `"LTG1"` + now + due + `"SEQ1"` +
  seq + llap). A parser that sees `"SEQ1"` at that offset uses the
  sequenced form; otherwise the older 20-byte prefix still applies.
  A plain peer never sees this form.

The test harness's peer speaks LTG1. The CTS window is a pure function of
guest stamps, not the 1 ms beacon clock: `cts_eof` is the last-byte tick
of the CTS (official emu stamps `now=tx_eof`, `due=0`, so `now` is the
eof; if `due` is non-zero it is the start tick and eof is
`due+(n+2)*BYTE`). The cable reports `rts_delivered_end` (the last-byte
RX tick of our RTS) as a command-2 control or in the next beacon, but
the peer already knows its own RTS eof (`due+5*BYTE`) and must use that
scheduled eof to open the window — a CTS may arrive before command 2,
and command 2 must not be the only way to accept it. The guest CTS
datagram carries its own TX eof stamp (the guest is frozen in collect
right after sending it); the
peer accepts the CTS iff `-CTS_FRAME <= cts_eof - rts_delivered_end
<= TIFGap1 + CTS frame time` (TIFGap1 is 448 us, 32*14 cycles at 1 MHz;
a 5-character control frame is 2485 C14M ticks, so the window is 8899
ticks; a leftover CTS whose eof is more than one CTS-frame before this
RTS is rejected, and the peer emits a command-3 no-reply).
`rts_delivered_end` is the last-byte RX tick of *this* RTS, never an
earlier in-flight peer frame. After any TX the peer waits for that
frame's command-2/beacon before starting a new `send_directed`, and
`due_now()` then starts at that eof+IDG. Binding a previous frame's eof
(for example a Tickle TResp still clocking in when a SendData RTS is
queued) makes a CTS that is inside the real RTS window look late; the
peer must not treat that as a collision or silently skip the CTS. When
the beacon clock passes the window bound, the peer still waits for any
datagram already stamped inside the window (it must not time out while
the CTS is still in flight in host time). Data then goes back as the
solicited reply, scheduled at `cts_eof+IFG`. If a guest RTS arrives mid-
`send_directed`, the peer CTSs it immediately and collects the guest's
solicited data before retrying its own RTS (retrying at the guest RTS
eof, or at CTS+IFG, would collide with that data). Backoff is from the
400 us inter-dialog gap in guest us; ATP and PAP timers are in guest
seconds. Until a beacon arrives the peer stays in the classic host-time
mode.

LLAP types used here: `$01` short DDP, `$02` extended DDP, `$81` ENQ, `$82`
ACK, `$84` RTS, `$85` CTS.

## Loopback / local only (hard constraint)

Default bind and membership are **127.0.0.1**. `IP_MULTICAST_IF=127.0.0.1`,
`IP_ADD_MEMBERSHIP` only on that interface, `IP_MULTICAST_TTL=0`,
`IP_MULTICAST_LOOP=1`, `SO_REUSEADDR` so two GSSquared instances and a
Python peer on the same host can share the port. Packets must not leave the
host. The implementation never joins a real NIC. If a 127.0.0.1 bind is
rejected, it may fall back to `0.0.0.0` **only** while still sending and
joining on 127.0.0.1 with TTL 0.

Two GSSquared processes, or one GSSquared and a test-harness peer, are
the network.

## Guest path

On the guest, a complete SDLC/LLAP frame is the data bytes written while the
transmitter is in SDLC mode, ended by Tx Underrun/EOM (the closing flag and
CRC are not in that buffer). That payload is one UDP datagram.

An incoming datagram is injected as: flag found (leave hunt), address search
against WR6 or `$FF`, the LLAP bytes, then CRC-16/X.25 (reflected poly
`$8408`, init/xorout `$FFFF`, low byte first) over dest+src+type+data. RR1
D7 (End of Frame) is visible when the last CRC byte is the next available
FIFO character, before it is read — ATP1 `RecvBytes` / `LastByte`
(lap_includes.aii) tests RR1 that way and does not consume the last FIFO
byte. The 3-byte SCC FIFO is fed from a pending queue so a long DDP frame
does not overrun. `$C044`/`$C045` are pulsed once per accepted frame.

ATP1 `RecvPacket` waits only about 200 µs (`TIFGap1`) after a transmit for
the first reply byte. A Python peer cannot answer in guest time that short,
so `tx_complete` polls the socket for a few milliseconds of **host** time
(the guest clock is frozen) and injects any ACK/CTS, or an already-queued
RTS+data burst, before returning.

Send abort (WR0 command `011`) discards a partial SDLC frame without a
datagram.

## ENQ counts (not a storm)

A free workstation node does not stop after TryNode's 20 ENQs. Apple's
`XmitPacket` treats a quiet reply to an ENQ as a collision and retries.

- `TryNode` (`lap_includes.aii:1031-1065`; shipped `ATP1.ATROM` flattened
  main image `$11B8` `LDX #$0078` for a server, `$11BF` `LDX #$0014` for a
  workstation) sends ENQ, then looks at `addrUsed`. It repeats 20 times
  (120 for a server) or returns early if `addrUsed` is set.
- After each of those ENQs, `XmitPacket` (`lap_includes.aii:2383-2406`;
  same image `$18A5-$18AF`) calls `RecvPacket`. If the type is `LAPEnq`
  (`$81`) and `addrUsed` is still 0, it "pretend[s] a collision, keep[s]
  trying enquiries" (`beq Collision`) and retries up to `colTries` = 32
  (`ldx #$2020` at `$1838`, stored into `colTries`/`defTries`).
- A taken node (an ACK, or any other frame to that address, sets
  `addrUsed`) returns from `XmitPacket` immediately, so TryNode stops
  after a small number of ENQs (1 when the ACK is seen on the first
  `RecvPacket`).
- A free node therefore emits exactly 20 × 32 = **640** ENQs. That is
  Apple's CSMA path on a quiet line, not a loop of our own frames.

If the sender-id filter failed and the guest accepted its own ENQ,
`RecvPacket` would set `addrUsed` on the first try, `NewNode` would walk
`$02`, `$03`, … and the peer would see those destinations. A harness test
asserts the 640-count on the free node, a small count on the ACKed node,
and no other ENQ destinations.

## Half-duplex line (one frame on the wire)

LocalTalk is half-duplex: one SDLC frame occupies the cable at a time, and
the next frame cannot start until the previous one has been received (EOF)
and the interframe gap has passed. RTS/CTS is the handshake the *sender*
uses before a data frame (`XmitPacket` `ChkCTS`); the cable itself only
serializes frames.

UDP can deliver a burst of complete LLAP datagrams that would have been
one-after-another on a physical LocalTalk line. `ltoudp_inbox` is the
cable model of that: complete incoming frames wait, and
`ltoudp_try_inject` runs only when `ltoudp_rx_ready` is true (SDLC
receive enabled, `rx_pending` empty, the 3-byte FIFO empty or holding
only the leftover last-CRC / EOF byte of the previous frame). The guest
then sees one frame, then the next, just as on a half-duplex wire.

That is not a substitute for handshake timing. A well-behaved sender
(ATP1, or the Python peer) still waits for CTS before putting the data
frame on the line. The inbox only serializes frames that have already
been sent.

## RecvInit and a held data frame

ATP1 `fXmitLAP` writes WR3 `DisRx` before it transmits CTS, then falls
into `RecvInit`. `RecvInit` writes WR3 `DisRx` again, WR0 ResetErr, WR0
`$23` (command 100, Enable Int on Next Rx Character, and point at WR3),
and finally WR3 `$DD` (`EnbRxSlv` + Enter Hunt). `writeCmd`'s
`update_queues` runs *before* that last write, so a drain from the
register access itself still sees receive disabled.

On the real cable the peer's data frame physically follows our CTS and
arrives after `RecvInit` has re-enabled the receiver. The guest then has only
`RecvPacket`'s ~200 µs `TIFGap1` to see `RR0` RxAvail. Immediate injection
from `write_register_3` once WR3 has set `r3_rx_enable` was a host-time
patch so a held inbox frame would be in the FIFO before `TIFGap1`. That
edge can also land *during* RecvInit, where PacketIRQ discards a data
TReq, which is Path S below. It is superseded by the guest-time cable
in the next section. First-character mode is still re-armed only by WR0
command 100 (`$20`), not by EOF.


## Guest-time cable (design)

The 5 ms host-time freeze in `tx_complete` and the immediate inject at
WR3 RX-enable are host-scheduled patches. They race `RecvPacket`'s
`TIFGap1` (~200 µs of *guest* time) and RecvInit / PacketIRQ. That is
why `rpm_pap` is not deterministic on the same exe: a run can take
Path L (Open ~1.5M instructions, Data + eof + CloseConn) or Path S
(Open ~67k–169k, no TResp). ROM 01 and ROM 3 both flake this way
(one harness run took Path S and failed; another took Path L and passed). Stop patching
the race. Model the cable in guest time.

### Path S under this model

Path S is not a second GS/OS Open implementation. It is the guest
taking a different branch because a reply arrived early or late in
host time.

ATP1 `fXmitLAP` writes WR3 `DisRx`, transmits CTS, then RecvInit
re-enables RX and `RecvPacket` waits `TIFGap1` (~200 µs) for `RR0`
RxAvail. PacketIRQ only accepts LAP control frames (RTS / ENQ); a data
TReq already in the FIFO during RecvInit is discarded (`ExitPktIRQ`).
The WR3-enable `ltoudp_drain(0)` injects a held ATP at that RecvInit
edge, *before* TIFGap. If the inject lands then, or a leftover RTS is
at the inbox head, `RecvPacket` never sees the credit. Open still
returns success (OpenConn completed, `rpmopenflag=1`,
`writeready=$0004`) after ~169k instructions, and the later Write's
SendData misses the same window (the peer retries 57 times, the guest
CTSs, no TResp). Path L is the same Open when the credit lands inside
TIFGap: `RecvPacket` accepts it, Open processes the printer's
SendData, ~1.5M instructions, and the later Write also catches TIFGap.
`$0103` (`qtrnotfound`) after Close with the flag already 0 appears on
both paths and on passing official ROM 01 runs; it is not the
CloseConn blocker.

A scheduler that delivers every solicited reply at TX EOF + IFG
(~200 µs) in guest time, inside TIFGap, should make Path S disappear.

### Rules

1. **Solicited replies.** When the guest finishes transmitting a frame
   the protocol answers, hold guest time (frozen, like today's
   `tx_complete` drain) until the collect ends, then deliver that frame
   at the protocol-correct *guest-time* offset: the inter-frame gap,
   about 200 µs, which is inside TIFGap. Do not inject at the WR3-enable
   edge, and do not inject at the end of the host poll.

   Under LTG1 the guest clock is the authority. A solicited collect
   ends only on guest-time evidence: a reply stamped for this transmit,
   or a peer beacon or explicit no-reply (command 3) whose `now` is
   past the CTS/IFG window (for CTS: `eof+IFG`; for RTS/ENQ:
   `eof+TIFGap1+5*BYTE`). A generous host safety bound (2 s) is only a
   valve: it may fire only after 2 s of *no messages* (a real peer
   crash or a spec violation). A `TIME` control or frame from a living
   peer resets it. A scripted guest or test harness is under the same
   rule: a host-time wait must not decide a collect outcome. If the
   bound expires, the cable logs `collect-peer-fault` and treats
   it as a no-answer, never as a silent collision. The fix is not a
   bigger host timeout; Windows timer/scheduler granularity is about
   15.6 ms unless `timeBeginPeriod` is set, and a Python peer's recv
   plus `sendto` can miss a 5 ms freeze even when the data is correctly
   stamped at `cts_eof+IFG`.

   A plain LToUDP peer (no LTG1) keeps the documented 5 ms host-time
   collect.

   Answered frames:

   - ENQ (`$81`) answered by ACK (`$82`), or by silence (free node) if
     the host timeout expires.
   - RTS (`$84`) answered by CTS (`$85`).
   - CTS (`$85`) answered by a data frame (the ATP / DDP payload).
   - A data TReq answered by a TResp (DDP type 3, ATP function TReq).

   The hold models "the other station is on the cable." For a plain
   peer the 5 ms timeout models "no answer." For an LTG1 peer, "no
   answer" is guest-time evidence or a logged peer fault. Results are
   deterministic for a given LTG1 peer.

2. **Arrival time.** On the real cable the reply is clocked after our
   closing flag and the IFG. RecvInit is a short register sequence,
   much less than 200 µs, so RX is on when the bits arrive, and
   `RecvPacket`'s WaitIFG is the window that sees them. At the
   scheduled guest time, inject only if the channel is SDLC, RX is
   enabled, and the line is idle (`ltoudp_rx_ready`). If RX is still
   off, the frame is lost, as it would be on the wire. 200 µs at
   14.31818 MHz is 2864 C14M ticks.

3. **Unsolicited frames.** Datagrams that are not the answer to a
   just-finished transmit (a peer's RTS starting a new dialog, a late
   retry after we have moved on) are delivered at a frame boundary
   when the line is idle. A new dialog (RTS or ENQ) is separated by
   the inter-dialog gap, 400 us (5727 C14M ticks), not merely one IFG.
   Piling an RTS at eof+IFG onto a just-received data frame overruns
   PacketIRQ (needData is still set, RecvInit still running). Solicited
   replies inside a dialog stay at eof+IFG. Unsolicited frames are not
   injected mid-frame or during our transmit.

4. **Half-duplex.** One frame on the virtual wire at a time. The inbox
   may hold a second datagram until the first has left the FIFO, then
   schedule it one IDG later if it starts a new dialog (RTS/ENQ), or
   one IFG later otherwise.

5. **Layer 1 unchanged.** With no peer, no datagrams arrive,
   `$C044` / `$C045` stay 0 after the pulse-less idle path, and the
   Finder still lands. First-character mode is still re-armed only by
   WR0 command 100, not by EOF.

### What this replaces

`tx_complete`'s host-time poll stays only as the *collection* wait
for a solicited datagram (guest frozen until the collect ends). For a
plain peer that wait is still 5 ms. For an LTG1 peer it is the 2 s
safety bound, ended early by guest-time evidence as above. It is no
longer the injection point. `write_register_3`'s immediate
`ltoudp_drain(0)` after RX enable is not the solicited-reply path;
keeping a non-blocking drain on register access / `update_queues` is
fine for pulling the socket, not for jumping a held reply into the
FIFO at RecvInit.

Host regression (`apps/sccltoudptest`) must cover: a CTS reply is not
visible until 2864 C14M ticks after TX complete, then RR0 RxAvail; a
host timeout with no peer injects nothing (plain ENQ still waits ~5 ms);
an unsolicited frame waits for an idle line; an unsolicited RTS/ENQ
after a previous frame is due at that frame's eof+IDG, not eof+IFG;
RX-off at fire time loses the frame; a late solicited reply under LTG1 (arriving after 5 ms of
host time) is still delivered at `cts_eof+IFG`; a no-reply beacon
(command 3, `now` past the window) ends the collect without waiting
the 2 s safety bound.

Implement additively in this fork, rebuild in a 0-emulator
gap, and prove with `rpm_pap` three identical Data+eof+CloseConn runs
per ROM before rerunning the goldens.

## Environment

| Variable | Default | Meaning |
|---|---|---|
| `A2GSPU_LTOUDP` | on | `0` / `off` / `no` / `false` disables the socket. |
| `A2GSPU_LTOUDP_PORT` | 1954 | UDP port for the group. |
| `A2GSPU_LTOUDP_TRACE` | off | Set to a log file path to enable collect/inject (and peek) traces. Any non-empty value enables the gate; the value is the file `fopen`ed for the log. |
| `A2GSPU_LTOUDP_PEEK` | on when TRACE is on | `0` / `n` / `f` disables the guest-memory peek from the SCC/LToUDP trace path. The hook is `z85c30_query_guest_peek` (`probe_peek`, no cycles). It reports PacketIRQ `needData` / `addrValid` / `busyFlag` / `pktError` / `gotCTS` / `gotData` and whether a GetRequest is posted on socket `$81`. |

Layer-1 idle boot (no peer) still reaches the Finder: no datagrams arrive,
`$C044`/`$C045` stay 0 after the pulse-less idle path.

## Test peer

The test harness's peer (a Python script) on the same host uses the same bind rules.
`--selftest` checks the CRC vector (`123456789` → `$906E`) and a
loopback multicast ENQ between two sockets. By default the peer ACKs only
the first ENQ destination (so the guest must acquire a different node),
answers RTS with CTS, and replies to NBP BrRq/LkUp with one tuple
(a fixed test server name unless overridden). Directed transmissions use `send_directed()`: lapRTS, wait for the
addressee's CTS (stamp window once LTG1 is up; a host bound otherwise),
then the data at `cts_eof+IFG`. There is no RTS+data burst. The other
direction is symmetric: a guest RTS is answered with a solicited CTS
due at `rts_eof+IFG`, including when the peer is mid-`send_directed`.
The solicited reply owns the line during that dialog; it is not queued
behind an unsolicited peer frame (a SendData credit already on the due
queue is parked in the inbox and runs after), and a later due stamp on
the CTS datagram does not delay it past `eof+IFG`. `--selftest` covers a
CTS stamped inside the window that arrives late in host time (accepted),
a CTS stamped outside the window (rejected, and a command-3 no-reply is
sent), a prior in-flight TX eof not used as `rts_delivered_end` (a CTS
inside this RTS's window is still accepted), and a guest RTS mid-
`send_directed` CTSd immediately at `rts_eof+IFG`. The peer logs the
wall-clock gap from CTS receipt to
`sendto`; the emulator logs `reply_host_us` from RTS/CTS send to the
datagram arriving. A plain LToUDP peer that does not speak LTG1 still
works, in the non-deterministic host-time mode (5 ms collect).

## Conservative lockstep (LTG1 peers)

Under LTG1 the guest must never get ahead of the peer on host time.
A Python peer's UDP/sleep gap used to leave the guest running at
`A2GSPU_PACE=fast` until collect started, so Close and other waits
accumulated a host-dependent number of instructions before a frame
landed. Plain (non-LTG1) peers keep the documented host-time collect
(5 ms) and are non-deterministic. This section is the discrete-event
protocol both sides must follow so official results are a pure
function of guest time.

Constants (C14M = 14,318,180 Hz), matching `lap_includes.aii` and
the cable model above:

| Name | Guest time | C14M ticks |
| --- | --- | --- |
| IFG | 200 us | 2864 |
| IDG | 400 us | 5727 |
| BYTE (230.4 kbps FM0, 8 bits) | ~34.7 us | 497 |
| TIFGap1 (32*14 cycles at 1 MHz) | 448 us | 6414 |
| CTS / ENQ-ACK window | TIFGap1 + 5*BYTE | 8899 |
| Idle lookahead | 500 ms | C14M/2 |
| IDG backoff cap | 15 ms | 214773 |
| LLAP collision tries | 32 | — |
| Safety (crash only) | 2 s host | — |

### Grant semantics

A grant T means: "this peer will not originate a new frame with due
< T." Grants are monotonic: a newly issued grant is never less than
the last issued grant (`last_grant`). A grant behind the current
C14M is stale and is ignored; it must not be installed as "now."

Each LTG1 time control (`TIME_REQ` 0, `TIME_BEACON` 1,
`TIME_DELIVERED` 2, `TIME_NOREPLY` 3) carries `(now, idle,
delivered, grant)`. An LTG1 LLAP carries `(now, due)` and no grant
word; the emulator treats an in-hand LLAP as an implicit grant
through `due + (n+2)*BYTE + IDG`, or through the reply deadline
below if that frame expects a guest reply (RTS, ENQ, data that
expects ACK/CTS/TResp), whichever is later. Older 2- and 3-word
controls still parse; a missing grant does not enable lockstep.
The first non-zero grant enables it.

Lookahead is at least one IFG. An idle grant (no pending origin) is
`max(due_now, last_grant) + 500 ms` so a rail `run N` can finish in
host time. After the peer has put a frame on the socket, T must move
at least to that frame's on-wire end plus one IDG, and if the peer
expects a guest reply (RTS waiting for CTS, ENQ waiting for ACK),
T must also cover the reply window (`eof + TIFGap1 + 5*BYTE` for
RTS/ENQ; `eof + IFG` for a CTS waiting for data) so the guest can
receive, reply, and have that reply accepted. Heartbeats extend
`last_grant` because `clock.now` is frozen while the guest is
blocked at the horizon and cannot `TIME_REQ`.

#### Null-message rule (lookahead invalidation)

A grant can only promise silence given the inputs the peer has seen.
Any guest frame is new input that can trigger a peer reaction, so it
invalidates all lookahead beyond that frame's eof plus the peer's
minimum reaction time. Every guest TX, collected or not (broadcast
data, NBP LkUp, a TReq with no collect, everything), clamps the
emulator horizon to `eof + IDG`, or to `eof + IFG` where a solicited
reply is possible.

The peer then owes, for every guest frame it receives, either a
frame or a fresh grant computed after processing that frame. Grants
are stamped with the last guest frame they account for (the frame's
eof tick, or an input sequence). A grant that does not account for
the latest guest TX is not trusted past the clamp. A grant with no
stamp (older 4-word controls, `accounted = 0`) accounts for nothing,
so it cannot lift a clamp. An in-hand peer LLAP is a frame (the
obligation is met) and may implicitly extend the horizon past the
clamp. The clamp also lifts on a stamped grant or on `TIME_NOREPLY`
that accounts for that TX.

Idle lookahead (the 500 ms heartbeat extension of `last_grant`) is
likewise invalidated by a new guest frame. A fresh grant after that
frame is computed from its eof (the handshake: eof+IDG, or the CTS/ACK
window for an originated RTS). The peer must not re-extend leftover
`last_grant` across that frame, and must not attach idle lookahead to
the grant that follows an originated frame if another origin is still
queued (Tickle TResp then SendData). Idle lookahead is only for when
the peer has nothing more to send.

### When the emulator may advance C14M

C14M may advance up to the current live horizon (the last non-stale
grant, including implicit extensions and after a clamp is lifted).
It must not advance past the horizon. `A2GSPU_PACE=fast` must not
change any guest-visible arrival tick.

The one exception is solicited-reply collect: after a guest TX that
expects a reply the CPU is parked (C14M frozen) until a collect-end
event below, so the reply can be scheduled at EOF+IFG. Because the
guest is frozen, the peer's `clock.now` cannot walk past the window
by itself. The peer must therefore end the collect with a message
that does not depend on further guest-time progress: a reply frame
or `TIME_NOREPLY`.

The 2 s host safety (`horizon-peer-fault` or `collect-peer-fault`)
may fire only if no peer message that advances the wait has arrived
in 2 s of host time — a real peer crash, or a violation of this
spec. A correct, living peer never lets either counter be non-zero
in an official PAP or AppleTalk run.

### Collect types and end events

Deadline is computed from the guest TX eof, in C14M.

| Guest TX | Expects | Deadline | Collect ends on |
| --- | --- | --- | --- |
| ENQ ($81) | ACK | eof + TIFGap1 + 5*BYTE | ACK in inbox; `TIME_NOREPLY` |
| RTS ($84) | CTS | eof + TIFGap1 + 5*BYTE | CTS in inbox; `TIME_NOREPLY` |
| CTS ($85) | data LLAP | eof + IFG | data in inbox; `TIME_NOREPLY` |
| ATP TReq | TResp | eof + TIFGap1 + 5*BYTE | TResp in inbox; `TIME_NOREPLY` |

Reasons: `reply` (schedule at eof+IFG or the frame's due), `noreply`
(a normal no-answer: ENQ to a free node, RTS not for this peer),
`peer-fault` (2 s host safety only).

An idle-lookahead grant (500 ms) is always past the reply window
(~622 us). A beacon whose grant is past the deadline therefore must
not end collect by itself: that would drop a CTS or ACK already on
the wire. The peer's obligation is to send `TIME_NOREPLY` when it
will not reply. The eof+IFG clamp lifts when the reply is in hand
or when `TIME_NOREPLY` arrives (and carries a grant past the
window).

### Peer obligations

Every peer decision is a pure function of guest time and received
frames: IDG backoff, LLAP retries, NBP/ATP/PAP retry timers, XO
release, SendData credits, and beacons all count in guest ticks (or
guest seconds derived from C14M). `time.time`, `sleep`, and socket
timeouts must not decide protocol behaviour. Socket timeouts remain
only as I/O polling (a short recv timeout to wake the serve loop).

Before the peer waits for a guest-time event T_wait, it must already
have sent a grant >= T_wait (unless `last_grant` is already past
it). Otherwise both sides wait: C14M is frozen, `clock.now` never
reaches T_wait.

Consequences:

- After every guest frame: send a reply frame, or a fresh grant
  computed after processing that frame, stamped with the frame's eof.
  A leftover idle-lookahead grant issued before the frame is not that
  message.
- Skip ACK or skip CTS: immediately `TIME_NOREPLY` with a grant past
  the reply window, stamped as accounting for the guest TX.
- A non-matching NBP lookup: the same `TIME_NOREPLY` + stamped grant,
  so the guest is not left in a silent wait.
- `send_directed`: IDG backoff and the 32 LLAP tries are guest-tick
  counters (start at one IDG, double up to the 15 ms cap). After RTS,
  grant through the CTS window; accept a CTS iff
  `cts_eof - rts_delivered_end` is inside the guest-time window
  (see `cts_in_window`). When guest now has passed the window, retry
  or fail. A zero-timeout drain of the socket at that instant may
  pick up a CTS already on the wire; there is no host-time "in
  flight" wait and no host-time cap that decides the outcome.
- While waiting, I/O polls heartbeat a grant (a `TIME_BEACON` that
  extends `last_grant` or covers T_wait), never a bare `TIME_REQ`
  with no grant. If the guest sends `TIME_REQ`, answer with a beacon
  and a live grant.
- On a valid CTS answering the peer's own RTS, data is due at
  `cts_eof+IFG` (`cts_eof` is the last-byte tick, above), with a
  grant past the data frame end + IDG, stamped as accounting for
  that CTS. Consuming the CTS inside `send_directed` does not
  discharge the "frame or fresh grant after every RX" obligation —
  the data frame *is* that frame. A leftover grant issued before
  the CTS cannot lift the guest's CTS clamp (eof+IFG). If the CTS
  is out of window, the peer still owes `TIME_NOREPLY` (or a fresh
  grant) accounting for it; the guest is parked in collect and a
  host-time timeout must not end that wait while the peer is alive.
- If the guest wins the line (RTS to us during our try), CTS at
  `rts_eof+IFG`, collect the guest data in guest time, then back off
  in guest ticks.
- ATP XO release (30 s), ATP requester retry (1 s), PAP SendData
  retry (2 s) and the 120 s tickle window, and PAP/ASP tickles, are
  all measured with `clock.seconds` (`now / C14M`). `poll()` runs
  when guest time has advanced, not because a recv timeout fired.

### Reliability (sequence, one inbox, NACK)

UDP may drop a datagram. A deterministic guest-time protocol cannot
tolerate a lossy transport underneath it: a lost CTS after an RTS
leaves the guest parked at the CTS clamp and the peer waiting in
`send_directed`, and a seed-dependent race on loopback multicast is
enough to change the guest-visible trace.

- Every LTG1 datagram (both directions, including time controls)
  carries a per-sender monotonic `seq`. Receivers log any gap.
- Each socket has one receive dispatcher. It drains the UDP socket
  into an ordered inbox and a small reorder hold. Every waiter
  (the serve loop, `send_directed`, `wait_pending_tx`,
  `wait_guest_data`, a scripted guest collect) consumes from that
  inbox. Nothing is discarded unread.
- `SO_RCVBUF` is set generously (1 MiB) so 1 ms beacons are less
  likely to overflow the Windows loopback multicast receive buffer.
  Remaining drops are counted by sequence gaps, not guessed.
- A sequence gap triggers a command-4 NACK naming the first missing
  seq. The sender resends that datagram and every later one still in
  its send window, with the original guest-time stamps, so a resend
  never changes guest-visible arrival ticks. Duplicates (seq already
  delivered) are ignored. Out-of-order arrivals wait in the hold and
  are released in seq order once the hole is filled.
- While a guest is parked in collect, it continues to emit sequenced
  beacons so a lost last frame (a CTS with no follow-up LLAP) is
  still visible as a gap on the next beacon.

The test harness's peer implements this. A selftest injects a dropped
CTS and still observes data due at last-byte `cts_eof+IFG`. The official
emulator implements the same rule: it parses and emits `SEQ1` after the
peer advertises it, keeps a single ordered inbox with a reorder hold,
NACKs a gap (command 4) and resends from the send window with the
original guest-time stamps, and emits sequenced keepalives from
`ltoudp_collect` / horizon wait (no guest-time effect: grant=0). Plain
peers stay on classic 4+LLAP. `apps/sccltoudptest` covers injected
drops in both directions (a dropped peer CTS or data frame, a dropped
guest frame, a dropped time control) with a guest-tick trace identical
to the no-drop run. NACK and resend counts belong in `run.log`, not in
official `.out`.

A seq is assigned only when it is on the wire (a 6-word time control, or
`SEQ1` after advertisement). Numbering an untagged LTG1 or classic 4+LLAP
leaves a permanent hole: the neighbour parses `seq=0`, then later `SEQ1`
frames look like a gap that resends can never fill. A NACK is sent once
per missing seq; a re-NACK is allowed after 50 ms if the hole remains
(lost-NACK recovery). NACK-on-every-later-datagram is a send storm.

### Deadlock freedom

At every wait, one side owes the other a message that advances time.
The debtor can emit that message without waiting for the other
side's clock to move first, or it has already granted the time it
is waiting for.

| Wait | Debtor | Owed message |
| --- | --- | --- |
| Guest blocked at horizon T | Peer | Beacon or frame with grant > T, stamped for the latest guest TX if any (heartbeat; no guest `TIME_REQ` required) |
| Guest blocked at the post-TX clamp (eof+IDG, or eof+IFG if a solicited reply is possible) | Peer | A frame, or a fresh grant stamped as accounting for that guest TX. An unstamped or stale-accounted grant cannot lift the clamp |
| Collect after ENQ | Peer | ACK, or `TIME_NOREPLY` |
| Collect after RTS | Peer | CTS at eof+IFG, or `TIME_NOREPLY` |
| Collect after CTS | Peer | data due at last-byte `cts_eof+IFG`, stamped as accounting for the CTS, or `TIME_NOREPLY`. `send_directed` consuming the CTS still owes that data frame; command 2 is not a prerequisite for accepting an in-window CTS |
| Collect after TReq | Peer | TResp, or `TIME_NOREPLY` |
| Peer waiting for CTS after its RTS | Guest, once C14M can run | CTS inside the window. Peer has already granted through the window, so C14M can reach it |
| Peer in IDG backoff until T_back | Peer, then guest | Peer first grants >= T_back; then C14M runs and `clock.now` reaches T_back |
| Peer waiting for delivered-at of its TX | Emulator | `TIME_DELIVERED` when C14M reaches due (the in-hand frame already extended the horizon through the frame end) |

If every wait has a unique debtor who can fire, the 2 s host safety
can only fire when the peer (or emulator) has crashed or violated
this spec.

`apps/sccltoudptest` covers a slow vs fast peer giving the identical
guest-tick delivery, a tight grant with no follow-up beacon, a stale
grant that must not cap at now, ENQ collect ended by `TIME_NOREPLY`,
and an uncollected guest TX (NBP-style data) whose leftover idle
grant cannot lift the eof+IDG clamp until a grant accounts for that
TX. The test harness has a property test:
the same scripted session (ENQ / NBP / OpenConn / Data / CloseConn)
with randomised host delays and scheduling jitter on both sides, N
seeds, requiring byte-identical guest-tick traces and zero safety
faults.

## Guest clocks, QtrSched, and PAP CloseConn

AppleTalk `QtrSched` (ATP_includes.aii) catch-up-bursts `tickcount` when
it sees a jump of 10 or more seconds in the IIgs RTC. Unpinned RTC
seconds used to follow host `time()`, so a 2 s LToUDP collect-peer-fault
or a rail HALT, with C14M frozen, piled up host time and fired that
catch-up after several faults without a QtrSched in between. PAP
`conntimer` (480 quarter-seconds) could expire in a fraction of a second
of CPU, and ATP/close completions that wait on quarter-second ticks
could fire immediately.

By intent, the VGC 1/4-second and 1-second interrupts, and the unpinned
RTC *seconds* that `QtrSched` reads, are paced by emulated 14M time
(C14M), like VBL. The time-of-day *origin* is still host wall-clock,
latched once (see `Docs/fidelity/sound_time.md`). A 2 s freeze with
C14M held still advances guest now by 0 (`sccltoudptest`). Earlier
prompt CloseConns under those catch-up bursts were artifacts of the
clock, not proof that Close finished in honest guest time.

The workstation also keeps a PAPRead outstanding from Open (RPM
`newread` / `rpmreadcomp`). A LaserWriter holds that SendData until it
has something to say, and answers with Data + eof once the job has
ended. The harness's PAP peer used to reply immediately with empty eof=0, which
re-issued PAPRead in a storm and never sent the read-side eof TResp, so
closephase stayed 1 and CloseConn never went out once the clock was
honest. It now holds the PAPRead (first typeless sequence, tid $0003
from $01:$84 in the measured runs) until the workstation's Data TResp
carries eof, then answers with Data eof=1. Official no-bp
harness PAP print test on this build then prints and closes on
both ROM 01 and ROM 3.

