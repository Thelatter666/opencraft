#!/usr/bin/env python3
"""Writes the T-B1 .vox test fixtures (docs/tasks/T-B1.md §3.1, "B0").

These files are OURS. They are not MagicaVoxel samples and contain no
third-party content: every byte below is spelled out in this script, which is
committed next to the generated files so a reviewer can check them without a
voxel editor (docs/04 red line 2/5).

    python3 make_fixtures.py            # rewrites the three files in place

Layout produced (all three share SIZE = 16x16x16):

  mob_column.vox    8 voxels in one vertical column at (x=8, y=8, z=0..7),
                    painted with three JOINT LABELS (see research/12 §4.3):
                      z = 0,1   colorIndex 5  -> joint 5 (leg)
                      z = 2..5  colorIndex 1  -> joint 1 (body)
                      z = 6,7   colorIndex 2  -> joint 2 (head)
                    The palette carries 8 opaque label colors and 247 unused
                    (all-zero) entries, exactly like an 8-color MagicaVoxel
                    palette would.

  mob_empty.vox     a syntactically valid file with numVoxels = 0. This is the
                    "parsed fine but there is nothing to draw" row of the
                    fallback table; it must decline, not crash.

  mob_column_palette.png   16x16 RGBA with one cell per colorIndex: the pixel
                    at (col, row) counted from the image's top-left holds
                    colorIndex row*16+col (cell 0 is the unused index, cell 1 is
                    the first label). Its colors are DELIBERATELY different
                    from the .vox palette so a test can tell which one won.

.vox format details that matter here (research/12 §5, the public spec at
ephtracy/voxel-model):
  * chunks are  id(4) + content_size(i32) + children_size(i32), little-endian;
  * MAIN carries every other chunk as its child and has content_size 0;
  * the RGBA chunk is 256 entries and colorIndex N reads entry N-1.
"""

import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Palette used by the .vox files. Index here is colorIndex (1-based).
# 9..255 stay zero, which is what an author who only used 8 colors exports.
VOX_PALETTE = {
    1: (180, 120, 70, 255),   # body
    2: (90, 170, 90, 255),    # head
    3: (200, 60, 60, 255),    # arm_l
    4: (60, 60, 200, 255),    # arm_r
    5: (170, 170, 60, 255),   # leg_l
    6: (60, 170, 170, 255),   # leg_r
    7: (170, 60, 170, 255),   # tail
    8: (240, 240, 240, 255),  # spare
}

# Palette for the PNG override, keyed by colorIndex: cell N = colorIndex N.
# so "which palette is live" is a one-pixel question.
PNG_PALETTE = {
    1: (255, 0, 255, 255),
    2: (0, 255, 255, 255),
    3: (255, 128, 0, 255),
    4: (0, 128, 255, 255),
    5: (255, 255, 0, 255),
    6: (128, 0, 255, 255),
    7: (0, 255, 0, 255),
    8: (32, 32, 32, 255),
}

COLUMN_VOXELS = [(8, 8, 0, 5), (8, 8, 1, 5),
                 (8, 8, 2, 1), (8, 8, 3, 1), (8, 8, 4, 1), (8, 8, 5, 1),
                 (8, 8, 6, 2), (8, 8, 7, 2)]

SIZE = (16, 16, 16)
VERSION = 150


def chunk(chunk_id: bytes, content: bytes, children: bytes = b"") -> bytes:
    return chunk_id + struct.pack("<ii", len(content), len(children)) + content + children


def size_chunk() -> bytes:
    return chunk(b"SIZE", struct.pack("<iii", *SIZE))


def xyzi_chunk(voxels) -> bytes:
    content = struct.pack("<i", len(voxels))
    for x, y, z, color in voxels:
        content += struct.pack("<BBBB", x, y, z, color)
    return chunk(b"XYZI", content)


def rgba_chunk(palette) -> bytes:
    # Entry 0 holds colorIndex 1's color: the chunk is 0-based, the indices are
    # 1-based (research/12 §5.2(a)).
    content = b""
    for color_index in range(1, 257):
        content += bytes(palette.get(color_index, (0, 0, 0, 0)))
    return chunk(b"RGBA", content)


def vox_file(voxels, palette) -> bytes:
    body = size_chunk() + xyzi_chunk(voxels) + rgba_chunk(palette)
    return b"VOX " + struct.pack("<i", VERSION) + chunk(b"MAIN", b"", body)


def png_chunk(tag: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)


def palette_png(palette) -> bytes:
    raw = b""
    for row in range(16):
        raw += b"\x00"  # filter type 0
        for col in range(16):
            color_index = row * 16 + col
            raw += bytes(palette.get(color_index, (0, 0, 0, 0)))
    ihdr = struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", zlib.compress(raw, 9)) +
            png_chunk(b"IEND", b""))


def main() -> None:
    (HERE / "mob_column.vox").write_bytes(vox_file(COLUMN_VOXELS, VOX_PALETTE))
    (HERE / "mob_empty.vox").write_bytes(vox_file([], VOX_PALETTE))
    (HERE / "mob_column_palette.png").write_bytes(palette_png(PNG_PALETTE))
    for name in ("mob_column.vox", "mob_empty.vox", "mob_column_palette.png"):
        print(f"{name}: {(HERE / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
