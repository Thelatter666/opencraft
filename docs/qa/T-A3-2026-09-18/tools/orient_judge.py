#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A3 朝向判定：比较两张互为上下翻转的草方块侧面探针截图。

M = 两张图不同的像素（就是那批草方块侧面）。
对每一列 x 取 M 在该列的**最上**一个像素 y_top，看它在 white_top 那张里是白还是黑。
PNG 第 0 行 = 面的上沿 ⇒ y_top 处应当是**白**。

usage: python3 orient_judge.py <white_top.png> <black_top.png>
"""
import sys

import numpy as np
from PIL import Image


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    w = np.asarray(Image.open(argv[1]).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(argv[2]).convert("RGB"), dtype=np.int16)
    if w.shape != b.shape:
        print(f"size mismatch: {w.shape} vs {b.shape}")
        return 1

    diff = np.abs(w - b).sum(axis=2) > 0
    count = int(diff.sum())
    print(f"{argv[1]} vs {argv[2]}")
    print(f"  differing pixels: {count} of {diff.size}")
    if count == 0:
        print("  nothing differs - the probe did not take effect")
        return 1

    # HUD / crosshair live in the middle; restrict to columns that differ a lot
    col_count = diff.sum(axis=0)
    strong_cols = np.nonzero(col_count >= 40)[0]
    print(f"  columns with >=40 differing pixels: {len(strong_cols)} "
          f"(x {strong_cols.min()}..{strong_cols.max()})")

    # 不要用绝对阈值判黑白：引擎对面做了明暗（顶 255 / 侧X 210 / 侧Z 170 / 底 128），
    # 纯白会被压成 ~160 灰，纯黑仍是 ~0。可靠的判据是**两张之间谁更亮**。
    white_votes = black_votes = 0
    samples = []
    for x in strong_cols:
        ys = np.nonzero(diff[:, x])[0]
        y_top = int(ys.min())
        lw = int(w[y_top, x].sum())
        lb = int(b[y_top, x].sum())
        if lw > lb:
            white_votes += 1
        elif lb > lw:
            black_votes += 1
        if len(samples) < 6:
            samples.append((int(x), y_top, tuple(int(v) for v in w[y_top, x]),
                            tuple(int(v) for v in b[y_top, x])))

    total = white_votes + black_votes
    print("  first differing pixel from the top, per strong column:")
    print("    (x, y, colour in white_top shot, colour in black_top shot)")
    for s in samples:
        print(f"    x={s[0]:4d} y={s[1]:3d} W={s[2]} B={s[3]}")
    if total:
        print(f"  votes: W-brighter={white_votes} ({100.0*white_votes/total:.1f}%)  "
              f"B-brighter={black_votes} ({100.0*black_votes/total:.1f}%)")
    verdict = "PNG row 0 = top edge of the face  (correct)" if white_votes > black_votes else \
              "PNG row 0 = bottom edge  (FLIPPED)"
    print(f"  verdict: {verdict}")
    return 0 if white_votes > black_votes else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
