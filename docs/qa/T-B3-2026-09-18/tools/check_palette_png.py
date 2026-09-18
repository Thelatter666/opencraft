#!/usr/bin/env python3
"""T-B3 · 用 **第三方** PNG 解码器（Pillow）复核 assets/palettes/hollow_wretch.png。

为什么要有这一步：这张 PNG 是 vox_build.py 用 Python 标准库 zlib 手写字节拼出来的，
自己写的编码器配自己写的解码器可以"一致地错"。Pillow 是别人的实现，它能读通、
且读出来的每一格 RGB 与我们声明的色表逐格相同，才说明这张 PNG 真的是一张合法 PNG。

本副本 = docs/qa/T-B2b-2026-09-18/tools/check_palette_png.py 的**内容替换版**：
只换资产名与 WANT 表（契约 v2 下本模型真正用到了 9..16 这一段）。

用法：python3 check_palette_png.py <worktree_root>
"""
import sys

from PIL import Image

# colorIndex -> 期望 sRGB（与 hollow_wretch_layers.txt / hollow_wretch_palette.txt 的字符表一致）
WANT = {
    0: (0x1E, 0x1E, 0x22),   # U0，索引 0 永不使用，填 U0
    1: (0x6E, 0x6E, 0x74),   # S1 躯干主色
    2: (0xE2, 0xE6, 0xEC),   # S4 颅骨（漂白骨色）
    3: (0x4A, 0x4A, 0x50), 4: (0x4A, 0x4A, 0x50),   # S0 左/右臂
    5: (0x4A, 0x4A, 0x50), 6: (0x4A, 0x4A, 0x50),   # S0 左/右腿
    7: (0x1E, 0x1E, 0x22), 8: (0x1E, 0x1E, 0x22),   # 尾/备用：未用，填 U0
    # 关节第二色 9..16：★ 本模型真正用起来了前四格（契约 v2 的首个实例）
    9: (0x2E, 0x2A, 0x3A),   # S5 躯干二色 = 胸前空洞
    10: (0x28, 0x28, 0x2C),  # O0 头二色 = 眼窝
    11: (0xA6, 0xA6, 0xAC),  # S3 左臂二色 = 左手
    12: (0xA6, 0xA6, 0xAC),  # S3 右臂二色 = 右手
    13: (0x1E, 0x1E, 0x22), 14: (0x1E, 0x1E, 0x22),
    15: (0x1E, 0x1E, 0x22), 16: (0x1E, 0x1E, 0x22),  # 右腿/尾/备用的二色：未用，填 U0
    # 躯干组 17+
    17: (0x80, 0x80, 0x85),  # S2 肩背/驼峰
    18: (0x4A, 0x4A, 0x50),  # S0 胯/腰/破布
    19: (0x46, 0x35, 0x29),  # E0 背脊锈斑
}


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def main():
    root = sys.argv[1]
    path = f"{root}/assets/palettes/hollow_wretch.png"
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
