#!/usr/bin/env python3
"""Hold a movement key by re-posting keyDown every 60 ms.

docs/05 §3.1 rule 2: a synthetic hold is cleared ~9 ticks after it is posted, so
"hold W and walk far away" does not work with a single event. Re-posting the
keyDown faster than the client's 20 TPS poll keeps the state alive; the loop
below also polls the game's own position log and stops at the target, so the
displacement is measured, not assumed.

  walk.py <log> <key:w|a|s|d> <axis:x|z> <target> <max_seconds>
"""

import re
import subprocess
import sys
import time

TOOL = "/tmp/td4/td4input"
KEYS = {"w": 13, "a": 0, "s": 1, "d": 2}
AXIS = {"x": 0, "z": 2}


def state():
    import json

    with open("/tmp/td4/state.json") as handle:
        return json.load(handle)


def last_pos(log):
    position = None
    with open(log, errors="replace") as handle:
        for line in handle:
            found = re.search(r"pos \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", line)
            if found:
                position = tuple(float(value) for value in found.groups())
    return position


def main():
    log, key, axis, target, max_seconds = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5])
    game = state()
    subprocess.run([TOOL, "activate", str(game["pid"])], stdout=subprocess.DEVNULL)
    index = AXIS[axis]
    code = KEYS[key]
    start = time.time()
    last_report = 0.0
    while time.time() - start < max_seconds:
        subprocess.run([TOOL, "key", str(code), "down"], stdout=subprocess.DEVNULL)
        time.sleep(0.06)
        elapsed = time.time() - start
        if elapsed - last_report >= 2.0:
            last_report = elapsed
            position = last_pos(log)
            if position is not None:
                print(f"  t={elapsed:5.1f}s pos=({position[0]:.2f}, {position[1]:.2f}, {position[2]:.2f})", flush=True)
                reached = position[index] <= target if target <= 0 else position[index] >= target
                if reached:
                    print(f"  target {axis}={target} reached", flush=True)
                    break
    subprocess.run([TOOL, "key", str(code), "up"], stdout=subprocess.DEVNULL)
    print(f"final pos = {last_pos(log)}")


if __name__ == "__main__":
    main()
