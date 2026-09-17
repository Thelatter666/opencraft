#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A3 朝向探针：给 grass_block_side 写一张上下不对称的图（上半纯白/下半纯黑），
两张互为上下翻转，各截一张屏；再判定"哪半在屏幕上更靠上"。

判定方法（不靠肉眼看图）：
  1. M = 两张截图不同的像素集合（就是那批草方块侧面）；
  2. 对每一列 x，取 M 在该列的**最上**一个像素 y_top；
  3. 看它在"上半白"那张里是白还是黑。
     若 PNG 第 0 行 = 面的上沿 ⇒ y_top 处应当是**白**。
  4. 逐列投票，报白/黑票数。

usage: python3 orient_probe.py <assets/blocks> <mode>
    mode: white_top | black_top
"""
import os
import sys
import struct
import zlib


def write_png(path, top_rgb, bottom_rgb):
    raw = bytearray()
    for y in range(16):
        raw.append(0)
        rgb = top_rgb if y < 8 else bottom_rgb
        for _ in range(16):
            raw += bytes((rgb[0], rgb[1], rgb[2], 255))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    d, mode = sys.argv[1], sys.argv[2]
    os.makedirs(d, exist_ok=True)
    white, black = (255, 255, 255), (0, 0, 0)
    if mode == "white_top":
        write_png(os.path.join(d, "grass_block_side.png"), white, black)
    elif mode == "black_top":
        write_png(os.path.join(d, "grass_block_side.png"), black, white)
    else:
        print(__doc__)
        return 2
    print(f"probe written: {d}/grass_block_side.png ({mode})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
