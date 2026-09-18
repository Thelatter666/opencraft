#!/usr/bin/env python3
"""T-B4 · 把**手写的**字符稿翻译成 assets/ 下的 .vox + 调色板 PNG。

用法：
    python3 vox_build.py <worktree_root> blastbud

    读  <here>/blastbud_layers.txt
    读  <here>/blastbud_palette.txt
    写  <root>/assets/mobs/blastbud.vox
    写  <root>/assets/palettes/blastbud.png

与 T-B2b / T-B3 / T-B5 的原件同构（docs/qa/T-B5-2026-09-18/tools/vox_build.py）：
**脚本不是模型的作者**——它没有几何、没有对称展开、没有填充算法、没有随机数；
逐层读字符、逐个字符查表、按 .vox 的字节布局写出去。**外观改在稿子里，不在这里。**
（docs/tasks/T-B4.md §4：禁碰一切 C++/CMake —— 本脚本只放证据目录，不接进构建。）

与 T-B5 原件的差异只有两处，都是**本卡自己的模型**带来的：
  1. MODELS 表里只剩本卡这一只（T-B5 原件那张表里是 mossback + hollow_wretch；
     它们各自的逐层稿不在本证据目录里，留着就是死代码）；
  2. 本卡的画布尺寸（9×10×17）与字符表。

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

# 关节标签 → 显示名（契约 v2：1..8 主标签、9..16 关节第二色、17+ 躯干组）
JOINT_LABELS = {
    "1": "body 躯干",
    "2": "head 头",
    "3": "arm_l 左前肢/左臂",
    "4": "arm_r 右前肢/右臂",
    "5": "leg_l 左后肢/左腿",
    "6": "leg_r 右后肢/右腿",
    "7": "tail 尾",
    "8": "spare 备用(未用)",
}

# ── 每只模型一张表：画布 + 字符表 + 调色板字符表 ──────────────────────────────
# 字符 '0' **禁止**：索引 0 是"无颜色"，出现即报错。

MODELS = {
    # ── Blastbud（T-B4 v1：胀到快炸的种荚）───────────────────────────────────
    # 逐层稿 blastbud_layers.txt 是创作本体；本表只是它的字符字典。
    "blastbud": {
        "grid": (9, 10, 17),          # x 9 × y 10 × z 17（z 跨度 17 = 1.7 格 ÷ 0.1）
        "char_to_index": {
            "1": 1, "2": 2,               # 躯干主色 / 头主色
            "3": 3, "4": 4, "5": 5, "6": 6,  # 四足（左前/右前/左后/右后）
            "W": 9,                       # 躯干第二色 = 裂缝里露出的灼白内部
            "E": 10,                      # 头第二色 = 五官（眼/鼻/口/冠顶裂口内壁）
            "s": 17, "t": 18,             # 躯干组色槽：暗绿 / 亮绿
        },
        # 调色板字符 → sRGB，全部出自 docs/art/01-style-guide.md §2.1
        "swatch": {
            ".": (0x1E, 0x1E, 0x22),  # U0 未使用槽 / 索引 0
            "A": (0x46, 0x70, 0x3C),  # F1（腹体表皮）
            "B": (0xB4, 0x9E, 0x73),  # E3（芽冠：干的沙色，借色）
            "C": (0x42, 0x34, 0x26),  # W0（四足：树皮暗纹）
            "D": (0xE2, 0xE6, 0xEC),  # S4（裂缝里露出的灼白内部，借色）
            "E": (0x28, 0x28, 0x2C),  # O0（五官）
            "F": (0x2E, 0x4A, 0x28),  # F0（腹底/后背/颈的暗绿）
            "G": (0x5A, 0x8C, 0x4A),  # F2（肩顶的亮绿）
        },
        "swatch_for_index": {
            0: ".", 1: "A", 2: "B",
            3: "C", 4: "C", 5: "C", 6: "C",
            9: "D", 10: "E",
            17: "F", 18: "G",
        },
        # 本卡用了两个关节第二色：9（躯干）与 10（头）
        "second_colour_used": (9, 10),
    },
}


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def read_layers(path, grid):
    """读分层字符稿。每个 '=== layer z=N' 段后跟 GRID_Y 行、每行 GRID_X 字符。

    '#' 开头的整行是注释（稿子里的分组说明），跳过；行内不允许注释。
    """
    grid_x, grid_y, grid_z = grid
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
            assert len(line) == grid_x, (
                f"{path}:{lineno}: layer z={z} row has {len(line)} chars, want {grid_x}: {line!r}")
            rows.append(line)
    if z is not None:
        layers[z] = rows
    for z, rows in layers.items():
        assert len(rows) == grid_y, (
            f"{path}:layer z={z} has {len(rows)} rows, want {grid_y}")
    assert set(layers) == set(range(grid_z)), (
        f"{path}: layers {sorted(set(layers))} do not cover z 0..{grid_z - 1}")
    return layers


def read_palette(path, swatch):
    """读调色板稿：正好 16 行、每行 16 格，第 0 行第 0 格 = 索引 0。"""
    rows = []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if line.startswith("#") or line.startswith("===") or line == "":
                continue
            assert len(line) == 16, f"{path}:{lineno}: row has {len(line)} chars, want 16"
            for ch in line:
                assert ch in swatch, f"{path}:{lineno}: swatch {ch!r} is not in the legend"
            rows.append(line)
    assert len(rows) == 16, f"{path}: has {len(rows)} palette rows, want 16"
    return rows


def layers_to_voxels(layers, grid, char_to_index):
    """逐格抄写。row r 对应 y = GRID_Y-1-r（第 1 行 = y 最大 = 脸那一侧）。"""
    grid_x, grid_y, grid_z = grid
    voxels = []
    for z in range(grid_z):
        for r, row in enumerate(layers[z]):
            y = grid_y - 1 - r
            for x, ch in enumerate(row):
                if ch == ".":
                    continue
                assert ch in char_to_index, f"layer z={z}: character {ch!r} has no colorIndex"
                voxels.append((x, y, z, char_to_index[ch]))
    return voxels


def palette_from_swatches(palette_rows, swatch):
    """16×16 格 → 256 条 RGBA，格 (row, col) 填 colorIndex row*16+col。"""
    entries = []
    for row in palette_rows:
        for ch in row:
            cr, cg, cb = swatch[ch]
            entries.append((cr, cg, cb, 255))
    assert len(entries) == 256
    return entries


def palette_rgba_chunk(entries):
    """到 .vox 的 RGBA 块。

    ★ 规范那条错位说明落地处：**块内第 e 条 = colorIndex e+1**。
    所以 colorIndex N 写在字节偏移 (N-1)*4，不是 N*4。写错了不报错，
    只会让整张调色板偏一格（颜色"看起来还行但不对"）。
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


