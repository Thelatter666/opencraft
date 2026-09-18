#!/usr/bin/env python3
"""T-B4 · 三向日志判据的机器核对（卡面 §2⑥ / §5.2）。

做四件事，逐条打印判定：
  1. 本卡日志里有 `mobs: 3/3`、且三条 `mob model …` 都在（第三条含 blastbud 的行文）；
  2. **Mossback / hollow_wretch 两行与 T-B5 验收件逐字节同**（剥掉时间戳前缀再比，
     和 T-B5 自己的两份日志（开发者 run1 + PM 侧 pm_run_*）三方互比）；
  3. 两条日志的 `[warning]` / `[error]` 行数 = 0；
  4. 移走 assets/mobs/ 的那一次：`mobs: 0/3`、无 `mob model` 行、WARN 仍为 0。

用法：python3 check_log_lines.py <with_assets.log> <no_mobs.log> <ref_a.log> [<ref_b.log> …]
"""
import re
import sys

# 行首的两段 [] 前缀：第一段是时间戳，第二段是等级（[info]/[warning]/…）。
# 两段都剥掉才剩正文 —— 只有这样"逐字节比"才比在内容上，而不是比在时钟上。
STAMP = re.compile(r"^\[[^\]]*\]\s*")


def read(path):
    with open(path, encoding="utf-8") as f:
        out = []
        for ln in f:
            bare = STAMP.sub("", ln.rstrip("\n"), count=1)
            bare = STAMP.sub("", bare, count=1)
            out.append(bare)
        return out


def find(lines, prefix):
    return [ln for ln in lines if ln.startswith(prefix)]


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    mine_path, no_mobs_path, ref_paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    mine = read(mine_path)
    no_mobs = read(no_mobs_path)
    refs = [(p, read(p)) for p in ref_paths]

    ok = True
    print(f"with-assets log : {mine_path}")
    print(f"no-mobs log     : {no_mobs_path}")
    for p, _ in refs:
        print(f"reference log   : {p}")
    print()

    # —— 1. 本卡日志 ——
    print("== 1. 本卡日志的满额与三行 ==")
    load = find(mine, "mobs: ")
    print(f"  load line   : {load[0] if load else '(missing)'}")
    want_load = "mobs: 3/3 mob models loaded from ../assets/mobs"
    hit = bool(load) and load[0] == want_load
    ok = ok and hit
    print(f"  -> exactly '{want_load}': {hit}")

    models = find(mine, "mob model ")
    print(f"  model lines : {len(models)}")
    for ln in models:
        print(f"    {ln}")
    seg = [ln for ln in models if ln.startswith("mob model blastbud:")]
    want_seg = ("mob model blastbud: 510 voxels, 1084 triangles, 6 joints, 1.7 blocks tall, "
                "palette from palettes/blastbud.png")
    seg_hit = seg == [want_seg]
    ok = ok and seg_hit
    print(f"  -> blastbud line == expected: {seg_hit}")
    print()

    # —— 2. 两行零回归（剥时间戳逐字节比）——
    print("== 2. Mossback / hollow_wretch 两行零回归 ==")
    for tag in ("mossback", "hollow_wretch"):
        prefix = f"mob model {tag}:"
        mine_lines = find(mine, prefix)
        print(f"  {tag}: mine = {mine_lines[0] if mine_lines else '(missing)'}")
        for p, ref in refs:
            ref_lines = find(ref, prefix)
            same = mine_lines == ref_lines and len(mine_lines) == 1
            ok = ok and same
            print(f"    vs {p}: {'IDENTICAL' if same else 'DIFFERS'}")
            if not same:
                print(f"      ref = {ref_lines}")
    print()

    # —— 3. WARN/ERROR = 0 ——
    print("== 3. [warning] / [error] 计数 ==")
    for name, lines in [(mine_path, mine), (no_mobs_path, no_mobs)]:
        warn = [ln for ln in lines if "[warning]" in ln or "[error]" in ln]
        print(f"  {name}: {len(warn)} warning/error line(s)")
        for ln in warn:
            print(f"    {ln}")
        ok = ok and not warn
    print()

    # —— 4. 移走 assets/mobs/ 的那一次 ——
    print("== 4. 移走 assets/mobs/ 的对照 ==")
    nm_load = find(no_mobs, "mobs: ")
    want_nm = "mobs: 0/3 mob models loaded from ../assets/mobs"
    nm_hit = bool(nm_load) and nm_load[0] == want_nm
    nm_models = find(no_mobs, "mob model ")
    ok = ok and nm_hit and not nm_models
    print(f"  load line   : {nm_load[0] if nm_load else '(missing)'}")
    print(f"  -> exactly '{want_nm}': {nm_hit}")
    print(f"  mob model lines: {len(nm_models)} (must be 0)")
    print()

    print(f"RESULT: {'ALL PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
