# T-D60 实机证据 · 合成与工具 tiers（物品移动 UI / 2×2+3×3 / 工作台 / 木石档 / 采集等级）

工作根：`/Users/happy/Desktop/opencraft_worktree/opencraft-td60`
分支：`task/T-D60-crafting-tiers`
装置：`/Users/happy/Desktop/opencraft_scratch/ti2input`（HID 层注入，docs/05 §3.1；本卡未新增打点，
`strings build/opencraft | grep -c EVIDENCE` = **0**）

---

## 1. 这一轮在测什么

| 卡面 §5.4 条目 | 判据（机器可读优先） | 落在哪 |
|---|---|---|
| 5. 启动日志 | `atlas: 60/66`、`crack tiles at 66`、`mobs: 3/3`、`crafting: 11 recipe(s) loaded`、**WARN = 0** | `session3.log:11-17` |
| 1a. E 开背包（2×2） | `crafting: opened the pocket screen (2x2)` + 面板截图（含 18/19 空格） | `session3.log:37`、`02_pocket_open.png` |
| 1b. 2×2 造工作台 | 网格里 4 木板 → 产物格显示 assembly_bench → `crafted 1 x1 in the 2x2 grid` | `04_grid_bench_ready.png`、`session3.log:41` |
| 1c. 光标堆 | 产物进光标堆（截图里指针处 1 个工作台） | `05_bench_on_cursor.png` |
| 1d. 放置 + 右键开 3×3 | `placed assembly_bench at (7, 143, -164); consumed 1 from slot 2` → `assembly bench: opened a 3x3 surface at (7, 143, -164)` | `session2.log:56,59`、`07_bench_screen.png` |
| 3. 不暂停世界 | 界面开着时 `fps`/`autosave` 行照走、坐标仍在变（生物/物理在跑） | `session3.log`（16:09:44→16:09:56 区间） |
| 4. 输入门控 | **同一次右键**：面板开着 → 只有 `screen closed (E)` 前后无 `opened`；面板关着 → `assembly bench: opened a 3x3 surface`（差分对照） | `session3.log:56 / 64`、`08/09_*.png` |
| 2. canHarvest（徒手无掉落 vs 木镐有掉落） | ⚠ **未取**，见 §3 | — |

## 2. 装置与信任边界

- 注入走 `ti2input`（`CGEventPost(kCGHIDEventTap)`）：键码 `14`=E、`20`='3'；鼠标用
  `clicktap <button> <ms> <wx> <wy>` 的**显式窗口坐标**（docs/05 §3.1 第 3/10 条）。
- 每次注入的**时刻**逐条记在 `injection_timeline.txt`，日志行与注入行的对应关系因此可核对
  （例：`crafted` 出现在产物格点击后 0.3 s；`placed assembly_bench` 在放置点击后 0.4 s）。
- 复跑：`craft_chain_scene.sh`（需要先按 §4 起实例并拿到 PID）。
- 截图一律 `screencapture -x -o -l<windowID>`，且注入前先 `activate`（第 10 条：
  非前台窗口会返回旧表面）。

## 3. 未取到的证据（**如实登记**）

1. **§5.4 条目 2（canHarvest 的可感证据）未在实机取**：需要在石面上做"徒手按住 7.5 s 不破 /
   木镐 1.125 s 破并掉落可拾取"的对照，本机这一轮的注入窗口（且机器上同时有真人输入，见 §4）
   不足以稳定复现。该语义由单测逐行钉死（`test_mining_tiers.cpp`：§4.1 五行 + 150/23/12/300
   整数 tick 数 + `can_harvest_with` 的九行门控表）。**是否需要补充实机复验，请 PM 裁决。**
2. **§5.4 条目 1 的"石档"半条未在实机取**（木镐挖石 → 捡圆石 → 石镐）：同上，需要一段
   脚本化的挖掘+拾取，本轮未做；石档四件的配方与采集数值同样由单测覆盖
   （`test_recipe_registry.cpp` 的两档同形 + `test_mining_tiers.cpp` 的两档数值）。
3. 3×3 里**没**造出木剑（只验到"工作台屏幕打开 + 3×3 网格可见"）：造剑需要在 3×3 里
   摆出 2 板 + 1 棍再点产物，属于同一套已证机制的重复。

## 4. ★ 本机同时存在**真人输入**（对证据的影响，必须读）

三个会话期间，机器上同时有**真实输入在操作同一个实例**，可证：
`session.log` 里出现了我没有注入的事件——16:03:51 `vessel: poured water source`（我没有点右键，
也没有切到水桶格）、16:04:40 `place refused ... cell overlaps the actor`、玩家坐标在无注入期间
持续变化（16:04:32→16:05:13 从 (24,125,-13) 走到 (-1,134,-21)）、以及快捷栏选中项自己变了
（`98_concurrent_real_input.png`：画面里手持 "ASSEMBLY BENCH"）。这与 T-D59 记录的
"真键盘会进游戏污染会话"是同一类现象。

因此：

- 上面的每条结论都靠 `injection_timeline.txt` 的**时间对齐**归因，不靠"我点了所以就是我的"；
  表里列的关键行与注入时刻的间隔都在 0.3–0.5 s 内。
- **反过来说**：那些我没有编排、却真实发生了的合成/放置（session.log 16:04:29
  `crafted 1 x1 in the 2x2 grid`）本身就是"UI 可用"的重复观测——但**不作为**本卡的验收判据，
  只列在这里供 PM 参考。
- 收尾时已按惯例 `pkill -f '^\./opencraft$'` 清掉本卡起的实例。

## 5. 文件清单

| 文件 | 是什么 |
|---|---|
| `session3.log` | **主证据**：出货二进制（无打点）的完整启动 + 注入会话日志 |
| `session.log` / `session2.log` | 前两轮（含一次脚本自身的错：产物格那次光标上还有 4 板，被正确地拒了） |
| `injection_timeline.txt` | 每次注入的时刻与参数 |
| `craft_chain_scene.sh` | 装置源码（可复跑） |
| `01_before_e.png` … `09_gate_closed_click.png` | 面板/光标堆/手持/3×3/门控差分截图 |
| `98_concurrent_real_input.png` | 上一条现象的现场（非本卡判据） |
| `baseline_test_names.txt` / `td60_test_names.txt` | §5.1 用例名集合对照的原始输入 |
