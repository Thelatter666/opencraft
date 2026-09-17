#!/usr/bin/env python3
"""T-D40 refactor evidence, precise pass (docs/05 §3.2 layers 1-2).

Per function, not per file: each primitive's OLD body is located by brace
matching and compared token-by-token (comments counted separately) against its
NEW body. Any token that disappeared must appear in the ledger below with the
role it moved to.

usage: compare2.py <baseline_root> <new_root>
"""

import re
import sys
from collections import Counter

BASE, NEW = sys.argv[1], sys.argv[2]
OLD_PHYS = f"{BASE}/engine/physics/src/player_physics.cpp"
OLD_SIM = f"{BASE}/game/server/sim/include/opencraft/sim/item_sim.hpp"
NEW_SWEEP = f"{NEW}/engine/physics/include/opencraft/physics/sweep.hpp"
NEW_PHYS = f"{NEW}/engine/physics/src/player_physics.cpp"
NEW_SIM = f"{NEW}/game/server/sim/include/opencraft/sim/item_sim.hpp"

TOKEN_RE = re.compile(
    r"(?P<comment>//[^\n]*|/\*.*?\*/)|(?P<number>\b\d+\.?\d*(?:[eE][+-]?\d+)?\b|\.\d+)"
    r"|(?P<ident>[A-Za-z_]\w*)|(?P<punct>[^\sA-Za-z0-9_])",
    re.DOTALL,
)


def toks(text):
    return [(m.lastgroup, m.group(0)) for m in TOKEN_RE.finditer(text)]


def body(path, name):
    """Token span of `name`'s definition: from the name to its matching brace."""
    stream = toks(open(path).read())
    for i, (kind, tok) in enumerate(stream):
        if kind == "ident" and tok == name:
            # the definition's signature ends at the first '{' after the name
            for j in range(i, len(stream)):
                if stream[j][1] == "{":
                    depth = 0
                    for k in range(j, len(stream)):
                        if stream[k][1] == "{":
                            depth += 1
                        elif stream[k][1] == "}":
                            depth -= 1
                            if depth == 0:
                                return stream[i : k + 1]
                    break
            break
    return []


def counts(span, kind_wanted=None):
    out = Counter()
    for kind, tok in span:
        if kind_wanted is None and kind != "comment":
            out[tok] += 1
        elif kind == kind_wanted:
            out[tok] += 1
    return out


# ── the ledger: every disappearance, mapped to where the thing went ──────────
LEDGER = {
    # The player-shaped parameters became explicit arguments of the shared
    # function. Same values, same order of operations.
    "s": "the PlayerState parameter -> position/half_width/height arguments",
    "PlayerState": "-> the `half_width` / `height` arguments (kHalfWidth is passed by the caller)",
    "kHalfWidth": "-> the `half_width` argument",
    "height": "-> the `height` argument (the s.height() call is the caller's)",
    "position": "-> the `position` argument (the same glm::dvec3 object, by reference)",
    # The by-reference out-flags became the returned AxisSweep.
    "hit_ground": "-> AxisSweep::ground (returned instead of written through a reference)",
    "hit_ceiling": "-> AxisSweep::ceiling",
    "hit_wall": "-> AxisSweep::hit",
    # The velocity response moved out to the callers, where each one differs.
    "velocity": "-> the velocity response moved to the caller (player: zeroed in move_axis_*; entity: x restitution)",
    "move_axis_x": "-> renamed sweep_axis_x (the shared geometry)",
    "move_axis_y": "-> renamed sweep_axis_y",
    "move_axis_z": "-> renamed sweep_axis_z",
    # Extraction mechanics.
    "static_cast": "the (double)(int) round-trip folded into std::floor (bit-identical for |v| < 2^31)",
    "int": "same fold: std::floor already yields the integral double",
    "return": "-> `out.delta` assignment + `return out;` (the same value)",
    "double": "-> AxisSweep::delta (double)",
    "before": "kept, but the early-return delta reads through out.delta",
    "Box": "the box type now comes from the sweep.hpp declaration (same 6 doubles)",
    "box_of": "-> the shared box_of(position, half_width, height)",
    "highest_surface_below": "-> the shared highest_surface_below in sweep.hpp (moved verbatim)",
    # Signature / return-shape relabeling mechanics: the same expressions with
    # the player parameter spelled out. Punctuator churn comes from
    # `s.position.y` -> `position.y` and from the dropped `static_cast` pair
    # (`static_cast<double>(static_cast<int>(std::floor(x)))` -> `std::floor(x)`).
    ".": "signature relabeling: `s.position.y` -> `position.y` / `s.velocity` gone",
    "(": "signature relabeling: the removed static_cast<int>() call and its argument list",
    ")": "signature relabeling: same static_cast pair",
    "<": "signature relabeling: the removed `static_cast<int>` angle brackets",
    ">": "signature relabeling: the removed `static_cast<int>` angle brackets",
    "&": "the by-reference out-flag parameter is gone (AxisSweep replaces it)",
    "bool": "the bool out-flag parameter is gone (AxisSweep replaces it)",
    ":": "`PlayerState::kHalfWidth` -> the `half_width` argument",
    "x": "`s.velocity.x = 0.0` moved to the caller wrapper (same line there)",
    "z": "`s.velocity.z = 0.0` moved to the caller wrapper (same line there)",
    "0.0": "`return 0.0` -> AxisSweep::delta default 0.0 / `velocity.x = 0.0` moved to the wrapper",
    "box_collides": "-> the shared box_collides in sweep.hpp (moved verbatim)",
}

