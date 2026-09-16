#!/usr/bin/env python3
"""prodosfs.py -- read a file out of a ProDOS-order volume image, nothing more.

A small, read-only ProDOS reader for the tools that derive facts from a boot
volume (gsosmap.py). It follows directory blocks and the three storage kinds a
system file uses (seedling, sapling, tree) and refuses anything else loudly: a
reader that guessed at a structure would hand its caller someone else's bytes.

    from prodosfs import read_file
    data = read_file('corpus/gsos601.po', 'SYSTEM/GS.OS')

    python prodosfs.py <volume.po> <PATH/IN/VOLUME>     # prints size and house FNV
"""
import struct, sys

BLOCK = 512


class ProdosError(Exception):
    pass


def _block(img, n):
    o = n * BLOCK
    if o + BLOCK > len(img):
        raise ProdosError('block %d is beyond the image (%d bytes)' % (n, len(img)))
    return img[o:o + BLOCK]


def _dir_entries(img, key_block):
    """Yield (storage_type, name, key_pointer, eof) for every active entry."""
    blk, first = key_block, True
    seen = set()
    while blk:
        if blk in seen:
            raise ProdosError('directory block chain loops at block %d' % blk)
        seen.add(blk)
        b = _block(img, blk)
        nxt = struct.unpack_from('<H', b, 2)[0]
        entry_len, per_block = (b[0x23], b[0x24]) if first else (0x27, 0x0D)
        if first and (entry_len != 0x27 or per_block != 0x0D):
            raise ProdosError('directory header at block %d has entry_length=%d entries_per_block=%d'
                              % (blk, entry_len, per_block))
        start = 4 + (entry_len if first else 0)
        count = per_block - (1 if first else 0)
        for i in range(count):
            e = b[start + i * 0x27: start + (i + 1) * 0x27]
            st = e[0] >> 4
            if st == 0:
                continue
            name = e[1:1 + (e[0] & 0x0F)].decode('ascii')
            key = struct.unpack_from('<H', e, 0x11)[0]
            eof = e[0x15] | (e[0x16] << 8) | (e[0x17] << 16)
            yield st, name, key, eof
        blk, first = nxt, False


def _data_blocks(img, st, key, eof):
    nblocks = (eof + BLOCK - 1) // BLOCK
    if st == 1:                                   # seedling: one data block
        return [key]
    if st == 2:                                   # sapling: one index block
        ib = _block(img, key)
        return [ib[i] | (ib[256 + i] << 8) for i in range(nblocks)]
    if st == 3:                                   # tree: a master index of index blocks
        mb = _block(img, key)
        out = []
        for m in range((nblocks + 255) // 256):
            ibn = mb[m] | (mb[256 + m] << 8)
            ib = _block(img, ibn) if ibn else bytes(BLOCK)
            for i in range(min(256, nblocks - len(out))):
                out.append(ib[i] | (ib[256 + i] << 8))
        return out
    raise ProdosError('storage type %d is not a file this reader handles' % st)


def read_file(image_path, path):
    img = open(image_path, 'rb').read()
    dir_key = 2
    parts = [p for p in path.upper().split('/') if p]
    for depth, part in enumerate(parts):
        last = depth == len(parts) - 1
        match = None
        for st, name, key, eof in _dir_entries(img, dir_key):
            if name.upper() == part:
                match = (st, key, eof)
                break
        if not match:
            raise ProdosError('%s: no entry %r' % (image_path, '/'.join(parts[:depth + 1])))
        st, key, eof = match
        if not last:
            if st != 0xD:
                raise ProdosError('%r is not a directory (storage type %d)' % (part, st))
            dir_key = key
            continue
        data = bytearray()
        for bn in _data_blocks(img, st, key, eof):
            data += _block(img, bn) if bn else bytes(BLOCK)   # a zero pointer is a sparse block
        return bytes(data[:eof])
    raise ProdosError('empty path')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    d = read_file(sys.argv[1], sys.argv[2])
    h = 1469598103934665603
    for b in d:
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    print('%s %s bytes=%d fnv=%016X' % (sys.argv[1], sys.argv[2], len(d), h))
