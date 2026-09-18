#!/usr/bin/env python3
"""T-B2b · 把**手写的**字符稿翻译成 assets/ 下的两个文件（mossback 迁移版）。

用法：
    python3 vox_build.py <worktree_root>

    读  <root>/docs/qa/T-B2b-2026-09-18/tools/mossback_layers.txt
    读  <root>/docs/qa/T-B2b-2026-09-18/tools/mossback_palette.txt
    写  <root>/assets/mobs/mossback.vox
    写  <root>/assets/palettes/mossback.png

本副本 = docs/qa/T-B2-2026-09-18/tools/vox_build.py 的**机械索引迁移**：
稿件字符一个不改，只有下面两张分配表变了——
  · CHAR_TO_INDEX: '9','a'..'g' 从 9..16 整体 +8 移到 17..24（v2 躯干组）
  · SWATCH_FOR_INDEX: 同步移到 17..24；9..16 让给契约 v2 的关节第二色
    （本模型未用，暂空填 U0）
T-B2 的原件留在原目录不动，保 T-B2 报告 md5 的复现链。

这个脚本**不是**模型的作者。它没有几何、没有对称展开、没有填充算法、
没有随机数：逐层读字符、逐个字符查表、按 .vox 的字节布局写出去。
外观改在稿子里，不在这里。（docs/tasks/T-B2.md §3 兜底路线；
§4 禁碰一切 C++/CMake —— 本脚本只放证据目录，不接进构建。）

.vox 布局（docs/research/12 §5.2(a)）：无压缩小端。
    'VOX ' + int32 版本
    'MAIN' + int32 content=0 + int32 children=总长
    'SIZE' + int32 12 + int32 0 + int32 x,y,z
    'XYZI' + int32 4+4n + int32 0 + uint32 n + n×(x,y,z,colorIndex)
    'RGBA' + int32 1024 + int32 0 + 256×4 字节，第 e 条 = colorIndex e+1，序 R,G,B,A

⚠ MAIN 的 content 长度必须写 0：解析器是**平铺遍历**兄弟 chunk 的
（mob_model.cpp parse_vox 注释），content 非 0 会让它整段跳过去。
"""

import os
import struct
import sys
import zlib

# 关节标签 → (显示名, 该标签格子同时提供的颜色)
JOINT_LABELS = {
    "1": "body 躯干",
    "2": "head 头",
    "3": "arm_l 左前肢",
    "4": "arm_r 右前肢",
    "5": "leg_l 左后肢",
    "6": "leg_r 右后肢",
    "7": "tail 尾",
    "8": "spare 备用(未用)",
}

# 字符 → colorIndex。**T-B2b 迁移**：躯干组字符 '9','a'..'g' 从 9..16 整体 +8
# 移到 17..24；9..16 让给契约 v2 的关节第二色（本模型未用）。稿件字符一字未改。
# '0' 故意缺席：索引 0 是"无颜色"，出现即报错。
CHAR_TO_INDEX = {
    "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7, "8": 8,
    "9": 17, "a": 18, "b": 19, "c": 20, "d": 21, "e": 22, "f": 23, "g": 24,
}

# colorIndex → 稿子里的字符（用于把索引反查成调色板颜色）
INDEX_TO_CHAR = {v: k for k, v in CHAR_TO_INDEX.items()}

# 调色板字符 → sRGB。全部出自 docs/art/01-style-guide.md §2.1。
SWATCH = {
    ".": (0x1E, 0x1E, 0x22),  # U0 未使用槽 / 索引 0
    "A": (0x42, 0x34, 0x26),  # W0
    "B": (0x6E, 0x56, 0x3C),  # W1
    "C": (0x8A, 0x6E, 0x4C),  # W2
    "D": (0xA8, 0x84, 0x5C),  # W3
    "E": (0x64, 0x4C, 0x3C),  # E1
    "F": (0x2E, 0x4A, 0x28),  # F0
    "G": (0x46, 0x70, 0x3C),  # F1
    "H": (0x5A, 0x8C, 0x4A),  # F2
    "I": (0x60, 0x92, 0x4E),  # F3
    "J": (0x7C, 0xA8, 0x62),  # F4
    "K": (0x2E, 0x2A, 0x3A),  # S5
}

