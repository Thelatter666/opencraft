#!/usr/bin/env python3
"""T-D45 death-cycle evidence driver (macOS).

What it drives, in one continuous session, with the patched save (see
patch_level.py: the player is 40 blocks up, airborne, and the persisted view
pitch looks at the ground):

  A. the fall kills the player -> the death screen appears, the drops land at
     the death point (visible because the camera looks down);
  B. while dead, keyboard WASD + a left click at a neutral point do nothing:
     the only position readout the client prints (the 2 s "fps ... pos (x, y, z)"
     line) must not move, and no dig/place/attack line may appear;
  C. clicking RESPAWN returns the player to the respawn point, full hearts.

usage: death_scene.py <pid> <window_id> <log> <outdir> <build_dir>
"""
import re
import shutil
import subprocess
import sys
import time

PID, WIN, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
BUILD = sys.argv[5] if len(sys.argv) > 5 else ""
TOOL = "/tmp/td45input"
# Window bounds (the CGWindowList rect includes the 28 px title bar of the
# 1280x720 client area - the same "+28" the pause-menu clicks use).
BOUNDS = None


def win_rect():
    global BOUNDS
    if BOUNDS is None:
        out = subprocess.run([TOOL, "win", PID], capture_output=True, text=True).stdout.split()
        BOUNDS = (float(out[1]), float(out[2]))  # x, y of the window's top-left
    return BOUNDS


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL)


def shot(name):
    act()
    time.sleep(0.5)
    path = f"{OUT}/{name}.png"
    for _ in range(2):  # the second capture is the fresh one (docs/05 §3.1 rule 10)
        subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
        time.sleep(0.3)
    print(f"  shot {name}", flush=True)


def click(fb_x, fb_y, ms=120):
    """fb_x/fb_y are CLIENT-AREA pixels (1280x720 at the top-left of the window)."""
    wx, wy = win_rect()
    act()
    time.sleep(0.25)
    subprocess.run([TOOL, "clicktap", "0", str(ms), str(wx + fb_x), str(wy + 28 + fb_y)], check=True)


def key(code, ms=15):
    act()
    time.sleep(0.2)
    subprocess.run([TOOL, "keytap", str(code), str(ms)], check=True)


def log_text():
    with open(LOG, errors="replace") as handle:
        return handle.read()


def wait_for(pattern, timeout, what):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if re.search(pattern, log_text()):
            print(f"  {what}: matched {pattern!r}", flush=True)
            return True
        time.sleep(0.25)
    print(f"  !! {what}: TIMEOUT waiting for {pattern!r}", flush=True)
    return False


def last_positions():
    return re.findall(r"pos \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)", log_text())


act()
time.sleep(0.5)

# ── A. the fall, the death screen, the drops ────────────────────────────────
if not wait_for(r"player died at", 40, "A death"):
    sys.exit(4)
time.sleep(0.6)
shot("01_death_screen_and_drops")

# ── B. a dead player's inputs do nothing ───────────────────────────────────
# Dense 15 ms pulses for W/A (docs/05 §3.1 rule 7: 60 ms pulses never register
# on this machine, 15 ms ones do), then a left click at a point far from the
# RESPAWN button so it cannot be the button's own click.
before = last_positions()
for _ in range(6):
    key(13)  # W
    key(0)   # A
click(300, 200)
time.sleep(2.6)
after = last_positions()
print(f"  positions before dead-input probes: {before[-1] if before else None}", flush=True)
print(f"  positions after  dead-input probes: {after[-1] if after else None}", flush=True)
shot("02_dead_inputs_ignored")

# The save the DEATH write-back produced (the 200-tick autosave runs while the
# player lies dead, so it stores health 0 and the death position). Copied out
# here, before the respawn heals the player, because the 0-health scenario of
# acceptance 8 must read the REAL artifact rather than a hand-edited file.
LEVEL = f"{BUILD}/saves/world/level.ocd"
if BUILD and re.search(r"autosave", log_text()):
    shutil.copy(LEVEL, f"{OUT}/level_after_death_autosave.ocd")
    print(f"  saved {OUT}/level_after_death_autosave.ocd", flush=True)

# ── C. respawn ──────────────────────────────────────────────────────────────
click(640, 390)  # the RESPAWN button band, 36 px tall, centred at cy+30
if not wait_for(r"respawned at", 10, "C respawn"):
    sys.exit(5)
time.sleep(1.2)
shot("03_after_respawn")

# ── D. the CONTROL: the same injection moves a live player ──────────────────
# Without this, "the position did not move while dead" could just mean the
# injection channel was dead (docs/05 §3.1: the HID channel is session-flaky, and
# a silent no-op has faked this conclusion before). Same pulses, same order.
before = last_positions()
for _ in range(6):
    key(13)
    key(0)
time.sleep(2.6)
after = last_positions()
print(f"  positions before live-input control: {before[-1] if before else None}", flush=True)
print(f"  positions after  live-input control: {after[-1] if after else None}", flush=True)
shot("05_live_input_control")
print("done", flush=True)
