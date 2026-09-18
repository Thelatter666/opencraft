#!/usr/bin/env python3
"""T-B2b · 眼睛可行性夹具（卡面 §5.5，一次性，只进证据目录、不进 assets/）。

造三个微型 .vox，差别只在头部的涂色：
  eye_a_mono.vox      头 = 索引 2 单色（基线）
  eye_b_two.vox       头 = 索引 2 + 索引 10 两色（B3 要画的眼睛的形态）
  eye_c_second.vox    头 = 只涂索引 10（第二色），主标签 2 一格不用（★判据放大器）

用法：python3 make_eye_demo_fixture.py <out_dir>

跑产品码的方法（在 worktree/build 里）：
    OPENCRAFT_ASSETS_DIR=<临时 assets 根> ./opencraft
临时根的 mobs/mossback.vox 依次换成三个夹具，比对日志行
`mob model mossback: ...` 的 triangles/joints。

判据：
  · B 与 A 形体逐格相同 ⇒ triangles 与 joints **逐字相同** ⇒ 两色头仍是一关节一次
    glDrawArrays（第二色没有把 head 拆成第二个 part）。
  · C 把区分力做到最大：v2 下索引 10 归 head ⇒ joints = 4；若是 v1（10 归躯干、
    head 无体素）⇒ joints 会变 3。C 打出 4 即产品路径下 v2 生效。

.vox 布局同 vox_build.py（无压缩小端；★RGBA 第 0 条 = colorIndex 1 的整体错位
在这份独立小实现里重做一遍，作为互证）。
"""

import os
import struct
import sys


def chunk(tag, content):
    return tag + struct.pack("<ii", len(content), 0) + content


def build(voxels, size, rgba_entries):
    sx, sy, sz = size
    xyzi = chunk(b"XYZI", struct.pack("<I", len(voxels)) + b"".join(
        bytes(v) for v in voxels))
    body = bytearray(4 * 256)
    for index in range(1, 256):
        r, g, b, a = rgba_entries[index]
        base = (index - 1) * 4
        body[base:base + 4] = bytes((r, g, b, a))
    rgba = chunk(b"RGBA", bytes(body))
    children = chunk(b"SIZE", struct.pack("<iii", *size)) + xyzi + rgba
    main = b"MAIN" + struct.pack("<ii", 0, len(children)) + children
    return b"VOX " + struct.pack("<i", 150) + main


U0 = (0x1E, 0x1E, 0x22, 255)


def palette():
    entries = [U0] * 256
    entries[1] = (0x6E, 0x56, 0x3C, 255)   # W1 躯干
    entries[2] = (0xA8, 0x84, 0x5C, 255)   # W3 头主色
    entries[5] = (0x42, 0x34, 0x26, 255)   # W0 leg_l
    entries[6] = (0x42, 0x34, 0x26, 255)   # W0 leg_r
    entries[10] = (0x2E, 0x2A, 0x3A, 255)  # S5 头第二色（眼睛）
    return entries


# 形体三版相同：腿 (1,1,0)/(4,1,0)，躯干 z=1，头 z=2。只换头的涂色。
LEGS = [(1, 1, 0, 5), (4, 1, 0, 6)]
TORSO = [(2, 1, 1, 1), (3, 1, 1, 1)]
HEAD_MONO = [(2, 1, 2, 2), (3, 1, 2, 2)]
HEAD_TWO = [(2, 1, 2, 2), (3, 1, 2, 10)]   # 同形体，右半格改第二色
HEAD_ONLY_SECOND = [(2, 1, 2, 10), (3, 1, 2, 10)]  # 全用第二色，索引 2 缺席


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    size = (6, 3, 3)
    variants = {
        "eye_a_mono.vox": LEGS + TORSO + HEAD_MONO,
        "eye_b_two.vox": LEGS + TORSO + HEAD_TWO,
        "eye_c_second.vox": LEGS + TORSO + HEAD_ONLY_SECOND,
    }
    for name, voxels in variants.items():
        data = build([(x, y, z, c) for x, y, z, c in voxels], size, palette())
        path = os.path.join(out, name)
        with open(path, "wb") as f:
            f.write(data)
        print(f"wrote {path} ({len(data)} bytes, {len(voxels)} voxels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
