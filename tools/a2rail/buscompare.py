#!/usr/bin/env python3
"""buscompare.py -- compare two slot-bus captures, cycle by cycle.

    python tools/a2rail/buscompare.py REFERENCE CAPTURE [options]
    python tools/a2rail/buscompare.py selftest

Both files are slot-bus dumps as A2GSPU_BUS_DUMP writes them: an 8-byte header
(the magic "~BUS" or "~BS2" and a little-endian record count) and 5-byte records
[addr lo, addr hi, data, ctl, seq]. REFERENCE is usually the emulator's format 2
dump; CAPTURE is what a card sampled on a real IIgs, converted to the same records
by the card's own tooling. The fidelity study's validation step is exactly this
comparison: until a real capture agrees, format 2 describes what a card should see,
not what it measurably sees.

The comparison walks both streams. Records that agree on the compared fields are
matched. At a disagreement it looks ahead (--window) for the point where --sync
consecutive records agree again, and reports the gap as records missing from the
capture or extra in it. A disagreement that no look-ahead explains is a field
mismatch (address, data, or named ctl bits).

Options:
    --ctl-mask HEX      ctl bits to compare (default FF). A capture that cannot see a
                        signal masks its bit out, and the report says which were ignored.
    --writes-only       compare only write cycles (ctl bit 0 clear)
    --no-read-data      compare reads by address and ctl, not data (floating-bus reads)
    --sync N            records that must agree to re-lock after a gap (default 8)
    --window N          how far ahead to look for that point (default 256)
    --show N            divergences to print (default 20)
    --json              print the summary as JSON

Exit 0 when nothing differs under the chosen masks, 1 otherwise, 2 on bad input.
"""
import json
import struct
import sys

CTL = ['READ', 'M2B0', 'M2SEL', 'DEVSEL', 'IOSEL', 'IOSTRB', 'INH', 'FAST']


def read_dump(path):
    data = open(path, 'rb').read()
    if len(data) < 8:
        raise ValueError(f'{path}: shorter than the 8-byte header')
    magic = data[:4].decode('latin-1')
    if magic not in ('~BUS', '~BS2'):
        raise ValueError(f'{path}: magic {magic!r} is neither ~BUS nor ~BS2')
    count = struct.unpack('<I', data[4:8])[0]
    body = data[8:]
    if len(body) % 5:
        raise ValueError(f'{path}: {len(body)} record bytes is not a multiple of 5')
    recs = [(body[i] | (body[i + 1] << 8), body[i + 2], body[i + 3], body[i + 4]) for i in range(0, len(body), 5)]
    return magic, count, recs


def select(recs, writes_only):
    return [r for r in recs if not (writes_only and (r[2] & 1))]


def key(r, ctl_mask, read_data):
    is_read = r[2] & 1
    return (r[0], r[1] if (read_data or not is_read) else None, r[2] & ctl_mask)


def compare(ref, cap, ctl_mask=0xFF, read_data=True, sync=8, window=256, show=20):
    kr = [key(r, ctl_mask, read_data) for r in ref]
    kc = [key(r, ctl_mask, read_data) for r in cap]
    i = j = 0
    out = {'reference': len(ref), 'capture': len(cap), 'matched': 0, 'missing': 0, 'extra': 0,
           'addr': 0, 'data': 0, 'ctl_bits': {n: 0 for n in CTL}, 'divergences': []}

    def note(kind, **kw):
        if len(out['divergences']) < show:
            out['divergences'].append(dict(kind=kind, **kw))

    def locks(a, b):
        n = min(sync, len(kr) - a, len(kc) - b)
        return n > 0 and all(kr[a + t] == kc[b + t] for t in range(n))

    while i < len(kr) and j < len(kc):
        if kr[i] == kc[j]:
            out['matched'] += 1
            i += 1
            j += 1
            continue
        # a gap? prefer the smallest skip on either side that re-locks
        skip = None
        for d in range(1, window + 1):
            if i + d < len(kr) and locks(i + d, j):
                skip = ('missing', d)
                break
            if j + d < len(kc) and locks(i, j + d):
                skip = ('extra', d)
                break
        if skip and skip[0] == 'missing':
            note('missing', ref_index=i, count=skip[1], first=fmt(ref[i]))
            out['missing'] += skip[1]
            i += skip[1]
            continue
        if skip and skip[0] == 'extra':
            note('extra', cap_index=j, count=skip[1], first=fmt(cap[j]))
            out['extra'] += skip[1]
            j += skip[1]
            continue
        r, c = ref[i], cap[j]
        fields = []
        if r[0] != c[0]:
            out['addr'] += 1
            fields.append('addr')
        if kr[i][1] != kc[j][1]:
            out['data'] += 1
            fields.append('data')
        diff = (r[2] ^ c[2]) & ctl_mask
        for b in range(8):
            if diff & (1 << b):
                out['ctl_bits'][CTL[b]] += 1
                fields.append(CTL[b])
        note('mismatch', ref_index=i, cap_index=j, fields=fields, reference=fmt(r), capture=fmt(c))
        i += 1
        j += 1
    if i < len(kr):
        out['missing'] += len(kr) - i
        note('missing', ref_index=i, count=len(kr) - i, first=fmt(ref[i]), at='end of capture')
    if j < len(kc):
        out['extra'] += len(kc) - j
        note('extra', cap_index=j, count=len(kc) - j, first=fmt(cap[j]), at='end of reference')
    out['ctl_bits'] = {k: v for k, v in out['ctl_bits'].items() if v}
    out['ignored_ctl'] = [CTL[b] for b in range(8) if not (ctl_mask & (1 << b))]
    out['identical'] = (out['missing'] == out['extra'] == out['addr'] == out['data'] == 0 and not out['ctl_bits'])
    return out


