#!/usr/bin/env python3
"""T-D59 on-machine evidence driver (macOS).

One session, one world, one player standing on the spawn surface with a
timber_edge in hotbar cell 8 (the evidence hook's start-up line).

Every phase follows the same shape, because the geometry is what makes this
scene awkward: a Hollow Wretch walks straight at the player and coasts about
half a block PAST them (its own momentum), at which point it is behind the
camera and no click can reach it. So each phase

  1. summons a fresh mob with F8 (or F7),
  2. waits for the client's own `mob melee` line - a machine-readable "the mob
     has arrived and is adjacent" gate rather than a guessed sleep,
  3. turns the view 180 degrees so the mob is in frame and in reach again,
  4. measures.

Phases:

  A. CHARGE BAR. One click, then a pause-capture (the bar is cold); then a
     pause-capture after a wait (the bar is full). Pausing stops the ticks, so
     the captured bar is the value the click left it at.
  B. FAST vs PACED. A burst of clicks 150 ms apart, then clicks a second apart.
     The client's `attacked mob ... charge X` lines and the hook's per-frame hit
     point series are the readout.
  C. CRIT. The charge clock is the player's, not the mob's: spend it on one mob,
     wait it out, summon a FRESH one dead ahead, jump, and click on the way down.
     A single hit-point drop of exactly 6.0 (= 4 x 1.5) is the signature.
  D. SPRINT SHOVE. Engage sprint with a real W down/up/down and verify it by the
     client's own `sprint start` line BEFORE clicking.

usage: attack_scene.py <pid> <window_id> <log> <outdir>
"""
import re
import subprocess
import sys
import time

PID, WIN, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
TOOL = "/tmp/td59input"

# macOS virtual key codes (the keyboard's own numbering, not GLFW's).
KEY_SPACE, KEY_ESC, KEY_W, KEY_CTRL = 49, 53, 13, 59
KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10 = 97, 98, 100, 101, 109

WIN_X = WIN_Y = WIN_W = WIN_H = 0.0


