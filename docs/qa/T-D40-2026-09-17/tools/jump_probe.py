#!/usr/bin/env python3
"""Jump probe: hold Space (dense pulses) and read the CEILING of the client's own
position log. A jump shows up as fps samples ABOVE the ground level the player
walks on; standing still shows a constant y."""
import subprocess, sys, time
PID, LOG, LABEL = sys.argv[1], sys.argv[2], sys.argv[3]
TOOL = "/tmp/td40_ab/td40input"

def act():
    subprocess.run([TOOL, "front", PID], capture_output=True)
    subprocess.run([TOOL, "activate", PID], capture_output=True)

def ys():
    out = subprocess.run(["grep", "-o", "pos ([^)]*)", LOG], capture_output=True, text=True).stdout
    return [float(l.split(",")[1]) for l in out.splitlines()]

act(); time.sleep(0.5)
before = ys()[-6:]
for _ in range(300):
    subprocess.run([TOOL, "key", "49", "down"], capture_output=True)
    time.sleep(0.015)
subprocess.run([TOOL, "key", "49", "up"], capture_output=True)
time.sleep(0.5)
after = ys()[-8:]
print(f"[{LABEL}] y before jump hold: {before}")
print(f"[{LABEL}] y during/after jump hold: {after}")
print(f"[{LABEL}] distinct y values: {sorted(set(after))}")
