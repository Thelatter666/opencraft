#!/usr/bin/env python3
"""T-D40 in-game movement scene: walk, sprint (double-tap W per docs/05 §3.1
rule 3 - Ctrl+Space is eaten by macOS), sprint-jump.

The client logs `sprint start/stop` and `sprint-jump arc: N moves, ...` itself,
so the verdict is a log line, not a matter of opinion. Everything is injected at
the HID layer with a DENSE pulse train (15 ms): the ~9-tick key-state clear
after focus loss makes the sparse 60 ms cadence unreliable (debt T-D21), which
this card's first attempt hit.

usage: movement.py <pid> <log> <outdir> <label>
"""

import subprocess
import sys
import time

PID, LOG, OUT, LABEL = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
TOOL = "/tmp/td40_ab/td40input"
CENTER = ("960", "463")
W, SPACE = 13, 49


def log(*args):
    print(f"[{LABEL}]", *args, flush=True)


def act():
    subprocess.run([TOOL, "front", PID], capture_output=True)
    subprocess.run([TOOL, "activate", PID], capture_output=True)


def key(code, down):
    subprocess.run([TOOL, "key", str(code), "down" if down else "up"], capture_output=True)


def dense_hold(code, seconds, every=0.015):
    deadline = time.time() + seconds
    while time.time() < deadline:
        key(code, True)
        time.sleep(every)
    key(code, False)


def pos():
    out = subprocess.run(["tail", "-500", LOG], capture_output=True, text=True).stdout
    for line in reversed(out.splitlines()):
        if "pos (" in line:
            return line.split("pos (")[1].split(")")[0]
    return "?"


def lines(pattern):
    return subprocess.run(["grep", "-c", pattern, LOG], capture_output=True, text=True).stdout.strip()


def shot(name):
    subprocess.run(["screencapture", "-x", "-o", "-l",
                    subprocess.run([TOOL, "win", PID], capture_output=True, text=True).stdout.split()[0],
                    f"{OUT}/{LABEL}_{name}.png"], check=True)


act()
time.sleep(0.8)
start = pos()
log("start pos", start)
shot("M1_before_walk")

log("walk (dense W 15 ms, 1.0 s)")
act()
dense_hold(W, 1.0)
time.sleep(1.3)
walked = pos()
log("after walk", walked)

log("double-tap W -> sprint")
act()
time.sleep(0.2)
# A real key-UP between the taps: the client polls glfwGetKey per frame, so a
# dense pulse train never produces the release edge the double-tap needs.
# Several tap pairs: HID sessions drop events (debt T-D21), and the physics
# arms a 7-tick window on the first press and engages on the next one inside
# it, so a short tap train gives the edge several chances to land.
key(W, True)
time.sleep(0.09)
key(W, False)
for _ in range(3):
    time.sleep(0.15)
    key(W, True)
    time.sleep(0.09)
    key(W, False)
time.sleep(0.12)
key(W, True)
dense_hold(W, 2.2)  # held: sprint keeps running
sprinted = pos()
log("after sprint", sprinted)
shot("M2_sprint")

log("sprint-jump (dense Space while W still held)")
act()
for _ in range(5):
    key(W, True)
    key(SPACE, True)
    time.sleep(0.09)
    key(SPACE, False)
    time.sleep(0.22)
key(W, False)
time.sleep(2.0)
jumped = pos()
log("after sprint-jump", jumped)
shot("M3_after_jump")

time.sleep(1.5)
log("sprint lines:", lines("sprint start"), "start /", lines("sprint stop"), "stop")
log("arc lines:", lines("sprint-jump arc"))
subprocess.run(["grep", "-E", "sprint", LOG], capture_output=True, text=True)
print("\n".join(l for l in subprocess.run(["grep", "-E", "sprint", LOG], capture_output=True,
                                           text=True).stdout.splitlines() if "sprint" in l), flush=True)
log("movement scene done")
