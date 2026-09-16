#!/usr/bin/env python3
"""gsosmap.py -- GS/OS kernel segment map, read out of the boot volume's own GS.OS file.

WHY THIS EXISTS

Names by default (SHAPE M7) named the ROM's tool sets and the RAM tool sets the
live pointer table lists, and nothing of GS/OS itself: a PC inside the SCM, the
Dispatcher or the System Loader printed as unnamed. The kernel file names its own
parts. SYSTEM/GS.OS is a chain of segments, each behind a $30-byte header:

    +$00 u16  body length
    +$02 u32  load address (24-bit)
    +$06 u16  flags ($8000 on the Init Manager parts, which run during boot)
    +$08 ...  the segment's name and version, high-bit ASCII, space padded
    +$30      the body

ending in a $FFFF length word. This tool writes, per segment, the load address,
length, flags, a name derived from that header, and a house-FNV hash of every
256-byte page of the body. It ships no code from the file: the only bytes it
copies are a vector table's slot opcodes, below.

A segment that is a vector table -- a whole number of 4-byte slots, most of them
JML ($5C) -- is written with SLOTOPS, the opcode byte of each slot, because GS/OS
installs its jump TARGETS at run time and a page hash of the file could never
match the live table. For such a segment the emulator checks the live opcodes
against SLOTOPS instead: the table's shape is the evidence, its targets are data.

The emulator names a PC inside a segment "GSOS.<Name>+$off" only while the live
bytes of that page hash-match (src/iigs_gsos_names.hpp): segments overlap (the
Init Manager parts load over others during boot), language-card banking swaps
some of them out, and a ProDOS 8 program replaces them, so an address alone is
not evidence. A page that does not match is unnamed, not misnamed.

USAGE
    python gsosmap.py <volume.po> [--out assets/symbols/apple2gs/gsos.segs]

The output holds one MAP section per kernel file hash; running it on another
volume adds that kernel's section and keeps the others.
"""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from prodosfs import read_file

HOUSE_FNV_BASIS = 1469598103934665603
HOUSE_FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


def house_fnv(data):
    h = HOUSE_FNV_BASIS
    for b in data:
        h = ((h ^ b) * HOUSE_FNV_PRIME) & MASK64
    return h


def symbol_name(ident):
    """'System Loader,Pt 2    v04.00' -> 'SystemLoaderPt2' (the version is kept apart)."""
    text = re.sub(r'\s+v\d+\.\d+\s*$', '', ident).strip()
    words = re.findall(r'[A-Za-z0-9]+', text)
    return ''.join(w if w.isupper() or w[0].isdigit() else w[0].upper() + w[1:] for w in words)


def segments(data):
    o, out = 0, []
    while True:
        if o + 2 > len(data):
            raise SystemExit('GS.OS: the segment chain runs off the end of the file at $%04X' % o)
        length = struct.unpack_from('<H', data, o)[0]
        if length == 0xFFFF:
            return out, o
        if o + 0x30 + length > len(data):
            raise SystemExit('GS.OS: segment at $%04X claims %d bytes past the end of the file' % (o, length))
        addr = struct.unpack_from('<I', data, o + 2)[0] & 0xFFFFFF
        flags = struct.unpack_from('<H', data, o + 6)[0]
        ident = bytes(b & 0x7F for b in data[o + 8:o + 0x30]).decode('ascii', 'replace').replace('\x00', ' ').strip()
        version = (re.search(r'v\d+\.\d+', ident) or [''])[0]
        body = data[o + 0x30:o + 0x30 + length]
        pages = [house_fnv(body[k:k + 256]) for k in range(0, length, 256)]
        slotops = None
        if length % 4 == 0 and length >= 16:
            ops = body[0::4]
            if sum(1 for b in ops if b == 0x5C) * 4 >= len(ops) * 3:   # three quarters JML: a vector table
                slotops = ops
        out.append({'addr': addr, 'len': length, 'flags': flags, 'name': symbol_name(ident),
                    'version': version, 'pages': pages, 'slotops': slotops})
        o += 0x30 + length


def render_section(file_fnv, volume, segs):
    lines = ['MAP %016X %s' % (file_fnv, os.path.basename(volume))]
    for s in segs:
        lines.append('SEG %06X %d %04X %s %s' % (s['addr'], s['len'], s['flags'], s['name'], s['version'] or '-'))
        if s['slotops'] is not None:
            lines.append('SLOTOPS ' + s['slotops'].hex().upper())
            continue
        for i in range(0, len(s['pages']), 8):
            lines.append('PAGES ' + ' '.join('%016X' % p for p in s['pages'][i:i + 8]))
    lines.append('END')
    return lines


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__)
        return 2
    volume = argv[1]
    out = argv[argv.index('--out') + 1] if '--out' in argv else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets', 'symbols', 'apple2gs', 'gsos.segs')
    data = read_file(volume, 'SYSTEM/GS.OS')
    file_fnv = house_fnv(data)
    segs, end = segments(data)
    print('SYSTEM/GS.OS on %s: %d bytes, house FNV %016X, %d segments, chain ends at $%04X'
          % (volume, len(data), file_fnv, len(segs), end))
    for s in segs:
        print('  $%06X %5d %04X %-22s %s%s' % (s['addr'], s['len'], s['flags'], s['name'], s['version'],
                                             '  (vector table: slot opcodes)' if s['slotops'] is not None else ''))
    # keep every other kernel's section; replace this one's
    kept, header = [], ['# GS/OS kernel segment maps, written by tools/a2rail/gsosmap.py from each boot volume\'s own',
                        '# SYSTEM/GS.OS: MAP <file house-FNV> <volume>, then SEG <addr> <len> <flags> <name> <version>',
                        '# and the house-FNV of every 256-byte page of that segment -- or, for a vector table,',
                        '# SLOTOPS: the opcode byte of each 4-byte slot (its targets are installed at run time).',
                        '# No kernel code. Read by src/iigs_gsos_names.hpp, which names a PC only while its page',
                        '# hash-matches (or its table\'s opcodes match) the live bytes.']
    if os.path.exists(out):
        cur, skip = [], False
        for line in open(out, encoding='ascii').read().splitlines():
            if line.startswith('#'):
                continue
            if line.startswith('MAP '):
                skip = line.split()[1] == '%016X' % file_fnv
            if not skip:
                kept.append(line)
            if line == 'END':
                skip = False
    text = '\n'.join(header + kept + render_section(file_fnv, volume, segs)) + '\n'
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'w', encoding='ascii', newline='\n').write(text)
    print('wrote %s' % os.path.normpath(out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
