#!/usr/bin/env python3
"""T-B5 追加需求（用户 2026-09-18：「一定要有明显的面部特征」）· 面部特写出图。

复用 vox_inspect.py 的解析器与**透视**光栅器，只把相机挪到脸前面 1.6 格、正对着头的中心。
它不是"游戏机位"（卡面 §2.5 的那张仍由 vox_inspect.py 出）——这张只回答一个问题：
**站在生物面前看它，脸上有没有眼睛/鼻孔/嘴可读。**

用法：python3 face_closeup.py <vox_path> <out_png> <head_center_x> <head_center_y> <head_center_z>
      （坐标是体素中心，1 体素 = 0.1 格；给的是"看哪里"，相机自动放在它前方 1.6 格、略高 0.3 格）
"""
import math
import os
import sys

sys.dont_write_bytecode = True   # 证据目录不得落 __pycache__（docs/05 §2 过程垃圾不入仓）

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vox_inspect as vi  # noqa: E402


def main():
    if len(sys.argv) != 6:
        print(__doc__)
        return 2
    vox_path, out_png = sys.argv[1], sys.argv[2]
    tx, ty, tz = (float(v) for v in sys.argv[3:6])
    _version, size, voxels, palette, _chunks = vi.read_vox(vox_path)

    def color(x, y, z, c, pal):
        return pal[c][:3] if pal[c] else (255, 0, 255)

    # 相机：站在脸正前方（+y）1.6 格 = 16 体素处，**与目标同高**（真实玩家眼高 1.62 格
    # 与 1.8 格高生物的眼窝基本齐平）⇒ 脸的竖向不被俯角压扁
    eye = (tx, ty + 16.0, tz)
    w, h, buf, _bbox, _f = vi.render_perspective(
        size, voxels, palette, color, eye, (tx, ty, tz), 78.0, (720, 540), center=(tx, ty, tz))
    vi.write_png(out_png, w, h, buf)
    print(f"wrote {out_png} ({w}x{h}) eye={eye} target=({tx}, {ty}, {tz}) px_per_unit=78")
    return 0


if __name__ == "__main__":
    sys.exit(main())
