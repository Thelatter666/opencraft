#!/usr/bin/env python3
"""T-D40 in-game evidence: the same scripted scene, driven through both binaries.

HID-layer injection only (docs/05 §3.1: no osascript). The pitch is driven by
SATURATION (a delta large enough to hit the -90 deg clamp), which makes the
camera state deterministic across runs without needing the delta to be exact;
yaw is never touched, so it stays at its spawn value in both runs.

usage: scenario.py <pid> <log> <outdir> <label>
"""

import os
import subprocess
import sys
import time

from PIL import Image, ImageChops

PID = sys.argv[1]
LOG = sys.argv[2]
OUT = sys.argv[3]
LABEL = sys.argv[4]
TOOL = "/tmp/td40_ab/td40input"

os.makedirs(OUT, exist_ok=True)
WIN = None
CENTER = ("960", "463")


def log(*args):
    print(f"[{LABEL}]", *args, flush=True)


def run(*args, **kwargs):
    return subprocess.run(list(args), capture_output=True, text=True, **kwargs)


def find_window():
    global WIN
    for _ in range(40):
        found = run(TOOL, "win", PID).stdout.split()
        if found:
            WIN = found[0]
            log("window", " ".join(found))
            return True
        time.sleep(0.5)
    raise SystemExit("no game window found for pid " + PID)


def act():
    run(TOOL, "front", PID)
    run(TOOL, "activate", PID)


def shot(name):
    path = f"{OUT}/{LABEL}_{name}.png"
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    return Image.open(path).convert("RGB")


def diff_bbox(a, b):
    return ImageChops.difference(a, b).getbbox()


def hold(key, seconds, every=0.06):
    """Repeated keydowns: the client polls per frame and GLFW clears the state
    when focus is lost (docs/05 §3.1 rule 2), so a long hold is a pulse train."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        run(TOOL, "key", str(key), "down")
        time.sleep(every)
    run(TOOL, "key", str(key), "up")


def hold_left(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        run(TOOL, "click", "0", "down", *CENTER)
        time.sleep(0.06)
    run(TOOL, "click", "0", "up", *CENTER)


log("pre-flight:", run(TOOL, "trust", "x").stdout.strip())
find_window()

# ── 1. pristine frame ───────────────────────────────────────────────────────
act()
time.sleep(1.0)
s1 = shot("S1_pristine")
log("S1 pristine captured")

# ── 2. deterministic aim: saturate the pitch against its clamp, then back off
# by a fixed delta. The saturation erases whatever pitch the run started with,
# so the resting angle is a function of the delta alone (docs/05 §3.1: the
# "saturate then back off" recipe). -50 deg: steep enough that the crosshair
# lands on the ground in front, shallow enough that the target cell is not the
# actor's own (a straight-down placement is refused).
SATURATE_PX = 3000
BACKOFF_PX = -279  # 40 deg up from the -90 deg clamp, at 0.0025 rad/px
for attempt in range(6):
    act()
    time.sleep(0.4)
    before = shot("tmp_before")
    run(TOOL, "move", "0", str(SATURATE_PX))
    time.sleep(0.7)
    after = shot("tmp_after")
    moved = diff_bbox(before, after) is not None
    log(f"aim saturate attempt {attempt}: view changed = {moved}")
    if moved:
        break
for attempt in range(6):
    before = shot("tmp_before2")
    run(TOOL, "move", "0", str(BACKOFF_PX))
    time.sleep(0.7)
    after = shot("tmp_after2")
    moved = diff_bbox(before, after) is not None
    log(f"aim backoff attempt {attempt}: view changed = {moved}")
    if moved:
        break
s2 = shot("S2_aimed")
log("S2 aimed captured (pitch = -50 deg)")

# ── 3. dig the block below: the drop must spawn (log line) ──────────────────
act()
time.sleep(0.3)
hold_left(1.15)
time.sleep(0.35)
s3 = shot("S3_drop_a")
time.sleep(0.6)
s4 = shot("S4_drop_b")
log("S3/S4 captured (drop visible frames)")

# ── 4. place a block with the selected hotbar item (greyrock) ───────────────
act()
time.sleep(0.3)
run(TOOL, "click", "1", "down", *CENTER)
time.sleep(0.08)
run(TOOL, "click", "1", "up", *CENTER)
time.sleep(0.8)
s5 = shot("S5_after_place")
log("S5 captured (after place)")

# ── 5. walk + jump: the live client must still move and hop ─────────────────
act()
time.sleep(0.2)
hold(13, 0.9)  # W
act()
run(TOOL, "keytap", "49", "120")  # Space
time.sleep(0.45)
run(TOOL, "keytap", "49", "120")
time.sleep(0.55)
s6 = shot("S6_after_walk_jump")
log("S6 captured (after walk + jump)")

# ── 6. let the autosave cadence (200 ticks) flush the world ─────────────────
log("waiting for the autosave cadence...")
time.sleep(13.0)
tail = run("tail", "-40", LOG).stdout
log("log tail:\n" + tail)
log("done")
