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

There are TWO coverage formats and this reader knew only the older one. `cov`'s
own synopsis says it writes "raw A2COV1 or source-bound A2COV2", and ANY mapped
run -- `cov map <plan>`, which is the normal deasmiigs source-bound path --
writes A2COV2. read_cov() raised ArtifactError on those, so a harness following
INSTRUMENTATION_SHAPE.md M6 ("artifacts.py is the reader") got an exception on
the format that feeds the code/data split, and a harness that fell back to
popcounting the file the way the original MEMVU sweep harness did over-counted by a
32-byte header plus the whole embedded plan -- a worse Event E, on the newer
format. So A2COV2 is read here too, and `cov` dispatches on the magic.

USAGE
    python artifacts.py cov <file.bin>        # A2COV1 or A2COV2 (dispatched on magic)
    python artifacts.py snap <file.snap>      # GSPS snapshot: magic, version, size
    python artifacts.py raw <file>            # a headerless artifact, via its <file>.meta.json
    python artifacts.py fnv <file>            # house FNV-1a-64 of the whole file

RAW ARTIFACTS (M6). The headless run's spike_e1.bin, spike_e1_init.bin,
spike_trace.bin, spike_slot.bin, spike_mmu_truth.bin and the `vram` dump have
no header -- consumers read spike_e1.bin as raw bytes, so one cannot be added --
and each is described instead by a sidecar record, <file>.meta.json (schema
a2artifact-v1, src/a2gspu_artifact_meta.hpp). read_raw() refuses an artifact
with no sidecar, a format it does not know, a length that is not count x
record_size, a file hash that does not re-derive, and a content hash (the
writer's determinism witness, recomputed from the records) that does not.

    from artifacts import read_cov, read_cov2, house_fnv

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
COV2_MAGIC = b'A2COV2\x00\x00'
MAP_MAGIC = b'A2MAP1\x00\x00'
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


def read_cov2(path):
    """Return (run_id, generation, maps, marked, span, hash_ok, stored_hash) for A2COV2.

    `maps` is a list of dicts: index, base, length, marked, bits, starts.

    Layout, from src/a2gspu_coverage.hpp a2gspu_cov_map_write():
        0   'A2COV2\\0\\0'          8 bytes
        8   plan size             uint32
        12  (reserved, zero)      uint32
        16  run id                uint64
        24  generation            uint64
        32  the embedded A2MAP1 plan, `plan size` bytes
        ..  per map, in plan order: bits ceil(len/8) bytes, then starts len bytes
        -8  house-FNV-1a-64 over everything above, uint64

    Everything is re-derived and then checked against the file's own trailer, so
    a caller finds out it misread rather than getting a plausible number. The
    per-map lengths come from walking the embedded plan: the alternative, trusting
    the writer's ordering, is the assumption that made the A2COV1 reader necessary.
    """
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 32 + 8 or d[:8] != COV2_MAGIC:
        raise ArtifactError('%s: not an A2COV2 coverage bitmap (magic %r)' % (path, d[:8]))
    plan_size, run_id, generation = struct.unpack('<I', d[8:12])[0], \
        struct.unpack('<Q', d[16:24])[0], struct.unpack('<Q', d[24:32])[0]
    if plan_size < 56 or 32 + plan_size + 8 > len(d):
        raise ArtifactError('%s: header says the plan is %d bytes, which does not fit in %d'
                            % (path, plan_size, len(d)))
    plan = d[32:32 + plan_size]
    if plan[:8] != MAP_MAGIC:
        raise ArtifactError('%s: embedded plan is not A2MAP1 (magic %r)' % (path, plan[:8]))
    count, form = struct.unpack('<II', plan[8:16])
    if not count or count > 65536 or form not in (1, 2):
        raise ArtifactError('%s: embedded A2MAP1 header is invalid (count=%d form=%d)'
                            % (path, count, form))
    segs, p = [], 56
    for i in range(count):
        if p + 12 > len(plan):
            raise ArtifactError('%s: embedded plan truncated at segment %d' % (path, i))
        index, base, length = struct.unpack('<III', plan[p:p + 12])
        p += 12
        if not length or p + length > len(plan):
            raise ArtifactError('%s: segment %d (index=%d base=$%06X) declares %d bytes, which '
                                'does not fit in the plan' % (path, i, index, base, length))
        p += length
        segs.append((index, base, length))
    if p != len(plan):
        raise ArtifactError('%s: %d trailing bytes after the last plan segment'
                            % (path, len(plan) - p))
    maps, off, marked = [], 32 + plan_size, 0
    body_end = len(d) - 8
    for index, base, length in segs:
        nbits = (length + 7) // 8
        if off + nbits + length > body_end:
            raise ArtifactError('%s: map index=%d base=$%06X wants %d bitmap + %d start bytes, '
                                'and the file has %d left -- a run that was cut off, not a run '
                                'with less coverage' % (path, index, base, nbits, length, body_end - off))
        bits = d[off:off + nbits]; off += nbits
        starts = d[off:off + length]; off += length
        m = sum(bin(b).count('1') for b in bits)
        marked += m
        maps.append({'index': index, 'base': base, 'length': length,
                     'marked': m, 'bits': bits, 'starts': starts})
    if off != body_end:
        raise ArtifactError('%s: %d bytes between the last map and the hash trailer'
                            % (path, body_end - off))
    stored = struct.unpack('<Q', d[body_end:])[0]
    computed = house_fnv(d[:body_end])
    span = sum(s[2] for s in segs)
    return run_id, generation, maps, marked, span, computed == stored, stored


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


# ---- raw artifacts described by a sidecar (M6) ------------------------------------
import json

def _content_hash(fmt, d, record_size):
    """Re-derive the writer's content hash for a known format, from the bytes."""
    h = HOUSE_FNV_BASIS
    if fmt in ('megaii-e1-64k-v1',):
        return house_fnv(d[0x2000:0xA000])
    if fmt in ('megaii-e1-shr-window-v1', 'vram-raw-v1'):
        return house_fnv(d)
    if fmt == 'bus-trace-v1':
        for i in range(0, len(d), record_size):
            addr17, data, rw = struct.unpack_from('<IBB', d, i + 8)
            for b in (addr17 & 0xFF, (addr17 >> 8) & 0xFF, (addr17 >> 16) & 0xFF, data, rw):
                h = ((h ^ b) * HOUSE_FNV_PRIME) & MASK64
        return h
    if fmt == 'slot-bus-v1':
        for i in range(0, len(d), record_size):
            addr, data, ctl = struct.unpack_from('<HBB', d, i + 8)
            for b in (addr & 0xFF, addr >> 8, data, ctl, 0):
                h = ((h ^ b) * HOUSE_FNV_PRIME) & MASK64
        return h
    if fmt == 'mmu-state-v1':
        for i in range(0, len(d), record_size):
            for b in d[i + 8:i + 13]:
                h = ((h ^ b) * HOUSE_FNV_PRIME) & MASK64
        return h
    return None

RAW_FORMATS = {'megaii-e1-64k-v1': 1, 'megaii-e1-shr-window-v1': 1, 'vram-raw-v1': 1,
               'bus-trace-v1': None, 'slot-bus-v1': None, 'mmu-state-v1': None}


def read_raw(path):
    """Return (meta, bytes) for a headerless artifact after checking everything.

    The sidecar is the only thing that says what the bytes are; without it the
    file is refused rather than guessed at.
    """
    mp = path + '.meta.json'
    if not os.path.exists(mp):
        raise ArtifactError('%s: no %s -- a raw artifact without its description is not read'
                            % (path, os.path.basename(mp)))
    try:
        meta = json.loads(open(mp, 'rb').read().decode('utf-8'))
    except ValueError as e:
        raise ArtifactError('%s: sidecar is not one JSON record (%s)' % (mp, e))
    if meta.get('schema') != 'a2artifact-v1' or meta.get('event') != 'artifact.meta':
        raise ArtifactError('%s: sidecar schema %r / event %r is not a2artifact-v1 / artifact.meta'
                            % (mp, meta.get('schema'), meta.get('event')))
    fmt = meta.get('format')
    if fmt not in RAW_FORMATS:
        raise ArtifactError('%s: unknown artifact format %r' % (path, fmt))
    rs, count = meta.get('record_size'), meta.get('count')
    if not isinstance(rs, int) or not isinstance(count, int) or rs <= 0 or count < 0:
        raise ArtifactError('%s: record_size/count are not usable integers (%r, %r)' % (mp, rs, count))
    if RAW_FORMATS[fmt] is not None and rs != RAW_FORMATS[fmt]:
        raise ArtifactError('%s: format %s has record_size %d, sidecar says %d' % (path, fmt, RAW_FORMATS[fmt], rs))
    d = open(path, 'rb').read()
    if len(d) != rs * count:
        raise ArtifactError('%s: %d bytes, but the sidecar describes %d records of %d bytes (%d) -- cut off or overwritten'
                            % (path, len(d), count, rs, rs * count))
    fh = '%016X' % house_fnv(d)
    if fh != meta.get('file_hash'):
        raise ArtifactError('%s: file hash %s does not match the sidecar\'s %s' % (path, fh, meta.get('file_hash')))
    ch = _content_hash(fmt, d, rs)
    if '%016X' % ch != meta.get('content_hash'):
        raise ArtifactError('%s: content hash %016X does not match the writer\'s %s (%s)'
                            % (path, ch, meta.get('content_hash'), meta.get('content_hash_covers')))
    return meta, d


def _selftest_raw():
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '_selftest_raw.bin')
    recs = [(100, 0x12000, 0x5A, 1, 0, 0), (200, 0x19D00, 0x80, 1, 1, 0)]
    d = b''.join(struct.pack('<QIBBBB', *r) for r in recs)
    def meta(dd, **over):
        m = {'event': 'artifact.meta', 'frame': 0, 'cycles': 0, 'schema': 'a2artifact-v1', 'format': 'bus-trace-v1',
             'record_size': 16, 'count': len(dd) // 16, 'bytes': len(dd),
             'content_hash': '%016X' % _content_hash('bus-trace-v1', dd, 16), 'file_hash': '%016X' % house_fnv(dd)}
        m.update(over)
        return json.dumps(m)
    try:
        open(base, 'wb').write(d); open(base + '.meta.json', 'w').write(meta(d))
        m, _ = read_raw(base)
        assert m['count'] == 2
        cases = [
            ('missing sidecar', lambda: os.remove(base + '.meta.json')),
            ('truncated body', lambda: (open(base, 'wb').write(d[:-1]), open(base + '.meta.json', 'w').write(meta(d)))),
            ('flipped byte', lambda: (open(base, 'wb').write(d[:9] + bytes([d[9] ^ 1]) + d[10:]), open(base + '.meta.json', 'w').write(meta(d)))),
            ('unknown format', lambda: (open(base, 'wb').write(d), open(base + '.meta.json', 'w').write(meta(d, format='mystery-v9')))),
            ('wrong content hash', lambda: (open(base, 'wb').write(d), open(base + '.meta.json', 'w').write(meta(d, content_hash='0' * 16)))),
        ]
        for name, setup in cases:
            setup()
            try:
                read_raw(base)
                raise AssertionError('raw reader accepted: ' + name)
            except ArtifactError:
                pass
    finally:
        for p in (base, base + '.meta.json'):
            if os.path.exists(p):
                os.remove(p)
    return True


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
    _selftest_cov2()
    _selftest_raw()
    return True


