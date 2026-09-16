#!/usr/bin/env python3
"""report.py -- read hwtruth's results and say what each measurement means.

    python tools/hwtruth/report.py RESULTS            one run
    python tools/hwtruth/report.py HARDWARE EMULATOR  a real machine against the emulator
    python tools/hwtruth/report.py A B --json         machine-readable

RESULTS is either a 512-byte results page (run_emu.ps1 -Out) or a ProDOS disk
image the program ran from (its RESULTS file is found in the volume directory).

Every verdict is derived from the raw bytes with the rule printed beside it, so
a reader can disagree with the rule and still use the numbers. "inconclusive"
means the bytes fit none of the rules -- report those, do not guess.
"""
import json
import os
import sys

NAMES = {1: 'SLOT6', 2: 'E0AUX', 3: 'MEGAII', 4: 'SCANCLR', 5: 'C01X', 6: 'GAME', 7: 'BLKMOVE', 8: 'BANKS'}


def load(path):
    data = open(path, 'rb').read()
    if len(data) == 512:
        return data
    # a ProDOS image: find RESULTS in the volume directory (build.py's layout)
    block, first = 2, True
    while block:
        b = data[block * 512:(block + 1) * 512]
        off = 4
        for i in range(13):
            e = b[off:off + 39]
            off += 39
            if first and i == 0:
                continue
            n = e[0] & 0x0F
            if n and e[1:1 + n] == b'RESULTS':
                key = e[0x11] | (e[0x12] << 8)
                return data[key * 512:(key + 1) * 512]
        first = False
        block = b[2] | (b[3] << 8)
    raise SystemExit(f'{path}: no RESULTS file and not a 512-byte page')


def parse(page):
    if page[0:4] != b'HWT1':
        raise SystemExit('not a hwtruth results page (no HWT1 signature)')
    out = {'version': page[4], 'finished': page[5] == 1, 'disk_write': page[6], 'records': {}}
    i = 8
    while i < 510 and page[i] != 0:
        rid, n = page[i], page[i + 1]
        out['records'][rid] = list(page[i + 2:i + 2 + n])
        i += 2 + n
    return out


def w16(b, i):
    return b[i] | (b[i + 1] << 8)


