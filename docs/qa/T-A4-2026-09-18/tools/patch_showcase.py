#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A4 实机取证装置：把 6 种新贴图对应的方块摆到出生点视野里。

★ 为什么需要它：本卡 18 张对应的 6 个方块的来源如下（`region_tool.py hist` 实测，
见证据 README）——
    gravel / bedrock  worldgen 会生成（地下/海底，出生点视野里看不到）
    cobblestone / sandstone / snow_block / obsidian  存档里一处都没有
  而"文件名写错 = 静默回退"这条管线语义决定了：**光看日志 60/63 还不够，
  必须让这 6 张真的出现在画面上**（卡面 §6 第 3 条）。
  所以本脚本在存档里搭一块展示台（对应 T-D4 卡面授权的"改写存档坐标/传送"同类手法），
  这是**证据装置**，不是产品改动。

摆法（全部落在已落盘区块 (0,-1) = x 0..15, z -16..-1 内）：
  y=199  石台 9×9（玩家站立面）
  y=200  A 排：6 种方块各一块，站在台上 ⇒ 画面上看到 顶面 + 侧面
  y=202  B 排：同样 6 块悬空（y=201 是空气）⇒ 玩家眼高 201.62 低于 202 ⇒ 看到 **底面**
  玩家 (8.5, 200.0, -6.5)，yaw=0（forward=(0,0,-1)），pitch=0
  ⇒ A 排在 7.5 格外、眼下 0.6 格；B 排在 8.5 格外、眼上 0.4 格：两排都收在画面中部，
  6 块横排约跨 43°，不顶到画面边缘。

usage: patch_showcase.py <build/saves 目录>
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import region_tool as rt  # noqa: E402

MAGIC = 0x4F434C44
PLAYER_X, PLAYER_Y, PLAYER_Z = 41, 49, 57
VEL_X, VEL_Y, VEL_Z = 65, 73, 81
YAW, PITCH = 89, 97
HEALTH, FALL_PEAK, FALL_DIST = 105, 113, 121
POSE, ON_GROUND, SELECTED = 129, 130, 131

SHOWCASE = ["cobblestone", "gravel", "sandstone", "bedrock", "snow_block", "obsidian"]
PLATFORM_Y = 199
ROW_A_Y = 200
ROW_B_Y = 202
XS = [6, 7, 8, 9, 10, 11]
ROW_A_Z = -14
ROW_B_Z = -15
PLATFORM = dict(x0=4, x1=12, z0=-15, z1=-5)
PLAYER = (8.5, 200.0, -6.5)
YAW_DEG, PITCH_DEG = 0.0, 0.0


def patch_level(path):
    image = open(path, "rb").read()
    magic, version, header_size, length = struct.unpack_from("<IHHI", image, 0)
    assert magic == MAGIC and version == 1 and header_size == 12
    payload = bytearray(image[12:12 + length])
    assert zlib.crc32(payload) == struct.unpack_from("<I", image, 12 + length)[0]
    payload[16] = 1                                     # has_player
    struct.pack_into("<ddd", payload, PLAYER_X, *PLAYER)
    struct.pack_into("<ddd", payload, VEL_X, 0.0, 0.0, 0.0)
    struct.pack_into("<d", payload, YAW, YAW_DEG)
    struct.pack_into("<d", payload, PITCH, PITCH_DEG)
    struct.pack_into("<d", payload, HEALTH, 20.0)
    struct.pack_into("<d", payload, FALL_PEAK, PLAYER[1] - 1.0)
    struct.pack_into("<d", payload, FALL_DIST, 0.0)
    payload[POSE] = 0
    payload[ON_GROUND] = 1
    struct.pack_into("<H", payload, SELECTED, rt.NAME_TO_ID["cobblestone"])
    header = struct.pack("<IHHI", MAGIC, 1, 12, len(payload))
    with open(path, "wb") as f:
        f.write(header + bytes(payload) + struct.pack("<I", zlib.crc32(payload)))
    print(f"level patched: player={PLAYER} yaw={YAW_DEG} pitch={PITCH_DEG}")


def patch_region(path):
    chunk, _, _ = rt.read_chunk(path, 0, -1)
    assert chunk is not None, "chunk (0,-1) is not in the save fixture"
    stone = rt.NAME_TO_ID["stone"]
    for x in range(PLATFORM["x0"], PLATFORM["x1"] + 1):
        for z in range(PLATFORM["z0"], PLATFORM["z1"] + 1):
            chunk.set(x, PLATFORM_Y, z, stone)
    for k, name in enumerate(SHOWCASE):
        bid = rt.NAME_TO_ID[name]
        chunk.set(XS[k], ROW_A_Y, ROW_A_Z, bid)
        chunk.set(XS[k], ROW_B_Y, ROW_B_Z, bid)
    # 悬空排下方必须是空气，否则底面被剔除
    for x in XS:
        chunk.set(x, ROW_B_Y - 1, ROW_B_Z, 0)
    rt.write_chunk(path, 0, -1, chunk)

    # 回读自检（读的是刚写出的文件）
    back, _, _ = rt.read_chunk(path, 0, -1)
    for k, name in enumerate(SHOWCASE):
        for (y, z) in ((ROW_A_Y, ROW_A_Z), (ROW_B_Y, ROW_B_Z)):
            got = back.get(XS[k], y, z)
            assert got == rt.NAME_TO_ID[name], f"readback {name} at ({XS[k]},{y},{z}) = {got}"
    print("region patched + read-back verified: A row y=%d z=%d, B row y=%d z=%d, platform y=%d"
          % (ROW_A_Y, ROW_A_Z, ROW_B_Y, ROW_B_Z, PLATFORM_Y))


def main():
    saves = sys.argv[1]
    patch_level(os.path.join(saves, "level.ocd"))
    patch_region(os.path.join(saves, "region", "r.0.-1.ocr"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
