#!/usr/bin/env python3
"""Focused probe: why does the feed right-click not register?"""
import subprocess, sys, time

PID, WIN, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
TOOL = "/tmp/tm2input"
# ⚠ The HUD cell labelled N is inventory slot N-1, and the client maps it with
# GLFW_KEY_1 + slot - so the cell showing the grain loaf (slot 6) is the "7" KEY.
KEY = {"7": 26, "S": 1, "F6": 97}


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL)


def shot(name):
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, f"{OUT}/{name}.png"], check=True)


def key(name, ms=120):
    act(); time.sleep(0.25)
    subprocess.run([TOOL, "keytap", str(KEY[name]), str(ms)], check=True)
    time.sleep(0.3)


def look(dy):
    act(); time.sleep(0.2)
    subprocess.run([TOOL, "move", "0", str(dy)], check=True)
    time.sleep(0.35)


def click(button=1, ms=180):
    act(); time.sleep(0.2)
    subprocess.run([TOOL, "clicktap", str(button), str(ms)], check=True)
    time.sleep(0.5)


def probes():
    out = []
    with open(LOG, errors="replace") as h:
        for line in h:
            if "EVIDENCE rclick" in line or "fed mob" in line or "feed refused" in line:
                out.append(line.strip().split("] ")[-1])
    return out


act(); time.sleep(0.5)
key("F6")
time.sleep(2.5)
shot("P0_summoned")
key("7")
shot("P1_food_selected")
time.sleep(2.0)
shot("P2_approached")

for i in range(8):
    click(1)
    got = probes()
    print(f"attempt {i}: {len(got)} probe lines; last={got[-1] if got else 'none'}", flush=True)
    if any("fed mob" in g for g in got):
        print("FEED ACCEPTED", flush=True)
        break
    look(40)
shot("P3_after")
print("--- all probe lines ---", flush=True)
for line in probes():
    print(" ", line, flush=True)
