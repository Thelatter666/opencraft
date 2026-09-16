"""T-A1 fluid-regression evidence: pour a water source through the authority and
measure how the water surface grows over the next seconds.

The client's fluid layer advances one block per 5 ticks (docs/research/10 §3), so
the flooded area must grow between the captures - which is what the blue-pixel
fraction below measures. The pour itself is a request (ActionKind::PourWater)."""

import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image

from ta1_hid import click, key, log_tail, move, shot  # noqa: E402

OUT = "/Users/happy/Desktop/opencraft_worktree/opencraft-T-A1/docs/qa/T-A1-2026-09-16"


def blue_fraction(name):
    im = Image.open(f"{OUT}/{name}.png").convert("RGB")
    im = im.crop((320, 89, 1600, 837))
    px = im.load()
    hits = 0
    total = 0
    for y in range(0, im.height, 2):
        for x in range(0, im.width, 2):
            r, g, b = px[x, y]
            total += 1
            if b > r + 20 and b > g + 10:
                hits += 1
    return 100.0 * hits / total


if __name__ == "__main__":
    pid = int(sys.argv[1])
    log = sys.argv[2]
    for _ in range(3):  # look down
        move(pid, 0, 152)
    for _ in range(2):  # back to the calibrated aim (see README)
        move(pid, 0, -152)
    shot(f"{OUT}/J1_ground_dry.png").save(f"{OUT}/J1_ground_dry.png")
    key(pid, 22)  # hotbar slot 6 = water_vessel
    click(1, 60)  # pour
    for label, delay in [("J2_t0_3s", 0.3), ("J3_t1s", 0.7), ("J4_t3s", 2.0), ("J5_t8s", 5.0)]:
        time.sleep(delay)
        shot(f"{OUT}/{label}.png").save(f"{OUT}/{label}.png")
    print("\n".join(log_tail(log, ["vessel", "refused"])))
    for name in ["J1_ground_dry", "J2_t0_3s", "J3_t1s", "J4_t3s", "J5_t8s"]:
        print(f"{name}: blue-ish screen fraction {blue_fraction(name):.1f}%")
    subprocess.run(["sync"], check=False)