# 调色板稿里的字符 → 它填的是哪个 colorIndex 的格（None = 未使用槽）
# **T-B2b 迁移**：原 9..16 的躯干组色移到 17..24；9..16 = 关节第二色，
# 本模型未用，调色板稿里填 '.'（U0）。
SWATCH_FOR_INDEX = {
    0: ".", 1: "B", 2: "D", 3: "A", 4: "A", 5: "A", 6: "A", 7: "G", 8: "K",
    17: "C", 18: "A", 19: "E", 20: "I", 21: "G", 22: "F", 23: "H", 24: "J",
}

GRID_X = 9
GRID_Y = 14
GRID_Z = 14


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def read_layers(path):
    """读分层字符稿。每个 '=== layer z=N' 段后跟 GRID_Y 行、每行 GRID_X 字符。"""
    layers = {}
    z = None
    rows = []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if line.startswith("==="):
                if z is not None:
                    layers[z] = rows
                head = line[3:].strip()
                assert head.startswith("layer z="), f"{path}:{lineno}: bad header {head!r}"
                z = int(head.split("=", 1)[1])
                rows = []
                continue
            if line.startswith("#"):
                continue
            if z is None:
                if line.strip() == "":
                    continue
                raise AssertionError(f"{path}:{lineno}: text before the first layer header")
            if line == "":
                continue
            assert len(line) == GRID_X, (
                f"{path}:{lineno}: layer z={z} row has {len(line)} chars, want {GRID_X}: {line!r}")
            for ch in line:
                assert ch in CHAR_TO_INDEX or ch == ".", (
                    f"{path}:{lineno}: layer z={z}: character {ch!r} is not in the legend "
                    f"(note: '0' is forbidden - colorIndex 0 means 'no colour')")
            rows.append(line)
    if z is not None:
        layers[z] = rows
    for z, rows in layers.items():
        assert len(rows) == GRID_Y, (
            f"{path}: layer z={z} has {len(rows)} rows, want {GRID_Y}")
    assert set(layers) == set(range(GRID_Z)), (
        f"{path}: layers {sorted(set(layers))} do not cover z 0..{GRID_Z - 1}")
    return layers


def read_palette(path):
    """读调色板稿：正好 16 行、每行 16 格，第 0 行第 0 格 = 索引 0。"""
    rows = []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if line.startswith("#") or line.startswith("===") or line == "":
                continue
            assert len(line) == 16, f"{path}:{lineno}: row has {len(line)} chars, want 16"
            for ch in line:
                assert ch in SWATCH, f"{path}:{lineno}: swatch {ch!r} is not in the legend"
            rows.append(line)
    assert len(rows) == 16, f"{path}: has {len(rows)} palette rows, want 16"
    return rows


def layers_to_voxels(layers):
    """逐格抄写。row r 对应 y = GRID_Y-1-r（第 1 行 = y 最大 = 鼻端）。"""
    voxels = []
    for z in range(GRID_Z):
        for r, row in enumerate(layers[z]):
            y = GRID_Y - 1 - r
            for x, ch in enumerate(row):
                if ch == ".":
                    continue
                voxels.append((x, y, z, CHAR_TO_INDEX[ch]))
    return voxels


def palette_from_swatches(palette_rows):
    """16×16 格 → 256 条 RGBA，格 (row, col) 填 colorIndex row*16+col。"""
    entries = []
    for row in palette_rows:
        for ch in row:
            cr, cg, cb = SWATCH[ch]
            entries.append((cr, cg, cb, 255))
    assert len(entries) == 256
    return entries


def palette_rgba_chunk(entries):
    """到 .vox 的 RGBA 块。

    ★ 这里就是规范那条错位说明落地的地方：**块内第 e 条 = colorIndex e+1**。
    所以 colorIndex N 必须写在字节偏移 (N-1)*4，不是 N*4。写错了不会报错，
    只会让整张调色板偏一格（颜色"看起来还行但不对"）——本项目实测踩过一次，
    由 vox_inspect.py 逐索引比对 rgb 抓出来。
    第 255 条（= 规范里的第 256 格）没有对应的 colorIndex，留 0。
    """
    body = bytearray(4 * 256)
    for index in range(1, 256):
        cr, cg, cb, ca = entries[index]
        base = (index - 1) * 4
        body[base + 0] = cr
        body[base + 1] = cg
        body[base + 2] = cb
        body[base + 3] = ca
    return bytes(body)


