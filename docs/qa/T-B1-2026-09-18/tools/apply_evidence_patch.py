#!/usr/bin/env python3
"""T-B1 evidence: inserts the "pinned mob" patch into game/server/sim/src/world_sim.cpp.

NOT PART OF THE DELIVERABLE. Applied to BOTH trees (the pre-change baseline and
the branch under test) for the on-machine screenshot pair, then reverted with
`git checkout` before the commit - the delivered tree must show
`git diff --stat game/server` empty.

Why a patch exists at all: the pixel claim ("no model file -> the picture is
unchanged") needs the SAME scene in both runs, and the game offers no way to put
a mob at a known spot:
  * entities are not persisted (level_file.hpp has no entity table),
  * natural passive spawning draws its spot from an RNG whose stream includes
    the TICK (mob_spawn.hpp: `chunk_seed(seed, cx, cz) ^ tick`), so two runs
    differ,
  * and a walking mob would make even an A/A pair inside ONE run differ, which
    would leave the device unable to tell "the code changed the picture" from
    "time passed".

What it does: skips the natural spawner and pins one mossback 2 blocks north of
the player and 1.6 above the player's feet - the spawn column faces a hill, so a
mob at foot height would be buried in it - facing south (yaw = pi), velocity 0.
`spawn_pass()` runs AFTER `step_mob_pass()` in `WorldSim::tick()`, so this is
the last writer of that mob's state each tick - the mob pass therefore draws
exactly one mob, always in the same place, at any tick.

The camera needs no help: with build/saves/ deleted, both trees generate the
same seed and the same spawn column, and view_yaw = view_pitch = 0 (main.cpp
reads them from the save; there is none).

usage: apply_evidence_patch.py <repo_dir>            # idempotent
       apply_evidence_patch.py --revert <repo_dir>   # git checkout the file
"""
import subprocess
import sys
from pathlib import Path

MARKER = "T-B1 EVIDENCE ONLY"
ANCHOR = """    if (candidates.empty()) {
        return;
    }
"""

BLOCK = """    // \u2500\u2500 T-B1 EVIDENCE ONLY (not part of the deliverable) \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
    {
        static EntityId evidence_mob = EntityStore::kNoEntity;
        const glm::dvec3 spot = player + glm::dvec3(0.0, 1.6, -3.5);
        if (evidence_mob == EntityStore::kNoEntity) {
            evidence_mob =
                spawn_mob(entities_, mobs_, mobs_.entity_type_of("mossback"), spot, mob_seed_, mob_rules_);
        }
        if (Entity *mob = entities_.find(evidence_mob); mob != nullptr) {
            mob->position = spot;
            mob->velocity = glm::dvec3(0.0);
            mob->on_ground = true;
            mob->ai.yaw = 3.14159265358979323846; // facing +Z, toward the camera
            mob->ai.pitch = 0.0;
            mob->ai.hurt_cooldown = 0;
            mob->ai.fuse = 0;
            mob->ai.baby = false;
        }
    }
    return; // natural spawning off: the scene has to be identical twice
"""

TARGET = Path("game/server/sim/src/world_sim.cpp")


def main(argv):
    if len(argv) >= 2 and argv[0] == "--revert":
        repo = Path(argv[1]).resolve()
        subprocess.run(["git", "-C", str(repo), "checkout", "--", str(TARGET)], check=True)
        print(f"reverted {repo / TARGET}")
        return 0
    if len(argv) != 1:
        print(__doc__)
        return 2
    repo = Path(argv[0]).resolve()
    path = repo / TARGET
    text = path.read_text()
    if MARKER in text:
        print("already patched")
    else:
        if ANCHOR not in text:
            print("ABORT: insertion point not found (wrong tree?)")
            return 3
        path.write_text(text.replace(ANCHOR, ANCHOR + BLOCK, 1))
        print(f"patched {path}")
    diff = subprocess.run(["git", "-C", str(repo), "diff", "--", str(TARGET)], capture_output=True, text=True)
    print(diff.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
