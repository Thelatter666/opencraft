#!/usr/bin/env python3
"""T-M2 on-machine evidence: the three launch mobs, driven through HID injection.

HID layer only (docs/05 §3.1: no osascript). Every step is verified against the
LOG, not against pixels: the client logs "fed mob N with item M" on an accepted
feed and the authority logs every hit, birth and blast, so a step either shows up
in the log or it did not happen.

Two staging rules learned on-machine (both are the RULES working, not bugs):

  * hold the food BEFORE summoning. A grazing mob that has already strolled past
    its 6-block tempt_start_range will not start following (research/11 §1.5.6),
    so a mob summoned first wanders off and stays away.
  * the HUD cell labelled N is inventory slot N-1 (the client maps it with
    GLFW_KEY_1 + slot), so the grain loaf in slot 6 is the "7" KEY.
  * NO MOUSE AIMING. The synthetic move delta turned out to be wildly nonlinear on
    this machine - a single 30 px step saturated the pitch at its 89-degree clamp,
    so a mob standing 1.2 blocks in front could never be hit again. Sneaking
    (left Shift, code 56) lowers the eye to 1.27, which is INSIDE a grazing mob's
    body (0..1.4), so a plain horizontal view picks it: no pitch injection at all.

usage: scenario.py <pid> <window_id> <log> <outdir>
"""

import subprocess
import sys
import time

PID, WIN, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
TOOL = "/tmp/tm2input"

# macOS virtual key codes.
KEY = {"1": 18, "7": 26, "W": 13, "S": 1, "F6": 97, "F7": 98, "F8": 100}


def REQUIRE(cond, message):
    if not cond:
        print(f"STAGING FAILURE: {message}", flush=True)
        sys.exit(3)


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL)


def shot(name):
    # The window must be FRONT before the capture: a window capture of a window
    # that is not on screen returns its last-drawn surface, which silently
    # produced three identical "different" frames on the first pass. Two captures
    # are taken and the second kept, so the buffer is certainly from after the
    # activation.
    act()
    time.sleep(0.45)
    path = f"{OUT}/{name}.png"
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    time.sleep(0.25)
    subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    print(f"  shot {name}", flush=True)


def key(name, ms=120):
    act()
    time.sleep(0.25)
    subprocess.run([TOOL, "keytap", str(KEY[name]), str(ms)], check=True)
    time.sleep(0.3)


def sneak_click(button=1):
    """Right click while sneaking: the crouched eye line meets a mob's body."""
    act()
    time.sleep(0.2)
    subprocess.run([TOOL, "key", "56", "down"], check=True)  # left Shift = sneak
    time.sleep(0.3)
    subprocess.run([TOOL, "clicktap", str(button), "180"], check=True)
    time.sleep(0.35)
    subprocess.run([TOOL, "key", "56", "up"], check=True)
    time.sleep(0.4)


def click(button=1, ms=180):
    act()
    time.sleep(0.2)
    subprocess.run([TOOL, "clicktap", str(button), str(ms)], check=True)
    time.sleep(0.5)


def log_lines(*patterns):
    found = []
    with open(LOG, errors="replace") as handle:
        for line in handle:
            if any(p in line for p in patterns):
                found.append(line.strip())
    return found


print(f"pre-flight trust: {subprocess.run([TOOL, 'trust', 'x'], capture_output=True, text=True).stdout.strip()}",
      flush=True)

act()
time.sleep(0.5)

# ── the spawner's own log ────────────────────────────────────────────────
natural = log_lines("mob mossback spawned at", "mob hollow_wretch spawned at", "mob blastbud spawned at")
print(f"[0] natural spawns by now: {len(natural)}", flush=True)
shot("00_pristine")

# ══ A. the grazing mob: hold the food, summon, and it walks over ═════════
GRAIN_LOAF = 24  # the grain loaf's item id in the launch registry


def held_now():
    lines = log_lines("EVIDENCE held=")
    if not lines:
        return None
    tail = lines[-1].split("EVIDENCE held=")[1]
    return int(tail.split(" ")[0])


