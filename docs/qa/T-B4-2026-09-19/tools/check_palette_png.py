#!/usr/bin/env python3
"""T-B4 · 用 **第三方** PNG 解码器（Pillow）复核本卡的调色板 PNG。

为什么要有这一步：这张 PNG 是 vox_build.py 用 Python 标准库 zlib 手写字节拼出来的，
自己写的编码器配自己写的解码器可以"一致地错"。Pillow 是别人的实现，它能读通、
且读出来的每一格 RGB 与我们声明的色表逐格相同，才说明这张 PNG 真的是合法 PNG。
（与 T-B5 原件的差异只有：名单从两只减到本卡这一只。）

用法：python3 check_palette_png.py <worktree_root>
"""
import sys

from PIL import Image

MODELS = ("blastbud",)

# colorIndex -> 期望 sRGB（契约 v2；全部取自 docs/art/01-style-guide.md §2.1）
U0 = (0x1E, 0x1E, 0x22)
WANT = {
    "blastbud": {
        0: U0,
        1: (0x46, 0x70, 0x3C),   # F1 腹体表皮
        2: (0xB4, 0x9E, 0x73),   # E3 芽冠（借色）
        3: (0x42, 0x34, 0x26),   # W0 左前足
        4: (0x42, 0x34, 0x26),   # W0 右前足
        5: (0x42, 0x34, 0x26),   # W0 左后足
        6: (0x42, 0x34, 0x26),   # W0 右后足
        9: (0xE2, 0xE6, 0xEC),   # S4 裂缝里露出的灼白内部（借色）
        10: (0x28, 0x28, 0x2C),  # O0 五官（眼/鼻/口/冠顶裂口内壁）
        17: (0x2E, 0x4A, 0x28),  # F0 腹底/后背/颈的暗绿
        18: (0x5A, 0x8C, 0x4A),  # F2 肩顶的亮绿
    },
}

# 每只模型**真正画上去**的索引（其余格填 U0，只是为了让整张图都过 §2.1 色板）
PAINTED = {
    "blastbud": [1, 2, 3, 4, 5, 6, 9, 10, 17, 18],
}


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    worst_all = 0.0
    for name in MODELS:
        path = f"{root}/assets/palettes/{name}.png"
        img = Image.open(path)
        print(f"=== {name} ===")
        print(f"file        : {path}")
        print(f"size        : {img.size}  mode {img.mode}  (must be 16x16 RGBA)")
        assert img.size == (16, 16), "palette PNG must be exactly 16x16"
        img = img.convert("RGBA")
        px = img.load()
        alphas = {px[c, r][3] for r in range(16) for c in range(16)}
        print(f"alpha set   : {sorted(alphas)} (opaque asset -> {{255}})")
        assert alphas == {255}, "palette PNG must be fully opaque"
        worst = 0.0
        bad = []
        for index in range(256):
            row, col = divmod(index, 16)
            got = px[col, row][:3]
            want = WANT[name].get(index)
            if want is None:
                want = (0x1E, 0x1E, 0x22)  # U0 未使用槽
            if got != want:
                bad.append((index, row, col, got, want))
            if index in PAINTED[name]:
                worst = max(worst, saturation(got))
        print(f"cells checked: 256; mismatches: {len(bad)}")
        for index, row, col, got, want in bad:
            print(f"  cell {index} (row {row}, col {col}): png {got} != declared {want}")
        assert not bad, "palette PNG cells do not match the declared table"
        print(f"painted-cell colours: {len(PAINTED[name])} distinct indices, "
              f"max saturation {worst:.3f} (cap 0.50)")
        assert worst <= 0.50, "saturation cap violated"
        worst_all = max(worst_all, worst)
    print(f"OK - palette reads back cell-for-cell identical "
          f"(max saturation over all painted cells {worst_all:.3f} <= 0.50)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
