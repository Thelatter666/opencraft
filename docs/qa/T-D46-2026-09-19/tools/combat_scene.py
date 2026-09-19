#!/usr/bin/env python3
"""T-D46 combat evidence driver (macOS).

One continuous session, one world, one pair of mobs in front of the player:

  A. BARE: a Hollow Wretch walks in and lands its melee hits. Each one costs the
     ⚖ 3.0 of the normal difficulty, the client logs `mob melee for 3.0 ...;
     health now ...` and the HUD hearts drop by 1.5 hearts a hit.
  B. ARMOURED: F9 puts the four timber_* pieces into the armour cells (the only
     thing the shipped client cannot do - see tools/td46_evidence_hook.patch).
     The same mob, same difficulty, now costs 2.3 a hit (research/01 §2.1's
     formula with 7 points and 0 toughness).
  C. WINDOW: a second Wretch of the same species, summoned a few ticks behind the
     first, lands hits INSIDE the ⚖ 10-tick hurt window. Those are absorbed - the
     log says so and the hearts do not move.

usage: combat_scene.py <pid> <window_id> <log> <outdir>
"""
import os
import re
import subprocess
import sys
import time

PID, WIN, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
TOOL = "/tmp/td46input"

# macOS virtual key codes (the keyboard's own numbering, not GLFW's).
KEY_F7, KEY_F8, KEY_F9, KEY_F10 = 98, 100, 101, 109


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


KEY_ESC = 53


def shot(name):
    """Two captures back to back: the second is the fresh one for a window that
    was not in front (docs/05 §3.1 rule 10). Kept cheap on purpose - a 20-tick
    attacker lands a hit every ~1.2 s, and every second this call spends is a hit
    the scene moves past."""
    act()
    path = f"{OUT}/{name}.png"
    for _ in range(2):
        subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    print(f"  shot {name}", flush=True)


def turn_around():
    """Turn the view ~180 degrees with HID mouse deltas.

    The wretch walks straight at the player and coasts about half a block PAST
    them (its own momentum - step_mob_motion damps after the move), so the camera
    ends up inside its box and the mob is invisible. Turning round is what puts it
    back in frame, and the turn has to happen BEFORE the ESC: a paused client
    ignores mouse look (docs/05 §3.1 rule 14).
    """
    subprocess.run([TOOL, "move", "1257", "0"], check=True)


def frozen_shot(name):
    """Screenshot with the simulation PAUSED.

    The ticks stop (the mob freezes where it stands, the hit points stop moving)
    while the frame loop keeps rendering, so the capture lands on the moment that
    matters instead of 1-2 melee hits later - a 20-tick attacker outruns the
    ~1.5 s an activate + two screencaptures costs. The pause overlay sits in the
    middle band; the hearts and the hotbar are below it (docs/05 §3.1 rule 14).
    """
    key(KEY_ESC, 60)
    shot(name)
    key(KEY_ESC, 60)
    time.sleep(0.4)


def key(code, ms=60):
    act()
    time.sleep(0.2)
    subprocess.run([TOOL, "keytap", str(code), str(ms)], check=True)


def log_text():
    with open(LOG, errors="replace") as handle:
        return handle.read()


def count(pattern):
    return len(re.findall(pattern, log_text()))


def wait_for(test, seconds, what):
    """Polls the log until `test(text)` is true; returns the text or exits."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        text = log_text()
        if test(text):
            return text
        time.sleep(0.1)
    print(f"  !! timed out waiting for {what}", flush=True)
    return log_text()


def tap_until(key_code, pattern, tries, what):
    """HID injection is session-dependent on this machine (docs/05 §3.1 rule 2):
    a key that produced no log line is re-tapped rather than assumed."""
    for attempt in range(tries):
        before = count(pattern)
        key(key_code)
        time.sleep(1.2)
        if count(pattern) > before:
            print(f"  {what}: ok on try {attempt + 1}", flush=True)
            return True
        print(f"  {what}: no effect on try {attempt + 1}, re-tapping", flush=True)
    return False


def main():
    print("waiting for the world to settle", flush=True)
    wait_for(lambda t: "respawn point" in t, 60, "the spawn")
    time.sleep(3.0)   # streaming: give the spawn area time to mesh
    shot("01_world_loaded")

    def health(text):
        found = re.findall(r"health now ([0-9.]+)", text)
        return float(found[-1]) if found else 20.0

    def wait_health_below(limit, seconds, what):
        return wait_for(lambda t: health(t) <= limit, seconds, what)

    # ── A: bare-handed. ONE hit is enough to read the number off the log. ────
    print("A: summon one wretch (F7), take ONE hit bare-handed", flush=True)
    if not tap_until(KEY_F7, r"EVIDENCE summon hollow_wretch", 3, "F7 summon"):
        sys.exit("F7 never registered")
    # The scene shot: freeze the approach. The wretch walks in at ~4.6 blocks/s
    # and coasts ~0.5 blocks PAST the player (its own momentum - step_mob_motion
    # damps after the move), at which point the camera is inside its box and the
    # mob is invisible; pausing ~0.15 s after the summon catches it about a block
    # in front, which is the "standing in front of the mob" frame.
    subprocess.run([TOOL, "keytap", str(KEY_F7), "60"], check=True)
    time.sleep(0.15)
    subprocess.run([TOOL, "keytap", str(KEY_ESC), "60"], check=True)
    shot("02a_wretch_closing_in")
    subprocess.run([TOOL, "keytap", str(KEY_ESC), "60"], check=True)
    time.sleep(0.3)
    wait_health_below(17.01, 40, "the first bare hit")
    after_bare = health(log_text())
    frozen_shot("02b_bare_after_one_hit")
    print(f"  health now {after_bare:.1f} after the bare hit", flush=True)

    # ── B: the same mob, now against four timber_* pieces ───────────────────
    print("B: wear the timber set (F9); the SAME mob's next hit is reduced", flush=True)
    if not tap_until(KEY_F9, r"EVIDENCE armour ON", 3, "F9 armour"):
        sys.exit("F9 never registered")
    before = health(log_text())
    wait_health_below(before - 1.0, 40, "the first armoured hit")
    print(f"  bare {after_bare:.1f} -> armoured {health(log_text()):.1f} from the same mob", flush=True)
    frozen_shot("03_armoured_2.3_per_hit")
    print(f"  health now {health(log_text()):.1f} after one armoured hit", flush=True)

    # ── C: three wretches in total, so that some pair is ALWAYS inside the window
    # Three points on a 20-tick attack cycle cannot all be more than 20/3 ticks
    # apart, so with three attackers at least one hit per cycle lands inside the
    # ⚖ 10-tick window and is absorbed.
    print("C: summon three more wretches - the window must swallow some hits", flush=True)
    key(KEY_F8)
    key(KEY_F7)
    key(KEY_F8)
    wait_for(lambda t: "absorbed by the hurt window" in t, 30, "an absorbed hit")
    frozen_shot("05_window_absorbed")
    text = log_text()
    print("  absorbed lines: " + str(count(r"absorbed by the hurt window")), flush=True)
    print("  landed lines:   " + str(count(r"mob melee for [0-9.]+ at")), flush=True)
    print("  health series:  " + " -> ".join(re.findall(r"health now ([0-9.]+)", text)), flush=True)


if __name__ == "__main__":
    main()
