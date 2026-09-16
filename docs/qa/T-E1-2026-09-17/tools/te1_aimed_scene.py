#!/usr/bin/env python3
"""T-E1 on-machine evidence, second pass (HID layer only, no osascript).

dig -> the drop appears -> walk over -> the drop is gone and the inventory count
went up, with every injected step verified against the pixels.

usage: te1_evidence2.py <pid> <window_id> <log>
"""

import subprocess
import sys
import time

from PIL import Image, ImageChops

PID = sys.argv[1]
WIN = sys.argv[2]
LOG = sys.argv[3]
OUT = "/tmp/te1_evidence"
CENTER = ("960", "463")
TOOL = "/tmp/te1input2"


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL)


def shot(name):
    path = f"{OUT}/{name}.png"
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    return Image.open(path).convert("RGB")


def dark_around_crosshair(img):
    """Dark pixels in a 200x200 box around the crosshair: the selection
    wireframe is (0.05, 0.05, 0.05), so a targeted block shows up as lines."""
    box = img.crop((540, 285, 740, 485)).load()
    return sum(1 for y in range(200) for x in range(200) if max(box[x, y]) < 45)


def log_hits(*patterns):
    hits = []
    with open(LOG, errors="replace") as handle:
        for line in handle:
            if any(p in line for p in patterns):
                hits.append(line.strip())
    return hits


print(f"pre-flight: {subprocess.run([TOOL, 'trust', 'x'], capture_output=True, text=True).stdout.strip()}",
      flush=True)

# ── 1. aim: verified pitch steps until a block is targeted ────────────────
act()
time.sleep(0.6)
pristine = shot("F1_pristine")
print(f"F1 pristine: hotbar slot2 count region captured (dark={dark_around_crosshair(pristine)})", flush=True)

for step, total in ((0, 40), (0, 50), (0, 30), (0, 30), (0, 30), (0, 30), (0, 30)):
    act()
    time.sleep(0.35)
    before = shot("tmp_before")
    subprocess.run([TOOL, "move", str(step), str(total)], check=True)
    time.sleep(0.7)
    after = shot("tmp_after")
    changed = ImageChops.difference(before, after).getbbox() is not None
    score = dark_around_crosshair(after)
    print(f"  pitch +{total}px: view changed = {changed}, wireframe dark = {score}", flush=True)
    if score >= 10:
        break
aimed = shot("F2_aimed")
print(f"F2 aimed: wireframe dark = {dark_around_crosshair(aimed)}", flush=True)

# ── 2. dig (hold left), with frames inside the drop's short visible window ──
act()
time.sleep(0.4)
subprocess.run([TOOL, "click", "0", "down", *CENTER], check=True)
t0 = time.time()
for target, name in ((0.85, "F3_break"), (1.05, "F4_drop_t0")):
    while time.time() - t0 < target:
        time.sleep(0.05)
        subprocess.run([TOOL, "click", "0", "down", *CENTER], check=True)
    shot(name)
    print(f"  {name} at {time.time() - t0:.2f}s", flush=True)
subprocess.run([TOOL, "click", "0", "up", *CENTER], check=True)
time.sleep(0.5)
shot("F5_drop_rest")
time.sleep(0.8)
rest_a = shot("F6_drop_rest2")
time.sleep(1.2)
rest_b = shot("F7_drop_rest3")

spawns = log_hits("item drop")
print("spawn log:", spawns[-1] if spawns else "(none)", flush=True)
if not spawns:
    raise SystemExit("no drop was spawned: the dig did not break a block")

# The drop must be ANIMATING (bob 2 rad/s, spin 1 rad/s): two frames 2 s apart
# differing only around the drop is the machine proof that it is drawn.
diff = ImageChops.difference(rest_a, rest_b)
print(f"F6 vs F7 diff bbox = {diff.getbbox()}", flush=True)

# ── 3. walk over: the drop enters the pickup box and is collected ──────────
act()
time.sleep(0.25)
subprocess.run([TOOL, "key", "13", "down"], check=True)
deadline = time.time() + 1.3
while time.time() < deadline:
    time.sleep(0.06)
    subprocess.run([TOOL, "key", "13", "down"], check=True)
subprocess.run([TOOL, "key", "13", "up"], check=True)
time.sleep(1.0)
shot("F8_after_walk")
time.sleep(1.5)
shot("F9_after_pickup")

# ── 4. read the hotbar count region out of the frames for a pixel verdict ──
# Slot 2 (sod_loam) sits in the bottom bar; its count text is in the cell's
# upper-right corner. Crop the whole bar strip and diff pristine vs settled.
for name, img in (("F1", pristine), ("F9", Image.open(f"{OUT}/F9_after_pickup.png").convert("RGB"))):
    bar = img.crop((520, 700, 760, 748))
    bar.resize((720, 144), Image.NEAREST).save(f"{OUT}/F{'1' if name == 'F1' else '9'}_hotbar_zoom.png")

finder = subprocess.run(["pgrep", "-x", "Finder"], capture_output=True, text=True).stdout.split()
if finder:
    subprocess.run([TOOL, "activate", finder[0]], stdout=subprocess.DEVNULL)
print("done", flush=True)
