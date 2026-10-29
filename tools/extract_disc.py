#!/usr/bin/env python3
"""Extract a GameCube disc image into a tree that the build and the runtime read instead.

The boot area and every file in the disc's file system are written out in full, plus a
`disc.manifest` that records the disc offset each one came from. From then on the image
is not needed -- the runtime serves disc reads out of the tree and the build takes
main.dol from it -- so the image itself can be deleted.

Usage: extract_disc.py <image> [outdir] [--disc-id ID] [--dol-sha1 SHA]

`image` is a plain .iso, a .ciso, or any other format `nodtool` reads (RVZ, WIA, GCZ,
WBFS, TGC, NFS): those are converted through nodtool first, so one tool covers every
image a user may have. `outdir` defaults to <project>/naruto/rom/disc, which is where
gcn_add_game looks for it; pass <project>/naruto2/rom/disc, with that game's --disc-id and
--dol-sha1, for the second game.
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
# gcn-recomp sits beside the game folders; a checkout from before the move kept its own
# copy in naruto/. Whichever is here is the one dol.py lives in.
for _recomp in ('gcn-recomp', os.path.join('naruto', 'gcn-recomp')):
    _path = os.path.join(PROJECT, _recomp, 'recomp')
    if os.path.isdir(_path):
        sys.path.insert(0, _path)
        break
from dol import DiscImage  # noqa: E402

DEFAULT_DISC_ID = 'GNREDA'
DEFAULT_DOL_SHA1 = '19251f7f15f8b6f7e0cd1c753409151ec49d9af2'
DEFAULT_OUT = os.path.join(PROJECT, 'naruto', 'rom', 'disc')

# The boot area of a GameCube disc: the first block up to the DOL, then the DOL, then the
# file system's table. Everything the console reads before the file system.
BOOT_BIN = (0x0000, 0x0440, 'sys/boot.bin')
BI2_BIN = (0x0440, 0x2000, 'sys/bi2.bin')
APPLOADER = 0x2440                                 # to the DOL
MANIFEST = 'disc.manifest'
HOLE_CHUNK = 1 << 21                               # how much padding to test at a time


def check(cond, msg):
    if not cond:
        sys.exit('error: ' + msg)


def as_plain_image(path, workdir):
    """`path` itself, or a plain ISO made from it by nodtool.

    A plain .iso or .ciso is read directly. Anything else -- RVZ, WIA, GCZ, WBFS, TGC,
    NFS -- nodtool converts, since that is the one tool that reads them all. The caller
    owns `workdir` and removes the conversion with it.
    """
    with open(path, 'rb') as f:
        head = f.read(0x8000)
    if head[:4] == b'CISO' or all(32 <= c < 127 for c in head[:6]):
        return path
    nodtool = shutil.which('nodtool')
    if not nodtool:
        sys.exit(f'error: {path} is not a plain .iso/.ciso, and nodtool (which reads RVZ, WIA,\n'
                 'GCZ, WBFS, TGC and NFS) is not installed. Either convert it first:\n'
                 '  cargo install --locked nodtool\n'
                 '  nodtool convert <image> <image>.iso\n'
                 'or install nodtool and run this again.')
    out = os.path.join(workdir, 'disc.iso')
    print(f'converting {os.path.basename(path)} to a plain ISO with nodtool...')
    r = subprocess.run([nodtool, 'convert', path, out], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f'error: nodtool could not convert {path}:\n{r.stderr.strip() or r.stdout.strip()}')
    return out


def trim(path):
    """No FST name may leave the tree it is extracted into."""
    parts = [p for p in path.split('/') if p not in ('', '.', '..')]
    check(parts, f'refusing to extract {path!r}')
    return '/'.join(parts)


def disc_files(fst, shift):
    """Every file in the FST, as (path, disc offset, size), files/ prefixed like dtk's."""
    count = struct.unpack('>I', fst[8:12])[0]
    strtab = 12 * count

    def name(off):
        check(strtab + off < len(fst), f'FST name {off:#x} is past the string table')
        end = fst.find(b'\0', strtab + off)
        check(end > 0, f'FST name {off:#x} is not terminated')
        return fst[strtab + off:end].decode('ascii')

    out = []

    def walk(i, prefix):
        """Adds one entry and its subtree; returns the index just past it."""
        t, off, size = struct.unpack('>III', fst[12 * i:12 * i + 12])
        if t >> 24 == 1:                     # directory: its last field bounds the subtree
            sub = prefix + '/' + name(t & 0xFFFFFF)
            j, end = i + 1, size
            check(end <= count, f'directory {sub} ends past the FST')
            while j < end:
                j = walk(j, sub)
            return end
        out.append((prefix + '/' + name(t & 0xFFFFFF), off << shift, size))
        return i + 1

    i = 1
    while i < count:
        i = walk(i, 'files')
    return out


