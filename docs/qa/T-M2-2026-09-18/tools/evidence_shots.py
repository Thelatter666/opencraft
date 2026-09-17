#!/usr/bin/env python3
"""T-M2 focused framing: one observable segment per mob, captured with the mob
walking TO the camera, so each shot has the mob large and centred.

usage: evidence_shots.py <pid> <window_id> <log> <outdir>
"""
import subprocess, sys, time

PID, WIN, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
TOOL = "/tmp/tm2input"
KEY = {"1": 18, "7": 26, "F6": 97, "F7": 98, "F8": 100}


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL)


def shot(name):
    act()
    time.sleep(0.5)
    path = f"{OUT}/{name}.png"
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    time.sleep(0.3)
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    print(f"  shot {name}", flush=True)


def key(name, ms=130):
    act()
    time.sleep(0.25)
    subprocess.run([TOOL, "keytap", str(KEY[name]), str(ms)], check=True)
    time.sleep(0.35)


def held_now():
    lines = [l for l in open(LOG, errors="replace") if "EVIDENCE held=" in l]
    return int(lines[-1].split("EVIDENCE held=")[1].split(" ")[0]) if lines else None


act()
time.sleep(0.5)
shot("01_pristine_surface")

# The grazing mob, tempted: hold the loaf, summon it, let it walk in.
for _ in range(8):
    key("7")
    time.sleep(0.5)
    if held_now() == 24:
        break
key("F6")
time.sleep(1.4)
shot("02a_mossback_approaching")
time.sleep(2.5)
shot("02b_mossback_tempted_adjacent")

# The melee hunter: summon it next to the player and let it land hits.
key("F7")
time.sleep(2.0)
shot("03a_wretch_approaching")
time.sleep(2.5)
shot("03b_wretch_melee_adjacent")
print(f"[melee] {[l.strip() for l in open(LOG, errors='replace') if 'mob event: melee' in l][-2:]}", flush=True)

# The exploder: summon it, catch the fuse, then the blast.
key("F8")
time.sleep(0.6)
shot("04a_blastbud_approaching")
time.sleep(0.8)
shot("04b_blastbud_fuse")
time.sleep(2.5)
shot("05_after_blast")
print(f"[blast] {[l.strip() for l in open(LOG, errors='replace') if 'blast at' in l][-1:]}", flush=True)
print(f"[health] {[l.strip() for l in open(LOG, errors='replace') if 'health now' in l][-1:]}", flush=True)
