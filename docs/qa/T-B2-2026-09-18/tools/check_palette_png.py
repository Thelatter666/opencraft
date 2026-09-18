#!/usr/bin/env python3
"""T-B2 · 用 **第三方** PNG 解码器（Pillow）复核 assets/palettes/mossback.png。

为什么要有这一步：这张 PNG 是 vox_build.py 用 Python 标准库 zlib 手写字节拼出来的，
自己写的编码器配自己写的解码器可以"一致地错"。Pillow 是别人的实现，它能读通、
且读出来的每一格 RGB 与我们声明的色表逐格相同，才说明这张 PNG 真的是一张合法 PNG。

用法：python3 check_palette_png.py <worktree_root>
"""
import sys

from PIL import Image

# colorIndex -> 期望 sRGB（与 mossback_layers.txt / mossback_palette.txt 的字符表一致）
WANT = {
    0: (0x1E, 0x1E, 0x22),   # U0，索引 0 永不使用，填 U0
    1: (0x6E, 0x56, 0x3C), 2: (0xA8, 0x84, 0x5C),
    3: (0x42, 0x34, 0x26), 4: (0x42, 0x34, 0x26),
    5: (0x42, 0x34, 0x26), 6: (0x42, 0x34, 0x26),
    7: (0x46, 0x70, 0x3C), 8: (0x2E, 0x2A, 0x3A),
    9: (0x8A, 0x6E, 0x4C), 10: (0x42, 0x34, 0x26), 11: (0x64, 0x4C, 0x3C),
    12: (0x60, 0x92, 0x4E), 13: (0x46, 0x70, 0x3C), 14: (0x2E, 0x4A, 0x28),
    15: (0x5A, 0x8C, 0x4A), 16: (0x7C, 0xA8, 0x62),
}


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def main():
    root = sys.argv[1]
    path = f"{root}/assets/palettes/mossback.png"
    im = Image.open(path)
    print(f"Pillow reads : {path}")
    print(f"format={im.format} mode={im.mode} size={im.size}")
    assert im.format == "PNG" and im.size == (16, 16), "not a 16x16 PNG"

    px = im.convert("RGBA").load()
    alphas = sorted({px[x, y][3] for y in range(16) for x in range(16)})
    print(f"alpha set    : {alphas} (opaque asset -> [255])")
    assert alphas == [255], "palette must be fully opaque"

    ok = True
    for idx, want in sorted(WANT.items()):
        col, row = idx % 16, idx // 16
        got = px[col, row][:3]
        same = got == want
        ok &= same
        print(f"  cell {idx:>2} (col {col:>2}, row {row}) {got}  want {want}  "
              f"{'match' if same else 'MISMATCH'}")
    assert ok, "a palette cell does not hold the declared colour"

    all_rgb = sorted({px[x, y][:3] for y in range(16) for x in range(16)})
    print(f"distinct RGB on the image: {len(all_rgb)}")
    worst = max((saturation(c), c) for c in all_rgb)
    print(f"max saturation: {worst[0]:.3f} at {worst[1]} (cap 0.50)")
    assert worst[0] <= 0.50, "palette breaks the 0.50 saturation cap"
    extra = [c for c in all_rgb if c not in WANT.values()]
    print(f"colours outside the declared table (unused cells): {extra}")
    print("VERDICT: third-party decoder agrees with the declared palette on every cell")
    return 0


if __name__ == "__main__":
    sys.exit(main())
