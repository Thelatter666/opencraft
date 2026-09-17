#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A3 首批方块贴图：手写网格稿 -> 16x16 PNG。

★ 这不是"程序化生成贴图"。
每个瓦片的 256 个像素由美术总监以字符网格**逐格手写**（42 个瓦片 × 16 行 = 672 行，
全部在本文件里可见），本脚本只做三件事：

  1. 查表：字符 -> 色板 RGBA（色板见 docs/art/01-style-guide.md §2）；
  2. 合成：矿石组 = 手写石底网格 + 手写矿点坐标表（逐块逐面手写点位，不是随机撒点）；
  3. 编码：把 16×16 RGBA8 写成标准 PNG。

脚本里**没有**随机数、没有噪声函数、没有渐变插值、没有重采样/缩放/抖动。
全部 42 个瓦片的像素内容都能在本文件里逐字读到。

usage: python3 art_source.py <assets/blocks 目录>
"""
import sys
import os

# ─────────────────────────────────────────────────────────────────────────────
# 1. 色板（32 色，见 docs/art/01-style-guide.md §2）
# ─────────────────────────────────────────────────────────────────────────────
PALETTE = {
    # 土系 EARTH
    'a': (0x46, 0x35, 0x29),  # E0 深土
    'b': (0x64, 0x4C, 0x3C),  # E1 土（★沿用 atlas.cpp:47 dirt {100,76,60}）
    'c': (0x8A, 0x6B, 0x4E),  # E2 浅土
    'd': (0xB4, 0x9E, 0x73),  # E3 沙影
    'e': (0xCE, 0xBE, 0x94),  # E4 沙（★沿用 atlas.cpp:55 sand {206,190,148}）
    # 石系 STONE
    'f': (0x4A, 0x4A, 0x50),  # S0 深石
    'g': (0x6E, 0x6E, 0x74),  # S1 石影
    'h': (0x80, 0x80, 0x85),  # S2 石（★沿用 atlas.cpp:51 stone {128,128,133}）
    'i': (0xA6, 0xA6, 0xAC),  # S3 石高光 / 矿石反光
    'j': (0xE2, 0xE6, 0xEC),  # S4 雪（★沿用 atlas.cpp:83）[第二批用]
    'k': (0x2E, 0x2A, 0x3A),  # S5 黑曜石（★沿用 atlas.cpp:86）[第二批用]
    # 植生 FOLIAGE
    'l': (0x2E, 0x4A, 0x28),  # F0 深叶
    'm': (0x46, 0x70, 0x3C),  # F1 叶（★沿用 atlas.cpp:63 leaves {70,112,60}）
    'n': (0x5A, 0x8C, 0x4A),  # F2 亮叶
    'o': (0x60, 0x92, 0x4E),  # F3 草（★沿用 atlas.cpp:49 grass_block {96,146,78}）
    'p': (0x7C, 0xA8, 0x62),  # F4 草高光
    # 木 WOOD
    'q': (0x42, 0x34, 0x26),  # W0 深木 / 板缝
    'r': (0x6E, 0x56, 0x3C),  # W1 木（★沿用 atlas.cpp:61 log {110,86,60}）
    's': (0x8A, 0x6E, 0x4C),  # W2 亮木
    't': (0xA8, 0x84, 0x5C),  # W3 木板（★沿用 atlas.cpp:66 planks {168,132,92}）
    # 水 WATER
    'u': (0x4E, 0x7A, 0x96),  # A1 水（★重定：原 {56,110,170} 饱和度 0.671 超标）
    'v': (0x7A, 0xA6, 0xBC),  # A2 浅水 / 波光
    # 玻璃 GLASS（w=框 x=面 G=斜向高光；G 与 x 同 RGB、不同 alpha）
    'w': (0x96, 0xB6, 0xC6),  # G0 玻璃框（★沿用 atlas.cpp:126 边框 {150,182,198}）
    'x': (0xAC, 0xC8, 0xD6),  # G1 玻璃面（★沿用 atlas.cpp:68 glass {172,200,214}）
    'G': (0xAC, 0xC8, 0xD6),  # G1h 高光（RGB 同 G1，alpha 更高）
    # 矿石点缀 ORE
    'y': (0x28, 0x28, 0x2C),  # O0 煤（★沿用 atlas.cpp:91 coal speck {40,40,44}）
    'z': (0xB0, 0x80, 0x5C),  # O1 铜（★重定：原 {182,118,76} 饱和度 0.582 超标）
    'A': (0xC8, 0xA8, 0x98),  # O2 铁（★沿用 atlas.cpp:96 iron speck {200,168,152}）
    'B': (0xD8, 0xBC, 0x78),  # O3 金（★重定：原 {216,182,104} 饱和度 0.519 超标）
    'C': (0x74, 0xC6, 0xCC),  # O4 钻石（★沿用 atlas.cpp:99 diamond speck {116,198,204}）
    # UI（本批贴图未用，留给 HUD / 物品图标）
    'D': (0x1E, 0x1E, 0x22),  # U0 UI 底 / 描边
    'E': (0x9A, 0x9A, 0xA2),  # U1 UI 边框
    'F': (0xE6, 0xE2, 0xD4),  # U2 UI 文字 / 高亮
}

# 透明资产的 alpha（其余一律 255）
ALPHA = {'u': 170, 'v': 190, 'w': 210, 'x': 44, 'G': 120}

# ─────────────────────────────────────────────────────────────────────────────
# 2. 手写网格稿（每行 16 字符 = 一整行像素）
# ─────────────────────────────────────────────────────────────────────────────

DIRT_TOP = [
    "bbcbbbbbbbcbbbbb",
    "bbbbbbcbbbbabbcb",
    "bcbbabbbbbcbbbbb",
    "bbbbbcbbabbbcbbb",
    "abbbcbbbbbbbabbb",
    "bbbcbbbabbbbbbcb",
    "bbabbcbbbbcbbabb",
    "cbbbbbabbcbbbbbb",
    "bbbcbbbbbcbabbcb",
    "bbabbcbbabbbbbab",
    "bbbbbcbbbbbcbbbb",
    "abbbcbabbbbcbbcb",
    "bbcbabbbbbbabbbb",
    "bbbbbcbbbcbbbbab",
    "cbabbbbabbbcbbbb",
    "bbbcbabbbabbcbab",
]

DIRT_SIDE = [
    "bbbbcbbbbabbcbba",
    "bbabbcbabbbbbbcb",
    "bcbabbbbbcbbabbb",
    "bbbbbcbbbcbabbbb",
    "babcbbbabbbcbabb",
    "bbbbcbbbcbbbbbcb",
    "cbbabbbbbcbbbbab",
    "bbabbcbbabbcbbbb",
    "bbbbabbcbabbbcbb",
    "bcbabbbbbcbbbbbb",
    "bbabbcbbbbbcbabb",
    "cbbbbbabbbbcbbcb",
    "bbcbbbcbabbbbbba",
    "abbbbcbbbbcbbabb",
    "bbbcbabbbabbcbbb",
    "cbabbbbcbbbbbbab",
]

DIRT_BOTTOM = [
    "bcbabbbbcbbabbbb",
    "bbbbabbcbabbbbcb",
    "bbabcbbbbbcbbbab",
    "cbbbbbcbabbbcbbb",
    "bbbcbbabbcbabbbb",
    "abbbcbbbbbbbabcb",
    "bbbbbcbbabbbbcbb",
    "bcbbabbcbbbabbbb",
    "bbbbcbbbbcbbabab",
    "abcbabbbbcbbbbbb",
    "bbbbcbbabbbcbbab",
    "bbabbcbabbbbbcbb",
    "cbbbbbcbbbcbabbb",
    "bbbcbbabbbcbabbb",
    "babbbbcbbbbbcbab",
    "bbcbbbabbbabbcbb",
]

GRASS_TOP = [
    "oopooooonoooopoo",
    "oooooopoooooonoo",
    "noopoooooopooooo",
    "oooooonooopoomoo",
    "opooooomoooopooo",
    "oooonoooopooooon",
    "moopooooonoooopo",
    "ooooopoomooooono",
    "noooooopooooomoo",
    "ooopoomoooooopoo",
    "oooooooonoooopoo",
    "opooomoooopooooo",
    "ooooopoooonoomoo",
    "oonoooopoooopooo",
    "oopoomooooonoooo",
    "omooooopooomooop",
]

# 侧面：顶部参差绿边 + 下部泥土。绿边深度逐列手写，实测（脚本可复算）：
#   4,5,3,4,5,4,3,5,4,3,5,4,4,3,4,5   （第 0 列 4 px … 第 15 列 5 px，即 3–5 px 参差）
GRASS_SIDE = [
    "opooooonooooopoo",
    "noopoooopooooono",
    "oopooomoooopoooo",
    "oobpoobonboopboo",
    "bpbbobbnbbobbbbp",
    "bbcbbbbabbcbabbb",
    "bbabbcbabbbbbbcb",
    "bcbabbbbbcbbabbb",
    "bbbbbcbbbcbabbbb",
    "babcbbbabbbcbabb",
    "bbbbcbbbcbbbbbcb",
    "cbbabbbbbcbbbbab",
    "bbabbcbbabbcbbbb",
    "bbbbabbcbabbbcbb",
    "bcbabbbbbcbbbbbb",
    "bbabbcbbbbbcbabb",
]

# 底面 = 纯泥土（与 dirt_bottom 同稿；三面齐全约定见规格 §5）
GRASS_BOTTOM = DIRT_BOTTOM[:]

STONE_TOP = [
    "hhhihhhghhhhihhh",
    "hhghhhihhhhghhhi",
    "ihhhhfhhihhhhhhg",
    "hhhihhhghhhfhhhi",
    "hghhhhihhhhhghhh",
    "hhhfhhhghhihhhhh",
    "ihhhghhhhhhhfhhg",
    "hhhihhhhfhhhghhh",
    "hghhhhihhhhghhhi",
    "hhhfhhhghhhhihhh",
    "ihhhghhhhfhhhhhg",
    "hhhihhhhghhhihhh",
    "hghhhhfhhhihhhhg",
    "hhhfhhihhhhghhhh",
    "ihhhhghhhhhhhfhi",
    "hhhihhhhghhhihhg",
]

STONE_SIDE = [
    "hhihhhhghhhihhhh",
    "hhhghhihhhhghhhi",
    "hihhhhfhhihhhhgh",
    "hhhihhhgfhhhihhh",
    "ghhhihhhhghhhhfi",
    "hhhfhhihhhhihhhg",
    "ihhhhghhhfhhhhhi",
    "hhihhhhghhhhihhh",
    "hhhghhfhhhihhhhi",
    "ihhhhhghhhfhhhhh",
    "hhhihhhhghhihhhg",
    "ghhhhfhhhihhhhhi",
    "hihhhhihhhhghhhh",
    "hhfhhhghhhhihhhh",
    "hhhihhhhfhhhihhg",
    "hghhhihhhhghhhih",
]

STONE_BOTTOM = [
    "hihhhhghhhhihhhh",
    "hhhfhhihhhhihhhg",
    "hhihhhhghhhhihhh",
    "ghhhihhhhghhhhfi",
    "ihhhhghhhfhhhhhi",
    "hhhghhfhhhihhhhi",
    "hhhihhhhghhihhhg",
    "hihhhhfhhihhhhgh",
    "hhfhhhghhhhihhhh",
    "hhhihhhgfhhhihhh",
    "hhhihhhhfhhhihhh",
    "hghhihhhhfhhhihh",
    "ihhhhhihhhhghhhh",
    "hhhihhhghhihhhhh",
    "hhghhhihhhhfhhhi",
    "hihhhhfhhihhhhhg",
]

SAND_TOP = [
    "eeeddeeeddeedeee",
    "edeeeddeeeeddeed",
    "eeedceedeeeddeee",
    "ddeeeeddeeddeedc",
    "eeeddeedceedeeed",
    "edeeeddeeeedceed",
    "eeeddeeddeeeddee",
    "deedceeddeeddeee",
    "eeeddeedeeeddeed",
    "ddeeeedceedeeedc",
    "eeeddeeddeeddeee",
    "edeeedceedeeedde",
    "eeeddeeddeeeddee",
    "deedceeddeeddeee",
    "eeeddeedeeedceed",
    "ddeeeeddeeddeede",
]

SAND_SIDE = [
    "deeddeedceeeddee",
    "eeeddeeddeedceed",
    "edeeedceedeeedde",
    "eeeddeeddeeeddee",
    "ddeeeeddeeddeedc",
    "eeedceedeeeddeee",
    "edeeeddeeeedceed",
    "eeeddeedeeeddeed",
    "deedceeddeeddeee",
    "eeeddeedceedeeed",
    "ddeeeedceedeeedc",
    "eeeddeeddeeddeee",
    "edeeeddeeeeddeed",
    "eeedceedeeeddeee",
    "deeddeeddeedceed",
    "eeeddeeddeeeddee",
]

SAND_BOTTOM = [
    "eeddeedceeddeede",
    "deedceedeeeddeed",
    "eeeddeeddeeeddee",
    "ddeeeeddeeddeedc",
    "eeeddeedceedeeed",
    "edeeeddeeeedceed",
    "eeeddeeddeeddeee",
    "deedceeddeeddeee",
    "eeeddeedeeedceed",
    "ddeeeedceedeeedc",
    "eeeddeeddeeeddee",
    "edeeedceedeeedde",
    "eeeddeeddeeddeed",
    "deeddeedceeeddee",
    "eeedceedeeeddeee",
    "ddeeeeddeeddeede",
]

# 原木侧面：竖向树皮条纹 + 一处节疤（第 5~6 行，x6..x9 转深）
LOG_SIDE = [
    "qsrrsqrsrrsrqsrr",
    "qsrssqrsrrsrqsrr",
    "qsrrsqrsrrsrqsrr",
    "qsrrsqrqrrsrqsrr",
    "qsrssqrsrrsrqsrr",
    "qsrrsqqqqqsrqsrr",
    "qsrrsqqqqqsrrsrr",
    "qsrrsqrsrrsrqsrr",
    "qsrssqrsrrsrqsrr",
    "qsrrsqrsrrsrqsrr",
    "qsrrsqrqrrsrqsrr",
    "qsrssqrsrrsrqqrr",
    "qsrrsqrsrrsrqsrr",
    "qsrrsqrsrrsrqsrr",
    "qsrssqrsrrsrqsrr",
    "qsrrsqrsrrsrqsrr",
]

# 原木顶/底：年轮（手写同心环，逐行字符串）
_RIM = "q" * 16                                        # 树皮外圈（2 行）
_R2 = "qq" + "s" * 12 + "qq"                          # 第 2 环
_R4 = "qq" + "ss" + "r" * 8 + "ss" + "qq"             # 第 4 环
_R6 = "qq" + "ss" + "rr" + "ssss" + "rr" + "ss" + "qq"
_R7 = "qq" + "ss" + "rr" + "s" + "qq" + "s" + "rr" + "ss" + "qq"        # 中心
_R7C = "qq" + "ss" + "rr" + "s" + "qq" + "q" * 7      # 中心 + 向右贯通的径向裂

LOG_TOP = [
    _RIM, _RIM, _R2, _R2, _R4, _R4, _R6, _R7,
    _R7, _R6, _R4, _R4, _R2, _R2, _RIM, _RIM,
]

LOG_BOTTOM = [
    _RIM, _RIM, _R2, _R2, _R4, _R4, _R6, _R7C,
    _R7C, _R6, _R4, _R4, _R2, _R2, _RIM, _RIM,
]

# 树叶：同一份 16 行手稿，三面用不同的**手写行序**打散（避免三面出现同一处特征）
LEAVES = [
    "mlmmnnmlmmllmnmm",
    "lmmnmlmmnnmlmmlm",
    "mmnmlmmnmlmmnmlm",
    "nmlmmnmmllmnmmnl",
    "mmlnnmlmmnmlmmnm",
    "lmmnmlmmnnmlmmlm",
    "mmnmlmmnmlmmnmlm",
    "nmmllmnmmnmlmmnl",
    "mlmmnmlmmnnmlmmm",
    "lmmnmlmmnmlmmnmm",
    "mmnmmllmnmmnmlmn",
    "nmlmmnmlmmnnmlmm",
    "mmlnnmlmmnmlmmnm",
    "lmmnmlmmnnmlmmlm",
    "mmnmlmmnmlmmnmlm",
    "mlmmllmnmmnmlmmn",
]
LEAVES_TOP_ORDER = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
LEAVES_SIDE_ORDER = [3, 7, 1, 11, 5, 9, 0, 13, 6, 2, 14, 8, 10, 4, 15, 12]
LEAVES_BOTTOM_ORDER = [8, 2, 12, 5, 0, 10, 14, 3, 9, 6, 1, 13, 7, 11, 4, 15]

# 木板顶/底：横向四块（第 3/7/11/15 行为板缝），其余行手写纹理
PLANK_ROWS = [
    "tttsttttttsstttt",
    "ttttttsttttttttt",
    "tsstttttttstttst",
    "qqqqqqqqqqqqqqqq",
    "tttttsstttttsttt",
    "ttsttttttttttstt",
    "ttttttssttttttts",
    "qqqqqqqqqqqqqqqq",
    "ttsstttttstttttt",
    "ttttttttstttttts",
    "tstttttttttssttt",
    "qqqqqqqqqqqqqqqq",
    "ttttttsttttssttt",
    "ttstttttttttttst",
    "tttttssttttttttt",
    "qqqqqqqqqqqqqqqq",
]

# 木板侧面：竖向四块（第 3/7/11/15 列为板缝）；横向端接头错缝——
#   第 0、2 竖块在第 5 行接头，第 1、3 竖块在第 11 行接头（逐格手写，非随机）
PLANK_SIDE = [
    "tstqttsqsttqtstq",
    "ttsqsttqttsqsttq",
    "tstqttsqstsqttsq",
    "tttqtstqttsqsttq",
    "tstqtttqsttqttsq",
    "qqqqtttqqqqqtttq",
    "ttsqtttqsttqtttq",
    "sttqttsqtttqtstq",
    "tttqsttqttsqttsq",
    "tstqtttqtstqtttq",
    "ttsqtstqtttqsttq",
    "tttqqqqqtttqqqqq",
    "sttqttsqsttqttsq",
    "tttqttsqtttqtstq",
    "tstqtttqttsqtttq",
    "ttsqsttqttsqsttq",
]

_GROW = "w" * 16             # 玻璃外框行
_GROW1 = "w" + "x" * 14 + "w"  # 玻璃面行

WATER_TOP = [
    "uuuuvvuuuuuuvvuu",
    "uuuvvuuuuuuvvuuu",
    "uuvvuuuuuuvvuuuu",
    "uvvuuuuuuuvvuuuu",
    "vvuuuuuuuuvvuuuu",
    "vuuuuuuuuuvvuuuu",
    "uuuuuuuuuvvuuuuv",
    "uuuuuuuuvvuuuuuu",
    "uuuuuuvvuuuuuuvv",
    "uuuuvvuuuuuuvvuu",
    "uuuvvuuuuuuvvuuu",
    "uuvvuuuuuuvvuuuu",
    "uvvuuuuuuuvvuuuu",
    "vvuuuuuuuuvvuuuu",
    "vuuuuuuuuuvvuuuu",
    "uuuuuuuuuvvuuuuv",
]

WATER_SIDE = [
    "vvvvvvvvvvvvvvvv",
    "uuuuuuuuuuuuuuuu",
    "vvvvvvvvvvvvvvuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuvvvvvvvvvvvvvv",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "vvvvvvvvvvvvvvvv",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "vvvvvvvvvvvvvuuu",
    "uuuuuuuuuuuuuuuu",
]

WATER_BOTTOM = [
    "uuuuuuuuuuuuuuuu",
    "uuuuvvuuuuuuuuuu",
    "uuvvuuuuuuuuuuuu",
    "uuuuuuuuuuvvuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuvvuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuvv",
    "uuvvuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuvvuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuvvuuuuuuuu",
    "uuuuuuuuuuuuuuuu",
    "uuuuuuuuuuvvuuuu",
    "uuuuuuuuuuuuuuuu",
]


def _glass_diag(reverse):
    """手写玻璃斜向高光：2 px 宽，逐行位移 1 px（第 1..12 行）。

    高光必须**停在边框内侧**：reverse=False 时行 12 的 x0=0 会压掉左边框，
    故把它裁到 x0=1（与右端 x0+1=13 对称），保证四边框完整。
    """
    rows = []
    for r in range(1, 13):
        x0 = (r + 1) if reverse else (12 - r)
        x0 = min(max(x0, 1), 13)
        row = ["x"] * 16
        row[0] = "w"
        row[15] = "w"
        row[x0] = "G"
        row[x0 + 1] = "G"
        rows.append("".join(row))
    return rows


GLASS_TOP = [_GROW] + _glass_diag(False) + [_GROW1, _GROW1, _GROW]
GLASS_SIDE = [_GROW] + _glass_diag(True) + [_GROW1, _GROW1, _GROW]
_GC = "w" + "x" * 12 + "GG" + "w"
_GC2 = "w" + "x" * 11 + "GG" + "x" + "w"
GLASS_BOTTOM = [_GROW, _GROW1, _GROW1, _GC, _GC2] + [_GROW1] * 10 + [_GROW]

# ─────────────────────────────────────────────────────────────────────────────
# 3. 矿石组：手写石底（stone 三面）+ 手写矿点/高光坐标表
# ─────────────────────────────────────────────────────────────────────────────
STONE_BY_SLOT = {0: STONE_TOP, 1: STONE_SIDE, 2: STONE_BOTTOM}

# 坐标一律 (x, y)，y 从上往下（与 PNG 行序一致）
ORE_SPECKS = {
    "coal_ore": {  # 煤是哑光，不打高光
        "color": "y",
        0: {"ore": [(2, 2), (3, 2), (2, 3), (3, 3), (9, 5), (10, 5), (9, 6),
                    (5, 10), (6, 10), (7, 10), (5, 11), (6, 11), (12, 12), (13, 12), (12, 13)],
            "shine": None},
        1: {"ore": [(4, 3), (5, 3), (4, 4), (11, 7), (12, 7), (11, 8), (12, 8),
                    (2, 11), (3, 11), (2, 12), (8, 13), (9, 13)],
            "shine": None},
        2: {"ore": [(6, 4), (7, 4), (6, 5), (12, 9), (13, 9), (12, 10),
                    (3, 12), (4, 12), (3, 13), (9, 2), (10, 2)],
            "shine": None},
    },
    "copper_ore": {
        "color": "z",
        0: {"ore": [(3, 4), (4, 4), (3, 5), (10, 8), (11, 8), (10, 9), (11, 9), (6, 12), (7, 12)],
            "shine": (4, 3)},
        1: {"ore": [(5, 6), (6, 6), (5, 7), (12, 3), (13, 3), (12, 4), (2, 10), (3, 10), (2, 11)],
            "shine": (6, 5)},
        2: {"ore": [(8, 5), (9, 5), (8, 6), (13, 11), (14, 11), (4, 13), (5, 13)],
            "shine": (9, 4)},
    },
    "iron_ore": {
        "color": "A",
        0: {"ore": [(11, 3), (12, 3), (11, 4), (12, 4), (2, 6), (3, 6), (2, 7),
                    (7, 11), (8, 11), (7, 12)],
            "shine": (10, 2)},
        1: {"ore": [(6, 2), (7, 2), (6, 3), (13, 9), (14, 9), (13, 10), (3, 12), (4, 12), (3, 13)],
            "shine": (7, 1)},
        2: {"ore": [(9, 7), (10, 7), (9, 8), (2, 4), (3, 4), (12, 13), (13, 13), (12, 14)],
            "shine": (10, 6)},
    },
    "gold_ore": {
        "color": "B",
        0: {"ore": [(4, 3), (5, 3), (4, 4), (12, 7), (13, 7), (12, 8), (8, 12), (9, 12), (8, 13)],
            "shine": (5, 2)},
        1: {"ore": [(7, 5), (8, 5), (7, 6), (2, 9), (3, 9), (2, 10), (11, 13), (12, 13), (11, 14)],
            "shine": (8, 4)},
        2: {"ore": [(13, 4), (14, 4), (13, 5), (5, 11), (6, 11), (5, 12), (9, 2), (10, 2)],
            "shine": (14, 3)},
    },
    "diamond_ore": {
        "color": "C",
        0: {"ore": [(10, 4), (11, 4), (10, 5), (3, 9), (4, 9), (3, 10), (13, 12), (14, 12)],
            "shine": (11, 3)},
        1: {"ore": [(6, 8), (7, 8), (6, 9), (12, 2), (13, 2), (3, 13), (4, 13), (3, 14)],
            "shine": (7, 7)},
        2: {"ore": [(8, 3), (9, 3), (8, 4), (2, 10), (3, 10), (2, 11), (12, 7), (13, 7)],
            "shine": (9, 2)},
    },
}

# 木板顶/底的竖向端接头：逐面手写（行区间, 列）
PLANK_JOINTS = {
    "top": [((0, 2), 5), ((4, 6), 11), ((8, 10), 3), ((12, 14), 13)],
    "bottom": [((0, 2), 9), ((4, 6), 3), ((8, 10), 13), ((12, 14), 6)],
}

# ─────────────────────────────────────────────────────────────────────────────
# 4. 合成 + 校验 + 导出
# ─────────────────────────────────────────────────────────────────────────────


def check_rows(name, rows):
    if len(rows) != 16:
        raise SystemExit(f"{name}: {len(rows)} rows, expected 16")
    for i, r in enumerate(rows):
        if len(r) != 16:
            raise SystemExit(f"{name} row {i}: {len(r)} chars, expected 16 -> {r!r}")
        for ch in r:
            if ch not in PALETTE:
                raise SystemExit(f"{name} row {i}: char {ch!r} not in palette")


def apply_plank_joints(rows, joints):
    grid = [list(r) for r in rows]
    for (r0, r1), x in joints:
        for r in range(r0, r1 + 1):
            grid[r][x] = "q"
    return ["".join(r) for r in grid]


def saturation(rgb):
    mx = max(rgb)
    mn = min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def rows_to_rgba(rows):
    out = []
    for r in rows:
        line = []
        for ch in r:
            cr, cg, cb = PALETTE[ch]
            line.append((cr, cg, cb, ALPHA.get(ch, 255)))
        out.append(line)
    return out


def write_png(path, px):
    """手写 PNG 编码（zlib + 结构拼装），不引入任何图像库；8 位 RGBA、不缩放。"""
    import struct
    import zlib

    raw = bytearray()
    for row in px:
        raw.append(0)  # filter type 0
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


SURFACE_BLOCKS = ["grass_block", "dirt", "stone", "sand", "log", "leaves", "planks", "water", "glass"]
ORE_BLOCKS = ["coal_ore", "iron_ore", "gold_ore", "diamond_ore", "copper_ore"]
SLOT_NAMES = ["top", "side", "bottom"]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)

    # 色板自检：饱和度上限 0.50（规格 §3）
    worst = max((saturation(v), k, v) for k, v in PALETTE.items())
    print(f"palette: {len(PALETTE)} colours, max saturation {worst[0]:.3f} "
          f"(key {worst[1]!r} {worst[2]})")
    assert worst[0] <= 0.50, "palette violates the 0.50 saturation cap"

    tiles = {}

    # —— 地表组 ——
    tiles["dirt_top"] = DIRT_TOP
    tiles["dirt_side"] = DIRT_SIDE
    tiles["dirt_bottom"] = DIRT_BOTTOM
    tiles["grass_block_top"] = GRASS_TOP
    tiles["grass_block_side"] = GRASS_SIDE
    tiles["grass_block_bottom"] = GRASS_BOTTOM
    tiles["stone_top"] = STONE_TOP
    tiles["stone_side"] = STONE_SIDE
    tiles["stone_bottom"] = STONE_BOTTOM
    tiles["sand_top"] = SAND_TOP
    tiles["sand_side"] = SAND_SIDE
    tiles["sand_bottom"] = SAND_BOTTOM
    tiles["log_top"] = LOG_TOP
    tiles["log_side"] = LOG_SIDE
    tiles["log_bottom"] = LOG_BOTTOM
    tiles["leaves_top"] = [LEAVES[i] for i in LEAVES_TOP_ORDER]
    tiles["leaves_side"] = [LEAVES[i] for i in LEAVES_SIDE_ORDER]
    tiles["leaves_bottom"] = [LEAVES[i] for i in LEAVES_BOTTOM_ORDER]
    tiles["planks_top"] = apply_plank_joints(PLANK_ROWS, PLANK_JOINTS["top"])
    tiles["planks_side"] = PLANK_SIDE
    tiles["planks_bottom"] = apply_plank_joints(PLANK_ROWS, PLANK_JOINTS["bottom"])
    tiles["water_top"] = WATER_TOP
    tiles["water_side"] = WATER_SIDE
    tiles["water_bottom"] = WATER_BOTTOM
    tiles["glass_top"] = GLASS_TOP
    tiles["glass_side"] = GLASS_SIDE
    tiles["glass_bottom"] = GLASS_BOTTOM

    # —— 矿石组 ——
    for block, spec in ORE_SPECKS.items():
        for slot in (0, 1, 2):
            grid = [list(r) for r in STONE_BY_SLOT[slot]]
            for (x, y) in spec[slot]["ore"]:
                grid[y][x] = spec["color"]
            if spec[slot]["shine"] is not None:
                sx, sy = spec[slot]["shine"]
                grid[sy][sx] = "i"
            tiles[f"{block}_{SLOT_NAMES[slot]}"] = ["".join(r) for r in grid]

    # 完整性：14 个方块 × 3 面 = 42 张
    for block in SURFACE_BLOCKS + ORE_BLOCKS:
        for s in SLOT_NAMES:
            key = f"{block}_{s}"
            assert key in tiles, f"missing tile {key}"
    assert len(tiles) == 42, f"tile count {len(tiles)} != 42"

    for name, rows in sorted(tiles.items()):
        check_rows(name, rows)
        write_png(os.path.join(out_dir, name + ".png"), rows_to_rgba(rows))

    print(f"wrote {len(tiles)} tiles to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
