; ===========================================================================
; hwtruth.s -- hardware facts the emulator has to be told, measured on a IIgs.
; ===========================================================================
; Docs/fidelity/README.md keeps a list of behaviours no document settles. Each
; test here records RAW bytes and nothing else: this program does not decide
; what a byte means. tools/hwtruth/report.py interprets the bytes and compares
; a real machine's run with the emulator's, so the same binary answers both.
;
; It runs bare: a2engine's ProDOS boot block (diskiigs create --bootable) loads
; the first SYS file to $2000 and jumps here with the boot unit in $43 and the
; block driver's entry at ($48). Nothing else is loaded. On the emulator the
; rail can also `load 00:2000` it directly; with no disk the results block is
; not written (RESBLK stays 0) and the harness reads $1000 instead.
;
; RESULTS PAGE, $1000 (512 bytes, also written to the disk block in RESBLK):
;   $1000  "HWT1"
;   $1004  format version (1)
;   $1005  state: $00 running, $01 every test ran
;   $1006  disk write: $00 not attempted, $80 ok, $C0|code error
;   $1008  records: [id][len][len bytes] ... [$00]
;
; RECORDS (what each measures -- report.py has the reasoning):
;   $01 SLOT6   $C02D, then IWM register reads at $C0EE with $C02D bit 6 clear,
;               then set: status (Q6 on) twice, data register (Q6 off) twice.
;   $02 E0AUX   $E0:4000 / $E1:4000 after a write to $E0 with RAMWRT on; then
;               $E0:4000 read with RAMRD on (from a stub running in $E1).
;   $03 MEGAII  $C041, then $C046 after 4 frames and after 25 frames with the
;               Mega II interrupts disabled, then after 25 frames enabled.
;   $04 SCANCLR $C023 with a scan-line interrupt pending, again after reading
;               $C02E/$C02F, again after writing $C032.
;   $05 C01X    $C011-$C01F as read.
;   $06 GAME    $C061-$C063; paddle 0-3 loop counts (16-bit, $1000 = timeout).
;   $07 BLKMOVE MVN with x=1 across $00FF, then MVP across $0000: final X, Y
;               (16-bit) and the four destination bytes, for each.
;   $08 BANKS   $D0:1234, $D0:5678, $80:1234, $7F:1234, $C036, $FBB3, $FBC0,
;               and IDRoutine ($FE1F): P, A, X, Y.
;   $09 IWMREG  with Q6 on and the motor off: status through an even address,
;               an odd one, even, odd; then with Q6 and Q7 on, an even read; then
;               the handshake register (Q6 off, Q7 on) twice.
;   $0A RTC     polls of $C034 until bit 7 clears for a write transfer and a read
;               transfer (16-bit each; $0000 = gave up), the seconds-lo byte, then
;               BRAM $10-$13 by the two-byte command and by z010ab01, and BRAM
;               $00-$03 by the two-byte command and by z1abcd01.
;   $0B VDA     $C01C (PAGE2) before and after STA $C0FF,X with X=$56: the write
;               lands at $C155, and the 65816's extra cycle puts $C055 on the bus
;               with VDA=VPA=0. PAGE2 on afterwards means that cycle reached I/O.
;
; NOTHING HERE IS DESTRUCTIVE. The IWM is read, with a two-second wait first so
; a boot drive's motor has stopped; its mode register is written once, with the
; value it already holds, because an odd address with Q6 and Q7 on latches the
; bus. The clock chip is only read. Every soft switch touched is put back. SHR
; is on for one test, for a few frames.
; ===========================================================================

            CPU  65816
            ORG  $2000
            LONGA OFF           ; emulation mode throughout, except t_blkmove, which says so
            LONGI OFF           ; (asmiigs otherwise starts with 16-bit immediates)

RES         EQU  $1000          ; results page (one ProDOS block)
REC         EQU  $1008          ; first record
OUTY        EQU  $E0            ; write cursor into REC
LENPOS      EQU  $E1            ; where the open record's length byte is
LENSTART    EQU  $E2            ; cursor value after that length byte
WCNT        EQU  $E3
WLAST       EQU  $E4
WCNT2       EQU  $E5
PNUM        EQU  $E6
CNTL        EQU  $E7
CNTH        EQU  $E8
SAVE        EQU  $1400          ; scratch saves (outside the results page and the markers)