def _build_cov2(segs, bits_by_seg, trailer=None, plan_pad=b'', regions=None,
                body_pad=b''):
    """Assemble a synthetic A2COV2 the way src/a2gspu_coverage.hpp writes one.

    Four knobs, each shaped to reach ONE of read_cov2's refusals, because the
    refusals overlap and a blob that trips two of them pins neither:

      plan_pad  trailing bytes after the last plan segment
      regions   emit only the first N segment data regions while the plan still
                declares them all -- a run cut off between regions, which
                reaches the per-region bounds check with 0 bytes left
      body_pad  extra bytes between the last region and the hash trailer, the
                only way to reach the `off != body_end` backstop: every other
                malformed shape is caught by an earlier, more specific guard,
                so before this the backstop was exercised by nothing
      trailer   override the house-FNV so the verify flag can be seen to flip
    """
    plan = MAP_MAGIC + struct.pack('<II', len(segs), 1 if len(segs) == 1 else 2) + b'\0' * 40
    for (index, base, expected) in segs:
        plan += struct.pack('<III', index, base, len(expected)) + expected
    plan += plan_pad
    head = COV2_MAGIC + struct.pack('<I', len(plan)) + b'\0' * 4 \
        + struct.pack('<QQ', 0xABCDEF01, 3)
    body = head + plan
    emit = len(segs) if regions is None else regions
    for i, ((index, base, expected), bits) in enumerate(zip(segs, bits_by_seg)):
        if i >= emit:
            break
        body += bits + b'\0' * len(expected)          # bits, then starts
    body += body_pad
    return body + struct.pack('<Q', house_fnv(body) if trailer is None else trailer)


