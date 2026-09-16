#!/usr/bin/env python3
"""build.py -- assemble hwtruth.s and make the bootable 800K disk that runs it.

    python tools/hwtruth/build.py [--out build/hwtruth] [--asm PATH] [--disk PATH]

Produces:
    <out>/hwtruth.bin   the program (also copied to tools/hwtruth/hwtruth.bin, the
                        committed copy the emulator gate loads without this toolchain)
    <out>/hwtruth.po    an 800K ProDOS volume HWTRUTH: a2engine's boot block,
                        RESULTS (one zeroed block the program writes its results
                        into) and HWTRUTH.SYSTEM (the program, loaded at $2000)

The tools are a2engine's: asmiigs and diskiigs, found through --asm/--disk,
A2_ASMIIGS/A2_DISKIIGS, or ROSETTA_ROOT/build/apps/<tool>/<tool>.exe.

RESBLK (the two bytes at offset 3 of the program) is patched with the RESULTS
file's block, read from the volume directory after the file is added -- so the
program writes where report.py will look, whatever block diskiigs chose.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def tool(explicit, env, name):
    if explicit:
        return explicit
    if os.environ.get(env):
        return os.environ[env]
    root = os.environ.get('ROSETTA_ROOT', r'D:\projects\toolchain\rosetta_v3')
    return os.path.join(root, 'build', 'apps', name, name + '.exe')


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit('failed: ' + ' '.join(cmd))
    return r.stdout


def prodos_entries(image):
    """(name, file_type, key_block, eof) for every volume directory entry."""
    with open(image, 'rb') as f:
        data = f.read()
    out = []
    block = 2
    first = True
    while block:
        b = data[block * 512:(block + 1) * 512]
        nxt = b[2] | (b[3] << 8)
        off = 4
        for i in range(13):
            e = b[off:off + 39]
            off += 39
            if first and i == 0:
                continue                      # the volume header
            length = e[0] & 0x0F
            if length == 0:
                continue
            name = e[1:1 + length].decode('ascii')
            out.append((name, e[0x10], e[0x11] | (e[0x12] << 8), e[0x15] | (e[0x16] << 8) | (e[0x17] << 16)))
        first = False
        block = nxt
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'hwtruth'))
    ap.add_argument('--asm')
    ap.add_argument('--disk')
    a = ap.parse_args()
    asm = tool(a.asm, 'A2_ASMIIGS', 'asmiigs')
    disk = tool(a.disk, 'A2_DISKIIGS', 'diskiigs')
    os.makedirs(a.out, exist_ok=True)
    src = os.path.join(HERE, 'hwtruth.s')
    binp = os.path.join(a.out, 'hwtruth.bin')
    run([asm, '--cpu', '65816', '--format', 'bin', '-l', os.path.join(a.out, 'hwtruth.lst'), '-o', binp, src])

    img = os.path.join(a.out, 'hwtruth.po')
    if os.path.exists(img):
        os.remove(img)
    run([disk, 'create', img, '1600', 'HWTRUTH', '--bootable'])
    zeros = os.path.join(a.out, 'results.zero')
    with open(zeros, 'wb') as f:
        f.write(bytes(512))
    run([disk, 'add', img, zeros, 'RESULTS', '06'])
    blk = [e for e in prodos_entries(img) if e[0] == 'RESULTS']
    if not blk or blk[0][3] != 512:
        raise SystemExit('RESULTS is not a one-block file in the directory')
    resblk = blk[0][2]

    with open(binp, 'rb') as f:
        prog = bytearray(f.read())
    if prog[0] != 0x4C or bytes(c & 0x7F for c in prog[5:13]) != b'HWTRUTH1':   # ASC sets the high bit
        raise SystemExit('hwtruth.bin does not start JMP / RESBLK / "HWTRUTH1"')
    committed = os.path.join(HERE, 'hwtruth.bin')
    with open(committed, 'wb') as f:
        f.write(prog)                          # RESBLK 0: the gate's load-and-run copy
    prog[3] = resblk & 0xFF
    prog[4] = resblk >> 8
    patched = os.path.join(a.out, 'HWTRUTH.SYSTEM')
    with open(patched, 'wb') as f:
        f.write(prog)
    run([disk, 'add', img, patched, 'HWTRUTH.SYSTEM', 'FF', '2000'])

    print(f'hwtruth.bin  {len(prog)} bytes')
    print(f'hwtruth.po   {img}  (RESULTS at block {resblk})')
    print(f'committed    {committed}  (RESBLK 0)')


if __name__ == '__main__':
    main()