def build_vox(voxels, rgba, grid):
    grid_x, grid_y, grid_z = grid
    size = chunk(b"SIZE", struct.pack("<iii", grid_x, grid_y, grid_z))
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


def build_one(root, here, name):
    spec = MODELS[name]
    grid = spec["grid"]
    char_to_index = spec["char_to_index"]
    swatch = spec["swatch"]

    layers_path = os.path.join(here, f"{name}_layers.txt")
    palette_path = os.path.join(here, f"{name}_palette.txt")
    vox_out = os.path.join(root, "assets", "mobs", f"{name}.vox")
    png_out = os.path.join(root, "assets", "palettes", f"{name}.png")

    print(f"=== {name} ===")

    # —— 色板自检：饱和度上限 0.50（规格 §3）——
    worst = max((saturation(v), k, v) for k, v in swatch.items())
    print(f"swatches: {len(swatch)} colours, max saturation {worst[0]:.3f} "
          f"({worst[1]!r} {worst[2]})")
    assert worst[0] <= 0.50, "swatch violates the 0.50 saturation cap"

    layers = read_layers(layers_path, grid)
    palette_rows = read_palette(palette_path, swatch)
    voxels = layers_to_voxels(layers, grid, char_to_index)
    entries = palette_from_swatches(palette_rows, swatch)

    # —— 稿子与调色板必须一致：每个用到的索引，其标签格颜色 = 字符表里的颜色 ——
    for index in sorted({c for _, _, _, c in voxels}):
        want = swatch[spec["swatch_for_index"][index]]
        got = entries[index][:3]
        assert want == got, (
            f"colorIndex {index}: layers say {want}, palette PNG cell {index} paints {got}")

    used = sorted({c for _, _, _, c in voxels})
    print(f"voxels: {len(voxels)} (budget 1000)")
    print(f"grid: {grid[0]} x {grid[1]} x {grid[2]} "
          f"(max side {max(grid)}, cap 32)")
    print(f"colorIndex used: {used}")
    prev = [i for i in used if 9 <= i <= 16]
    print(f"joint second colours painted: {prev} "
          f"(declared {list(spec['second_colour_used'])})")
    assert 0 not in used, "colorIndex 0 is never painted"
    assert set(prev) == set(spec["second_colour_used"]), (
        "the painted joint second colours must match the declared set")
    assert not [i for i in used if 25 <= i <= 255], "colour slots 25..255 are unused here"
    assert len(voxels) <= 1000, "voxel budget exceeded"
    assert max(grid) <= 32, "longest side exceeds the budget"

    rgba = palette_rgba_chunk(entries)
    os.makedirs(os.path.dirname(vox_out), exist_ok=True)
    os.makedirs(os.path.dirname(png_out), exist_ok=True)
    with open(vox_out, "wb") as f:
        f.write(build_vox(voxels, rgba, grid))
    write_png(png_out, [entries[r * 16:(r + 1) * 16] for r in range(16)])

    print(f"wrote {vox_out} ({os.path.getsize(vox_out)} bytes)")
    print(f"wrote {png_out} ({os.path.getsize(png_out)} bytes)")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    root = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    which = sys.argv[2]
    names = sorted(MODELS) if which == "all" else [which]
    for name in names:
        build_one(root, here, name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
