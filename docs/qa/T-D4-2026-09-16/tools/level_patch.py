#!/usr/bin/env python3
"""T-D4 on-machine evidence: dump / patch the player fields of an OpenCraft
level.ocd.

The card sanctions "改写存档坐标或传送" for making a long displacement, because
HID input dies after ~9 ticks on this machine (docs/05 §3.1 rule 2) - walking
144+ blocks by holding W is not possible. This tool is the displacement, not a
product change: it rewrites the level file the game itself wrote, and recomputes
the CRC-32 the reader checks (zlib.crc32 is the same reflected CRC-32 the
storage layer implements, level_file.cpp).

Layout (level_file.hpp): 12-byte header (magic "OCLD", u16 version, u16 header
size, u32 payload length), then the payload, then u32 CRC-32 of the payload.
Payload offsets used here: seed u64 @0, ticks u64 @8, has_player u8 @16,
spawn f64 x3 @17, player f64 x3 @41, player velocity f64 x3 @65, yaw @89,
pitch @97, health @105, fall_peak_y @113, fall_distance @121, pose u8 @129,
on_ground u8 @130, selected block u16 @131.
"""

import argparse
import struct
import sys
import zlib

PLAYER_X, PLAYER_Y, PLAYER_Z = 41, 49, 57
VEL_X, VEL_Y, VEL_Z = 65, 73, 81
YAW, PITCH = 89, 97
HEALTH, FALL_PEAK, FALL_DIST = 105, 113, 121
POSE, ON_GROUND, SELECTED = 129, 130, 131
MAGIC = 0x4F434C44


def load(path):
    with open(path, "rb") as handle:
        image = handle.read()
    magic, version, header_size, length = struct.unpack_from("<IHHI", image, 0)
    if magic != MAGIC:
        raise SystemExit(f"bad magic {magic:#x}")
    if version != 1 or header_size != 12:
        raise SystemExit(f"unsupported header {version}/{header_size}")
    payload = bytearray(image[12 : 12 + length])
    stored_crc = struct.unpack_from("<I", image, 12 + length)[0]
    if zlib.crc32(payload) != stored_crc:
        raise SystemExit("payload checksum mismatch before patching")
    return payload


def store(path, payload):
    header = struct.pack("<IHHI", MAGIC, 1, 12, len(payload))
    with open(path, "wb") as handle:
        handle.write(header + bytes(payload) + struct.pack("<I", zlib.crc32(payload)))


def f64(payload, offset):
    return struct.unpack_from("<d", payload, offset)[0]


def set_f64(payload, offset, value):
    struct.pack_into("<d", payload, offset, float(value))


def dump(payload):
    print(f"  player      = ({f64(payload, PLAYER_X):.3f}, {f64(payload, PLAYER_Y):.3f}, "
          f"{f64(payload, PLAYER_Z):.3f})")
    print(f"  velocity    = ({f64(payload, VEL_X):.3f}, {f64(payload, VEL_Y):.3f}, {f64(payload, VEL_Z):.3f})")
    print(f"  yaw / pitch = {f64(payload, YAW):.4f} / {f64(payload, PITCH):.4f}")
    print(f"  health      = {f64(payload, HEALTH):.1f}   fall_peak_y = {f64(payload, FALL_PEAK):.3f}"
          f"   fall_distance = {f64(payload, FALL_DIST):.3f}")
    print(f"  pose        = {payload[POSE]}   on_ground = {payload[ON_GROUND]}"
          f"   selected_block = {struct.unpack_from('<H', payload, SELECTED)[0]}")
    print(f"  has_player  = {payload[16]}   seed = {struct.unpack_from('<Q', payload, 0)[0]:#x}"
          f"   ticks = {struct.unpack_from('<Q', payload, 8)[0]}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("level")
    parser.add_argument("--create", action="store_true", help="write a fresh level file instead of patching one")
    parser.add_argument("--x", type=float)
    parser.add_argument("--y", type=float)
    parser.add_argument("--z", type=float)
    parser.add_argument("--yaw", type=float)
    parser.add_argument("--pitch", type=float)
    parser.add_argument("--health", type=float)
    parser.add_argument("--stand", action="store_true", help="zero the velocity and clear the fall state")
    args = parser.parse_args()

    if args.create:
        payload = bytearray(133)
        struct.pack_into("<Q", payload, 0, 0x4F50454E43524146)  # WorldSim::kSeed
        struct.pack_into("<Q", payload, 8, 0)
        payload[16] = 1  # has_player
        for offset in (17, 25, 33, PLAYER_X, PLAYER_Y, PLAYER_Z):
            struct.pack_into("<d", payload, offset, 0.0)
        set_f64(payload, HEALTH, 20.0)
        payload[ON_GROUND] = 1
        struct.pack_into("<H", payload, SELECTED, 3)  # grayrock, the launch kit's first cell
        print(f"created ({args.level}):")
    else:
        payload = load(args.level)
    if not args.create:
        print(f"before ({args.level}):")
        dump(payload)

    if args.x is not None:
        set_f64(payload, PLAYER_X, args.x)
    if args.y is not None:
        set_f64(payload, PLAYER_Y, args.y)
    if args.z is not None:
        set_f64(payload, PLAYER_Z, args.z)
    if args.yaw is not None:
        set_f64(payload, YAW, args.yaw)
    if args.pitch is not None:
        set_f64(payload, PITCH, args.pitch)
    if args.health is not None:
        set_f64(payload, HEALTH, args.health)
    if args.stand:
        for offset in (VEL_X, VEL_Y, VEL_Z):
            set_f64(payload, offset, 0.0)
        set_f64(payload, FALL_PEAK, f64(payload, PLAYER_Y))
        set_f64(payload, FALL_DIST, 0.0)
        payload[POSE] = 0
        payload[ON_GROUND] = 1

    store(args.level, payload)
    print(f"after ({args.level}):")
    dump(load(args.level))
    return 0


if __name__ == "__main__":
    sys.exit(main())
