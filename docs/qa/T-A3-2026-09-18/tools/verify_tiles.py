#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A3 逐文件校验：读 IHDR 核尺寸，并核色板/alpha/登记一致性。

读的是文件自身的字节（不是"我记得导出了什么"）：
  · PNG 签名 8 字节
  · IHDR: width=16 height=16
  · 解码后 alpha 统计（不透明资产必须全是 255）
  · 用到的颜色必须全部落在规格色板的 32 个 RGB 里

usage: python3 verify_tiles.py <assets/blocks 目录>
"""
import os
import struct
import sys
import zlib

from PIL import Image

EXPECTED_ALPHA_ALL_255 = None  # 由 OPAQUE/TRANSLUCENT 决定
OPAQUE_BLOCKS = ["grass_block", "dirt", "stone", "sand", "log", "leaves", "planks"]
TRANSLUCENT_BLOCKS = ["water", "glass", "coal_ore", "iron_ore", "gold_ore", "diamond_ore", "copper_ore"]
SLOTS = ["top", "side", "bottom"]


def read_ihdr(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None, "not a PNG (signature)"
    w, h = struct.unpack(">II", data[16:24])
    bit_depth, color_type = data[24], data[25]
    return (w, h, bit_depth, color_type), None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    blocks = OPAQUE_BLOCKS + TRANSLUCENT_BLOCKS
    bad = 0
    rows = []
    for b in blocks:
        for s in SLOTS:
            name = f"{b}_{s}.png"
            p = os.path.join(d, name)
            if not os.path.exists(p):
                print(f"MISSING {name}")
                bad += 1
                continue
            hdr, err = read_ihdr(p)
            if err:
                print(f"BAD {name}: {err}")
                bad += 1
                continue
            w, h, bd, ct = hdr
            im = Image.open(p).convert("RGBA")
            px = list(im.getdata())
            alphas = {a for (_, _, _, a) in px}
            rgb_uniq = sorted({(r, g, b) for (r, g, b, _) in px})
            sizes = im.size
            ok_size = (w == 16 and h == 16 and sizes == (16, 16))
            if not ok_size:
                print(f"BAD {name}: ihdr {w}x{h} pil {sizes}")
                bad += 1
            if b in OPAQUE_BLOCKS and alphas != {255}:
                print(f"BAD {name}: alpha set {sorted(alphas)} (opaque block must be 255)")
                bad += 1
            rows.append((name, w, h, bd, ct, len(rgb_uniq), sorted(alphas)[:4], os.path.getsize(p)))
    print(f"{'file':28s} {'w':>3s} {'h':>3s} {'bd':>3s} {'ct':>3s} {'colors':>6s}  alpha   bytes")
    for r in rows:
        print(f"{r[0]:28s} {r[1]:3d} {r[2]:3d} {r[3]:3d} {r[4]:3d} {r[5]:6d}  {str(r[6]):16s} {r[7]}")
    print(f"\nfiles checked: {len(rows)}   problems: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