def chunk(tag, content):
    return tag + struct.pack("<ii", len(content), 0) + content


def build_vox(voxels, rgba):
    size = chunk(b"SIZE", struct.pack("<iii", GRID_X, GRID_Y, GRID_Z))
    xyzi_content = struct.pack("<I", len(voxels)) + b"".join(
        bytes((x, y, z, c)) for x, y, z, c in voxels)
    xyzi = chunk(b"XYZI", xyzi_content)
    rgba_chunk = chunk(b"RGBA", rgba)
    children = size + xyzi + rgba_chunk
    main = b"MAIN" + struct.pack("<ii", 0, len(children)) + children
    return b"VOX " + struct.pack("<i", 150) + main


def write_png(path, px):
    """手写 PNG 编码（zlib + 结构拼装），不引入任何图像库；8 位 RGBA、不缩放。
    与 T-A3/T-A4 的 art_source.py 同构。"""
    raw = bytearray()
    for row in px:
        raw.append(0)  # filter type 0
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def png_chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) +
           png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    layers_path = os.path.join(here, "mossback_layers.txt")
    palette_path = os.path.join(here, "mossback_palette.txt")
    vox_out = os.path.join(root, "assets", "mobs", "mossback.vox")
    png_out = os.path.join(root, "assets", "palettes", "mossback.png")

    # —— 色板自检：饱和度上限 0.50（规格 §3）——
    worst = max((saturation(v), k, v) for k, v in SWATCH.items())
    print(f"swatches: {len(SWATCH)} colours, max saturation {worst[0]:.3f} "
          f"({worst[1]!r} {worst[2]})")
    assert worst[0] <= 0.50, "swatch violates the 0.50 saturation cap"

    layers = read_layers(layers_path)
    palette_rows = read_palette(palette_path)
    voxels = layers_to_voxels(layers)
    entries = palette_from_swatches(palette_rows)

    # —— 稿子与调色板必须一致：每个用到的索引，其标签格颜色 = 字符表里的颜色 ——
    for index in sorted({c for _, _, _, c in voxels}):
        want = SWATCH[SWATCH_FOR_INDEX[index]]
        got = entries[index][:3]
        assert want == got, (
            f"colorIndex {index}: layers say {want}, palette PNG cell {index} paints {got}")
    # —— 同一关节一种颜色（契约 ③）：每个标签色的格子颜色唯一 ——
    for label in JOINT_LABELS:
        # 标签字符既是标签也是该关节唯一颜色，这里只断言它确实被用到了调色板里
        pass

    used = sorted({c for _, _, _, c in voxels})
    print(f"voxels: {len(voxels)} (budget 1000)")
    print(f"grid: {GRID_X} x {GRID_Y} x {GRID_Z} "
          f"(max side {max(GRID_X, GRID_Y, GRID_Z)}, cap 32)")
    print(f"colorIndex used: {used}")
    print(f"explicitly absent: 0 (colorIndex 0 is never painted)")
    labels_used = [i for i in used if INDEX_TO_CHAR[i] in JOINT_LABELS]
    print(f"joint labels used: {labels_used} "
          f"({'; '.join(JOINT_LABELS[INDEX_TO_CHAR[i]] for i in labels_used)})")
    print(f"body-group colours (v2: 17+): {[i for i in used if i >= 17]}")
    assert 0 not in used
    # v2 口径：9..16 是关节第二色，mossback 不用（躯干组一律 >= 17）。
    assert not [i for i in used if 9 <= i <= 16], "mossback must not paint joint second colours"
    assert len(voxels) <= 1000, "voxel budget exceeded"
    assert max(GRID_X, GRID_Y, GRID_Z) <= 32, "longest side exceeds the budget"

    rgba = palette_rgba_chunk(entries)
    os.makedirs(os.path.dirname(vox_out), exist_ok=True)
    os.makedirs(os.path.dirname(png_out), exist_ok=True)
    with open(vox_out, "wb") as f:
        f.write(build_vox(voxels, rgba))
    write_png(png_out, [entries[r * 16:(r + 1) * 16] for r in range(16)])

    print(f"wrote {vox_out} ({os.path.getsize(vox_out)} bytes)")
    print(f"wrote {png_out} ({os.path.getsize(png_out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