; ProDOS block driver interface, as the boot block left it
DCMD        EQU  $42
DBUFLO      EQU  $44
DBUFHI      EQU  $45
DBLKLO      EQU  $46
DBLKHI      EQU  $47
DRVLO       EQU  $48

COUT        EQU  $FDED
PRBYTE      EQU  $FDDA
CROUT       EQU  $FD8E
HOME        EQU  $FC58
SETVID      EQU  $FE93
SETKBD      EQU  $FE89
INIT        EQU  $FB2F

            JMP  main
RESBLK      DW   $0000          ; patched by build.ps1: the RESULTS file's block
            ASC  "HWTRUTH1"

main
            SEI
            CLD
            LDA  $C082          ; ROM in the language card space (Monitor, IDRoutine)
            ; clear the results page
            LDX  #$00
            TXA
clr         STA  RES,X
            STA  RES+$100,X
            INX
            BNE  clr
            LDA  #'H'
            STA  RES
            LDA  #'W'
            STA  RES+1
            LDA  #'T'
            STA  RES+2
            LDA  #'1'
            STA  RES+3
            LDA  #$01
            STA  RES+4
            LDA  #$00
            STA  OUTY

            JSR  t_c01x         ; first: before anything changes the switches
            JSR  t_banks
            JSR  t_game
            JSR  t_blkmove
            JSR  t_e0aux
            JSR  t_megaii
            JSR  t_slot6
            JSR  t_scanclr
            JSR  t_iwmreg
            JSR  t_rtc
            JSR  t_vda

            LDA  #$00           ; record terminator
            JSR  put
            LDA  #$01
            STA  RES+5          ; finished (before the write, so the disk copy says so)
            JSR  wrresult
            JSR  show
done        JMP  done

; ---------------------------------------------------------------------------
; record helpers
; ---------------------------------------------------------------------------
put         LDY  OUTY
            STA  REC,Y
            INC  OUTY
            RTS
rec_begin   JSR  put            ; A = id
            LDA  OUTY
            STA  LENPOS
            LDA  #$00
            JSR  put
            LDA  OUTY
            STA  LENSTART
            RTS
rec_end     LDA  OUTY
            SEC
            SBC  LENSTART
            LDY  LENPOS
            STA  REC,Y
            RTS

; A = number of $C019 bit-7 edges to wait for (two per frame)
waitedges   STA  WCNT
            LDA  $C019
            AND  #$80
            STA  WLAST
we1         LDA  $C019
            AND  #$80
            CMP  WLAST
            BEQ  we1
            STA  WLAST
            DEC  WCNT
            BNE  we1
            RTS

; ---------------------------------------------------------------------------
; $05 C01X
; ---------------------------------------------------------------------------
t_c01x      LDA  #$05
            JSR  rec_begin
            LDX  #$11
c1          LDA  $C000,X
            JSR  put
            INX
            CPX  #$20
            BNE  c1
            JMP  rec_end

; ---------------------------------------------------------------------------
; $08 BANKS
; ---------------------------------------------------------------------------
t_banks     LDA  #$08
            JSR  rec_begin
            LDA  >$D01234
            JSR  put
            LDA  >$D05678
            JSR  put
            LDA  >$801234
            JSR  put
            LDA  >$7F1234
            JSR  put
            LDA  $C036
            JSR  put
            LDA  $FBB3
            JSR  put
            LDA  $FBC0
            JSR  put
            SEC
            JSR  $FE1F          ; IDRoutine: carry clear on a IIgs, Y = ROM version
            PHP
            STA  SAVE
            STX  SAVE+1
            STY  SAVE+2
            PLA
            JSR  put            ; P
            LDA  SAVE
            JSR  put            ; A
            LDA  SAVE+1
            JSR  put            ; X
            LDA  SAVE+2
            JSR  put            ; Y
            JMP  rec_end

; ---------------------------------------------------------------------------
; $06 GAME
; ---------------------------------------------------------------------------
t_game      LDA  #$06
            JSR  rec_begin
            LDA  $C061
            JSR  put
            LDA  $C062
            JSR  put
            LDA  $C063
            JSR  put
            LDX  #$00
pl          STX  PNUM
            LDA  $C070          ; start the paddle timers
            LDA  #$00
            STA  CNTL
            STA  CNTH
pc          LDX  PNUM
            LDA  $C064,X
            BPL  pdone
            INC  CNTL
            BNE  pc
            INC  CNTH
            LDA  CNTH
            CMP  #$10
            BNE  pc
