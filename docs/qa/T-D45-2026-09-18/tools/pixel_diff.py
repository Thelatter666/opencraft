#!/usr/bin/env python3
"""T-D45 evidence check: decode two screenshots and report WHERE they differ.

Two uses:
  * md5 identifies identical files, but the pixels are the claim - this decodes
    both images and counts the differing pixels plus their bounding box
    (docs/05 §3.1: 截图 A/B 须解码逐像素, md5 不等于像素);
  * while the player is dead the camera is frozen and nothing but the drops
    animates, so a small localized difference around the drop pile is evidence
    that the pile is real, and a zero difference would mean the capture returned
    a stale surface.

usage: pixel_diff.py <a.png> <b.png> [--crop x0 y0 x1 y1]
"""
import sys

import numpy as np
from PIL import Image


def main(argv):
    a = np.asarray(Image.open(argv[1]).convert("RGBA"), dtype=np.int16)
    b = np.asarray(Image.open(argv[2]).convert("RGBA"), dtype=np.int16)
    if a.shape != b.shape:
        print(f"size mismatch: {a.shape} vs {b.shape}")
        return 1
    if "--crop" in argv:
        i = argv.index("--crop")
        x0, y0, x1, y1 = (int(v) for v in argv[i + 1:i + 5])
        a, b = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
    diff = np.abs(a - b).sum(axis=2) > 0
    count = int(diff.sum())
    print(f"{argv[1]} vs {argv[2]}: {count} differing pixel(s) of {diff.size}")
    if count:
        ys, xs = np.nonzero(diff)
        print(f"  bounding box: x {xs.min()}..{xs.max()}, y {ys.min()}..{ys.max()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
