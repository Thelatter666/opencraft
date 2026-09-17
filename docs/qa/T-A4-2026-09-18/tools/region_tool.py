#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T-A4 in-game evidence device: read / patch OpenCraft .ocr region files.

★ 这是**证据装置**，不是产品改动：它改写游戏自己写出的存档，用来把 6 种新贴图
对应的方块摆到出生点视野里（本卡 18 张里只有 snow_block / obsidian 出现在初始
物品栏，另外 4 种在 worldgen 里根本不生成 —— 不摆就看不出前后差异）。
格式依据（只读源码）：
  game/server/storage/include/opencraft/storage/region_file.hpp
  engine/voxel/include/opencraft/voxel/chunk.hpp（索引 = (y_local*16 + z)*16 + x）
压缩用本机 zstd CLI（格式固定 zstd，层级不影响解码）。

usage:
  region_tool.py hist   <region.ocr>              逐方块名统计该 region 的方块
  region_tool.py exists <region.ocr> <cx> <cz>    该区块是否已落盘
  region_tool.py get    <region.ocr> <wx> <wy> <wz>
  region_tool.py set    <region.ocr> <wx> <wy> <wz> <block_id>...
"""
import os
import struct
import subprocess
import sys

ZSTD = "/opt/homebrew/bin/zstd"
SECTOR = 4096
HEADER_SIZE = 8 * SECTOR  # 8192
SIDE = 32
MAGIC = 0x4F435246       # "OCRF"
BLOCK_MAGIC = 0x4F434342  # "OCCB"
SIZE_X = SIZE_Z = 16
SIZE_Y = 384
SECTION = 16
SECTIONS = SIZE_Y // SECTION

BLOCK_IDS = [
    "air", "dirt", "grass_block", "stone", "cobblestone", "sand", "gravel",
    "sandstone", "log", "leaves", "planks", "glass", "water", "bedrock",
    "coal_ore", "copper_ore", "iron_ore", "gold_ore", "diamond_ore",
    "snow_block", "obsidian",
]
NAME_TO_ID = {n: i for i, n in enumerate(BLOCK_IDS)}


def zstd_decompress(data):
    out = subprocess.run([ZSTD, "-d", "-c", "-q"], input=data, capture_output=True, check=True)
    return out.stdout


def zstd_compress(data):
    out = subprocess.run([ZSTD, "-1", "-c", "-q"], input=data, capture_output=True, check=True)
    return out.stdout


# ── 区块载荷 ─────────────────────────────────────────────────────────────────

class Section:
    """一个 16^3 section：bits=0 表示整段同值。"""

    def __init__(self, bits=0, uniform=0, cells=None):
        self.bits = bits
        self.uniform = uniform
        self.cells = cells  # None 表示 uniform

    @staticmethod
    def parse(buf, pos):
        bits = buf[pos]
        pos += 1
        count = struct.unpack_from("<H", buf, pos)[0]
        pos += 2
        palette = list(struct.unpack_from(f"<{count}H", buf, pos))
        pos += 2 * count
        if bits == 0:
            return Section(0, palette[0], None), pos
        nwords = struct.unpack_from("<I", buf, pos)[0]
        pos += 4
        words = struct.unpack_from(f"<{nwords}Q", buf, pos)
        pos += 8 * nwords
        per_word = 64 // bits
        mask = (1 << bits) - 1
        cells = [palette[(words[i // per_word] >> ((i % per_word) * bits)) & mask]
                 for i in range(SECTION ** 3)]
        return Section(bits, 0, cells), pos

    def serialize(self):
        if self.cells is None:
            return struct.pack("<BHH", 0, 1, self.uniform)
        palette = []
        for c in self.cells:
            if c not in palette:
                palette.append(c)
        bits = 4 if len(palette) <= 16 else (8 if len(palette) <= 256 else 16)
        per_word = 64 // bits
        nwords = (len(self.cells) + per_word - 1) // per_word
        words = [0] * nwords
        slot_of = {v: i for i, v in enumerate(palette)}
        for i, c in enumerate(self.cells):
            words[i // per_word] |= slot_of[c] << ((i % per_word) * bits)
        out = struct.pack("<BH", bits, len(palette))
        out += struct.pack(f"<{len(palette)}H", *palette)
        out += struct.pack("<I", nwords) + struct.pack(f"<{nwords}Q", *words)
        return out


class Chunk:
    def __init__(self):
        self.sections = [Section() for _ in range(SECTIONS)]  # 默认全 air
        self.fluid_raw = struct.pack("<B", 0)

    @staticmethod
    def parse(payload):
        ch = Chunk()
        pos = 0
        (version,) = struct.unpack_from("<I", payload, pos)
        pos += 4
        assert version in (1, 2), f"unsupported chunk version {version}"
        non_empty = payload[pos]
        pos += 1
        for _ in range(non_empty):
            idx = payload[pos]
            pos += 1
            ch.sections[idx], pos = Section.parse(payload, pos)
        if version >= 2:
            ch.fluid_raw = payload[pos:]  # 原样保留（本卡不碰流体层）
        return ch

    def serialize(self):
        filled = [i for i, s in enumerate(self.sections) if not (s.cells is None and s.uniform == 0)]
        out = struct.pack("<I", 2) + struct.pack("<B", len(filled))
        for i in filled:
            out += struct.pack("<B", i) + self.sections[i].serialize()
        out += self.fluid_raw
        return out

    def get(self, x, y, z):
        sec = self.sections[y // SECTION]
        i = ((y % SECTION) * SIZE_Z + z) * SIZE_X + x
        return sec.uniform if sec.cells is None else sec.cells[i]

    def set(self, x, y, z, block_id):
        sec = self.sections[y // SECTION]
        if sec.cells is None:
            sec.cells = [sec.uniform] * (SECTION ** 3)
            sec.uniform = 0
        sec.cells[((y % SECTION) * SIZE_Z + z) * SIZE_X + x] = block_id


# ── region 文件 ──────────────────────────────────────────────────────────────

def read_region(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, hsize, flags = struct.unpack_from("<IHHI", data, 0)
    assert magic == MAGIC, f"bad region magic {magic:#x}"
    assert version == 1 and hsize == HEADER_SIZE, f"header {version}/{hsize}"
    header = bytearray(data[:HEADER_SIZE])
    stored_crc = struct.unpack_from("<I", header, 12)[0]
    struct.pack_into("<I", header, 12, 0)
    assert (zlib_crc32(bytes(header)) & 0xFFFFFFFF) == stored_crc, "header CRC mismatch"
    locs = []
    for i in range(SIDE * SIDE):
        raw = struct.unpack_from("<I", data, 16 + 4 * i)[0]
        locs.append((raw >> 8, raw & 0xFF))
    return data, locs


def zlib_crc32(data):
    import zlib
    return zlib.crc32(data)


def read_chunk(path, cx, cz):
    """(cx, cz) 是世界区块坐标；返回 (Chunk, local_x, local_z) 或 (None, ...)。"""
    data, locs = read_region(path)
    rx, lx = divmod(cx, SIDE)
    rz, lz = divmod(cz, SIDE)
    sector, count = locs[lz * SIDE + lx]
    if sector == 0 or count == 0:
        return None, lx, lz
    off = sector * SECTOR
    bmagic, ulen, ctype, clen, ccrc = struct.unpack_from("<IIB3xII", data, off)
    assert bmagic == BLOCK_MAGIC, f"bad block magic {bmagic:#x}"
    comp = data[off + 20: off + 20 + clen]
    assert (zlib_crc32(comp) & 0xFFFFFFFF) == ccrc, "chunk block CRC mismatch"
    payload = comp if ctype == 0 else zstd_decompress(comp)
    assert len(payload) == ulen, f"uncompressed length {len(payload)} != {ulen}"
    return Chunk.parse(payload), lx, lz


def write_chunk(path, cx, cz, chunk):
    data, locs = read_region(path)
    rx, lx = divmod(cx, SIDE)
    rz, lz = divmod(cz, SIDE)
    payload = chunk.serialize()
    comp = zstd_compress(payload)
    block = struct.pack("<IIB3xII", BLOCK_MAGIC, len(payload), 1, len(comp), zlib_crc32(comp) & 0xFFFFFFFF) + comp
    pad = (-len(block)) % SECTOR
    block += b"\0" * pad
    nsectors = len(block) // SECTOR
    header = bytearray(data[:HEADER_SIZE])
    old_sector, old_count = locs[lz * SIDE + lx]
    used = {s for s, c in locs for s in range(s, s + c) if c}
    start = HEADER_SIZE // SECTOR   # 数据区第一块扇区的下标（0..7 是文件头）
    while any(start + i in used for i in range(nsectors)):
        start += 1
    if start + nsectors > (len(data) - HEADER_SIZE) // SECTOR:
        data = data + b"\0" * (SECTOR * (start + nsectors - (len(data) - HEADER_SIZE) // SECTOR))
    data = bytearray(data)
    data[start * SECTOR: (start + nsectors) * SECTOR] = block
    struct.pack_into("<I", header, 16 + 4 * (lz * SIDE + lx), (start << 8) | nsectors)
    struct.pack_into("<I", header, 4104 + 4 * (lz * SIDE + lx), 0)
    struct.pack_into("<I", header, 12, 0)
    struct.pack_into("<I", header, 12, zlib_crc32(bytes(header)) & 0xFFFFFFFF)
    data[:HEADER_SIZE] = header
    with open(path, "wb") as f:
        f.write(bytes(data))
    print(f"wrote chunk ({cx},{cz}) -> sectors {start}..{start + nsectors - 1}")


def main():
    cmd = sys.argv[1]
    path = sys.argv[2]
    if cmd == "hist":
        from collections import Counter
        counts = Counter()
        for name in sorted(os.listdir(os.path.dirname(path))):
            if not name.endswith(".ocr"):
                continue
            full = os.path.join(os.path.dirname(path), name)
            rx, rz = name[2:-4].split(".")
            for lz in range(SIDE):
                for lx in range(SIDE):
                    ch, _, _ = read_chunk(full, int(rx) * SIDE + lx, int(rz) * SIDE + lz)
                    if ch is None:
                        continue
                    for sec in ch.sections:
                        vals = [sec.uniform] if sec.cells is None else sec.cells
                        for v in vals:
                            if v:
                                counts[v] += 1
        for bid, n in counts.most_common():
            print(f"  {BLOCK_IDS[bid] if bid < len(BLOCK_IDS) else bid}: {n}")
        missing = [b for b in ("cobblestone", "gravel", "sandstone", "bedrock", "snow_block", "obsidian")
                   if NAME_TO_ID[b] not in counts]
        print("not present anywhere in save:", missing)
        return 0
    if cmd == "exists":
        cx, cz = int(sys.argv[3]), int(sys.argv[4])
        ch, _, _ = read_chunk(path, cx, cz)
        print("exists" if ch else "absent")
        return 0
    if cmd == "get":
        wx, wy, wz = (int(a) for a in sys.argv[3:6])
        ch, _, _ = read_chunk(path, wx // 16, wz // 16)
        v = ch.get(wx % 16, wy, wz % 16) if ch else 0
        print(f"({wx},{wy},{wz}) = {BLOCK_IDS[v] if v < len(BLOCK_IDS) else v}")
        return 0
    if cmd == "set":
        # set <region> <wx> <wy> <wz> <id0> [id1 ...]  —— 从 (wx,wy,wz) 沿 +x 连续摆
        wx, wy, wz = (int(a) for a in sys.argv[3:6])
        ids = [NAME_TO_ID[a] if a in NAME_TO_ID else int(a) for a in sys.argv[6:]]
        ch, _, _ = read_chunk(path, wx // 16, wz // 16)
        if ch is None:
            raise SystemExit(f"chunk ({wx // 16},{wz // 16}) not in save - patch a saved chunk")
        for k, bid in enumerate(ids):
            ch.set(wx % 16 + k, wy, wz % 16, bid)
        write_chunk(path, wx // 16, wz // 16, ch)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
