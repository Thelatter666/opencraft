#!/usr/bin/env python3
"""T-A2 evidence: report WHERE two screenshots differ, pixel by pixel.

md5 tells you two files are equal; the claim here is about pixels, so the
images get decoded and compared pixel by pixel (docs/05 §3.1: 截图 A/B 须解码
逐像素，md5 不等于像素). Also prints the colour histogram of the differing
pixels, which is what turns "something changed" into "the dirt side turned
magenta".

usage: pixel_diff.py <a.png> <b.png>
"""
import sys

import numpy as np
from PIL import Image


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    a = np.asarray(Image.open(argv[1]).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(argv[2]).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        print(f"size mismatch: {a.shape} vs {b.shape}")
        return 1

    diff = np.abs(a - b).sum(axis=2) > 0
    count = int(diff.sum())
    print(f"{argv[1]} vs {argv[2]}")
    print(f"  differing pixels: {count} of {diff.size}")
    if count == 0:
        return 0

    ys, xs = np.nonzero(diff)
    print(f"  bounding box: x {xs.min()}..{xs.max()}, y {ys.min()}..{ys.max()}")
    changed = b[diff]
    colors, counts = np.unique(changed.reshape(-1, 3), axis=0, return_counts=True)
    order = np.argsort(-counts)
    print("  most common colours in b at changed pixels:")
    for i in order[:6]:
        r, g, bl = colors[i]
        print(f"    rgb({r:3d},{g:3d},{bl:3d}) x{counts[i]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