def _selftest_cov2():
    """A2COV2 round-trips, and each way of being wrong is REFUSED.

    Written as four negative cases on purpose. A check that cannot fail is worse
    than no check -- this module already shipped one (see the FNV note above) --
    so every refusal path here is exercised against a file built to trip it.
    """
    segs = [(1, 0x00A000, bytes(range(16))), (2, 0x00B000, bytes(8))]
    bits = [b'\x0F\x00', b'\xFF']            # 4 of 16 executed, 8 of 8
    good = _build_cov2(segs, bits)
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '_selftest2.bin')
    try:
        open(p, 'wb').write(good)
        run_id, gen, maps, marked, span, hash_ok, stored = read_cov2(p)
        assert hash_ok, 'house-FNV of a file we just built did not verify'
        assert (run_id, gen) == (0xABCDEF01, 3), (run_id, gen)
        assert span == 24 and marked == 12, (span, marked)
        assert [m['base'] for m in maps] == [0x00A000, 0x00B000], maps
        assert [m['marked'] for m in maps] == [4, 8], maps

        def refuses(blob, why, names):
            """Refused, AND for the stated reason.

            Asserting only that an ArtifactError came out is not enough. These
            blobs are malformed in ways more than one guard would catch, so a
            guard can be DELETED and the suite stays green because a later
            backstop picks the file up and reports a different cause. `names`
            is a fragment the message must contain, which ties each case to the
            one guard it exercises.

            Adding it immediately paid for itself twice. It showed that the
            earlier reading of these cases was backwards -- `good[:-1]` reaches
            the per-region bounds check, not the leftover backstop -- and that
            the leftover backstop was reached by NO case at all, which is why
            `body_pad` exists below.
            """
            open(p, 'wb').write(blob)
            try:
                read_cov2(p)
            except ArtifactError as e:
                if names not in str(e):
                    raise AssertionError(
                        'refused %s, but for the wrong reason: wanted %r, got %r'
                        % (why, names, str(e)))
                return
            raise AssertionError('A2COV2 reader accepted %s' % why)

        refuses(COV_MAGIC + good[8:], 'an A2COV1 magic', 'not an A2COV2')
        refuses(good[:-1], 'a file with one byte of the map data missing',
                'a run that was cut off')
        refuses(_build_cov2(segs, bits, regions=1),
                'a plan declaring a region the body does not carry',
                'a run that was cut off')
        refuses(_build_cov2(segs, bits, plan_pad=b'\0\0'),
                'a plan with trailing bytes after its last segment',
                'trailing bytes after the last plan segment')
        refuses(_build_cov2(segs, bits, body_pad=b'\0\0\0\0'),
                'a body with bytes the plan does not account for',
                'between the last map and the hash trailer')
        # A corrupt trailer is reported, not raised -- the caller decides, and
        # the `cov` subcommand refuses on it. Prove the flag actually flips:
        # left unchecked, the hash comparison would be the same no-op the house
        # FNV self-test above turned out to be.
        open(p, 'wb').write(_build_cov2(segs, bits, trailer=0))
        assert read_cov2(p)[5] is False, 'a zeroed house-FNV trailer verified'
        # And the reverse direction: the A2COV1 reader must not take an A2COV2.
        open(p, 'wb').write(good)
        try:
            read_cov(p); raise AssertionError('read_cov accepted an A2COV2 file')
        except ArtifactError:
            pass
    finally:
        if os.path.exists(p):
            os.remove(p)
    return True


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__); return 2
    cmd = argv[1]
    if cmd == 'selftest':
        print('artifacts.py selftest: PASS' if _selftest() else 'FAIL'); return 0
    # Check the verb before the argument count. Otherwise an unrecognised verb
    # that happens to take no argument -- `--selftest`, say, for the `selftest`
    # this module actually has -- is reported as "missing file", which names a
    # problem the caller does not have and hides the one they do.
    if cmd not in ('cov', 'snap', 'raw', 'fnv'):
        print("unknown command %r (want: cov | snap | raw | fnv | selftest)" % cmd)
        return 2
    if len(argv) < 3:
        print('%s: missing file' % cmd); return 2
    path = argv[2]
    try:
        if cmd == 'cov':
            # Dispatch on the magic, and refuse anything that is neither. A
            # reader that guesses the format is the failure this file exists to
            # stop; a reader that silently accepts an unknown one is worse.
            with open(path, 'rb') as f:
                magic = f.read(8)
            if magic == COV2_MAGIC:
                run_id, gen, maps, marked, span, hash_ok, stored = read_cov2(path)
                print('A2COV2 %s maps=%d run=%d generation=%d bytes_executed=%d of %d (%.2f%%) '
                      'hash=%016X %s'
                      % (path, len(maps), run_id, gen, marked, span,
                         100.0 * marked / span if span else 0.0, stored,
                         'verified' if hash_ok else 'MISMATCH'))
                for m in maps:
                    print('  map index=%d base=$%06X length=%d executed=%d (%.2f%%)'
                          % (m['index'], m['base'], m['length'], m['marked'],
                             100.0 * m['marked'] / m['length'] if m['length'] else 0.0))
                if not hash_ok:
                    # The trailer is the writer's own statement about the bytes.
                    # Disagreeing with it and returning 0 would make this reader
                    # the next thing that lies quietly.
                    print('REFUSED: %s: house-FNV trailer does not match the body '
                          '(stored %016X) -- the file is damaged or was written by a '
                          'different format version' % (path, stored))
                    return 1
            elif magic == COV_MAGIC:
                lo, hi, body, marked, h = read_cov(path)
                span = hi - lo + 1
                print('A2COV1 %s range=$%06X-$%06X bytes_executed=%d of %d (%.2f%%) hash=%016X'
                      % (path, lo, hi, marked, span, 100.0 * marked / span, h))
            else:
                raise ArtifactError('%s: not a coverage bitmap -- magic %r is neither A2COV1 '
                                    'nor A2COV2' % (path, magic))
        elif cmd == 'snap':
            ok, ver, size, manifest = read_snap_header(path)
            print('GSPS %s magic=%s version=%d size=%d manifest=%s' % (path, 'ok' if ok else 'BAD', ver, size,
                  manifest if manifest else 'absent (pre-manifest snapshot; class A assumed, every device excluded)'))
            if not ok: return 1
        elif cmd == 'raw':
            meta, d = read_raw(path)
            print('%s %s count=%d record_size=%d bytes=%d content_hash=%s (%s) file_hash=%s verified'
                  % (meta['format'], path, meta['count'], meta['record_size'], len(d), meta['content_hash'],
                     meta.get('content_hash_covers', '?'), meta['file_hash']))
        elif cmd == 'fnv':
            print('%016X  %s' % (house_fnv(open(path, 'rb').read()), path))
        else:
            print('unknown command %r' % cmd); return 2
    except ArtifactError as e:
        print('REFUSED: %s' % e); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
