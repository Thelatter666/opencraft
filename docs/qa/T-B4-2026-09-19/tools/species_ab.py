#!/usr/bin/env python3
"""T-B4 · 三只生物并排对照图（"这只跟另两只不像"的机器证据）。

用途：卡面要求报告里给出剪影 / 接触表 / 非正交游戏机位图，并体现**新模型与既有两只
互不混淆**（"新旧无关度"）。同一机位、同一 px/voxel（vox_inspect 的 `render_perspective`
用固定 16 px/voxel、固定画布，不按模型自适应缩放），因此三张图可直接并排比。

对照对象是 **T-B5 已验收的 v5 交付件**（历史 QA 目录只读引用，未改动其中任何文件）。

用法：python3 species_ab.py <out_png> <png1> <png2> <png3>
"""
import os
import sys

from PIL import Image, ImageDraw

GAP = 12
BG = (24, 24, 28)


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    out_path, srcs = sys.argv[1], sys.argv[2:]
    imgs = [Image.open(p).convert("RGB") for p in srcs]
    height = max(im.height for im in imgs)
    width = sum(im.width for im in imgs) + GAP * (len(imgs) + 1)
    out = Image.new("RGB", (width, height), BG)
    x = GAP
    for im in imgs:
        out.paste(im, (x, (height - im.height) // 2))
        x += im.width + GAP
    draw = ImageDraw.Draw(out)
    x = GAP
    for im in imgs[:-1]:
        x += im.width + GAP // 2
        draw.line([(x, 0), (x, height)], fill=(200, 200, 210), width=1)
        x += GAP - GAP // 2
    out.save(out_path)
    print(f"wrote {out_path} ({out.width}x{out.height}) from "
          + " + ".join(os.path.basename(p) for p in srcs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
