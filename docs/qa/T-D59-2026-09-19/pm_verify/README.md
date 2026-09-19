== PM 验收补充证据（第五任 PM，2026-09-19）==

## 1. 白名单核对（基线 = 分支起点 66b3152）
```
docs/qa/T-D59-2026-09-19/01_world_loaded.png
docs/qa/T-D59-2026-09-19/02_bar_full_never_attacked.png
docs/qa/T-D59-2026-09-19/03_bar_cold_just_after_a_swing.png
docs/qa/T-D59-2026-09-19/04_bar_charged_past_the_gate.png
docs/qa/T-D59-2026-09-19/05_after_fast_clicking.png
docs/qa/T-D59-2026-09-19/06_after_paced_clicking.png
docs/qa/T-D59-2026-09-19/07_crit_jump_attack.png
docs/qa/T-D59-2026-09-19/08_after_sprint_hit.png
docs/qa/T-D59-2026-09-19/attack_session.log
docs/qa/T-D59-2026-09-19/README.md
docs/qa/T-D59-2026-09-19/tools/attack_scene.py
docs/qa/T-D59-2026-09-19/tools/run_attack_scene.sh
docs/qa/T-D59-2026-09-19/tools/td59_evidence_hook.patch
docs/qa/T-D59-2026-09-19/tools/td59input.m
docs/tasks/T-D59.report.md
game/client/src/hud.cpp
game/client/src/hud.hpp
game/client/src/interaction.hpp
game/client/src/main.cpp
game/client/src/player_life.hpp
game/client/src/tick.cpp
game/common/include/opencraft/game/item_registry.hpp
game/common/include/opencraft/game/protocol.hpp
game/common/src/item_registry.cpp
game/server/sim/include/opencraft/sim/world_sim.hpp
game/server/sim/src/world_sim.cpp
tests/CMakeLists.txt
tests/test_attack_charge.cpp
```
28 文件 = 卡面 §6 逐项（含 tests/CMakeLists.txt 新测试注册）；
禁碰项（STATE/卡面/docs/01/docs/research/engine/physics/mob_type）diff 为空；mining 文件 0 行 diff。

## 2. 实机读数回代（PM 重算）
```
t=5: 公式 0.354880 vs 日志 0.35488  MATCH  → 4×charge=1.41952
t=6: 公式 0.416320 vs 日志 0.41632  MATCH  → 4×charge=1.66528
t=8: 公式 0.569920 vs 日志 0.56992  MATCH  → 4×charge=2.27968
t=11: 公式 0.877120 vs 日志 0.87712  MATCH  → 4×charge=3.50848
```
充能条：0.848×232 = 196.74px，bar_x0=524 ⇒ 刻度 x=720.7（报 720..721）✓；
填充 97px/232 = 0.4181 vs charge 0.416320（差 0.0018 ≈ 1px 量化）✓。

## 3. 裁决 S-4 的 wiring 改动（PM 亲手，合并结果上验证）
```
+++ b/game/client/src/inventory_wiring.hpp
+    // T-D59 裁决 S-4: the sword and the shovel join the kit so the charge ramp,
+    // the crit and the sprint knockback are reachable in the shipped client (the
+    // pick and the axe alone cover damage but leave the gate's flagship weapon
+    // unwieldable). TEMPORARY by design: once the crafting card lands, its
+    // ruling decides whether these two leave the kit.
+    {18, "timber_edge", 1},    {19, "timber_spade", 1},
```
合并结果：cmake 离线 reconfigure → build rc=0 → ctest 492/492（含启动包两行的 wiring 断言）。
