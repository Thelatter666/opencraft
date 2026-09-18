#!/usr/bin/env python3
"""T-B4 · 正面正视投影（机器可读的"脸上有什么"判据）。

为什么要有这一步：`face_closeup.py` 出的图是给人看的，肉眼在图里数格子容易错位
（本卡 v1 就差点把 z13 的口读成 z14 的鼻）。本脚本把**从 +y 方向看到的第一格**
逐格打出来 —— 与渲染无关，只依赖占用表与颜色索引，因此可以直接当判据引用。

用法：python3 front_projection.py <vox_path> [<x_lo> <x_hi> <z_lo> <z_hi>]
      不给范围就打印整只模型。

输出：每个 z 一行（自高向低），行内每列是该 (x, z) 处**最靠前（y 最大）**那一格的
colorIndex；'.' = 该列在这一层是空的。图例见文件末尾的 LEGEND。
"""
import os
import sys

sys.dont_write_bytecode = True  # 证据目录不得落 __pycache__（docs/05 §2）

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vox_inspect as vi  # noqa: E402

# colorIndex -> 单字符（与本卡 blastbud_layers.txt 的字符表一致）
GLYPH = {1: "1", 2: "2", 3: "3", 4: "4", 5: "5", 6: "6", 9: "W", 10: "E",
         17: "s", 18: "t"}
LEGEND = ("1 躯干 F1  2 头 E3  3/4/5/6 四足 W0  W 躯干二色=裂缝里的灼白 S4  "
          "E 头二色=五官 O0  s 躯干组 F0  t 躯干组 F2")


def main():
    if len(sys.argv) not in (2, 6):
        print(__doc__)
        return 2
    path = sys.argv[1]
    _version, size, voxels, _palette, _chunks = vi.read_vox(path)
    front = {}  # (x, z) -> (y, colorIndex)
    for x, y, z, c in voxels:
        key = (x, z)
        if key not in front or y > front[key][0]:
            front[key] = (y, c)
    if len(sys.argv) == 6:
        x_lo, x_hi, z_lo, z_hi = (int(v) for v in sys.argv[2:6])
    else:
        x_lo = min(v[0] for v in voxels)
        x_hi = max(v[0] for v in voxels)
        z_lo = min(v[2] for v in voxels)
        z_hi = max(v[2] for v in voxels)

    print(f"front projection of {path}")
    print(f"columns x {x_lo}..{x_hi} (left -> right), rows z {z_hi}..{z_lo} (top -> bottom)")
    print("each cell = the colorIndex of the frontmost (max y) voxel in that column")
    print()
    header = "z |" + "".join(f"{x % 10}" for x in range(x_lo, x_hi + 1))
    print(header)
    print("-" * len(header))
    for z in range(z_hi, z_lo - 1, -1):
        cells = []
        for x in range(x_lo, x_hi + 1):
            hit = front.get((x, z))
            cells.append(GLYPH.get(hit[1], "?") if hit else ".")
        print(f"{z:>2}|" + "".join(cells))
    print()
    print("LEGEND: " + LEGEND)
    return 0


if __name__ == "__main__":
    sys.exit(main())
