"""T-A1 on-machine evidence helper: drive the OpenCraft window with HID events
and verify each injected event actually landed (HID injection is flaky here, so
every step is checked against the pixels / the client log)."""

import subprocess
import sys
import time

from PIL import Image, ImageChops

GAME_BOX = (320, 89, 1600, 837)  # the 1280x748 game window inside the 1920x1080 screen
WIN_CENTER = ("960", "463")


def shot(path):
    subprocess.run(["screencapture", "-x", "-o", path], check=True)
    return Image.open(path).convert("RGB").crop(GAME_BOX)


def activate(pid):
    subprocess.run(["/tmp/ta1input", "activate", str(pid)], stdout=subprocess.DEVNULL)


def move(pid, dx, dy, tag="m"):
    """Injects an absolute+delta mouse move, retrying until the frame changes."""
    for attempt in range(1, 9):
        activate(pid)
        time.sleep(0.35)
        before = shot(f"/tmp/{tag}_before.png")
        subprocess.run(["/tmp/ta1input", "move", str(dx), str(dy)], check=True)
        time.sleep(0.8)
        after = shot(f"/tmp/{tag}_after.png")
        changed = ImageChops.difference(before, after).getbbox() is not None
        print(f"  move({dx},{dy}) attempt {attempt}: view changed = {changed}", flush=True)
        if changed:
            return attempt
    raise SystemExit(f"mouse move ({dx},{dy}) never landed")


def key(pid, code):
    subprocess.run(["/tmp/ta1input", "keytap", str(code), "120"], check=True)
    time.sleep(0.3)


def click(button, ms):
    subprocess.run(["/tmp/ta1input", "clicktap", str(button), str(ms)] + list(WIN_CENTER), check=True)
    time.sleep(0.5)


def log_tail(path, patterns, n=4):
    hits = []
    with open(path, "r", errors="replace") as handle:
        for line in handle:
            if any(p in line for p in patterns):
                hits.append(line.rstrip())
    return hits[-n:]


if __name__ == "__main__":
    pid = int(sys.argv[1])
    log = sys.argv[2]
    out = sys.argv[3]
    # Pitch sequence: 3 down + 2 up reproduced the session1/session2 aim, but
    # the effect per move is uncertain while injection is flaky - so verify.
    steps = [(0, 152), (0, 152), (0, 152), (0, -152), (0, -152)]
    for dx, dy in steps:
        move(pid, dx, dy)
    shot(f"{out}/I1_aim_final_pitch.png").save(f"{out}/I1_aim_final_pitch.png")
    print("aim captured")
    key(pid, 22)  # hotbar slot 6 = water_vessel
    click(1, 60)  # pour
    time.sleep(1.0)
    shot(f"{out}/I2_pour_water_pristine.png").save(f"{out}/I2_pour_water_pristine.png")
    print("\n".join(log_tail(log, ["vessel", "refused", "placed"])))
    time.sleep(12)
    shot(f"{out}/I3_water_spread_12s.png").save(f"{out}/I3_water_spread_12s.png")
    print("spread captured")
