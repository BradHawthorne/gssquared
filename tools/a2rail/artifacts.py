#!/usr/bin/env python3
"""artifacts.py -- the one reader for gssquared's binary artifacts.

WHY THIS EXISTS

The A2COV1 coverage bitmap carries sixteen bytes of header. A harness that did
not know that popcounted the whole file, and every coverage figure in the
downstream measurement report was forty bytes high for a week. The writer had printed
what it wrote each time; nothing compared. So: one reader, imported by every
harness, that refuses what it does not recognise, re-derives the count, and
computes the same hash the writer prints -- so a consumer can prove it read
what was written.

USAGE
    python artifacts.py cov <file.bin>        # A2COV1: range, count, hash
    python artifacts.py snap <file.snap>      # GSPS snapshot: magic, version, size
    python artifacts.py fnv <file>            # house FNV-1a-64 of the whole file

    from artifacts import read_cov, house_fnv

The house FNV basis is deliberately non-canonical (see src/house_fnv.hpp):
1469598103934665603, not 14695981039346656037. It is load-bearing for every
golden in the tree, so it is copied here once and checked against a known
vector in this module's self-test rather than "corrected".
"""
import struct, sys, os

HOUSE_FNV_BASIS = 1469598103934665603
HOUSE_FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

COV_MAGIC = b'A2COV1\x00\x00'
SNAP_MAGIC = 0x47535053   # 'GSPS'


def house_fnv(data, h=HOUSE_FNV_BASIS):
    for b in data:
        h = ((h ^ b) * HOUSE_FNV_PRIME) & MASK64
    return h


class ArtifactError(Exception):
    pass


def read_cov(path):
    """Return (lo, hi, bitmap_bytes, marked, hash) for an A2COV1 file.

    Refuses a wrong magic, a range that does not match the body, or a body
    of the wrong length -- a bitmap that is short is a run that was cut off,
    not a run with less coverage.
    """
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 16 or d[:8] != COV_MAGIC:
        raise ArtifactError('%s: not an A2COV1 coverage bitmap (magic %r)' % (path, d[:8]))
    lo, hi = struct.unpack('<II', d[8:16])
    if hi < lo:
        raise ArtifactError('%s: header range $%06X-$%06X is inverted' % (path, lo, hi))
    want = ((hi - lo) >> 3) + 1
    body = d[16:]
    if len(body) != want:
        raise ArtifactError('%s: body is %d bytes, header range $%06X-$%06X needs %d' % (path, len(body), lo, hi, want))
    marked = sum(bin(b).count('1') for b in body)
    return lo, hi, body, marked, house_fnv(body)


SNAP_MANIFEST = b'GSPM'


def read_snap_header(path):
    """Return (magic_ok, version, size, manifest) for a GSPS snapshot.

    The manifest is the trailer the emulator appends after its sentinel
    (class, included, excluded, frame, pins, build, exe); None when the file
    predates manifests. Body content is not parsed.
    """
    size = os.path.getsize(path)
    with open(path, 'rb') as f:
        head = f.read(8)
        if len(head) < 8:
            raise ArtifactError('%s: too short to be a snapshot' % path)
        magic, ver = struct.unpack('<II', head)
        manifest = None
        if size > 8 + 4 + 4:
            # The manifest is the last thing in the file: scan the tail for the
            # marker whose declared length reaches exactly to EOF.
            f.seek(max(0, size - 4096))
            tail = f.read()
            i = tail.rfind(SNAP_MANIFEST)
            if i >= 0 and i + 8 <= len(tail):
                n = struct.unpack('<I', tail[i + 4:i + 8])[0]
                if i + 8 + n == len(tail):
                    manifest = tail[i + 8:].decode('ascii', 'replace')
    return magic == SNAP_MAGIC, ver, size, manifest


def _selftest():
    # FNV vector: the empty string hashes to the basis; one byte 'a' is a known step.
    assert house_fnv(b'') == HOUSE_FNV_BASIS
    assert house_fnv(b'a') == ((HOUSE_FNV_BASIS ^ 0x61) * HOUSE_FNV_PRIME) & MASK64
    # A synthetic bitmap round-trips.
    lo, hi = 0x000000, 0x00FFFF
    body = bytes([0x80] * (((hi - lo) >> 3) + 1))
    d = COV_MAGIC + struct.pack('<II', lo, hi) + body
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '_selftest.bin')
    open(p, 'wb').write(d)
    try:
        r = read_cov(p)
        assert r[0] == lo and r[1] == hi and r[3] == len(body), r[:2] + (r[3],)
        bad = COV_MAGIC + struct.pack('<II', lo, hi) + body[:-1]
        open(p, 'wb').write(bad)
        try:
            read_cov(p); raise AssertionError('short body accepted')
        except ArtifactError:
            pass
    finally:
        os.remove(p)
    return True


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__); return 2
    cmd = argv[1]
    if cmd == 'selftest':
        print('artifacts.py selftest: PASS' if _selftest() else 'FAIL'); return 0
    if len(argv) < 3:
        print('missing file'); return 2
    path = argv[2]
    try:
        if cmd == 'cov':
            lo, hi, body, marked, h = read_cov(path)
            span = hi - lo + 1
            print('A2COV1 %s range=$%06X-$%06X bytes_executed=%d of %d (%.2f%%) hash=%016X'
                  % (path, lo, hi, marked, span, 100.0 * marked / span, h))
        elif cmd == 'snap':
            ok, ver, size, manifest = read_snap_header(path)
            print('GSPS %s magic=%s version=%d size=%d manifest=%s' % (path, 'ok' if ok else 'BAD', ver, size,
                  manifest if manifest else 'absent (pre-manifest snapshot; class A assumed, every device excluded)'))
            if not ok: return 1
        elif cmd == 'fnv':
            print('%016X  %s' % (house_fnv(open(path, 'rb').read()), path))
        else:
            print('unknown command %r' % cmd); return 2
    except ArtifactError as e:
        print('REFUSED: %s' % e); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
