#!/usr/bin/env python3
"""T-A2 evidence: write the probe texture(s) that go into assets/blocks/.

Deliberately written with zlib only (no image library), so the probe files are
produced independently of both the stb_image decoder under test and the C++
fixture writer in tests/test_asset_atlas.cpp.

usage: make_probe_png.py <out.png> <mode>
  magenta   - flat pure magenta 16x16 (the task card's example probe)
  orient    - 16x16, top half magenta / bottom half white (proves the flip)
  size32    - flat magenta 32x32 (rejection probe, not used for screenshots)
"""
import struct
import sys
import zlib


def chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def png(width: int, height: int, rows: list[list[tuple[int, int, int, int]]]) -> bytes:
    raw = b"".join(
        b"\x00" + b"".join(struct.pack("BBBB", *px) for px in row) for row in rows
    )
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def flat(width: int, height: int, color: tuple[int, int, int, int]) -> bytes:
    return png(width, height, [[color] * width for _ in range(height)])


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    out, mode = argv[1], argv[2]
    magenta = (255, 0, 255, 255)
    if mode == "magenta":
        data = flat(16, 16, magenta)
    elif mode == "size32":
        data = flat(32, 32, magenta)
    elif mode == "orient":
        rows = [[magenta] * 16 for _ in range(8)] + [[(255, 255, 255, 255)] * 16 for _ in range(8)]
        data = png(16, 16, rows)
    else:
        print(f"unknown mode: {mode}")
        return 2
    with open(out, "wb") as handle:
        handle.write(data)
    print(f"{out}: {mode}, {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
