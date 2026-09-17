#!/usr/bin/env python3
"""T-D45 evidence tool: read / modify / re-write build/saves/world/level.ocd.

The card's §6.4 says the decisive scene goes inside the injection window, or is
created by rewriting the save. HID key/mouse injection on this machine is
unreliable (docs/05 §3.1 rule 2), so the death scene is built by POSITIONING the
player in the save (a 40-block drop) and by aiming the persisted view pitch at
the ground - both are fields the format already carries, so no product code is
touched for the evidence.

The format (game/server/storage/src/level_file.cpp):
    u32 magic "OCLD", u16 version=1, u16 header_size=12, u32 payload_len,
    payload, u32 crc32(payload)
Payload fields, in order:
    u64 seed, u64 tick_count, u8 has_player,
    f64 spawn_x, spawn_y, spawn_z,
    f64 player_x, player_y, player_z,
    f64 player_vx, player_vy, player_vz,
    f64 yaw, pitch, health, fall_peak_y, fall_distance,
    u8 pose, u8 on_ground, u16 selected_block

Usage:
    patch_level.py show  <path>
    patch_level.py set   <path> key=value [key=value ...]
    patch_level.py drop  <path> dy=40 pitch=-1.35     # the shortcut used here
"""

import struct
import sys
import zlib

MAGIC = 0x4F434C44  # "OCLD"
FIELDS = [
    ("seed", "u64"),
    ("tick_count", "u64"),
    ("has_player", "u8"),
    ("spawn_x", "f64"), ("spawn_y", "f64"), ("spawn_z", "f64"),
    ("player_x", "f64"), ("player_y", "f64"), ("player_z", "f64"),
    ("player_vx", "f64"), ("player_vy", "f64"), ("player_vz", "f64"),
    ("yaw", "f64"), ("pitch", "f64"), ("health", "f64"),
    ("fall_peak_y", "f64"), ("fall_distance", "f64"),
    ("pose", "u8"), ("on_ground", "u8"), ("selected_block", "u16"),
]
FMT = {"u8": "<B", "u16": "<H", "u32": "<I", "u64": "<Q", "f64": "<d"}
SIZE = {"u8": 1, "u16": 2, "u32": 4, "u64": 8, "f64": 8}


def read(image):
    if len(image) < 12:
        raise SystemExit("file shorter than the header")
    magic, version, header_size, payload_len = struct.unpack_from("<IHHI", image, 0)
    if magic != MAGIC:
        raise SystemExit(f"bad magic {magic:#x}")
    if version != 1 or header_size != 12:
        raise SystemExit(f"unsupported version/header {version}/{header_size}")
    payload = image[12:12 + payload_len]
    crc = struct.unpack_from("<I", image, 12 + payload_len)[0]
    if zlib.crc32(payload) & 0xFFFFFFFF != crc:
        raise SystemExit("payload checksum mismatch")
    values, off = {}, 0
    for name, kind in FIELDS:
        (values[name],) = struct.unpack_from(FMT[kind], payload, off)
        off += SIZE[kind]
    if off != len(payload):
        raise SystemExit(f"payload has {len(payload) - off} trailing bytes")
    return values


def write(path, values):
    payload = b"".join(struct.pack(FMT[kind], values[name]) for name, kind in FIELDS)
    header = struct.pack("<IHHI", MAGIC, 1, 12, len(payload))
    image = header + payload + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF)
    with open(path, "wb") as out:
        out.write(image)
        out.flush()
        import os
        os.fsync(out.fileno())


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    command, path = argv[1], argv[2]
    with open(path, "rb") as src:
        values = read(src.read())
    if command == "show":
        for name, _ in FIELDS:
            print(f"{name} = {values[name]}")
        return 0
    if command not in ("set", "drop"):
        raise SystemExit(__doc__)
    for arg in argv[3:]:
        key, _, raw = arg.partition("=")
        if not raw:
            raise SystemExit(f"bad argument {arg!r}, want key=value")
        if key == "dy":  # the death scene: this much higher, airborne, looking down
            values["player_y"] += float(raw)
            values["fall_peak_y"] = values["player_y"]
            values["player_vy"] = 0.0
            values["on_ground"] = 0
        elif key == "dx":
            values["player_x"] += float(raw)
        elif key == "dz":
            values["player_z"] += float(raw)
        elif key in values:
            kind = dict(FIELDS)[key]
            values[key] = int(raw) if kind in ("u8", "u16", "u32", "u64") else float(raw)
            if key == "player_y":
                values["fall_peak_y"] = values["player_y"]
        else:
            raise SystemExit(f"unknown field {key!r}")
    write(path, values)
    print("patched:")
    for name, _ in FIELDS:
        print(f"  {name} = {values[name]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
