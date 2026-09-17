#!/usr/bin/env python3
"""Focused probe: does a W pulse train move the player? (device self-check)

usage: probe_walk.py <pid> <log> <label>
"""

import subprocess
import sys
import time

PID, LOG, LABEL = sys.argv[1], sys.argv[2], sys.argv[3]
TOOL = "/tmp/td40_ab/td40input"


def pos():
    out = subprocess.run(["tail", "-400", LOG], capture_output=True, text=True).stdout
    for line in reversed(out.splitlines()):
        if "pos (" in line:
            return line.split("pos (")[1].split(")")[0]
    return "?"


def act():
    subprocess.run([TOOL, "front", PID], capture_output=True)
    subprocess.run([TOOL, "activate", PID], capture_output=True)


def hold(code, seconds, every):
    deadline = time.time() + seconds
    while time.time() < deadline:
        subprocess.run([TOOL, "key", str(code), "down"], capture_output=True)
        time.sleep(every)
    subprocess.run([TOOL, "key", str(code), "up"], capture_output=True)


print(f"[{LABEL}] start pos = {pos()}", flush=True)
for label, code, seconds, every in (
    ("W 13 / 60ms x1.0s", 13, 1.0, 0.06),
    ("W 13 / 30ms x1.5s", 13, 1.5, 0.03),
    ("W 13 / 15ms x1.5s", 13, 1.5, 0.015),
    ("S 1  / 30ms x1.0s", 1, 1.0, 0.03),
    ("A 0  / 30ms x1.0s", 0, 1.0, 0.03),
    ("Space 49 x1", 49, 0.12, 0.12),
):
    act()
    time.sleep(0.3)
    before = pos()
    hold(code, seconds, every)
    time.sleep(1.2)
    after = pos()
    print(f"[{LABEL}] {label}: {before} -> {after}  moved={before != after}", flush=True)
print("done", flush=True)