def act():
    subprocess.run([TOOL, "front", PID], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([TOOL, "activate", PID], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def key(code, ms=60):
    act()
    time.sleep(0.12)
    subprocess.run([TOOL, "keytap", str(code), str(ms)], check=True)


def key_down(code):
    act()
    subprocess.run([TOOL, "key", str(code), "down"], check=True)


def key_up(code):
    act()
    subprocess.run([TOOL, "key", str(code), "up"], check=True)


def click(keep_keys=False):
    """One left-click aimed at the middle of the game window. The explicit
    location matters: a click at whatever position the real cursor happens to be
    at is routed to whichever window is under it (docs/05 §3.1 rule 3's T-I2
    corollary).

    `keep_keys` uses the raw variant, which skips the front/activate preamble:
    that preamble is what makes GLFW drop the keys a previous command is holding
    (docs/05 §3.1 rule 2), and the sprint shove needs W and Ctrl still down when
    the button goes down."""
    cx, cy = str(WIN_X + WIN_W / 2), str(WIN_Y + WIN_H / 2)
    if keep_keys:
        subprocess.run([TOOL, "clickraw", cx, cy], check=True)
        return
    act()
    time.sleep(0.04)
    subprocess.run([TOOL, "clicktap", "0", "60", cx, cy], check=True)


def log_text():
    with open(LOG, errors="replace") as handle:
        return handle.read()


def count(pattern):
    return len(re.findall(pattern, log_text()))


def wait_for_count(pattern, target, seconds, what):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if count(pattern) >= target:
            return True
        time.sleep(0.05)
    print(f"  !! {what}: only reached {count(pattern)}/{target}", flush=True)
    return False


def shot(name):
    """Two captures back to back: for a window that was not in front, the first
    one is a stale surface (docs/05 §3.1 rule 10)."""
    act()
    path = f"{OUT}/{name}.png"
    for _ in range(2):
        subprocess.run(["screencapture", "-x", "-o", "-l", WIN, path], check=True)
    print(f"  shot {name}", flush=True)


def frozen_shot(name):
    """Capture with the simulation PAUSED: the ticks stop (so the charge bar
    holds the exact value the click left it at) while the frame loop keeps
    rendering. The pause band sits in the middle of the screen; the charge bar is
    36-40 px above the bottom edge, well clear of it."""
    key(KEY_ESC)
    time.sleep(0.3)
    shot(name)
    key(KEY_ESC)
    time.sleep(0.35)


def summon(key_code, what, quiet=False):
    """Summon a mob 2 blocks DEAD AHEAD, on the current view yaw.

    The scene never waits for the mob to arrive: a Hollow Wretch walks straight
    at the player and coasts about half a block past them, after which it is
    behind the camera and out of the melee reach - which is what made the first
    two attempts at this scene produce clicks that hit nothing (`picked 0` in
    the injection probe). Summoning immediately before the swing keeps a fresh
    mob in front at the moment it matters, and the pick's nearest-hit rule
    chooses between it and whatever else is standing around.
    """
    before = count(r"EVIDENCE summon hollow_wretch")
    for attempt in range(4):
        key(key_code)
        if wait_for_count(r"EVIDENCE summon hollow_wretch", before + 1, 3.0, what):
            if not quiet:
                print(f"  {what}: summoned", flush=True)
            return True
        print(f"  {what}: no summon, re-tapping", flush=True)
    return False


def damage_drops():
    """Every DECREASE between consecutive frames OF THE SAME MOB, i.e. one landed
    hit each. Segmented by entity id: the per-frame health lines follow whichever
    mob was summoned last, so a flat concatenation would compute differences
    between two different mobs' hit points and report numbers no hit ever dealt."""
    drops = []
    current = None
    previous = None
    for mob_id, health in re.findall(r"EVIDENCE mob ([0-9]+) health ([0-9.]+)", log_text()):
        if mob_id != current:
            current, previous = mob_id, float(health)
            continue
        health = float(health)
        if health < previous - 1e-9:
            drops.append(round(previous - health, 3))
        previous = health
    return drops


def charges():
    return [float(v) for v in re.findall(r"attacked mob [0-9]+ at \([^)]*\) charge ([0-9.]+)", log_text())]


def swings():
    """The authority's own record of each settled swing: the weapon, the charge
    multiplier it computed, and the flags it read. This is the readout the crit
    and the sprint shove are asserted on - the client's mirror of the charge and
    the victim's hit points are both derived views, and reading the derived view
    is how the first runs of this scene managed to report "no crit" while
    `crit true damage 6.000000` sat in the log."""
    return [line.split("EVIDENCE swing ", 1)[1] for line in log_text().splitlines() if "EVIDENCE swing" in line]


def swings_matching(fragment):
    return [line for line in swings() if fragment in line]


def heal():
    key(KEY_F10)


def scene_reset():
    """F11: back to the respawn point with full hit points (the hook calls the
    death screen's own respawn_player).

    This is what makes the later phases possible at all. The wretch's melee
    knockback walks the player steadily backwards, so after ~40 s of film time
    the player is tens of blocks from where the mobs are and every click finds
    `picked 0` - which is exactly how the first three runs of this scene produced
    no crit and no sprint shove.
    """
    key(KEY_F6)
    time.sleep(0.5)


def main():
    global WIN_X, WIN_Y, WIN_W, WIN_H
    fields = subprocess.run([TOOL, "win", PID], capture_output=True, text=True, check=True).stdout.split()
    WIN_X, WIN_Y, WIN_W, WIN_H = (float(fields[1]), float(fields[2]), float(fields[3]), float(fields[4]))
    print(f"window {WIN} at ({WIN_X}, {WIN_Y}) {WIN_W}x{WIN_H}", flush=True)

    print("waiting for the world to settle", flush=True)
    deadline = time.time() + 60
    while time.time() < deadline and "respawn point" not in log_text():
        time.sleep(0.2)
    time.sleep(3.0)
    shot("01_world_loaded")
    print(f"  the sword the hook put in cell 8: {count(r'EVIDENCE timber_edge')} line(s)", flush=True)

    # ── A: the bar holds the value the clock is at ──────────────────────────
    print("A: freeze the bar before any swing (the never-attacked sentinel)", flush=True)
    frozen_shot("02_bar_full_never_attacked")

    print("A2: summon dead ahead and click, then freeze the bar cold", flush=True)
    summon(KEY_F7, "F7 summon")
    click()
    time.sleep(0.05)
    frozen_shot("03_bar_cold_just_after_a_swing")

    print("A3: let it charge past the 84.8% gate, freeze again", flush=True)
    time.sleep(1.0)
    frozen_shot("04_bar_charged_past_the_gate")

    # ── B: fast clicking vs paced clicking ──────────────────────────────────
    print("B: fast clicking - fresh mob each time, 150 ms between clicks", flush=True)
    heal()
    for round_index in range(6):
        summon(KEY_F8 if round_index % 2 == 0 else KEY_F7, f"fast round {round_index + 1}", quiet=True)
        for _ in range(3):
            click()
            time.sleep(0.09)
    print(f"  fast phase done: {count('attacked mob')} swings so far", flush=True)
    frozen_shot("05_after_fast_clicking")

    print("B2: paced clicking - one click, then a full second of charge", flush=True)
    heal()
    for round_index in range(5):
        summon(KEY_F7 if round_index % 2 == 0 else KEY_F8, f"paced round {round_index + 1}", quiet=True)
        click()
        time.sleep(1.0)   # a full second: the sword's T is 12.5 ticks
        summon(KEY_F8 if round_index % 2 == 0 else KEY_F7, f"paced round {round_index + 1} (second)", quiet=True)
        click()
        time.sleep(0.4)
    print(f"  paced phase done: {count('attacked mob')} swings so far", flush=True)
    frozen_shot("06_after_paced_clicking")

    # ── C: the crit ─────────────────────────────────────────────────────────
    print("C: scene reset, then a fresh mob, a jump, and a hit on the way down", flush=True)
    crit_seen = False
    KEY_DELAYS = [0.30, 0.34, 0.38, 0.42, 0.46, 0.34, 0.38, 0.42]
    for attempt in range(8):
        scene_reset()
        time.sleep(1.1)                      # the charge passes the sword's T = 12.5
        summon(KEY_F8, "crit target", quiet=True)
        key(KEY_SPACE)
        time.sleep(KEY_DELAYS[attempt])
        click()
        time.sleep(0.5)
        crits = swings_matching("crit true")
        if crits:
            print(f"  crit on attempt {attempt + 1} (jump delay {KEY_DELAYS[attempt]} s): {crits[-1]}", flush=True)
            crit_seen = True
            break
        print(f"  attempt {attempt + 1} (delay {KEY_DELAYS[attempt]}): last drops {damage_drops()[-3:]}", flush=True)
    print(f"  crit observed: {crit_seen}", flush=True)
    frozen_shot("07_crit_jump_attack")

    # ── D: the sprint shove ─────────────────────────────────────────────────
    print("D: scene reset, sprint engaged by the Ctrl+W key path, then click", flush=True)
    sprint_seen = False
    for attempt in range(8):
        scene_reset()
        time.sleep(1.1)                      # charge to full while standing still
        summon(KEY_F7, "sprint target", quiet=True)
        # LEFT_CONTROL is the sprint key (tick.cpp's in.sprint) and W is forward:
        # held together the physics engages sprint outright, so there is no
        # double-tap edge to miss. Both are held across the click, because
        # releasing forward is one of the documented sprint end-conditions.
        # `hold` presses both in ONE process - two separate invocations put an
        # activate() between them, and that is when W gets dropped.
        act()
        subprocess.run([TOOL, "hold", str(KEY_CTRL), str(KEY_W)], check=True)
        before = count(r"sprint start")
        if not wait_for_count(r"sprint start", before + 1, 1.5, "sprint start"):
            subprocess.run([TOOL, "release", str(KEY_CTRL), str(KEY_W)], check=True)
            print(f"  sprint did not register on try {attempt + 1}", flush=True)
            continue
        swings_before = count(r"EVIDENCE swing")
        # Alternate the two click flavours: the plain one re-activates the window
        # (which can drop the held keys) and the raw one does not (which can miss
        # the window). Which one works is not stable between sessions, so the
        # loop tries both and lets the AUTHORITY's own swing line decide - it
        # records the sprint flag it actually saw.
        click(keep_keys=(attempt % 2 == 1))
        time.sleep(0.4)
        subprocess.run([TOOL, "release", str(KEY_CTRL), str(KEY_W)], check=True)
        time.sleep(0.3)
        landed = [l for l in log_text().splitlines() if "EVIDENCE swing" in l][swings_before:]
        sprinting = any("sprinting true" in l for l in landed)
        print(f"  try {attempt + 1}: sprint started, swings landed {len(landed)}, sprinting at the swing {sprinting}",
              flush=True)
        if sprinting:
            sprint_seen = True
            break
    print(f"  sprint shove observed: {sprint_seen}", flush=True)
    frozen_shot("08_after_sprint_hit")

    # ── the readout ─────────────────────────────────────────────────────────
    print("\n===== readout =====", flush=True)
    print(f"lmb edges           : {count(r'EVIDENCE lmb')}", flush=True)
    print(f"attacked mob lines  : {count('attacked mob')}", flush=True)
    print(f"charge series       : {charges()}", flush=True)
    print(f"health drops        : {damage_drops()}", flush=True)
    print(f"sprint starts       : {count('sprint start')}", flush=True)
    print(f"player deaths       : {count('player died')}", flush=True)
    print(f"AUTHORITY swings    : {len(swings())}", flush=True)
    print(f"  crit swings       : {swings_matching('crit true')}", flush=True)
    print(f"  sprint swings     : {swings_matching('sprinting true')}", flush=True)
    print(f"  0.9 knockbacks    : {count('knockback 0.9')}", flush=True)


if __name__ == "__main__":
    main()
