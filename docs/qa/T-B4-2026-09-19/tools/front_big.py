#!/usr/bin/env python3
"""T-B4 · 正面**高倍**正交出图（给人看形体与五官用；判据用 front_projection.py）。

`vox_inspect.py` 的 contact sheet 里正面那张是 22 px/体素，整只 1.7 格高时五官只占
几个像素块，肉眼数格子容易错位。本脚本复用它的解析器与光栅器，只把**正交正面**
的像素密度提高（默认 48 px/体素，仍是硬边、无插值 —— 与 §1 的"禁止抗锯齿"一致，
只是把已经量化好的格子画大）。

用法：python3 front_big.py <vox_path> <out_png> [px_per_voxel]
"""
import os
import sys

sys.dont_write_bytecode = True  # 证据目录不得落 __pycache__（docs/05 §2）

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vox_inspect as vi  # noqa: E402


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    vox_path, out_png = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) == 4 else 48
    _version, size, voxels, palette, _chunks = vi.read_vox(vox_path)

    def true_color(x, y, z, c, pal):
        return pal[c][:3] if pal[c] else (255, 0, 255)

    basis_front = vi.norm((1.0, 0.0, 0.0)), vi.norm((0.0, 0.0, 1.0)), vi.norm((0.0, 1.0, 0.0))
    w, h, buf = vi.render_view(size, voxels, palette, true_color, basis_front, scale, 4)
    vi.write_png(out_png, w, h, buf)
    print(f"wrote {out_png} ({w}x{h}) front orthographic @ {scale} px/voxel, hard-edged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