def entry_shift(fst, img, image_size):
    """How far the FST's file offsets are shifted left to get a disc offset.

    A normal disc stores them divided by four, since files are four-byte aligned. An image
    that has had the disc's padding removed and its files repacked to the offsets it keeps
    -- NKit, or a rebuilt ISO -- stores them as they are. Both readings stay inside the
    image for some discs, so the one that lands on real data wins: the other points at
    padding, which is zero.
    """
    count = struct.unpack('>I', fst[8:12])[0]
    files = []
    for i in range(count):
        t, off, size = struct.unpack('>III', fst[12 * i:12 * i + 12])
        if t >> 24 == 0 and size:
            files.append((size, off))
    files.sort(reverse=True)
    last = max((off + size for size, off in files), default=0)

    fits = [shift for shift in (2, 0) if (last << shift) <= image_size]
    check(fits, f'the disc file table does not fit in the image ({image_size} bytes)')
    if len(fits) == 1:
        return fits[0]
    for shift in fits:
        for size, off in files[:4]:
            # A quarter of the way in, past any header, where real data almost always is.
            at = (off << shift) + size // 4
            if any(img.read(at, min(size // 4 or 1, 256))):
                return shift
    return fits[0]



def cover(pieces):
    """Sort by disc offset and drop what an earlier file already covers.

    Files do not overlap on a well-formed disc, but the boot area's bounds come from the
    disc header and might overlap one; a read then has to map to exactly one file, so the
    manifest records the part of each file that no earlier one already covers.
    """
    extents, end = [], 0
    for off, size, path, foff in sorted(pieces, key=lambda p: p[0]):
        if off < end:
            skip = end - off
            if skip >= size:
                continue
            off, foff, size = off + skip, foff + skip, size - skip
        extents.append((off, size, foff, path))
        end = off + size
    return extents


def verify_tree(image, outdir, exact):
    """Read the tree back through the manifest and compare it with the image.

    The runtime serves the guest's disc reads out of the tree, so the tree has to answer
    every offset the disc's own file table names exactly as the image does. With `exact`,
    the whole image is compared, padding included. Returns the first offset that differs.
    """
    src = DiscImage(image)
    tree = DiscImage(outdir)
    if exact:
        runs = [(0, os.path.getsize(image))]
    else:
        runs = [(start, end - start) for start, end in tree.spans]
    step = 1 << 22
    for start, length in runs:
        for off in range(start, start + length, step):
            n = min(step, start + length - off)
            if tree.read(off, n) != src.read(off, n):
                return off
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('image', help='your own disc image, in any format nodtool reads')
    ap.add_argument('outdir', nargs='?', default=DEFAULT_OUT,
                    help=f'where to extract, default {DEFAULT_OUT}')
    ap.add_argument('--disc-id', default=DEFAULT_DISC_ID)
    ap.add_argument('--dol-sha1', default=DEFAULT_DOL_SHA1)
    ap.add_argument('--no-verify', action='store_true',
                    help='skip the read-back comparison of the tree with the image')
    ap.add_argument('--exact', action='store_true',
                    help='also keep padding that is not zeroes, so every offset of the '
                         'image is reproduced byte for byte (much larger)')
    args = ap.parse_args()
    if not os.path.isfile(args.image):
        sys.exit(f'error: no such file: {args.image}')

    workdir = tempfile.mkdtemp(prefix='gcn-disc-')
    try:
        image = as_plain_image(args.image, workdir)
        main_with(image, args)
        if not args.no_verify:
            print('verifying the tree against the image...', flush=True)
            bad = verify_tree(image, args.outdir, args.exact)
            if bad is not None:
                sys.exit(f'error: the tree differs from the image at {bad:#x}')
            print('verified: every file reads the same as from the image')
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def main_with(image, args):
    img = DiscImage(image)
    hdr = img.read(0, 0x440)
    check(hdr[:6] == args.disc_id.encode(),
          f'{args.image} is disc {hdr[:6]!r}, not {args.disc_id!r}')

    dol_off = struct.unpack('>I', hdr[0x420:0x424])[0]
    fst_off, fst_size = struct.unpack('>II', hdr[0x424:0x42C])
    dh = img.read(dol_off, 0x100)
    offs = struct.unpack('>18I', dh[0x00:0x48])
    sizes = struct.unpack('>18I', dh[0x90:0xD8])
    dol_size = max(o + s for o, s in zip(offs, sizes) if s)
    dol = img.read(dol_off, dol_size)
    sha1 = hashlib.sha1(dol).hexdigest()
    check(sha1 == args.dol_sha1,
          f'main.dol SHA-1 {sha1} is not the supported build ({args.dol_sha1})')

    pieces = [(*BOOT_BIN, 0), (*BI2_BIN, 0)]          # (disc off, size, path, file off)
    # The apploader's extent is in its header: this 0x20 bytes, then the image and its
    # trailer. The rest up to the DOL is padding.
    al = img.read(APPLOADER, 0x20)
    al_code, al_trailer = struct.unpack('>II', al[0x14:0x1C])
    pieces.append((APPLOADER, 0x20 + al_code + al_trailer, 'sys/apploader.img', 0))
    pieces.append((dol_off, dol_size, 'sys/main.dol', 0))
    pieces.append((fst_off, fst_size, 'sys/fst.bin', 0))
    image_size = os.path.getsize(image)
    fst = img.read(fst_off, fst_size)
    shift = entry_shift(fst, img, image_size)
    if shift == 0:
        print('this image is repacked without the disc padding (NKit, or rebuilt), so its'
              ' FST offsets are used as they are')
    for path, off, size in disc_files(fst, shift):
        pieces.append((off, size, trim(path), 0))

    # Between the files there is padding. A disc fills it with zeroes, and the guest never
    # reads it -- the file table is the only map -- so it is not extracted. `--exact`
    # keeps the holes that are not zeroes too, for an image whose padding is not, which
    # makes the tree answer every offset of the image byte for byte.
    extents = cover(pieces)
    if args.exact:
        holes, prev = [], 0
        for off, size, _, _ in extents:
            if off > prev:
                holes.append((prev, off))
            prev = off + size
        if prev < image_size:
            holes.append((prev, image_size))
        for start, end in holes:
            for at in range(start, end, HOLE_CHUNK):
                n = min(HOLE_CHUNK, end - at)
                if img.read(at, n).count(0) != n:
                    pieces.append((at, n, f'sys/padding/{at:08x}.bin', 0))
        extents = cover(pieces)

    os.makedirs(args.outdir, exist_ok=True)
    total = 0
    for off, size, _, path in extents:
        dst = os.path.join(args.outdir, *path.split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        data = img.read(off, size)
        check(len(data) == size, f'{path}: {len(data)} of {size} bytes read at {off:#x}')
        with open(dst, 'wb') as f:
            f.write(data)
        total += size

    with open(os.path.join(args.outdir, MANIFEST), 'w') as f:
        f.write('# GameCube disc manifest: offsets of the files beside it, extents to read them\n')
        f.write(f'disc_id {args.disc_id}\n')
        f.write(f'dol_sha1 {sha1}\n')
        f.write(f'image_size {image_size:x}\n')
        for off, size, foff, path in extents:
            f.write(f'extent {off:08x} {size:08x} {foff:08x} {path}\n')

    print(f'{len(pieces)} files, {total / (1 << 20):.1f} MiB -> {args.outdir}')
    print(f'disc {args.disc_id}, main.dol {sha1[:12]}')
    print(f'\nThe image is no longer needed; it can be deleted:\n  rm {args.image!r}')


if __name__ == '__main__':
    main()