def fmt(r):
    names = [CTL[b] for b in range(8) if r[2] & (1 << b)]
    return f"${r[0]:04X} {'R' if r[2] & 1 else 'W'} ${r[1]:02X} ctl=${r[2]:02X}({','.join(names)})"


def selftest():
    import random
    rnd = random.Random(7)
    base = []
    for _ in range(2000):
        a = rnd.choice([0xC0B0 + rnd.randrange(16), 0x2000 + rnd.randrange(0x8000), 0xC300 + rnd.randrange(256)])
        base.append((a, rnd.randrange(256), rnd.choice([0x04, 0x05, 0x0C, 0x0D, 0x14, 0x06]), 0))
    fails = 0

    def check(name, ok):
        nonlocal fails
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        fails += 0 if ok else 1

    r = compare(base, list(base))
    check('identical streams compare identical', r['identical'] and r['matched'] == 2000)
    cap = list(base)
    cap[500] = (cap[500][0], cap[500][1] ^ 0xFF, cap[500][2], 0)
    r = compare(base, cap)
    check('one changed data byte is one data mismatch', r['data'] == 1 and r['matched'] == 1999 and not r['identical'])
    cap = base[:700] + base[703:]
    r = compare(base, cap)
    check('three dropped records are three missing, the rest matched', r['missing'] == 3 and r['matched'] == 1997)
    cap = base[:900] + [(0x1234, 0x56, 0x04, 0)] * 2 + base[900:]
    r = compare(base, cap)
    check('two inserted records are two extra, the rest matched', r['extra'] == 2 and r['matched'] == 2000)
    cap = [(a, d, c | 0x80, s) for (a, d, c, s) in base]
    r = compare(base, cap)
    check('a ctl bit the capture sets differs...', r['ctl_bits'].get('FAST') == 2000)
    r = compare(base, cap, ctl_mask=0x7F)
    check('...and is ignored, and named, when masked out', r['identical'] and r['ignored_ctl'] == ['FAST'])
    cap = [(a, (d ^ 0x55) if (c & 1) else d, c, s) for (a, d, c, s) in base]
    r = compare(base, cap, read_data=False)
    check('--no-read-data ignores read data only', r['identical'])
    r = compare(select(base, True), select(cap, True))
    check('--writes-only compares writes only', r['identical'])
    print(f"buscompare selftest: {'ALL GREEN' if not fails else str(fails) + ' FAILED'}")
    return 1 if fails else 0


def main(argv):
    if len(argv) == 2 and argv[1] == 'selftest':
        return selftest()
    args = [a for a in argv[1:] if not a.startswith('--')]
    opts = argv[1:]

    def opt(name, default, conv=int):
        if name in opts:
            k = opts.index(name)
            return conv(opts[k + 1])
        return default
    if len(args) < 2:
        print(__doc__)
        return 2
    ctl_mask = opt('--ctl-mask', 0xFF, lambda v: int(v, 16))
    for flag in ('--ctl-mask', '--sync', '--window', '--show'):
        if flag in opts:
            args.remove(opts[opts.index(flag) + 1])
    try:
        m1, n1, ref = read_dump(args[0])
        m2, n2, cap = read_dump(args[1])
    except (OSError, ValueError) as e:
        print(f'buscompare: {e}')
        return 2
    writes_only = '--writes-only' in opts
    res = compare(select(ref, writes_only), select(cap, writes_only), ctl_mask, '--no-read-data' not in opts,
                  opt('--sync', 8), opt('--window', 256), opt('--show', 20))
    res.update(reference_magic=m1, capture_magic=m2, reference_header_count=n1, capture_header_count=n2)
    if '--json' in opts:
        print(json.dumps(res, indent=2))
    else:
        print(f"reference {args[0]}: {m1}, {len(ref)} records (header says {n1})")
        print(f"capture   {args[1]}: {m2}, {len(cap)} records (header says {n2})")
        if m1 != m2:
            print(f"  NOTE: formats differ ({m1} vs {m2}); ctl bits mean different things in each")
        print(f"matched {res['matched']}, missing from capture {res['missing']}, extra in capture {res['extra']}, "
              f"address {res['addr']}, data {res['data']}, ctl bits {res['ctl_bits'] or '-'}"
              + (f" (ignored: {','.join(res['ignored_ctl'])})" if res['ignored_ctl'] else ''))
        for d in res['divergences']:
            print('  ' + json.dumps(d))
        print('IDENTICAL' if res['identical'] else 'DIFFERENT')
    return 0 if res['identical'] else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
