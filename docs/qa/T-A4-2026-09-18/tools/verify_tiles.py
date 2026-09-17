#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A4 逐文件校验：只读 PNG 自身的字节，不依赖 art_source.py 的内存状态。

  1. PNG 签名 8 字节；IHDR 的 width=height=16、位深/色彩类型
  2. zlib 解 IDAT，按 filter 0 解出 RGBA8（本卡 18 张均由 art_source.py 以 filter 0 写出）
  3. alpha 集合（本卡 6 个方块全不透明 ⇒ 必须只有 {255}）
  4. 用到的 RGB 必须全部落在 docs/art/01-style-guide.md §2.1 的 32 色表内
  5. 每个用到的色 S ≤ 0.50（规格 §3）
  6. 无 5×5 同色正方形（规格 §4.1「不得出现 4 px 以上的实心色块」的可执行判据）
  7. 孤立点统计：某像素在四邻域内无同色者即为孤立点（规格 §4.1「不做 1 px 孤立
     胡椒点」）。**逐个列出**，用于人工核对：§4.4 允许每面 1 px 镜面反光，
     其余孤立点都视为问题。

usage: python3 verify_tiles.py <assets/blocks 目录>
"""
import os
import struct
import sys
import zlib

# docs/art/01-style-guide.md §2.1（32 个不同 RGB）
PALETTE = {
    "E0": (0x46, 0x35, 0x29), "E1": (0x64, 0x4C, 0x3C), "E2": (0x8A, 0x6B, 0x4E),
    "E3": (0xB4, 0x9E, 0x73), "E4": (0xCE, 0xBE, 0x94),
    "S0": (0x4A, 0x4A, 0x50), "S1": (0x6E, 0x6E, 0x74), "S2": (0x80, 0x80, 0x85),
    "S3": (0xA6, 0xA6, 0xAC), "S4": (0xE2, 0xE6, 0xEC), "S5": (0x2E, 0x2A, 0x3A),
    "F0": (0x2E, 0x4A, 0x28), "F1": (0x46, 0x70, 0x3C), "F2": (0x5A, 0x8C, 0x4A),
    "F3": (0x60, 0x92, 0x4E), "F4": (0x7C, 0xA8, 0x62),
    "W0": (0x42, 0x34, 0x26), "W1": (0x6E, 0x56, 0x3C), "W2": (0x8A, 0x6E, 0x4C),
    "W3": (0xA8, 0x84, 0x5C),
    "A1": (0x4E, 0x7A, 0x96), "A2": (0x7A, 0xA6, 0xBC),
    "G0": (0x96, 0xB6, 0xC6), "G1": (0xAC, 0xC8, 0xD6),
    "O0": (0x28, 0x28, 0x2C), "O1": (0xB0, 0x80, 0x5C), "O2": (0xC8, 0xA8, 0x98),
    "O3": (0xD8, 0xBC, 0x78), "O4": (0x74, 0xC6, 0xCC),
    "U0": (0x1E, 0x1E, 0x22), "U1": (0x9A, 0x9A, 0xA2), "U2": (0xE6, 0xE2, 0xD4),
}
RGB_TO_NAME = {v: k for k, v in PALETTE.items()}

BLOCKS = ["cobblestone", "gravel", "sandstone", "bedrock", "snow_block", "obsidian"]
SLOTS = ["top", "side", "bottom"]


def read_png(path):
    """只用标准库解 PNG：返回 (ihdr 元组, 16x16 的 RGBA 行列表)。"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: 不是 PNG（签名不符）")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", chunk)
        elif tag == b"IDAT":
            idat += chunk
        pos += 12 + ln
    w, h, depth, ctype = ihdr[0], ihdr[1], ihdr[2], ihdr[3]
    raw = zlib.decompress(idat)
    rows, stride = [], w * 4
    for y in range(h):
        off = y * (stride + 1)
        assert raw[off] == 0, f"{path}: 期望 filter 0"
        rows.append([tuple(raw[off + 1 + 4 * x: off + 5 + 4 * x]) for x in range(w)])
    return (w, h, depth, ctype), rows


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def max_uniform_square(rows):
    best, where = 1, (0, 0)
    for side in range(1, 17):
        found = None
        for y in range(0, 17 - side):
            for x in range(0, 17 - side):
                c = rows[y][x]
                if all(rows[y + dy][x + dx] == c for dy in range(side) for dx in range(side)):
                    found = (x, y)
                    break
            if found:
                break
        if found:
            best, where = side, found
        else:
            break
    return best, where


def lonely_pixels(rows):
    """四邻域内无同色像素的格子 -> {颜色: [(x, y), ...]}"""
    out = {}
    for y in range(16):
        for x in range(16):
            c = rows[y][x]
            nb = [rows[y + dy][x + dx]
                  for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1))
                  if 0 <= y + dy < 16 and 0 <= x + dx < 16]
            if c not in nb:
                out.setdefault(c, []).append((x, y))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out_dir = sys.argv[1]
    problems = 0
    print(f"{'file':24s} {'IHDR':>12s} {'d/ct':>6s} {'alpha':>8s} "
          f"{'colors':>6s} {'sq':>4s} {'lonely':>7s}")
    for b in BLOCKS:
        for s in SLOTS:
            name = f"{b}_{s}.png"
            path = os.path.join(out_dir, name)
            (w, h, depth, ctype), rows = read_png(path)
            alpha = sorted({p[3] for row in rows for p in row})
            rgbs = {p[:3] for row in rows for p in row}
            bad_rgb = [c for c in rgbs if c not in RGB_TO_NAME]
            bad_sat = {RGB_TO_NAME.get(c, c): round(saturation(c), 3)
                       for c in rgbs if saturation(c) > 0.50}
            sq, sqat = max_uniform_square(rows)
            lonely = lonely_pixels(rows)
            nlonely = sum(len(v) for v in lonely.values())
            lonely_names = {RGB_TO_NAME.get(c, c): v for c, v in lonely.items()}
            flag = ""
            if (w, h) != (16, 16):
                flag += f" SIZE({w}x{h})"
                problems += 1
            if alpha != [255]:
                flag += f" ALPHA({alpha})"
                problems += 1
            if bad_rgb:
                flag += f" OFF-PALETTE({bad_rgb})"
                problems += 1
            if bad_sat:
                flag += f" SATURATION({bad_sat})"
                problems += 1
            if sq >= 5:
                flag += f" SOLID({sq}x{sq}@{sqat})"
                problems += 1
            print(f"{name:24s} {w:5d}x{h:<5d} {depth}/{ctype:<4d} {str(alpha):>8s} "
                  f"{len(rgbs):6d} {sq:3d}@{str(sqat):>8s} {nlonely:3d} {flag}")
            for cname, pts in sorted(lonely_names.items()):
                print(f"    lonely {cname}: {pts}")
    print(f"\nchecked 18 files, problems: {problems}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
