#!/usr/bin/env python3
"""T-D4 on-machine evidence driver.

Same shape as the T-A1 harness (docs/qa/T-A1-2026-09-16/tools/ta1_hid.py), with
one difference that removes the flakiness: the aim comes from the *save* (patched
yaw/pitch), not from injected mouse deltas. Injection here is only the click that
digs or places, and it is aimed at the window centre so a drifted real cursor
cannot route it to another window (docs/05 §3.1 rule 3).

Commands (state, pid and window geometry live in /tmp/td4/state.json):

  start <tag>            launch build/opencraft, log to /tmp/td4/<tag>.log
  shot <file>            window-scoped screencapture of the game window
  click <btn> <ms>       HID click at the window centre (0 = left, 1 = right)
  log <pattern> [...]    print matching log lines
  kill                   terminate the running game
"""

import json
import os
import signal
import subprocess
import sys
import time

WORKTREE = "/Users/happy/Desktop/opencraft_worktree/opencraft-T-D4"
BUILD = f"{WORKTREE}/build"
TOOL = "/tmp/td4/td4input"
STATE = "/tmp/td4/state.json"


def load_state():
    with open(STATE) as handle:
        return json.load(handle)


def save_state(state):
    with open(STATE, "w") as handle:
        json.dump(state, handle, indent=2)


def window_for(pid):
    out = subprocess.run([TOOL, "win", str(pid)], capture_output=True, text=True, check=True).stdout.strip()
    if not out:
        raise SystemExit(f"no window for pid {pid}")
    line = out.splitlines()[0].split()
    wid, x, y, w, h = line[0], float(line[1]), float(line[2]), float(line[3]), float(line[4])
    return {"id": wid, "x": x, "y": y, "w": w, "h": h, "cx": int(x + w / 2), "cy": int(y + h / 2)}


def cmd_start(tag):
    log = f"/tmp/td4/{tag}.log"
    handle = open(log, "w")
    proc = subprocess.Popen(["./opencraft"], cwd=BUILD, stdout=handle, stderr=subprocess.STDOUT)
    window = None
    for _ in range(60):
        time.sleep(0.5)
        try:
            window = window_for(proc.pid)
            break
        except SystemExit:
            continue
    if window is None:
        raise SystemExit("the game window never appeared")
    save_state({"pid": proc.pid, "log": log, "tag": tag, "window": window})
    print(f"started pid={proc.pid} log={log} window={window['id']} {window['w']:.0f}x{window['h']:.0f}"
          f" at ({window['x']:.0f},{window['y']:.0f}) centre ({window['cx']},{window['cy']})")


def cmd_shot(path):
    state = load_state()
    subprocess.run([TOOL, "activate", str(state["pid"])], stdout=subprocess.DEVNULL)
    time.sleep(0.4)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    subprocess.run(["screencapture", "-x", "-o", f"-l{state['window']['id']}", path], check=True)
    print(f"shot {path}")


def cmd_click(button, ms):
    state = load_state()
    subprocess.run([TOOL, "activate", str(state["pid"])], stdout=subprocess.DEVNULL)
    time.sleep(0.4)
    win = state["window"]
    subprocess.run([TOOL, "clicktap", str(button), str(ms), str(win["cx"]), str(win["cy"])], check=True)
    print(f"click button={button} ms={ms} at ({win['cx']},{win['cy']})")
    time.sleep(0.5)


def cmd_log(*patterns):
    state = load_state()
    hits = []
    with open(state["log"], errors="replace") as handle:
        for line in handle:
            if not patterns or any(p in line for p in patterns):
                hits.append(line.rstrip())
    print("\n".join(hits) if hits else "(no matching lines)")


def cmd_kill():
    state = load_state()
    try:
        os.kill(state["pid"], signal.SIGKILL)
    except ProcessLookupError:
        pass
    time.sleep(0.5)
    print(f"killed pid={state['pid']}")


def main():
    command = sys.argv[1]
    if command == "start":
        cmd_start(sys.argv[2])
    elif command == "shot":
        cmd_shot(sys.argv[2])
    elif command == "click":
        cmd_click(int(sys.argv[2]), int(sys.argv[3]))
    elif command == "log":
        cmd_log(*sys.argv[2:])
    elif command == "kill":
        cmd_kill()
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
