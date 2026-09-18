# T-M2 实机证据　2026-09-18

> 分支 `taskT-M2`　worktree `/Users/happy/Desktop/opencraft_worktree/T-M2-mob-ai`
> 环境：macOS 24.6.0（arm64）；窗口 `1280×748`（按 PID + 尺寸过滤反查）
> 单实例确认：每次运行前 `pgrep -x opencraft` 为空；每段取证结束后 kill 自己的实例（脚本内建）

## 0. 两个二进制

| | 取证二进制 | 交付二进制 |
|---|---|---|
| 用途 | 驱动三段行为（含临时召唤键） | 提交的代码 |
| `build/opencraft` md5 | `a80487738a85db05796b263f43f0446b` | 见 `T-M2.report.md §8` |
| 差异 | 仅 `main.cpp` 的 F6/F7/F8 临时钩子 + `tick.cpp`/`main.cpp` 的取证探针 | — |

临时钩子与探针在取证后**已全部还原**（判据：`strings build/opencraft | grep -c EVIDENCE` = **0**，
且源码里 `grep -c "TEMPORARY EVIDENCE"` = 0）。钩子原文存档在
`tools/tm2_evidence_hook.patch`（临时钩子）与 `tools/tm2_feed_probe.patch`（瞄准探针），供 PM 复现。
> 2026-09-18 PM 更正：本节原先写作 `evidence_hook.patch` / `feed_probe.patch`，与实际文件名不符
> （会把复现者引到一个不存在的路径）。**按实际文件名更正，内容与其他章节未动。**

## 1. ★ 观测装置（必须先读，决定这些证据能证明什么）

| 项 | 装置 | 能证明 / 不能证明 |
|---|---|---|
| 世界 | 真客户端 + 真 `server::WorldSim`（无存档，种子 `0x4F50454E43524146`） | 是生产路径 |
| 输入 | HID 层注入（`CGEventPost(kCGHIDEventTap)`，由 T-D40 的 `td40input.m` 复用而来，禁用 osascript） | 是真键盘事件 |
| 三方生物来源 | **临时召唤键 F6/F7/F8**（脚下 → 视线方向 2 格处 `WorldSim::summon_mob`） | ⚠ 证明的是"AI 行为 + 渲染"，**不是**"敌对生物的自然刷怪" |
| 自然刷怪 | 被动：实机自然生成（`mob mossback spawned at …`）；敌对：**只在洞穴**（光照 0） | 证明刷怪规则；⚠ 洞穴生物玩家不可达，故行为取证用了召唤键 |
| 判据 | **日志**（客户端 + 权威侧）为主，截图为辅 | 卡面第 10 条允许"截图**或**日志" |

⚠ **截图装置本身不可靠**（本机实测）：`screencapture -l <window>` 对**未在前台**的窗口会返回
上一次绘制的表面，导致多张"不同"的截图 md5 相同。已采取的缓解：每次截图前
`activate` + 连拍两张取第二张。**结果仍有重复帧**：本目录保留的 PNG 是**互相不同**的那些帧，
其中我逐张看过的是 `shots/02b_mossback_tempted_adjacent.png`（苔背兽贴脸：褐色身体 + 绿色冠）
与 `shots/03a_wretch_approaching.png`（空塚巡行者贴脸：灰色石身 + 砂砾头，HUD 红心 7）。

## 2. ★ 行为证据（日志，可直接转述）

来源：`scene_session.log`（一次连贯运行，含被动/繁殖/近战/自爆四段）。

### 2.1 刷怪（实机自然发生，未用召唤键）

```
mob mossback spawned at (-75.1, 137.0, -43.5) in chunk (-5, -3)      ← 被动：地表（光照 15 也刷）
mob mossback spawned at (-73.2, 137.0, -46.4) in chunk (-5, -3)      ← 包（同 tick 同区块，成员分散）
mob hollow_wretch spawned at (50.8, 99.0, 86.2) in chunk (3, 5)      ← 敌对：y=99 地下洞穴（光照 0）
```
本轮共 **22** 条自然生成行；敌对**全部**出现在 y≈40–99 的地下，地表一条也没有 —— 与
`docs/01 §6` 的"敌对生成光照等级 0"一致（本世界无昼夜，地表天光恒 15）。

### 2.2 被动：引诱（走过来）

喂食前先手持谷物面包（cell 7 = 槽 6），再召唤 → 生物主动走到 `actor_dist=1.21`：
```
EVIDENCE nearest_mob type=mossback pos=(0.50,132.00,-0.71) dist=2.02 goals=288 actor_dist=1.21 sees=true
```
`goals=288` = 位 5(引诱) + 位 8(注视) —— **两个目标同时运行**（卡面验收 1 的实机形态）。
另有一次反例（有价值）：先召唤、2.5 s 后才切到食物时，生物已游荡出 **6 格**开始半径 →
`goals=256`（只有注视）不再跟随 —— 正是 `research/11 §1.5.6` 的"开始跟随 6 格"。