def verdicts(recs):
    v = []

    def add(rid, question, answer, rule, raw):
        v.append({'record': NAMES.get(rid, rid), 'question': question, 'answer': answer, 'rule': rule,
                  'raw': ' '.join(f'{x:02X}' for x in raw)})

    r = recs.get(1)
    if r and len(r) == 9:
        on, off = r[1:5], r[5:9]
        iwm_like = on[0] == on[1] and on[2] == on[3]
        if not iwm_like:
            ans = 'inconclusive (bit 6 clear did not read a steady IWM)'
        elif off == on:
            ans = 'NO: the IWM still answers with bit 6 set'
        else:
            ans = 'YES: bit 6 set takes $C0E0-$C0EF away from the IWM'
        add(1, '$C02D bit 6 deselects the IWM', ans,
            'bit 6 clear reads status twice and data twice, each pair steady; bit 6 set reading the same four bytes means the IWM still answers',
            r)
        add(1, 'IWM data register reads all ones with the motor off', 'YES' if on[2] == on[3] == 0xFF else 'NO',
            'Q6 off, Q7 off, motor off: the IWM spec says $FF (the raw bytes say what it read instead)', on[2:4])
    r = recs.get(2)
    if r and len(r) == 3:
        wr = ('YES' if (r[0], r[1]) == (0x11, 0x33) else 'NO' if (r[0], r[1]) == (0x33, 0x22) else 'inconclusive')
        rd = ('YES' if r[2] == 0x55 else 'NO' if r[2] == 0x44 else 'inconclusive')
        add(2, 'bank $E0 follows RAMWRT', wr, '$E0:4000 $11 and $E1:4000 $33 after writing $33 to $E0 with RAMWRT on', r[:2])
        add(2, 'bank $E0 follows RAMRD', rd, '$E0:4000 read with RAMRD on gives aux ($55), not main ($44)', r[2:3])
    r = recs.get(3)
    if r and len(r) == 4:
        add(3, 'Mega II VBL flag latches while disabled', 'YES' if r[1] & 0x08 else 'NO',
            '$C046 bit 3 after 4 frames with $C041 = 0', r[1:2])
        add(3, 'Mega II quarter-second flag latches while disabled', 'YES' if r[2] & 0x10 else 'NO',
            '$C046 bit 4 after 25 frames with $C041 = 0', r[2:3])
        add(3, 'sanity: flags set when enabled', 'yes' if (r[3] & 0x18) == 0x18 else 'NO -- the test itself is suspect',
            '$C046 bits 3 and 4 after 25 frames with $C041 = $18', r[3:4])
    r = recs.get(4)
    if r and len(r) == 3:
        if not r[0] & 0x20:
            ans = 'inconclusive (no scan-line interrupt became pending)'
        else:
            ans = 'YES' if not r[1] & 0x20 else 'NO'
        add(4, 'reading $C02E/$C02F clears a pending scan-line interrupt', ans,
            '$C023 bit 5 set before the counter reads; clear after them means the reads cleared it', r)
        add(4, 'sanity: $C032 clears it', 'yes' if not r[2] & 0x20 else 'NO', '$C023 bit 5 after writing 0 to $C032', r[2:3])
    r = recs.get(5)
    if r and len(r) == 15:
        add(5, '$C011-$C01F as read (bit 7 is the switch; bits 0-6 are the question)', 'recorded',
            'compare bits 0-6 between machines', r)
    r = recs.get(6)
    if r and len(r) == 11:
        add(6, 'buttons 0-2 ($C061-$C063)', ' '.join('pressed' if x & 0x80 else 'up' for x in r[:3]),
            'bit 7; bits 0-6 recorded for the float question', r[:3])
        counts = [w16(r, 3 + 2 * k) for k in range(4)]
        add(6, 'paddles 0-3 timers', ' '.join('timeout' if c >= 0x1000 else 'zero' if c == 0 else 'counted' for c in counts),
            'loop counts in the raw bytes (16-bit each): $1000 = the timer never ended, 0 = it had already ended', r[3:])
    r = recs.get(7)
    if r and len(r) == 16:
        mvn, mvp = r[:8], r[8:]
        dst = tuple(mvn[4:8])
        ans = ('16-bit stepping (read $0100/$0101)' if dst == (0xA1, 0xA2, 0xA3, 0xA4) else
               '8-bit wrap (read $0000/$0001)' if dst == (0xA1, 0xA2, 0xB3, 0xB4) else
               'index high byte survived SEP #$10' if dst[0] == 0xC1 else 'inconclusive')
        add(7, 'MVN with x=1 across $00FF', ans, f'final X=${w16(mvn, 0):04X} Y=${w16(mvn, 2):04X}; destination bytes', mvn)
        dst = tuple(mvp[4:8])
        ans = ('8-bit wrap (read $00FF/$00FE)' if dst[:2] == (0xA1, 0xA2) and dst[2:] == (0xB3, 0xB4) else
               '16-bit stepping (read $FFFF/$FFFE)' if dst[2:] == (0xB3, 0xB4) else 'inconclusive')
        add(7, 'MVP with x=1 across $0000', ans, f'final X=${w16(mvp, 0):04X} Y=${w16(mvp, 2):04X}; destination bytes', mvp)
    r = recs.get(8)
    if r and len(r) == 11:
        add(8, 'unpopulated bank reads ($D0:1234, $D0:5678, $80:1234)',
            'bank number' if r[0] == 0xD0 and r[2] == 0x80 else 'all $FF' if r[0] == r[1] == r[2] == 0xFF else 'other',
            'ROM 01 is recalled as the bank number, ROM 3 as $FF', r[:3])
        add(8, 'machine', f'IDRoutine carry {"clear (IIgs)" if not r[7] & 1 else "set"}, ROM version Y=${r[10]:02X}; $C036=${r[4]:02X}',
            '$FE1F with carry set', r[4:])
    return v


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    as_json = '--json' in sys.argv
    if not args or len(args) > 2:
        raise SystemExit(__doc__)
    runs = []
    for a in args:
        p = parse(load(a))
        runs.append({'source': a, 'finished': p['finished'], 'disk_write': p['disk_write'],
                     'records': {NAMES.get(k, k): ' '.join(f'{x:02X}' for x in b) for k, b in p['records'].items()},
                     'verdicts': verdicts(p['records'])})
    if as_json:
        print(json.dumps(runs, indent=2))
        return
    for run in runs:
        print(f"== {run['source']}  finished={run['finished']}  disk_write=${run['disk_write']:02X}")
        for v in run['verdicts']:
            print(f"  {v['record']:8s} {v['question']}: {v['answer']}")
            print(f"           rule: {v['rule']}   raw: {v['raw']}")
    if len(runs) == 2:
        a, b = runs
        print(f"== differences ({os.path.basename(a['source'])} vs {os.path.basename(b['source'])})")
        n = 0
        for va, vb in zip(a['verdicts'], b['verdicts']):
            if va['answer'] != vb['answer']:
                n += 1
                print(f"  {va['question']}: {va['answer']}  |  {vb['answer']}")
        for k in sorted(set(a['records']) | set(b['records'])):
            if a['records'].get(k) != b['records'].get(k):
                print(f"  raw {k}: {a['records'].get(k)}  |  {b['records'].get(k)}")
        if n == 0:
            print('  no verdict differs')


if __name__ == '__main__':
    main()
