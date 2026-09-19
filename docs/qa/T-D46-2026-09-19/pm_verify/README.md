== PM 验收补充证据（第五任 PM，2026-09-19）==

## 1. 白名单核对（基线 = 分支起点 e322a77）
```
docs/qa/T-D46-2026-09-19/01_world_loaded.png
docs/qa/T-D46-2026-09-19/02a_wretch_closing_in.png
docs/qa/T-D46-2026-09-19/02b_bare_after_one_hit.png
docs/qa/T-D46-2026-09-19/03_armoured_2.3_per_hit.png
docs/qa/T-D46-2026-09-19/05_window_absorbed.png
docs/qa/T-D46-2026-09-19/README.md
docs/qa/T-D46-2026-09-19/combat_session.log
docs/qa/T-D46-2026-09-19/combat_session_run1.log
docs/qa/T-D46-2026-09-19/combat_session_run2.log
docs/qa/T-D46-2026-09-19/tools/combat_scene.py
docs/qa/T-D46-2026-09-19/tools/run_combat_scene.sh
docs/qa/T-D46-2026-09-19/tools/td46_evidence_hook.patch
docs/qa/T-D46-2026-09-19/tools/td46input.m
docs/tasks/T-D46.report.md
engine/physics/include/opencraft/physics/player_state.hpp
game/client/src/player_life.hpp
game/client/src/tick.cpp
game/common/include/opencraft/game/item_registry.hpp
game/common/src/item_registry.cpp
game/server/sim/include/opencraft/sim/mob_sim.hpp
game/server/sim/src/world_sim.cpp
tests/test_item_registry.cpp
tests/test_mob_ai.cpp
tests/test_mob_authority.cpp
tests/test_player_death.cpp
```
25 个文件：源码 7 + 测试 4 + 证据目录 13 + 报告 1，全部在卡面 §6 白名单内；
STATE.md / docs/tasks/T-D46.md / docs/0* / docs/research 零改动（diff 为空）；protocol.hpp 未碰。

## 2. 用例名集合（⚖ 零缺失判据）
```
baseline 459（git 638417e 主仓构建）vs 本卡 477（干净检出 acfc500）
comm -23 (基线有本卡无) = 空  ⇒ 存量零缺失
comm -13 (本卡新增)     = 18 条，见 test_names_477.txt
```

## 3. 契约⑦：damage_mob 判定块零改动
```
@@ -546,6 +546,35 @@ inline void set_move_target(Entity &e, const glm::dvec3 &destination) {
+inline void knock_back(Entity &entity, const glm::dvec3 &from, const double speed) {
@@ -1002,7 +1031,15 @@ inline void apply_goal(MobContext &ctx, const game::GoalKind kind, Steering &ste
-            event.position = ctx.actor != nullptr ? ctx.actor->feet : self.position;
+            event.position = self.position;
```
diff 只有两处：新增 knock_back()（damage_mob 之后）与 MeleeHit 事件 position 语义；
525-531 判定块无任何 hunk 触及。

## 4. 权威算例复核（PM 重算）
```
armor 7 vs dmg 3.0: reduction 22.00%  landed 2.340  (日志实测 2.3)
armor 7 vs dmg 49 : reduction 5.60%  landed 46.256  (报告 46.3)
```