def hold_food():
    """Presses the grain-loaf cell until the client reports holding it.

    A keytap can be dropped when the window is not frontmost for that instant, so
    the press is retried and VERIFIED against the log rather than assumed."""
    for attempt in range(8):
        key("7")  # cell 7 = grain loaf (slot 6)
        time.sleep(0.6)
        if held_now() == GRAIN_LOAF:
            print(f"[A] holding the grain loaf after {attempt + 1} press(es)", flush=True)
            return True
        print(f"[A] still holding {held_now()} after press {attempt + 1}, retrying", flush=True)
    return False


REQUIRE(hold_food(), "could not select the grain loaf")
shot("A1_holding_grain_loaf")
key("F6")
time.sleep(1.5)
shot("A2_mossback_summoned")
time.sleep(2.5)
shot("A3_mossback_followed")

# ══ B. breeding: feed TWO DIFFERENT mobs, then let them find each other ══
# ⚖ research/11 §6.2 needs 两头同种成体 in love. The staging problem is that a
# tempted mob FOLLOWS the player, so it is always the nearest one in the view and
# a second right click re-feeds it (which the rule allows and which produces no
# baby). The recipe below therefore:
#   1. feeds the first mob,
#   2. puts the FOOD AWAY (so it stops following) and walks off, leaving it behind,
#   3. summons and feeds a second mob,
#   4. puts the food away again - which is also what hands the steering over to the
#      pair's own 求偶 goal, so the two walk to each other and the ⚖ 2.5 s timer runs.
def fed_ids():
    return {line.split("fed mob ")[1].split(" ")[0] for line in log_lines("fed mob") if "fed mob " in line}


def aim_and_feed(wanted):
    """Sweeps the view down until `wanted` distinct mobs have been fed."""
    for attempt in range(12):
        sneak_click(1)
        ids = fed_ids()
        print(f"[B] feed attempt {attempt}: ids={sorted(ids)}", flush=True)
        if len(ids) >= wanted:
            return True
    return False


key("F6")
time.sleep(2.5)
shot("B1_first_summoned")
REQUIRE(aim_and_feed(1), "could not feed the first mob")
shot("B2_first_fed")

key("1")  # food away: the follower stops, and stays where it is
time.sleep(1.0)
key("W", 1400)  # walk off so the first mob is no longer in front
time.sleep(1.0)
shot("B3_left_the_first_behind")

REQUIRE(hold_food(), "could not re-select the grain loaf")
key("F6")
time.sleep(2.5)
shot("B4_second_summoned")
REQUIRE(aim_and_feed(2), "could not feed a second mob")
shot("B5_two_fed")

key("1")  # food away: 求偶 takes the steering and the pair converges
time.sleep(6.0)
shot("B6_baby")
print(f"[B] births: {log_lines('born at')}", flush=True)
print(f"[B] fed ids: {sorted(fed_ids())}", flush=True)

# ══ C. the melee hunter ═════════════════════════════════════════════════
key("F7")
time.sleep(1.0)
shot("C1_wretch_approach")
time.sleep(5.0)
shot("C2_wretch_melee")
melee = log_lines("mob event: melee")
print(f"[C] melee events: {len(melee)}", flush=True)

# ══ D. the exploder ═════════════════════════════════════════════════════
key("F8")
time.sleep(0.8)
shot("D1_fuse")
time.sleep(2.5)
shot("D2_after_blast")
print(f"[D] blasts: {log_lines('blast at')}", flush=True)

print("=== SUMMARY ===", flush=True)
print(f"natural spawns: {len(log_lines('spawned at'))}", flush=True)
print(f"summons: {len(log_lines('EVIDENCE summon'))}", flush=True)
print(f"feeds: {log_lines('fed mob')}", flush=True)
print(f"births: {log_lines('born at')}", flush=True)
print(f"melee: {len(log_lines('mob event: melee'))}", flush=True)
print(f"blasts: {log_lines('blast at')}", flush=True)
print(f"explosion events: {log_lines('mob event: explosion')}", flush=True)
print(f"player damage applied: {log_lines('health now')}", flush=True)
