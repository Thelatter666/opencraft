# T-D46 实机证据　2026-09-19

战斗基础：玩家侧受击无敌帧 / 护甲减伤 / 击退。

> 分支 `task/T-D46-combat-basics`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-td46`
> 环境：macOS 24.6.0（arm64）；窗口 `1280×720` 客户区（`win` 反查得 `1280×748`，含 28 px 标题栏）
> 单实例确认：每段取证前后 `pgrep -x opencraft` 均为 0（脚本内建 kill）

## 0. 两个二进制

| | 取证二进制 | 交付二进制 |
|---|---|---|
| 用途 | 跑出下面三段场景 | 提交的代码 |
| 路径 | `<worktree>/build/opencraft`（同一目录，先后构建） | 同左 |
| `build/opencraft` md5 | `17dc3dab87e229c9c83bbbe402d1613e` | `b6395887070d968b3621ac76075fe003` |
| 差异 | `main.cpp` 的 **T-D46 临时钩子**（F7/F8 召唤、F9/F10 穿脱盔甲） | — |

⚠ **本机 md5 不是"同一份源码"的判据**（2026-09-19 实测，值得后续卡沿用）：把源码 `touch` 一下重链两次，
产物有 **4 段共 81 字节**不同（其中 16 字节是 Mach-O 的 `LC_UUID`，链接器每次随机生成；
`otool -l` 可直接看到两个不同的 uuid）。⇒ 判"取证用的是交付源码"只能靠**源码级**判据：
`git status` 干净 + `git diff` 只含白名单 + `grep -c "TEMPORARY EVIDENCE HOOK" game/client/src/main.cpp` = 0
+ `strings build/opencraft | grep -c EVIDENCE` = 0。md5 只作为"这个文件"的编号记录。

临时钩子在取证后**已全部还原**（判据：`grep -c "TEMPORARY EVIDENCE HOOK" game/client/src/main.cpp` = **0**、
`strings build/opencraft | grep -c EVIDENCE` = **0**，且 `git diff --stat game/client/src/main.cpp` 为空）。
钩子原文存档在 `tools/td46_evidence_hook.patch`（含插入点与还原判据），驱动脚本 `tools/combat_scene.py`
可原样复跑。

⚠ **本卡的产品码实机证据受两条限制**（详见 §5）：

1. **召唤键不是产品功能**：敌对生物只在光照 0 处生成（`docs/01 §6`，本世界无昼夜，地表天光恒 15），
   所以"生物站在你面前"这个场景本身需要钩子——与 T-M2 取证用的是同一个钩子形态（`docs/qa/T-M2-2026-09-18/tools/tm2_evidence_hook.patch`）。
2. **盔甲在交付版里穿不上**：库存 36..39 四个盔甲格只有 `Inventory::set_slot` 会写，而客户端从不写它们
   （无装备键、无背包界面；初始装备把四件 `timber_*` 放在**主背包 14..17 格**，连拿在手上都做不到）。
   ⇒ F9/F10 直接写那四个格子（**写的是产品代码的同一个 `inventory`**），伤害仍由交付的
   `player_life.hpp` / `tick.cpp` 计算。这是一条**内容缺口**，已登记进报告「需 PM 裁决项」①。

## 1. 观测装置（决定这些证据能证明什么）

| 项 | 装置 | 能证明 / 不能证明 |
|---|---|---|
| 世界 | 真客户端 + 真 `server::WorldSim`（每次 `rm -rf build/saves` 全新档，种子 `0x4F50454E43524146`） | 是生产路径 |
| 生物 | F7/F8 → `WorldSim::summon_mob("hollow_wretch", 玩家前方 2 格)` | 证明**近战伤害链路 + 玩家侧三条规则**；**不是**"敌对生物的自然刷怪" |
| 穿甲 | F9/F10 → 写库存 36..39 四格（`Inventory::set_slot`） | 证明**穿戴状态下的减伤**；**不是**"产品里能穿甲"（产品里做不到，见 §0） |
| 输入 | HID 层（`CGEventPost(kCGHIDEventTap)`，`tools/td46input.m` 由 T-D45/T-D40 的 `td45input.m` 原样复制，禁用 osascript） | 是真键盘事件 |
| 定格 | **ESC 暂停后连拍**（暂停不跑 tick 但每帧仍渲染） | 解决"20 tick 的攻击者比 1.5 s 截图开销跑得快"：帧落在该落的那一刻 |
| 截图 | `screencapture -x -o -l<窗口号>`，先 `activate` 再连拍两张 | ⚠ 本机对非前台窗口会返回旧表面（`docs/05 §3.1` 第 10 条） |
| 判据 | **日志为主**（客户端 + 权威侧），截图为辅 | 数量与数值全部由日志给出，截图只证"HUD 可读" |

## 2. 三段场景（一次连贯会话，`combat_session.log`）

一次会话里，同一只 Hollow Wretch（id=9）先打空手玩家、再打穿甲玩家，随后 4 只生物一起验证无敌帧：

```
12:55:54.139  EVIDENCE summon hollow_wretch id=9 at (0.5, 132.0, -1.5)
12:55:54.778  mob melee for 3.0 at (0.73, 132.00, 0.97); health now 17.0     ← 空手：⚖ 3.0 一击
12:55:56.161  mob melee for 3.0 at (1.05, 133.00, -1.09); health now 14.0
12:55:57.945  mob melee for 3.0 at (-0.89, 131.00, 0.58); health now 11.0
12:55:58.016  EVIDENCE armour ON: 4 cell(s), 7 point(s), toughness 0          ← 穿上 timber_* 四件
12:55:58.938  mob melee for 2.3 at (-0.89, 131.00, 0.58); health now 8.7      ← 同一只生物：2.34 一击
12:55:59.945  mob melee for 2.3 at (-0.89, 131.00, 0.58); health now 6.3
12:56:01.494  mob melee for 2.3 at (-0.39, 130.00, 1.97); health now 4.0
12:56:02.257  mob melee for 2.3 at (-0.30, 131.53, 1.84); health now 1.6
12:55:56.424  mob melee for 3.0 absorbed by the hurt window (8 tick(s) left, last 3.0)   ← ① 窗口吞击
12:55:58.042  mob melee for 3.0 absorbed by the hurt window (8 tick(s) left, last 3.0)
…（本会话共 7 条 absorbed、3 条 3.0、4 条 2.3；全库 3 段独立会话日志）…
```

读法（逐条对验收标准）：

| 观测量 | 数值 | 对应 |
|---|---|---|
| 空手一击 | `for 3.0`（每击 3.0 → 20→17→14） | ⑥ 的对照基线；伤害**未**被护甲改动的现状 |
| 穿甲后同一生物一击 | `for 2.3`（每击 2.34 → 14→11.7→9.3→7.0→4.6） | ③ 公式：leather 7 / toughness 0 对 damage 3.0 = **22%** 减伤（`§4.1` 表） |
| `health now` 递减量 | 3.00 → 2.34 | 客户端侧实际扣血，与日志同源 |
| 窗口内的第二击 | `absorbed by the hurt window (7 tick(s) left, last 3.0)` | ① ≤ 原伤害则免疫，**血量不动**（该行无 `health now`） |
| 窗口内的大伤害 | 单测覆盖（`p.33` 的 mirror 用例把两侧逐 tick 对齐） | ① 更高只结算差值 |
| 玩家位移 | `fps … pos` 序列（**未注入任何移动键**）：<br>`(0.50,132.00,0.50)` → `(-0.35,132.00,0.12)` → `(-0.30,131.00,0.95)` → `(-0.30,130.00,2.29)` → `(-0.30,130.00,3.95)` | ⑥ 玩家被推：首段净位移 **0.93 格**（另一段会话实测 0.876 格；预测 0.4/(1−0.546)=0.881，差值是动量截断与地形坡度） |

## 3. 截图（逐张看过，与日志数值对齐）

| 文件 | 内容（我看过这一张） |
|---|---|
| `01_world_loaded.png` | 新档睁眼：草原/湖面，十字准星居中，底栏 `GREYROCK 64`，**10 颗满心** |
| `02a_wretch_closing_in.png` | 召唤后 0.15 s 按 ESC 定格的**接近帧**：灰色人形 Hollow Wretch 站在正前方约 1 格，`PAUSED` 菜单，**10 颗满心** |
| `02b_bare_after_one_hit.png` | 空手挨一击后定格：生物贴脸（灰色躯体占满画面中央）、`PAUSED`、**7 颗心**（= 日志的 `health now 14.0`） |
| `03_armoured_2.3_per_hit.png` | 穿甲后再挨几击：`PAUSED`、**3.5 颗心**（= 日志的 `health now 7.0`） |
| `05_window_absorbed.png` | 无敌帧吞掉一击那一刻的定格：HUD 只剩**半颗心**（≈1 HP），右上方可见一只 Hollow Wretch；同一秒的日志行是 `absorbed by the hurt window` |

⚠ 截图只证"血条可读、随伤害变化"；**伤害数值的证据在日志**（截图有 ~1.5 s 开销，正好落在一击之间）。

## 4. 工具

| 文件 | 用途 |
|---|---|
| `td46input.m` | HID 注入（`activate`/`front`/`win`/`keytap`/`clicktap`/`move`）。**由 `docs/qa/T-D45-2026-09-18/tools/td45input.m` 原样复制**，只改头部注释；编译：`clang -framework Foundation -framework CoreGraphics -framework AppKit -o /tmp/td46input td46input.m` |
| `td46_evidence_hook.patch` | 临时钩子的**原文 + 插入点 + 还原判据**（F7/F8 召唤、F9/F10 穿脱盔甲） |
| `combat_scene.py` | 驱动三段场景：等世界 → F7 召唤 → 定格截图 → F9 穿甲 → 定格截图 → 再召唤 3 只 → 等 `absorbed` 行 → 定格截图。含"注入无效就重按"的重试（`docs/05 §3.1` 第 2 条） |
| `run_combat_scene.sh` | 编排：单实例检查 → 清档 → 起进程 → 按 PID+尺寸取窗口号 → 跑 `combat_scene.py` → kill → 打印日志摘要 |

## 5. 本目录没有证明的东西（诚实清单）

| 项 | 说明 |
|---|---|
| **产品里能穿甲** | **不能**。交付版没有装备路径（无装备键、无背包界面；初始装备把四件放在主背包）。本目录的穿甲证据来自临时钩子直接写库存格；伤害计算仍走交付代码。已登记为内容缺口（报告「需 PM 裁决项」①） |
| 生物的**自然**刷怪 | 未取证（敌对只在光照 0 处生成，本世界无昼夜）——与 T-M2 同一限制 |
| 摔落伤害不被护甲减免 | 单测覆盖（穿满甲与空手同额）；实机未取证（本场景没有摔落） |
| 无敌帧吞掉的伤害"本来会扣多少" | 日志只给"被吞"，不给反事实；反事实由单测的 mirror 用例逐 tick 对齐 |
| 附魔 / 疾跑击退 / 攻击充能 | 本卡范围外（C-4 明令不做），见报告 |
| Retina / HiDPI | 本机 1x，未验证（`docs/05 §3.1` 第 11 条） |