pdone       LDA  CNTL
            JSR  put
            LDA  CNTH
            JSR  put
            LDX  PNUM
            INX
            CPX  #$04
            BNE  pl
            JMP  rec_end

; ---------------------------------------------------------------------------
; $07 BLKMOVE -- MVN/MVP with 8-bit index registers
; ---------------------------------------------------------------------------
; Markers: $00FE-$0101 = A1 A2 A3 A4 (continuous 16-bit stepping reads these),
; $0000/$0001 = B3 B4 (an 8-bit wrap reads these), $12FE-$1301 = C1-C4 (an
; index whose high byte survived SEP #$10 reads these). $FFFF/$FFFE are ROM.
t_blkmove   LDA  #$07
            JSR  rec_begin
            ; save $00-$01, $F0-$F3, $FE-$FF, $0100-$0101
            LDA  $00
            STA  SAVE+$10
            LDA  $01
            STA  SAVE+$11
            LDA  $F0
            STA  SAVE+$12
            LDA  $F1
            STA  SAVE+$13
            LDA  $F2
            STA  SAVE+$14
            LDA  $F3
            STA  SAVE+$15
            LDA  $FE
            STA  SAVE+$16
            LDA  $FF
            STA  SAVE+$17
            LDA  $0100
            STA  SAVE+$18
            LDA  $0101
            STA  SAVE+$19
            LDA  #$A1
            STA  $FE
            LDA  #$A2
            STA  $FF
            LDA  #$A3
            STA  $0100
            LDA  #$A4
            STA  $0101
            LDA  #$B3
            STA  $00
            LDA  #$B4
            STA  $01
            LDA  #$C1
            STA  $12FE
            LDA  #$C2
            STA  $12FF
            LDA  #$C3
            STA  $1300
            LDA  #$C4
            STA  $1301
            JSR  zdest

            CLC
            XCE                 ; native
            REP  #$30
            LONGA ON
            LONGI ON
            LDX  #$12FE
            LDY  #$12F0
            SEP  #$10           ; x=1
            LONGI OFF
            LDX  #$FE
            LDY  #$F0
            LDA  #$0003         ; four bytes
            MVN  $00,$00
            REP  #$10
            LONGI ON
            STX  SAVE+4
            STY  SAVE+6
            SEP  #$30
            LONGA OFF
            LONGI OFF
            SEC
            XCE
            JSR  putmove
            JSR  zdest

            CLC
            XCE
            REP  #$30
            LONGA ON
            LONGI ON
            SEP  #$10
            LONGI OFF
            LDX  #$01
            LDY  #$F3
            LDA  #$0003
            MVP  $00,$00
            REP  #$10
            LONGI ON
            STX  SAVE+4
            STY  SAVE+6
            SEP  #$30
            LONGA OFF
            LONGI OFF
            SEC
            XCE
            JSR  putmove

            LDA  SAVE+$10       ; restore
            STA  $00
            LDA  SAVE+$11
            STA  $01
            LDA  SAVE+$12
            STA  $F0
            LDA  SAVE+$13
            STA  $F1
            LDA  SAVE+$14
            STA  $F2
            LDA  SAVE+$15
            STA  $F3
            LDA  SAVE+$16
            STA  $FE
            LDA  SAVE+$17
            STA  $FF
            LDA  SAVE+$18
            STA  $0100
            LDA  SAVE+$19
            STA  $0101
            JMP  rec_end
zdest       LDA  #$00
            STA  $F0
            STA  $F1
            STA  $F2
            STA  $F3
            RTS
putmove     LDA  SAVE+4
            JSR  put
            LDA  SAVE+5
            JSR  put
            LDA  SAVE+6
            JSR  put
            LDA  SAVE+7
            JSR  put
            LDA  $F0
            JSR  put
            LDA  $F1
            JSR  put
            LDA  $F2
            JSR  put
            LDA  $F3
            JMP  put

; ---------------------------------------------------------------------------
; $02 E0AUX -- does bank $E0 follow RAMWRT and RAMRD?
; ---------------------------------------------------------------------------
t_e0aux     LDA  #$02
            JSR  rec_begin
            STA  $C004          ; RAMWRT off (a write throws it; the value is ignored)
            STA  $C002          ; RAMRD off
            LDA  #$11
            STA  >$E04000
            LDA  #$22
            STA  >$E14000
            STA  $C005          ; RAMWRT on
            LDA  #$33
            STA  >$E04000
            STA  $C004          ; RAMWRT off
            LDA  >$E04000
            JSR  put            ; $11: $E0 followed RAMWRT; $33: it did not
            LDA  >$E14000
            JSR  put            ; $33 or $22 correspondingly
            LDA  #$44
            STA  >$E04000
            LDA  #$55
            STA  >$E14000
            LDX  #$00
cps         LDA  stub,X
            STA  >$E1B000,X
            INX
            CPX  #stubend-stub
            BNE  cps
            JSL  $E1B000
            LDA  >$E1B0F0
            JSR  put            ; $55: $E0 followed RAMRD; $44: it did not
            JMP  rec_end
; runs in $E1, which RAMRD does not move, so its own fetches are safe
stub        STA  >$00C003       ; RAMRD on
            LDA  >$E04000
            STA  >$E1B0F0
            STA  >$00C002       ; RAMRD off
            RTL
stubend

; ---------------------------------------------------------------------------
; $03 MEGAII -- do the VBL and quarter-second flags latch while disabled?
; ---------------------------------------------------------------------------
t_megaii    LDA  #$03
            JSR  rec_begin
            LDA  $C041
            STA  SAVE+8
            JSR  put
            LDA  #$00
            STA  $C041
            STA  $C047          ; clear the VBL and quarter-second flags
            LDA  #8
            JSR  waitedges
            LDA  $C046
            JSR  put            ; bit 3 set here: VBL latches while disabled
            LDA  #50
            JSR  waitedges
            LDA  $C046
            JSR  put            ; bit 4 set here: quarter-second latches while disabled
            LDA  #$18
            STA  $C041          ; both enabled (IRQs are masked)
            STA  $C047
            LDA  #50
            JSR  waitedges
            LDA  $C046
            JSR  put            ; enabled: bits 3 and 4 expected
            LDA  SAVE+8
            STA  $C041
            STA  $C047
            JMP  rec_end

; ---------------------------------------------------------------------------
; $01 SLOT6 -- does $C02D bit 6 take $C0E0-$C0EF away from the IWM?
; ---------------------------------------------------------------------------
t_slot6     LDA  #$01
            JSR  rec_begin
            LDA  $C02D
            STA  SAVE+9
            JSR  put
            LDA  #240           ; two seconds: a boot drive's motor has stopped
            JSR  waitedges
            LDA  SAVE+9
            AND  #$BF
            STA  $C02D          ; bit 6 clear: built-in port
            JSR  iwmsamp
            LDA  SAVE+9
            ORA  #$40
            STA  $C02D          ; bit 6 set: "Your Card"
            JSR  iwmsamp
            LDA  SAVE+9
            STA  $C02D
            JMP  rec_end
; status (Q6 on, Q7 off) twice a frame apart, then the data register (Q6 off)
; twice -- an idle IWM gives a steady status and $FF; the floating bus gives
; whatever video is on the bus.
iwmsamp     LDA  $C0ED
            LDA  $C0EE
            JSR  put
            LDA  #2
            JSR  waitedges
            LDA  $C0EE
            JSR  put
            LDA  $C0EC
            LDA  $C0EE
            JSR  put
            LDA  #2
            JSR  waitedges
            LDA  $C0EE
            JSR  put
            LDA  $C0EC
            RTS

; ---------------------------------------------------------------------------
; $04 SCANCLR -- does reading $C02E/$C02F clear a pending scan-line interrupt?
; ---------------------------------------------------------------------------
t_scanclr   LDA  #$04
            JSR  rec_begin
            LDA  $C029
            STA  SAVE+10
            LDA  $C023
            STA  SAVE+11
            LDA  SAVE+10
            ORA  #$C1           ; SHR on, aux linearised, bank latch kept
            STA  $C029
            LDX  #$00
            LDA  #$00
scb         STA  >$E19D00,X
            INX
            CPX  #200
            BNE  scb
            LDA  #$40
            STA  >$E19D64       ; line 100 raises the interrupt
            LDA  #$00
            STA  $C032          ; clear anything pending
            LDA  #$02
            STA  $C023          ; scan-line interrupt enable
            LDA  #20
            STA  WCNT2
sw          LDA  $C023
            AND  #$20
            BNE  sgot
            LDA  #1
            JSR  waitedges
            DEC  WCNT2
            BNE  sw
sgot        LDA  $C023
            JSR  put            ; pending
            LDA  $C02E
            LDA  $C02F
            LDA  $C023
            JSR  put            ; after the counters were read
            LDA  #$00
            STA  $C032
            LDA  $C023
            JSR  put            ; after $C032
            LDA  SAVE+11
            AND  #$0F
            STA  $C023
            LDA  #$00
            STA  >$E19D64
            STA  $C032
            LDA  SAVE+10
            STA  $C029
            JMP  rec_end

; ---------------------------------------------------------------------------
; $09 IWMREG -- what the IWM drives at odd addresses, with Q6 and Q7 both on,
; and in the handshake register's bits 0-6. Built-in port; the motor has been
; off since t_slot6's wait. Only CA0 ($C0E0/$C0E1) is used for the even/odd
; pairs: no strobe reaches a 3.5 drive and the 5.25 stepper is not enabled.
; ---------------------------------------------------------------------------
t_iwmreg    LDA  #$09
            JSR  rec_begin
            LDA  $C02D
            STA  SAVE+$20
            AND  #$BF
            STA  $C02D          ; built-in port
            LDA  $C0E8          ; ENABLE off
            LDA  $C0EE          ; Q7 off
            LDA  $C0ED          ; Q6 on: status selected
            LDA  $C0E0          ; even
            JSR  put
            LDA  $C0E1          ; odd (CA0 on)
            JSR  put
            LDA  $C0E0          ; even (CA0 off)
            JSR  put
            LDA  $C0E1          ; odd
            JSR  put
            LDA  $C0E0          ; CA0 off again
            LDA  $C0EE          ; status: bits 0-4 are the mode
            AND  #$1F
            STA  $C0EF          ; Q7 on: the mode register, rewritten with its own value
            LDA  $C0E0          ; even, Q6 and Q7 on
            JSR  put
            LDA  $C0EC          ; Q6 off: handshake
            JSR  put
            LDA  $C0E0          ; handshake again
            JSR  put
            LDA  $C0EE          ; Q7 off
            LDA  SAVE+$20
            STA  $C02D
            JMP  rec_end

; ---------------------------------------------------------------------------
; $0A RTC -- how long a transfer holds $C034 bit 7, and which BRAM bytes the
; one-byte commands reach. Reads only.
; ---------------------------------------------------------------------------
t_rtc       LDA  #$0A
            JSR  rec_begin
            PHP
            SEI
            LDA  $C034
            STA  SAVE+$21
            LDA  #$81           ; read seconds lo
            STA  $C033
            JSR  rtc_wpoll      ; the command byte: a write transfer, counted
            JSR  rtc_rpoll      ; the data byte: a read transfer, counted
            LDA  $C033
            JSR  put
            LDA  #$40           ; BRAM $10-$13, two-byte reads (second byte $40 + 4i)
            JSR  rtc_two4
            LDA  #$A1           ; the same four by z010ab01 ($A1 + 4i)
            JSR  rtc_one4
            LDA  #$00           ; BRAM $00-$03, two-byte reads (second byte 4i)
            JSR  rtc_two4
            LDA  #$C1           ; the same four by z1abcd01 ($C1 + 4i)
            JSR  rtc_one4
            LDA  SAVE+$21
            AND  #$1F           ; the transfer over: start, read and enable off
            STA  $C034
            PLP
            JMP  rec_end
; A = the first second byte (two-byte form, first byte $B8) or the first command
; (one-byte form); four reads, stepping it by 4, each byte put
rtc_two4    STA  SAVE+$25
            LDA  #4
            STA  SAVE+$26
rt2         LDX  SAVE+$25
            LDA  #$B8
            JSR  rtc_rd2
            LDA  SAVE+$25
            CLC
            ADC  #4
            STA  SAVE+$25
            DEC  SAVE+$26
            BNE  rt2
            RTS
rtc_one4    STA  SAVE+$25
            LDA  #4
            STA  SAVE+$26
rt1         LDA  SAVE+$25
            JSR  rtc_rd1
            LDA  SAVE+$25
            CLC
            ADC  #4
            STA  SAVE+$25
            DEC  SAVE+$26
            BNE  rt1
            RTS
; A = command: write it, read the byte back, put it
rtc_rd1     JSR  rtc_w
            JSR  rtc_r
            JMP  put
; A, X = the two command bytes: write both, read the byte back, put it
rtc_rd2     STX  SAVE+$22
            JSR  rtc_w
            LDA  SAVE+$22
            JSR  rtc_w
            JSR  rtc_r
            JMP  put
rtc_w       STA  $C033
            LDA  SAVE+$21
            AND  #$0F
            ORA  #$A0           ; start, write, enable
            STA  $C034
rtc_wait    LDA  $C034
            BMI  rtc_wait
            RTS
rtc_r       LDA  SAVE+$21
            AND  #$0F
            ORA  #$E0           ; start, read, enable
            STA  $C034
rtc_rwait   LDA  $C034
            BMI  rtc_rwait
            LDA  $C033
            RTS
; the counted forms: put the number of $C034 reads it took (16-bit)
rtc_wpoll   LDA  SAVE+$21
            AND  #$0F
            ORA  #$A0
            JMP  rtc_poll
rtc_rpoll   LDA  SAVE+$21
            AND  #$0F
            ORA  #$E0
rtc_poll    STA  $C034
            LDX  #$00
            STX  SAVE+$23
rp1         INX
            BNE  rp2
            INC  SAVE+$23
            BEQ  rpdone         ; 65536 reads: give up, and say so with $0000
rp2         LDA  $C034
            BMI  rp1
rpdone      TXA
            JSR  put
            LDA  SAVE+$23
            JMP  put

; ---------------------------------------------------------------------------
; $0B VDA -- does the IIgs decode I/O on the 65816's invalid-address cycle?
; In emulation mode STA abs,X always takes an extra cycle with DBR,AAH,AAL+XL on
; the bus and VDA=VPA=0 (datasheet note 4). $C0FF,X with X=$56 writes $C155, slot
; 1's ROM space, and that cycle addresses $C055, which sets PAGE2 when accessed.
; ---------------------------------------------------------------------------
t_vda       LDA  #$0B
            JSR  rec_begin
            LDA  $C01C          ; PAGE2 as the program found it
            STA  SAVE+$24
            LDA  $C054          ; PAGE2 off
            LDA  $C01C
            JSR  put
            LDX  #$56
            LDA  #$00
            STA  $C0FF,X
            LDA  $C01C
            JSR  put
            LDA  $CFFF          ; release any $C800 claim the $C155 access made
            LDA  $C054
            LDA  SAVE+$24
            BPL  vda1
            LDA  $C055          ; it was on: put it back
vda1        JMP  rec_end

; ---------------------------------------------------------------------------
; the results block back to the boot disk
; ---------------------------------------------------------------------------
wrresult    LDA  RESBLK
            ORA  RESBLK+1
            BEQ  wrskip
            LDA  #$02           ; ProDOS driver: write
            STA  DCMD
            LDA  #<RES
            STA  DBUFLO
            LDA  #>RES
            STA  DBUFHI
            LDA  RESBLK
            STA  DBLKLO
            LDA  RESBLK+1
            STA  DBLKHI
            LDA  #$80
            STA  RES+6          ; as written: "ok" (the block holds the claim it made)
            JSR  calldrv
            BCC  wrskip
            ORA  #$C0
            STA  RES+6          ; not written; say why in RAM and on screen
wrskip      RTS
calldrv     JMP  (DRVLO)

; ---------------------------------------------------------------------------
; the screen
; ---------------------------------------------------------------------------
show        JSR  SETVID
            JSR  SETKBD
            JSR  INIT
            JSR  HOME
            LDX  #$00
sh1         LDA  title,X
            BEQ  sh2
            ORA  #$80
            JSR  COUT
            INX
            BNE  sh1
sh2         LDA  RES+6
            JSR  PRBYTE
            JSR  CROUT
            LDA  #$00
            STA  OUTY
shrec       LDY  OUTY
            LDA  REC,Y
            BEQ  shend
            JSR  PRBYTE         ; id
            LDA  #':'+$80
            JSR  COUT
            LDY  OUTY
            LDA  REC+1,Y
            STA  WCNT           ; bytes left in this record
            INC  OUTY
            INC  OUTY
            LDA  #$00
            STA  WLAST          ; column counter
shb         LDA  WCNT
            BEQ  shnext
            LDA  #' '+$80
            JSR  COUT
            LDY  OUTY
            LDA  REC,Y
            JSR  PRBYTE
            INC  OUTY
            DEC  WCNT
            INC  WLAST
            LDA  WLAST
            CMP  #12
            BNE  shb
            LDA  #$00
            STA  WLAST
            JSR  CROUT
            LDA  #' '+$80
            JSR  COUT
            JSR  COUT
            JSR  COUT
            JMP  shb
shnext      JSR  CROUT
            JMP  shrec
shend       RTS
title       ASC  "HWTRUTH 1  DISK WRITE: "
            DFB  $00
