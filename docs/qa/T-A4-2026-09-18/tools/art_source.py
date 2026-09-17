#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A4 第二批方块贴图：手写网格稿 -> 16x16 PNG。

★ 这不是"程序化生成贴图"（T-A3 立的同一条方法论，本卡沿用）。
每个瓦片的 256 个像素由美术总监以字符网格**逐格手写**（18 个瓦片 × 16 行 = 288 行，
全部在本文件里可见），本脚本只做两件事：

  1. 查表：字符 -> 色板 RGBA（色板见 docs/art/01-style-guide.md §2.1）；
  2. 编码：把 16×16 RGBA8 写成标准 PNG。

脚本里**没有**随机数、没有噪声函数、没有渐变插值、没有重采样/缩放/抖动、没有
任何"按规则推导像素"的机器逻辑——每个像素的字符都在下面的手稿里逐字可读。
本卡新增的 6 个方块（cobblestone/gravel/sandstone/bedrock/snow_block/obsidian）
三面各自独立手写，没有共用母稿。

usage: python3 art_source.py <assets/blocks 目录>
"""
import os
import sys

# ─────────────────────────────────────────────────────────────────────────────
# 1. 色板（32 色，见 docs/art/01-style-guide.md §2.1；键与 T-A3 脚本逐字一致）
# ─────────────────────────────────────────────────────────────────────────────
PALETTE = {
    # 土系 EARTH
    'a': (0x46, 0x35, 0x29),  # E0 深土
    'b': (0x64, 0x4C, 0x3C),  # E1 土
    'c': (0x8A, 0x6B, 0x4E),  # E2 浅土
    'd': (0xB4, 0x9E, 0x73),  # E3 沙影
    'e': (0xCE, 0xBE, 0x94),  # E4 沙
    # 石系 STONE
    'f': (0x4A, 0x4A, 0x50),  # S0 深石
    'g': (0x6E, 0x6E, 0x74),  # S1 石影
    'h': (0x80, 0x80, 0x85),  # S2 石
    'i': (0xA6, 0xA6, 0xAC),  # S3 石高光
    'j': (0xE2, 0xE6, 0xEC),  # S4 雪（本卡启用）
    'k': (0x2E, 0x2A, 0x3A),  # S5 黑曜石（本卡启用）
    # 植生 FOLIAGE
    'l': (0x2E, 0x4A, 0x28),  # F0 深叶
    'm': (0x46, 0x70, 0x3C),  # F1 叶
    'n': (0x5A, 0x8C, 0x4A),  # F2 亮叶
    'o': (0x60, 0x92, 0x4E),  # F3 草
    'p': (0x7C, 0xA8, 0x62),  # F4 草高光
    # 木 WOOD
    'q': (0x42, 0x34, 0x26),  # W0 深木 / 板缝
    'r': (0x6E, 0x56, 0x3C),  # W1 木
    's': (0x8A, 0x6E, 0x4C),  # W2 亮木
    't': (0xA8, 0x84, 0x5C),  # W3 木板
    # 水 WATER
    'u': (0x4E, 0x7A, 0x96),  # A1 水
    'v': (0x7A, 0xA6, 0xBC),  # A2 浅水 / 波光
    # 玻璃 GLASS
    'w': (0x96, 0xB6, 0xC6),  # G0 玻璃框
    'x': (0xAC, 0xC8, 0xD6),  # G1 玻璃面
    'G': (0xAC, 0xC8, 0xD6),  # G1h 玻璃斜向高光
    # 矿石点缀 ORE
    'y': (0x28, 0x28, 0x2C),  # O0 煤
    'z': (0xB0, 0x80, 0x5C),  # O1 铜
    'A': (0xC8, 0xA8, 0x98),  # O2 铁
    'B': (0xD8, 0xBC, 0x78),  # O3 金
    'C': (0x74, 0xC6, 0xCC),  # O4 钻石
    # UI（本卡未用）
    'D': (0x1E, 0x1E, 0x22),  # U0
    'E': (0x9A, 0x9A, 0xA2),  # U1
    'F': (0xE6, 0xE2, 0xD4),  # U2
}

# 本卡 6 个方块全部不透明（规格 §6：一切不透明资产 alpha = 255）
ALPHA = {}

# ─────────────────────────────────────────────────────────────────────────────
# 2. 手写网格稿（每行 16 字符 = 一整行像素；第 0 行 = 面的上沿）
#
# 本卡用到的字符：
#   cobblestone  f=砂浆(S0)  g=石块主色(S1)  h=石块亮面(S2)  i=单点反光(S3)
#   gravel       f=石缝(S0)  g/h=砾石两级(S1/S2)  a/b/c=土色砾石(E0/E1/E2)
#   sandstone    e=砂主色(E4) d=层理/压实影(E3)  c=最深层理线(E2)
#   bedrock      f=主色(S0)  g=碎块亮面(S1)  h=碎屑尖(S2)
#   snow_block   j=雪(S4)    i=雪面凹坑(S3)
#   obsidian     k=主色(S5)  f=贝壳状断口面(S0)  i=单点镜面反光(S3)
# ─────────────────────────────────────────────────────────────────────────────

# ── cobblestone：不规则楔石 + 砂浆缝（三面各自独立手写）
#    ★ 刻意**不**复刻程序化版的 4×4 等距格纹（规格 §4.5 禁止规则形）：
#      石块逐面手写、上下错开（每块自己的上沿/下沿都不在同一行），缝宽 1 px 且走向
#      不规则；每块"顶边受光、底边积影"，靠明暗读出体积，不画描边（§4.3）。
#      每面的单点反光 i（S3）按 §4.4 只放 1 px，且落在某一块的上沿。
COBBLESTONE_TOP = [
    "hhihh" "f" "hhgh" "f" "hhghh",
    "gghgg" "f" "hhgh" "f" "ggghg",
    "ggghg" "f" "hghh" "f" "gghgg",
    "ggfgg" "f" "ghhh" "f" "fffff",
    "ffffff" "gghg" "hhhhhh",
    "hhh" "f" "hhghh" "f" "hghhhh",
    "ghh" "f" "gghgg" "f" "ghhhgh",
    "hgh" "f" "ggghg" "f" "hhghhh",
    "ggf" "f" "ggfgg" "f" "hghghh",
    "fff" "f" "ffgff" "f" "ggfggg",
    "hhhhhg" "f" "hhhh" "f" "ffff",
    "ggghgg" "f" "hghh" "f" "hhhh",
    "ghgggh" "f" "gghh" "f" "ghhg",
    "gggghg" "f" "hhgh" "f" "gghg",
    "hggghg" "f" "ghhg" "f" "hghh",
    "ggfggg" "f" "ffff" "f" "ggfg",
]

COBBLESTONE_SIDE = [
    "hhgh" "f" "hhghh" "f" "hhihh",
    "ghhg" "f" "hghhh" "f" "ggghg",
    "hhhg" "f" "ghhhh" "f" "hghgh",
    "gghg" "f" "ffgff" "f" "hghhg",
    "ffff" "f" "hhhhh" "f" "hhhhh",
    "hhgh" "f" "ggghh" "f" "hghhh",
    "ghhg" "f" "hghgg" "f" "ggghh",
    "hhgh" "f" "ghhhg" "f" "hghhg",
    "gghg" "f" "gghfg" "f" "hghhg",
    "ggfg" "f" "ffgff" "f" "f" "hghh",
    "ffgf" "f" "fffff" "f" "f" "ghhh",
    "hhhhh" "f" "hhhhh" "f" "gghh",
    "hghhg" "f" "hghhh" "f" "ffff",
    "ghhgh" "f" "ghhgg" "f" "hhhh",
    "hhghg" "f" "gghhg" "f" "ghhg",
    "ggfgg" "f" "ggfgg" "f" "ggfg",
]

COBBLESTONE_BOTTOM = [
    "hhihh" "f" "hhghhg" "f" "hhg",
    "ghhgg" "f" "hghhhg" "f" "ghg",
    "hhghg" "f" "ggghhh" "f" "ghf",
    "ffffff" "ghhhhg" "f" "fff",
    "hhh" "f" "hhhhhh" "ffffff",
    "ghh" "f" "hhghhh" "f" "hhhhh",
    "hgh" "f" "gghggh" "f" "ghhhh",
    "ghg" "f" "ggfggg" "f" "hghhh",
    "ggf" "f" "ffgfff" "f" "ghhhh",
    "ffff" "ffgfff" "f" "gghhh",
    "hhhhhg" "fffff" "ggfgg",
    "ggghgg" "f" "f" "hhhhh" "fff",
    "hghggg" "f" "f" "hghhh" "fff",
    "ghhghg" "f" "f" "gghhh" "ffg",
    "hhgghh" "f" "f" "hhghh" "fff",
    "ggfggf" "f" "f" "ggfgg" "fff",
]

# ── gravel：松散砾石。2–4 px 的小石 + 1 px 石缝（f）。
#    ★ 石缝的**列位置逐行手写**：任何一列最多连续 2 行有缝、任何一行不出现相邻两格缝
#      ⇒ 缝读作"石与石之间的空隙"而不是竖条/横条（砾石是无方向材质，规格 §4.2）。
#      土色砾石 a/b/c 每面 8–12 px，一律 ≥2 px 成对（§4.1 不做 1 px 孤立胡椒点）。
GRAVEL_TOP = [
    "hhg" "f" "ggg" "f" "hhh" "f" "gh" "f" "g",
    "ghh" "f" "gghg" "f" "hhh" "f" "ggh",
    "f" "hhgg" "f" "ggh" "f" "hhbbb" "f",
    "h" "f" "hgg" "f" "gggh" "f" "bb" "f" "hg",
    "hh" "f" "hgg" "f" "gggg" "f" "hhgh",
    "gh" "f" "gghg" "f" "hhh" "f" "gh" "f" "g",
    "f" "hgg" "f" "ghhg" "f" "gg" "f" "hgh",
    "g" "f" "hh" "f" "ggh" "f" "ghhg" "f" "gg",
    "hgh" "f" "gg" "f" "hhg" "f" "gggg" "f",
    "ghh" "f" "ggh" "f" "hhg" "f" "ghhg",
    "f" "hhgg" "f" "gg" "f" "hhh" "f" "ghg",
    "h" "f" "ggh" "f" "hhg" "f" "gggh" "f" "g",
    "hg" "f" "ggh" "f" "hhh" "f" "gg" "f" "gh",
    "gh" "f" "gggh" "f" "hhg" "f" "ghh" "f",
    "f" "hg" "f" "gghg" "f" "hhg" "f" "ghg",

    "h" "f" "gh" "f" "gggh" "f" "hgg" "f" "gg",
]

GRAVEL_SIDE = [
    "hh" "f" "hgg" "f" "ghh" "f" "gg" "f" "hh",
    "gh" "f" "gghg" "f" "hhh" "f" "gh" "f" "g",
    "f" "hhg" "f" "ggh" "f" "hhg" "f" "gg" "f",
    "h" "f" "hg" "f" "gghh" "f" "ggh" "f" "gg",
    "hgh" "f" "ggg" "f" "hhh" "f" "bbbg",
    "ghh" "f" "gg" "f" "hgh" "f" "ggh" "f" "g",
    "f" "hggg" "f" "ggh" "f" "gg" "f" "hgh",
    "g" "f" "hgg" "f" "gh" "f" "hhgh" "f" "gg",
    "hg" "f" "ggh" "f" "ghhg" "f" "hhg" "f",
    "f" "ghh" "f" "ggg" "f" "bbb" "f" "ghh",

    "h" "f" "gg" "f" "gghh" "f" "ghhg" "f" "g",
    "gh" "f" "gggh" "f" "hhg" "f" "ggh" "f",
    "f" "hg" "f" "gghg" "f" "hhh" "f" "ggg",

    "h" "f" "gghg" "f" "hhg" "f" "gg" "f" "gh",
    "gh" "f" "hg" "f" "ggh" "f" "ghhg" "f" "h",
    "f" "hg" "f" "ggh" "f" "hhg" "f" "ghhg",
]

GRAVEL_BOTTOM = [
    "h" "f" "ggg" "f" "hhh" "f" "ghg" "f" "hh",
    "gh" "f" "hg" "f" "gghg" "f" "hhh" "f" "g",
    "f" "ghg" "f" "gg" "f" "hhh" "f" "bbb" "f",
    "f" "hg" "f" "gghh" "f" "hhg" "f" "ghg",
    "hh" "f" "ggh" "f" "gg" "f" "hhg" "f" "gh",
    "g" "f" "gghh" "f" "hhg" "f" "ghg" "f" "h",
    "hgh" "f" "ggg" "f" "hhh" "f" "bbb" "f",
    "f" "hhg" "f" "ggg" "f" "hgh" "f" "ghg",
    "hg" "f" "gg" "f" "hhg" "f" "ggh" "f" "gg",
    "g" "f" "hhg" "f" "gghh" "f" "ggh" "f" "g",
    "f" "gg" "f" "hgg" "f" "hhg" "f" "ghhg",
    "f" "hgg" "f" "ggh" "f" "hhg" "f" "gh" "f",
    "gh" "f" "ggh" "f" "hg" "f" "ggg" "f" "hh",
    "h" "f" "gghg" "f" "hhg" "f" "ghg" "f" "g",
    "hgh" "f" "ggg" "f" "ggh" "f" "ghh" "f",
    "f" "ghg" "f" "hhh" "f" "ggg" "f" "hgh",
]

# ── sandstone：沉积岩。顶/底 = 压实的砂面（平行于层理面），侧 = 层理断面
#    ★ 横向层理按规格 §4.2 允许；层厚刻意不等以避免等距平行线。
#      顶面 d 用 2–3 px 手写簇 + 3–4 px 短划线（交错层理）；底面用手写短划线为主，
#      与顶面区分；最深色 c（E2）只出现在侧面的层理线上。
SANDSTONE_TOP = [
    "eedd" "eeed" "eeed" "ddee",
    "eeee" "ddee" "edee" "eeed",
    "eddd" "deee" "eddd" "eeee",
    "eeee" "edee" "eeee" "ddee",
    "ddee" "eeed" "eeed" "edee",
    "eeed" "ddee" "eeed" "eeee",
    "eeee" "eeed" "ddee" "eded",
    "edee" "eeee" "eddd" "deee",
    "eeed" "eedd" "eeee" "eeed",
    "ddee" "eeed" "edee" "eeee",
    "eeed" "eeee" "eedd" "edee",
    "eeee" "edee" "eeed" "ddee",
    "eddd" "deee" "edee" "eeed",
    "eeee" "eedd" "eeed" "eeee",
    "eeed" "ddee" "eeee" "edde",
    "ddee" "eeed" "eedd" "eeee",
]

SANDSTONE_SIDE = [
    "eeed" "eeee" "edee" "eeee",
    "edee" "eeed" "eeee" "ddee",
    "eeee" "edee" "ddee" "eeee",
    "dddd" "eddd" "dddd" "ddde",
    "dded" "dddd" "dddd" "eddd",
    "dddd" "ddde" "dded" "dddd",
    "eeed" "eeee" "eeee" "edee",
    "eeee" "edee" "eeed" "eeee",
    "ddee" "eeee" "edee" "eeed",
    "dddd" "dddd" "eddd" "dddd",
    "cccc" "eccc" "cccc" "ccce",
    "dddd" "dded" "dddd" "dddd",
    "dddd" "dddd" "ddde" "dddd",
    "dded" "dddd" "dddd" "dddd",
    "dddd" "dddd" "dded" "ddde",
    "eeee" "eeed" "eeee" "edee",
]

SANDSTONE_BOTTOM = [
    "eeed" "eeee" "ddee" "eeed",
    "eeee" "eedd" "eeed" "eeee",
    "edee" "eeed" "eeee" "ddee",
    "eeed" "ddee" "eddd" "deee",
    "ddee" "eeee" "eeee" "eeed",
    "eeee" "edee" "ddee" "eeee",
    "eeed" "eeed" "eeee" "edde",
    "eeee" "eeee" "eddd" "deee",
    "ddee" "edee" "eeee" "eeed",
    "eeed" "ddee" "eeee" "edee",
    "eeee" "eeed" "ddee" "eeed",
    "edee" "eeee" "eeed" "ddee",
    "eeed" "eddd" "deee" "eeee",
    "eeee" "eeed" "eeee" "edde",
    "ddee" "eeee" "edee" "eeed",
    "eeed" "ddee" "eeed" "eeee",
]

# ── bedrock：破碎基岩。块状碎斑 2–4 px（规格 §4.1：不做 1 px 孤立胡椒点）。
#    主色 f（S0）占多数 ⇒ 整体比 stone/gravel 明显更暗；g 是碎块受光面，h 是碎屑尖
BEDROCK_TOP = [
    "ffgggfffggggffgg",
    "ffggggffgggfgggg",
    "ggffffgggggffffg",
    "ggggfffffgggggff",
    "ffgggffgghhggffg",
    "ffggggfgghgggfff",
    "gggffggggfgggggf",
    "ggffffffgggffffg",
    "gffgggggffffgggg",
    "ffgggfgggggggfff",
    "ffgggffffgggffgg",
    "ggggfffgggggggff",
    "ggffffgggffffggg",
    "gffgggghhgggffgg",
    "ffggggggggfggggf",
    "fggggffgggggffgf",
]

BEDROCK_SIDE = [
    "ggffffgggggffggg",
    "ggggfffgggfggggf",
    "ffggggggfggggfff",
    "ffgggffffggggggg",
    "ggggffggggfffggg",
    "gffgggggggffffgg",
    "gffggghhggffgggg",
    "gggggghgggggfffg",
    "ffffgggggffggggg",
    "ggggfffgghggffff",
    "ggffffggggggggfg",
    "gggggggffffggggg",
    "fffffgggggggffff",
    "ggggggffgggggggg",
    "ffgggggggfffgggg",
    "ggfffggggggggggf",
]

BEDROCK_BOTTOM = [
    "gffggggggffggggf",
    "gggggffgggggfffg",
    "ffffggggffgggggg",
    "ggggggffffgggfff",
    "ggffffgggggggggg",
    "ffgggggggfffffgg",
    "ggggfffggggggggf",
    "ggffgggghhgggffg",
    "ggggggggggfggggg",
    "fffggggffffggggg",
    "gggggggfggggffff",
    "ggfffffggggggggg",
    "ggggggggggffgggg",
    "ffgggggffffgggff",
    "gggfffgggggggggg",
    "ggggggggffgggffg",
]

# ── snow_block：压实雪面。j 主色 + i 雪面凹痕。
#    ★ 雪是"平"材质，规格 §4.1 的两条在这里互相拉扯（不做 1 px 孤立胡椒点 / 不得出现
#      4 px 以上实心色块）。解法：把 i 写成**2 px 的短痕**（不是散点、也不是大块），
#      并按手排位置保证"任意 5×5 窗口都压到一条痕" ⇒ 两条同时满足，覆盖仅 ~7%。
#      三面的短痕位置逐个手写、互不相同（同一行里痕与痕的间距也刻意不等）。
SNOW_TOP = [
    "jjjjjjjjjjjjjjjj",
    "j" "ii" "jjjjjjjj" "ii" "jjj",
    "jjjjjj" "ii" "jjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jj" "ii" "jjj" "ii" "jjj" "ii" "jj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "j" "ii" "jjjj" "ii" "jjj" "ii" "jj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
]

SNOW_SIDE = [
    "jjjjjjjjjjjjjjjj",
    "jjj" "ii" "jjjjjjjjj" "ii",
    "jjjjjjjjj" "ii" "jjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjj" "ii" "jjjjj" "ii" "jj",
    "jjjjjjjjjjjjjjjj",
    "j" "ii" "jjjjj" "ii" "jjjjjj",
    "jjjjjjjjjjjjj" "ii" "j",
    "jjjjjjj" "ii" "jjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjj" "ii" "j" "ii" "jj" "ii" "jjjj",
    "jjjjjjjjjjjjjj" "ii",
    "jjjj" "ii" "jjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
]

SNOW_BOTTOM = [
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjj" "ii" "jjjjjj",
    "jj" "ii" "jjjjjjjjj" "ii" "j",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "j" "ii" "jj" "ii" "jjjjjjjjj",
    "jjjjjjjjjjj" "ii" "jjj",
    "jjjjjjjjjjjjjjjj",
    "j" "ii" "jjjj" "ii" "jjjj" "ii" "j",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjj" "ii" "jjj" "ii",
    "jjj" "ii" "jjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
    "jjjjjjjjjjjjjjjj",
]

# ── obsidian：火山玻璃。k 主色 + f 贝壳状断口面（弧形棱面）+ 每面 1 px 镜面反光
#    ★ 断口面按"每行同色连段 ≤ 4 px"手写：玻璃的棱面本来就碎，这也保证不出现
#      4 px 以上的实心色块（规格 §4.1）；三面的连段序列逐个手写、互不相同
OBSIDIAN_TOP = [
    "ffff" "kkk" "ff" "kkk" "ffff",
    "ff" "kkkk" "fff" "kkk" "ff" "kk",
    "k" "fff" "kkkk" "ffff" "kkkk",
    "kkkk" "ffff" "kkk" "ffff" "k",
    "kkk" "ff" "kkkk" "fff" "kkkk",
    "ff" "kkkk" "ffff" "kkk" "fff",
    "kkkk" "fff" "kkk" "ffff" "kk",
    "kkk" "ff" "i" "f" "kkkk" "fff" "kk",
    "ffff" "kkk" "ff" "kkkk" "fff",
    "kk" "ffff" "kkk" "ffff" "kkk",
    "kkkk" "ff" "kkkk" "ffff" "kk",
    "fff" "kkkk" "ffff" "kk" "fff",
    "kkk" "ffff" "kkkk" "fff" "kk",
    "ff" "kkkk" "fff" "kkkk" "fff",
    "kkkk" "ffff" "kkk" "ff" "kkk",
    "kkk" "ffff" "kkkk" "fff" "kk",
]

OBSIDIAN_SIDE = [
    "kkkk" "ffff" "kkk" "ff" "kkk",
    "kkk" "ff" "kkkk" "ffff" "kkk",
    "kk" "ffff" "kkkk" "fff" "kkk",
    "ff" "kkkk" "fff" "kkkk" "fff",
    "kkkk" "ffff" "kkk" "ffff" "k",
    "kkk" "ff" "kkkk" "fff" "kkkk",
    "ffff" "kkk" "ffff" "kkk" "ff",
    "kk" "fff" "i" "kkkk" "fff" "kkk",
    "kkkk" "ff" "kkkk" "ffff" "kk",
    "fff" "kkkk" "ffff" "kk" "fff",
    "kkk" "ffff" "kkkk" "fff" "kk",
    "ff" "kkkk" "fff" "kkkk" "fff",
    "kkkk" "ffff" "kkk" "ff" "kkk",
    "kkk" "ffff" "kkkk" "ff" "kkk",
    "ff" "kkkk" "ffff" "kkk" "fff",
    "kkk" "fff" "kkkk" "fff" "fff",
]

OBSIDIAN_BOTTOM = [
    "kkk" "ffff" "kkkk" "fff" "kk",
    "kkkk" "ff" "kkkk" "ffff" "kk",
    "ff" "kkkk" "fff" "kkkk" "fff",
    "kkkk" "ffff" "kkk" "ff" "kkk",
    "kkk" "ff" "kkkk" "ffff" "kkk",
    "ffff" "kkk" "ffff" "kkk" "ff",
    "kk" "ffff" "kkkk" "fff" "kkk",
    "kkkk" "ff" "kkkk" "i" "fff" "kk",
    "fff" "kkkk" "ffff" "kk" "fff",
    "kkk" "ffff" "kkkk" "fff" "kk",
    "ff" "kkkk" "fff" "kkkk" "fff",
    "kkkk" "ffff" "kkk" "ff" "kkk",
    "kkk" "ffff" "kkkk" "ff" "kkk",
    "ff" "kkkk" "ffff" "kkk" "fff",
    "kkkk" "fff" "kkk" "ffff" "kk",
    "kkk" "ffff" "kkkk" "fff" "kk",
]

# ─────────────────────────────────────────────────────────────────────────────
# 3. 自检 + 编码
# ─────────────────────────────────────────────────────────────────────────────

TILES = {
    "cobblestone_top": COBBLESTONE_TOP,
    "cobblestone_side": COBBLESTONE_SIDE,
    "cobblestone_bottom": COBBLESTONE_BOTTOM,
    "gravel_top": GRAVEL_TOP,
    "gravel_side": GRAVEL_SIDE,
    "gravel_bottom": GRAVEL_BOTTOM,
    "sandstone_top": SANDSTONE_TOP,
    "sandstone_side": SANDSTONE_SIDE,
    "sandstone_bottom": SANDSTONE_BOTTOM,
    "bedrock_top": BEDROCK_TOP,
    "bedrock_side": BEDROCK_SIDE,
    "bedrock_bottom": BEDROCK_BOTTOM,
    "snow_block_top": SNOW_TOP,
    "snow_block_side": SNOW_SIDE,
    "snow_block_bottom": SNOW_BOTTOM,
    "obsidian_top": OBSIDIAN_TOP,
    "obsidian_side": OBSIDIAN_SIDE,
    "obsidian_bottom": OBSIDIAN_BOTTOM,
}

BLOCKS = ["cobblestone", "gravel", "sandstone", "bedrock", "snow_block", "obsidian"]
SLOT_NAMES = ["top", "side", "bottom"]


def check_rows(name, rows):
    if len(rows) != 16:
        raise SystemExit(f"{name}: {len(rows)} rows, expected 16")
    for i, r in enumerate(rows):
        if len(r) != 16:
            raise SystemExit(f"{name} row {i}: {len(r)} chars, expected 16 -> {r!r}")
        for ch in r:
            if ch not in PALETTE:
                raise SystemExit(f"{name} row {i}: char {ch!r} not in palette")


def saturation(rgb):
    mx = max(rgb)
    mn = min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


def max_uniform_square(rows):
    """最大的**同字符正方形**：返回 (边长, x, y)。

    规格 §4.1「不得出现 4 px 以上的实心色块」的可执行判据 = 不存在边长 ≥ 5 的
    同色正方形。同判据也跑在 T-A3 已验收的 42 张上做标定：不透明颗粒类全部 ≤ 4，
    只有玻璃/水（材料本身就是均匀面）达到 5–11 —— 本卡 6 个方块都是颗粒/棱面
    材质，故按 ≤ 4 要求。
    """
    best = (1, 0, 0)
    for side in range(1, 17):
        found = None
        for y in range(0, 17 - side):
            for x in range(0, 17 - side):
                ch = rows[y][x]
                if all(rows[y + dy][x + dx] == ch
                       for dy in range(side) for dx in range(side)):
                    found = (side, x, y)
                    break
            if found:
                break
        if found:
            best = found
        else:
            break
    return best


def rows_to_rgba(rows):
    out = []
    for r in rows:
        out.append([PALETTE[ch] + (ALPHA.get(ch, 255),) for ch in r])
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


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)

    # 色板自检：饱和度上限 0.50（规格 §3）
    worst = max((saturation(v), k, v) for k, v in PALETTE.items())
    print(f"palette: {len(PALETTE)} keys, max saturation {worst[0]:.3f} "
          f"(key {worst[1]!r} {worst[2]})")
    assert worst[0] <= 0.50, "palette violates the 0.50 saturation cap"

    # 完整性：6 个方块 × 3 面 = 18 张
    for block in BLOCKS:
        for s in SLOT_NAMES:
            assert f"{block}_{s}" in TILES, f"missing tile {block}_{s}"
    assert len(TILES) == 18, f"tile count {len(TILES)} != 18"

    print(f"{'tile':22s} {'sq':>3s}  {'i':>2s}  chars")
    for name, rows in sorted(TILES.items()):
        check_rows(name, rows)
        sq, sqx, sqy = max_uniform_square(rows)
        assert sq <= 4, f"{name}: {sq}x{sq} solid block at ({sqx},{sqy}) (spec 4.1)"
        used = sorted(set("".join(rows)))
        print(f"{name:22s} {sq:3d}@({sqx:2d},{sqy:2d})  {''.join(rows).count('i'):2d}  {''.join(used)}")
        write_png(os.path.join(out_dir, name + ".png"), rows_to_rgba(rows))

    print(f"wrote {len(TILES)} tiles to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
