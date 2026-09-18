#!/usr/bin/env python3
"""T-B3 · 把**手写的**字符稿翻译成 assets/ 下的两个文件（hollow_wretch）。

用法：
    python3 vox_build.py <worktree_root>

    读  <root>/docs/qa/T-B3-2026-09-18/tools/hollow_wretch_layers.txt
    读  <root>/docs/qa/T-B3-2026-09-18/tools/hollow_wretch_palette.txt
    写  <root>/assets/mobs/hollow_wretch.vox
    写  <root>/assets/palettes/hollow_wretch.png

本副本 = docs/qa/T-B2b-2026-09-18/tools/vox_build.py（**契约 v2 版**）的**内容替换版**：
产线一个字节没改（同一条 .vox 布局、同一条 RGBA 错位规则、同一套字符→字节翻译），
换的是三个东西——稿件路径、画布尺寸（6×6×18）、以及**索引分配表**：

  · CHAR_TO_INDEX: 契约 v2 的**关节第二色**这一次真的被用上了
    （'H'→9 躯干二色、'E'→10 头二色、'L'→11 左臂二色、'R'→12 右臂二色）；
    躯干组字符改到 'a'/'b'/'c' → 17/18/19。
  · SWATCH: 换成冷灰石系（S0/S1/S2/S3/S5 + 眼窝 O0 + 一点土 E0），仍全部出自
    docs/art/01-style-guide.md §2.1 的 32 色表。
  · GRID: 9×14×14 → **6×6×18**（z 跨度恰 18 ⇒ 1 体素 = 0.1 格，契约 ②）。

这个脚本**不是**模型的作者。它没有几何、没有对称展开、没有填充算法、没有随机数：
逐层读字符、逐个字符查表、按 .vox 的字节布局写出去。外观改在稿子里，不在这里。
（卡面 §3.2 产线照抄；§4 禁碰一切 C++/CMake —— 本脚本只放证据目录，不接进构建。）

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
    "3": "arm_l 左臂",
    "4": "arm_r 右臂",
    "5": "leg_l 左腿",
    "6": "leg_r 右腿",
    "7": "tail 尾(未用)",
    "8": "spare 备用(未用)",
}

# 关节第二色标签（契约 v2：索引 9..16 = 关节 (idx-1) mod 8 的第二色）
SECOND_LABELS = {
    "H": (9, "body 躯干二色 = 胸前空洞"),
    "E": (10, "head 头二色 = 眼窝"),
    "L": (11, "arm_l 左臂二色 = 左手"),
    "R": (12, "arm_r 右臂二色 = 右手"),
}

# 字符 → colorIndex。'0' 故意缺席：索引 0 是"无颜色"，出现即报错。
CHAR_TO_INDEX = {
    "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7, "8": 8,
    "H": 9, "E": 10, "L": 11, "R": 12,
    "a": 17, "b": 18, "c": 19,
}

# colorIndex → 稿子里的字符（用于把索引反查成调色板颜色）
INDEX_TO_CHAR = {v: k for k, v in CHAR_TO_INDEX.items()}

# 调色板字符 → sRGB。全部出自 docs/art/01-style-guide.md §2.1。
SWATCH = {
    ".": (0x1E, 0x1E, 0x22),  # U0 未使用槽 / 索引 0
    "A": (0x4A, 0x4A, 0x50),  # S0 石系最暗斑
    "B": (0x6E, 0x6E, 0x74),  # S1 石系次暗斑
    "C": (0x80, 0x80, 0x85),  # S2 石系主色
    "D": (0xA6, 0xA6, 0xAC),  # S3 石系高光斑
    "P": (0xE2, 0xE6, 0xEC),  # S4 雪（本卡借作**漂白的颅骨**：明度不设上限，规格 §3）
    "F": (0x2E, 0x2A, 0x3A),  # S5 黑曜石
    "G": (0x28, 0x28, 0x2C),  # O0 煤
    "M": (0x46, 0x35, 0x29),  # E0 土系最暗颗粒
}

# 调色板稿里的字符 → 它填的是哪个 colorIndex 的格（'.' = 未使用槽）
SWATCH_FOR_INDEX = {
    0: ".", 1: "B", 2: "P", 3: "A", 4: "A", 5: "A", 6: "A", 7: ".", 8: ".",
    9: "F", 10: "G", 11: "D", 12: "D", 13: ".", 14: ".", 15: ".", 16: ".",
    17: "C", 18: "A", 19: "M",
}

# 契约 v2：索引 1..16 → 关节 (idx-1) mod 8；17+ → 躯干组（关节 0）。
def joint_of_index(index):
    return (index - 1) % 8 if 1 <= index <= 16 else 0

JOINT_NAMES = ["body 躯干", "head 头", "arm_l 左臂", "arm_r 右臂",
               "leg_l 左腿", "leg_r 右腿", "tail 尾", "spare 备用"]

# 契约 ②：z 跨度必须恰为 18，缩放才恰好是 1 体素 = 0.1 格。
GRID_X = 6
GRID_Y = 6
GRID_Z = 18
EXPECT_Z_SPAN = 18
# 本卡设计：6 个关节（躯干/头/左右臂/左右腿），无尾无备用。
EXPECT_JOINTS = 6


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
    """逐格抄写。row r 对应 y = GRID_Y-1-r（第 1 行 = y 最大 = 脸那一侧）。"""
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
    只会让整张调色板偏一格（颜色"看起来还行但不对"）——T-B2 实测踩过一次，
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
    """手写 PNG 编码（zlib + 结构拼装），不引入任何图像库；8 位 RGBA、不缩放。"""
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
    layers_path = os.path.join(here, "hollow_wretch_layers.txt")
    palette_path = os.path.join(here, "hollow_wretch_palette.txt")
    vox_out = os.path.join(root, "assets", "mobs", "hollow_wretch.vox")
    png_out = os.path.join(root, "assets", "palettes", "hollow_wretch.png")

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

    used = sorted({c for _, _, _, c in voxels})
    print(f"voxels: {len(voxels)} (budget 1000)")
    print(f"grid: {GRID_X} x {GRID_Y} x {GRID_Z} "
          f"(max side {max(GRID_X, GRID_Y, GRID_Z)}, cap 32)")
    print(f"colorIndex used: {used}")
    print(f"explicitly absent: 0 (colorIndex 0 is never painted)")
    labels_used = [i for i in used if i <= 8]
    second_used = [i for i in used if 9 <= i <= 16]
    body_used = [i for i in used if i >= 17]
    print(f"joint primary labels used: {labels_used} "
          f"({'; '.join(JOINT_LABELS[INDEX_TO_CHAR[i]] for i in labels_used)})")
    for i in second_used:
        label, why = SECOND_LABELS[INDEX_TO_CHAR[i]]
        assert label == i, f"{INDEX_TO_CHAR[i]!r} is labelled {label}, expected {i}"
        print(f"joint SECOND colour used: idx {i} -> joint {joint_of_index(i)} "
              f"{JOINT_NAMES[joint_of_index(i)]}  ({why})")
    print(f"body-group colours (v2: 17+): {body_used}")

    # —— 契约 v2 的硬检查 ——
    assert 0 not in used, "colorIndex 0 is forbidden"
    assert not [i for i in used if 13 <= i <= 16], (
        "indices 13..16 are unused in this model - the second colours are 9/10/11/12 only")
    assert max(body_used) <= 19, "this model's body group is 17..19 only"
    # —— 本卡的两条硬契约（几何）——
    lo = [min(v[i] for v in voxels) for i in range(3)]
    hi = [max(v[i] for v in voxels) for i in range(3)]
    span = [hi[i] - lo[i] + 1 for i in range(3)]
    print(f"span: x {span[0]}  y {span[1]}  z {span[2]}")
    assert span[2] == EXPECT_Z_SPAN, (
        f"z span is {span[2]}, but the contract needs exactly {EXPECT_Z_SPAN} "
        f"(1 voxel = 1.8/{EXPECT_Z_SPAN} = 0.1 blocks)")
    assert max(GRID_X, GRID_Y, GRID_Z) <= 32, "longest side exceeds the budget"
    assert len(voxels) <= 1000, "voxel budget exceeded"

    # —— 关节分组的机器自查（方位由独立读回器 vox_inspect.py 复核）——
    groups = {}
    for _, _, _, c in voxels:
        j = joint_of_index(c)
        groups.setdefault(j, []).append(c)
    print(f"joints present: {len(groups)} ({len(groups)} parts -> the startup log's "
          f"'joints' field)")
    for j in sorted(groups):
        print(f"  joint {j} {JOINT_NAMES[j]}: {len(groups[j])} voxels, "
              f"colorIndex {sorted(set(groups[j]))}")
    assert len(groups) == EXPECT_JOINTS, (
        f"expected {EXPECT_JOINTS} joints, got {len(groups)}")
    # 眼睛（索引 10）必须与头（索引 2）**同一个 joint** —— v2 的核心主张
    assert joint_of_index(10) == joint_of_index(2) == 1, "the eye must belong to the head joint"

    # 抗 y-up 判据 ①：头的 z 高于躯干（躯干 = 关节 0，含驼峰与驼峰色）
    head = [v for v in voxels if joint_of_index(v[3]) == 1]
    body = [v for v in voxels if joint_of_index(v[3]) == 0]
    hz = sum(v[2] for v in head) / len(head)
    bz = sum(v[2] for v in body) / len(body)
    print(f"head z centroid {hz:.2f} (max {max(v[2] for v in head)}) vs torso "
          f"{bz:.2f} (max {max(v[2] for v in body)})")
    assert min(v[2] for v in head) > max(v[2] for v in body) or hz > bz, "head is not above the torso"
    assert max(v[2] for v in head) > max(v[2] for v in body), "head is not the tallest part"
    # 抗 y-up 判据 ②（更锐）：接触地面的 z=0 层只许是腿
    bottom = sorted({joint_of_index(v[3]) for v in voxels if v[2] == lo[2]})
    print(f"lowest layer z={lo[2]} joints {bottom} "
          f"({'legs only' if all(j in (4, 5) for j in bottom) else 'NOT LEGS'})")
    assert all(j in (4, 5) for j in bottom), "the z=0 layer must be feet/legs only"

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
