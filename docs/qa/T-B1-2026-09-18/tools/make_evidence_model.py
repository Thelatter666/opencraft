#!/usr/bin/env python3
"""T-B1 evidence: builds a legible mob model for the screenshot.

NOT a delivered asset - it goes into assets/mobs/mossback.vox for the two
evidence runs only (assets/mobs/ is not part of this card's output). The
committed B0 fixture is a bare 8-voxel column, which proves the decoder but
reads as a thin stick in a screenshot; this one exists so the "it is no longer
two boxes" claim is obvious at a glance.

Writes `evidence_mossback.vox` next to this script. Shape, in the voxel file's
own axes (x = sideways, y = forward, z = up), painted with the joint labels of
research/12 §4.3:

    head  (2) : x 2..5, y 8..11, z 5..8
    body  (1) : x 2..5, y 2..7,  z 4..7
    arms  (3/4): x 1 and x 6, y 6, z 4..7
    legs  (5/6): x 2 and x 5, y 3, z 0..3

Occupied height is 9 voxels (z 0..8), so the client scales it to the mob's
1.4-block collision height.
"""
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
SIZE = (16, 16, 16)

PALETTE = {
    1: (150, 95, 55, 255),   # body
    2: (95, 165, 90, 255),   # head
    3: (130, 80, 45, 255),   # arm_l
    4: (130, 80, 45, 255),   # arm_r
    5: (165, 175, 70, 255),  # leg_l
    6: (165, 175, 70, 255),  # leg_r
    7: (200, 200, 200, 255), # tail (unused here)
    8: (255, 255, 255, 255), # spare (unused here)
}


def boxes():
    voxels = []

    def fill(color, x0, x1, y0, y1, z0, z1):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    voxels.append((x, y, z, color))

    fill(2, 2, 5, 8, 11, 5, 8)   # head
    fill(1, 2, 5, 2, 7, 4, 7)    # body
    fill(3, 1, 1, 6, 6, 4, 7)    # arm_l
    fill(4, 6, 6, 6, 6, 4, 7)    # arm_r
    fill(5, 2, 2, 3, 3, 0, 3)    # leg_l
    fill(6, 5, 5, 3, 3, 0, 3)    # leg_r
    return voxels


def chunk(chunk_id, content):
    return chunk_id + struct.pack("<ii", len(content), 0) + content


def main():
    voxels = boxes()
    body = chunk(b"SIZE", struct.pack("<iii", *SIZE))
    body += chunk(b"XYZI", struct.pack("<i", len(voxels)) +
                  b"".join(struct.pack("<BBBB", x, y, z, c) for x, y, z, c in voxels))
    rgba = b"".join(bytes(PALETTE.get(index, (0, 0, 0, 0))) for index in range(1, 257))
    body += chunk(b"RGBA", rgba)
    out = HERE / "evidence_mossback.vox"
    out.write_bytes(b"VOX " + struct.pack("<i", 150) + chunk(b"MAIN", b"") + body)
    print(f"{out}: {len(voxels)} voxels, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