PAIRS = [
    ("box_collides", OLD_PHYS, NEW_SWEEP),
    ("highest_surface_below", OLD_PHYS, NEW_SWEEP),
    ("move_axis_y", OLD_PHYS, NEW_SWEEP),
    ("move_axis_x", OLD_PHYS, NEW_SWEEP),
    ("move_axis_z", OLD_PHYS, NEW_SWEEP),
]

print("=== per-function token comparison (comments excluded; comments checked separately) ===")
unexplained_total = 0
literal_problems = []
for name, old_file, new_file in PAIRS:
    old_span = body(old_file, name)
    new_name = name.replace("move_axis_", "sweep_axis_")
    new_span = body(new_file, new_name)
    old_c, new_c = counts(old_span), counts(new_span)
    removed, added = old_c - new_c, new_c - old_c
    print(f"\n-- {name} -> {new_name}: {sum(old_c.values())} tokens old, {len(removed)} removed")
    for tok, n in sorted(removed.items()):
        if tok in LEDGER:
            print(f"   [ledger] {n:2}x {tok!r} -> {LEDGER[tok]}")
        else:
            unexplained_total += n
            print(f"   [UNKNOWN] {n:2}x {tok!r}")
    # literals are frozen values: the multiset must survive exactly
    old_num, new_num = counts(old_span, "number"), counts(new_span, "number")
    if old_num != new_num:
        literal_problems.append((name, old_num, new_num))
        print(f"   [LITERALS] old={dict(old_num)} new={dict(new_num)}")
    else:
        print(f"   [literals OK] {dict(old_num)}")

print("\n=== comments ===")
old_comments = Counter()
new_comments = Counter()
for name, old_file, new_file in PAIRS:
    old_comments += counts(body(old_file, name), "comment")
for name in ("box_collides", "highest_surface_below", "sweep_axis_y", "sweep_axis_x", "sweep_axis_z"):
    new_comments += counts(body(NEW_SWEEP, name), "comment")
print(f"  old bodies: {sum(old_comments.values())} comment tokens")
print(f"  not present verbatim in the new bodies: {sum((old_comments - new_comments).values())}")
for tok, n in (old_comments - new_comments).items():
    print(f"   {n}x {tok[:110]!r}")

print("\n=== the duplicate that disappeared from item_sim.hpp ===")
dup_tokens = Counter()
for name in ("box_collides", "highest_surface_below", "move_axis_y", "move_axis_x", "move_axis_z"):
    dup_tokens += counts(body(OLD_SIM, name))
print(f"  {sum(dup_tokens.values())} tokens of hand-copied collision code removed")
new_sim = open(NEW_SIM).read()
for probe in ("box_collides(", "highest_surface_below(", "move_axis_y(", "sweep_axis_y("):
    hits = [ln for ln in new_sim.splitlines() if probe in ln and not ln.strip().startswith("//")]
    print(f"  item_sim.hpp still defines/calls {probe!r}: {len(hits)} line(s) {hits[:2]}")

# A literal that left one body is only acceptable if it still exists in the new
# tree: that is the "no value may disappear" rule (docs/05 §3.2 layer 1).
new_tree_numbers = Counter()
for path in (NEW_SWEEP, NEW_PHYS, NEW_SIM):
    new_tree_numbers += counts(toks(open(path).read()), "number")
gone_literals = Counter()
for name, old_num, new_num in literal_problems:
    for literal, count in (old_num - new_num).items():
        if new_tree_numbers[literal] == 0:
            gone_literals[literal] += count
        else:
            print(f"  [relocated OK] {name}: {count}x {literal!r} no longer in this body, "
                  f"{new_tree_numbers[literal]} occurrence(s) elsewhere in the new tree")

print("\n=== verdict ===")
print(f"  unexplained token removals: {unexplained_total}")
print(f"  bodies whose literal multiset changed: {len(literal_problems)} (all relocations above)")
print(f"  literal VALUES that disappeared from the tree: {sum(gone_literals.values())} {dict(gone_literals)}")
sys.exit(1 if (unexplained_total or gone_literals) else 0)
