#!/usr/bin/env python3
"""T-B5 · 用 **第三方** PNG 解码器（Pillow）复核两张调色板 PNG。

为什么要有这一步：这两张 PNG 是 vox_build.py 用 Python 标准库 zlib 手写字节拼出来的，
自己写的编码器配自己写的解码器可以"一致地错"。Pillow 是别人的实现，它能读通、
且读出来的每一格 RGB 与我们声明的色表逐格相同，才说明这两张 PNG 真的是合法 PNG。

用法：python3 check_palette_png.py <worktree_root>
"""
import sys

from PIL import Image

# colorIndex -> 期望 sRGB（契约 v2）
WANT = {
    "mossback": {
        # ★ 本卡**一格未改**：与 T-B2b 交付的调色板逐字节相同（md5 复核见报告 §4）
        0: (0x1E, 0x1E, 0x22),
        1: (0x6E, 0x56, 0x3C), 2: (0xA8, 0x84, 0x5C),
        3: (0x42, 0x34, 0x26), 4: (0x42, 0x34, 0x26),
        5: (0x42, 0x34, 0x26), 6: (0x42, 0x34, 0x26),
        7: (0x46, 0x70, 0x3C), 8: (0x2E, 0x2A, 0x3A),
        9: (0x1E, 0x1E, 0x22), 10: (0x1E, 0x1E, 0x22), 11: (0x1E, 0x1E, 0x22),
        12: (0x1E, 0x1E, 0x22), 13: (0x1E, 0x1E, 0x22), 14: (0x1E, 0x1E, 0x22),
        15: (0x1E, 0x1E, 0x22), 16: (0x1E, 0x1E, 0x22),
        17: (0x8A, 0x6E, 0x4C), 18: (0x42, 0x34, 0x26), 19: (0x64, 0x4C, 0x3C),
        20: (0x60, 0x92, 0x4E), 21: (0x46, 0x70, 0x3C), 22: (0x2E, 0x4A, 0x28),
        23: (0x5A, 0x8C, 0x4A), 24: (0x7C, 0xA8, 0x62),
    },
    "hollow_wretch": {
        0: (0x1E, 0x1E, 0x22),
        1: (0x6E, 0x6E, 0x74), 2: (0xE2, 0xE6, 0xEC),
        3: (0x4A, 0x4A, 0x50), 4: (0x4A, 0x4A, 0x50),
        5: (0x4A, 0x4A, 0x50), 6: (0x4A, 0x4A, 0x50),
        7: (0x1E, 0x1E, 0x22), 8: (0x1E, 0x1E, 0x22),
        9: (0x2E, 0x2A, 0x3A), 10: (0x28, 0x28, 0x2C),
        11: (0xA6, 0xA6, 0xAC), 12: (0xA6, 0xA6, 0xAC),
        # ★ 本卡把 13/14 从 U0（未使用）改成 S3（骨色）：双脚（左腿/右腿的第二色）
        13: (0xA6, 0xA6, 0xAC), 14: (0xA6, 0xA6, 0xAC),
        15: (0x1E, 0x1E, 0x22), 16: (0x1E, 0x1E, 0x22),
        17: (0x80, 0x80, 0x85), 18: (0x4A, 0x4A, 0x50), 19: (0x46, 0x35, 0x29),
    },
}

# 每只模型**真正画上去**的索引（其余格填 U0，只是为了让整张图都过 §2.1 色板）
PAINTED = {
    "mossback": [1, 2, 3, 4, 5, 6, 7, 17, 19, 20, 21, 22, 23, 24],
    "hollow_wretch": [1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 17, 18, 19],
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
    for name in ("mossback", "hollow_wretch"):
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
    print(f"OK - both palettes read back cell-for-cell identical "
          f"(max saturation over all painted cells {worst_all:.3f} <= 0.50)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
