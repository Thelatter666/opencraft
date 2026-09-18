#!/usr/bin/env python3
"""Independent .vox fixture writer for the T-B1 acceptance (PM side).

Deliberately NOT derived from the developer's B0 fixture: an independent writer
catches a parser that shares a wrong assumption with its own test data (the
palette index-1 rule of research/12 §5.2 is exactly the kind of thing that goes
wrong in both places at once).

Format (research/12 §5.2): 'VOX ' + int32 version + MAIN chunk (header, 0 content,
0 children) + a list of [4-byte id][int32 content size][int32 children size][content].
Only SIZE / XYZI / RGBA matter.

usage: make_vox.py <outdir>
"""
import struct
import sys
import zlib
from pathlib import Path

# Palette colours, chosen to be unmistakable on screen. NOTE the index-1 rule:
# the FIRST colour in the RGBA chunk is palette index 1, so index 0 ("no colour")
# has no entry in the file at all. If a parser indexes the chunk directly, the
# body comes out one colour off - here red/green would become green/blue.
PALETTE = [
    (255, 0, 0, 255),  # index 1  body
    (0, 255, 0, 255),  # index 2  head
    (0, 0, 255, 255),  # index 3  unused by the model, present as a decoy
]

# A 7 x 5 x 3 asymmetric model: a 5-long row along +X at the base and a 3-tall
# column at its -X end. Asymmetry is the point: it makes an axis mix-up visible.
SIZE = (7, 5, 3)
VOXELS = [(x, 0, 0, 1) for x in range(5)] + [(0, y, 0, 2) for y in range(1, 4)]


def chunk(cid: bytes, content: bytes, children: bytes = b"") -> bytes:
    return cid + struct.pack("<ii", len(content), len(children)) + content + children


def size_chunk(size=SIZE) -> bytes:
    return chunk(b"SIZE", struct.pack("<iii", *size))


def xyzi_chunk(voxels) -> bytes:
    body = struct.pack("<i", len(voxels))
    for x, y, z, c in voxels:
        body += struct.pack("<BBBB", x, y, z, c)
    return chunk(b"XYZI", body)


def rgba_chunk() -> bytes:
    body = b"".join(struct.pack("<BBBB", *c) for c in PALETTE)
    return chunk(b"RGBA", body)


def wrap(chunks: bytes) -> bytes:
    header = b"VOX " + struct.pack("<i", 150)
    main = b"MAIN" + struct.pack("<ii", 0, len(chunks))
    return header + main + chunks


def write_png(path: Path, rows) -> None:
    """16x16 RGBA PNG, no external deps (the palette channel is 16x16 by §3.3)."""
    raw = b"".join(b"\x00" + b"".join(bytes(px) for px in rows[y]) for y in range(16))

    def blk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">i", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    png = b"\x89PNG\r\n\x1a\n"
    png += blk(b"IHDR", struct.pack(">iiBBBBB", 16, 16, 8, 6, 0, 0, 0))
    png += blk(b"IDAT", zlib.compress(raw, 9))
    png += blk(b"IEND", b"")
    path.write_bytes(png)


def main(argv) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)

    good = wrap(size_chunk() + xyzi_chunk(VOXELS) + rgba_chunk())
    (out / "pm_good.vox").write_bytes(good)

    # ── damaged variants, one per row of the fallback table (research/12 §6.5) ──
    (out / "pm_bad_signature.vox").write_bytes(b"VOX2" + good[4:])
    (out / "pm_truncated.vox").write_bytes(good[: len(good) - 12])
    (out / "pm_zero_voxels.vox").write_bytes(wrap(size_chunk() + xyzi_chunk([]) + rgba_chunk()))
    (out / "pm_out_of_bounds.vox").write_bytes(wrap(size_chunk() + xyzi_chunk(VOXELS + [(SIZE[0], 0, 0, 1)]) + rgba_chunk()))
    (out / "pm_no_size.vox").write_bytes(wrap(xyzi_chunk(VOXELS) + rgba_chunk()))
    (out / "pm_huge_size.vox").write_bytes(wrap(size_chunk((4096, 5, 3)) + xyzi_chunk(VOXELS) + rgba_chunk()))
    # chunk length says 400 bytes of voxels that are not there
    lying = struct.pack("<i", 100) + b"".join(struct.pack("<BBBB", *v) for v in VOXELS)
    (out / "pm_count_lies.vox").write_bytes(wrap(size_chunk() + b"XYZI" + struct.pack("<ii", len(lying), 0) + lying + rgba_chunk()))
    # a chunk whose declared size runs past the end of the file
    overrun = wrap(size_chunk() + b"XYZI" + struct.pack("<ii", 1 << 20, 0) + b"\x00" * 16)
    (out / "pm_chunk_overrun.vox").write_bytes(overrun[: len(overrun)])

    # palette PNG: magenta (cell 1) / cyan (cell 2), so "the PNG path is live" is
    # visible by eye - the built-in RGBA of pm_good.vox is red/green. Which cell
    # maps to which palette index is the loader's business (§3.3 leaves the
    # sampling to the implementation); either way the colours on screen change
    # from red/green to magenta/cyan only if the PNG was read.
    rows = [[(0, 0, 0, 0)] * 16 for _ in range(16)]
    rows[0][1] = (255, 0, 255, 255)
    rows[0][2] = (0, 255, 255, 255)
    rows[1][0] = (255, 0, 255, 255)
    rows[2][0] = (0, 255, 255, 255)
    write_png(out / "pm_palette_magenta_cyan.png", rows)

    for f in sorted(out.iterdir()):
        print(f"{f.name}: {f.stat().st_size} bytes")
    print(f"\nmodel: SIZE {SIZE}, {len(VOXELS)} voxels, palette 3 entries (index 1..3)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