### 2.3 被动：喂食 + 繁育（幼体）

```
fed mob 9 with item 24            ← 第一头（item 24 = grain_loaf）
fed mob 18 with item 24           ← 第二头（不同实体）
mob mossback born at (0.4, 133.0, -5.3)   ← 第二次喂食后 2.5 s（⚖ 交配时长）
```
两次喂食间隔 13.8 s、产出在第二次喂食后 **4.9 s**（含双方走近），与 `research/11 §6.2`
的"互相寻路靠近 → 约 2.5 秒 → 产仔"一致。**两头都是不同实体**（id 9 / id 18），
且脚本判据是"日志里出现两个不同 id 的 `fed mob`"，避免"同一头喂两次"的假阳性。

### 2.4 敌对近战：追赶 + 命中 + 客户端掉血

```
mob event: melee 3.0 damage at (0.5, 133.0, -5.2)        ← 权威侧事件（普通难度 3.0）
mob melee for 3.0 at (0.50, 133.00, -5.18); health now 17.0
…（每秒一次，共 9 次）
health now 2.0 → health now 0.0
```
命中间隔 **1.0 s**（= 20 tick，`MobDef::attack_cooldown`），掉血由**客户端**施加
（权威侧不持有玩家血量）——事件通道端到端可见。

### 2.5 敌对自爆：引信 → 爆炸 → 破坏掉落物 + 玩家伤害

```
blast at (0.5, 133.0, -7.2): 2 drop(s) destroyed
mob event: explosion 32.7 damage at (0.5, 133.0, -7.2)
explosion for 32.7 at (0.50, 133.00, -7.18); health now 0.0
```
`32.7 = 49 × (1 − 2.5/6)`（普通难度爆心 49 × 距离衰减）——`research/11 §1.5.5` 的
3 格触发 → 30 tick 引信 → 爆炸，且爆炸经 `destroy_items_in_radius`（T-E1 的既有入口）
销毁了附近掉落物。

### 2.6 另一轮（`shots_session.log`）的四段同类日志

同装置重跑一次，结论一致：自然生成 30 条、近战 6 次、爆炸 1 次（含 `blast … 3 drop(s)`）。

## 3. 截图

| 文件 | 内容 | 我是否逐张核看 |
|---|---|---|
| `00_pristine.png` | 干净启动的出生点 | 否 |
| `shots/02b_mossback_tempted_adjacent.png` | **苔背兽贴脸**（褐色身体 + 绿色冠），手持 GRAIN LOAF | ✅ |
| `shots/03a_wretch_approaching.png` | **空塚巡行者贴脸**（灰色石身 + 砂砾头），HUD 红心 7 | ✅ |
| `shots/02a/03b/04a/04b/05`、`A1..D2` | 同装置的后续帧（部分与其它帧相同，见 §1 的装置缺陷） | 否 |

判据说明：本卡的三段行为**以日志为准**（卡面第 10 条允许），截图的角色是"证明渲染层确实
在画生物"——`02b`/`03a` 两帧足够支撑这一点：两种生物的身体/头部配色与
`client::mob_skin` 表逐条对应（苔背兽 = dirt 身 + grass_block 头；巡行者 = cobblestone 身 +
gravel 头），且它们的**位置就是日志里 AI 走到的位置**（贴脸）。

## 4. 取证发现的三个真问题（都已修）

1. **`mob_skin` 用了 item id 当 block id** → 启动后第一次绘制即崩溃
   （`unknown block id: loam_clod`）。改用 `dirt/grass_block` 等**方块** id。**只有实机能发现**。
2. **刷怪 RNG 在 64 tick 窗口内重复** → 连续 4 tick 生成的 4 只敌对**站在同一格**
   （`tick/64` 折叠掉了 tick）。改为整 tick 入流。
3. **刷怪探针会穿透海底挖进洞穴** → 早期版本在洋面上把生物刷到地下几十格，
   把"光照 0"当成通过条件。改为"地表探针（不穿透）+ 洞穴探针"两段。

## 5. 复现

```
bash tools/run_scene.sh <build_dir> <outdir>     # 四段行为（需 /tmp/tm2input）
bash tools/run_shots.sh <build_dir> <outdir>     # 逐生物取景截图
python3 tools/feed_probe.py <pid> <win> <log> <outdir>   # 只跑喂食（诊断用）
```
`/tmp/tm2input` 由 `docs/qa/T-D40-2026-09-17/tools/td40input.m` 编译而来（HID 注入，无 osascript）。
**键位**（本机 macOS 虚拟键码）：F6=97（苔背兽）F7=98（巡行者）F8=100（爆芽）；
`7`=26 选谷物面包所在格（**HUD 第 N 格 = 槽 N−1**）；左 Shift=56（潜行）。
