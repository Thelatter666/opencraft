# T-D40 实机 + 轨迹证据　2026-09-17

> 分支 `task/T-D40-sweep-move`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-D40`
> 本卡是**纯重构**（抽公共碰撞原语），所以验收装置按 `docs/05 §3.2` 的三层证据组织：
> **冻结项 / 逐块 token / 实机 A/B**。全部证据在本目录。

## 0. 两个二进制（同一台机器、同一编译器、Release）

| | 基线（改动前） | 本卡（改动后） |
|---|---|---|
| 源码树 | `/tmp/td40_base_src`（`git archive d36d8e2` 的干净导出） | worktree 分支 `task/T-D40-sweep-move` |
| `opencraft` md5 | `09af4081c865b48efa032cd7e8aca377` | `8b736b38c616d4effb1a386077baae1e` |
| `opencraft_tests` md5 | `7b1c8cb83056d5d307ca5151aba8f7aa` | `e67c46bacb12ff5d9e956936ef48af70` |
| `libopencraft_physics.a` md5 | `feec02d6899d9d4fd93193c6f40b32e6` | `dc9472af1130e4050dc45c0857b093e6` |

测试：两边都是 **359 test cases / 10198 assertions，0 failed**（完整测试输出见主报告 §3，测试文件本卡零改动）。
环境：macOS 24.6.0（arm64），窗口 `486 @ (320,89) 1280×748`（按 PID 反查）；每次运行前确认无残留实例，
取证结束后 kill 自己的实例（`run_scene.sh` / `run_move.sh` / `run_jump.sh` 内建）。

## 1. 轨迹 A/B：决定性装置（`tools/ab_scenario.cpp` + `tools/build.sh`）

`ab_scenario.cpp` **同一份源码**分别对着两棵树编译，只使用两棵树都未改动的公开入口
（`physics::step_player`、`server::step_items`），打印状态时把每个 double 打成 **原始 bit 模式**
（`std::bit_cast`，比 `%.17g` 更严：连 `0.0` 与 `-0.0` 的差别都能抓到）。

```
$ ./ab_base > ab_base.out ; ./ab_new > ab_new.out
1253 行 与 1253 行；md5 双方都是 9f1da2e3fba0a0c38c130d47c3f5f3e5   → 逐字节相同
```

**装置自检**（防"两边其实跑的是同一份实现"）：

| 检查 | 基线 | 新 |
|---|---|---|
| `nm` 里的 `sweep_axis_y` | 0 | 1 |
| `nm` 里的 `move_axis_y` | 1 | 0 |
| 预处理后的 TU 里 `physics::sweep_axis_y(e.position` | 0 | 1 |
| 预处理后的 TU 里 `item_detail::move_axis_y` | 1 | 0 |

**覆盖度**（`logs/coverage.out`，判定脚本 `tools/coverage.sh`）：X 夹紧 164 tick、Z 夹紧 56 tick、
天花板 16 tick、落地 10 tick、step-assist 触发 1 tick、空中 518 tick、潜行 122 tick；
掉落物侧：落到整块、落到 **0.25 半砖面（精确 64.25）**、墙角 X+Z 夹紧、冰面滑行、静止成对合并（n=2→1）、
虚空下落消失（n=0）。即：本卡触碰过的每条分支都被真实执行过，而不是"没测到所以没差异"。

## 2. 冻结项 / 逐块 token（`logs/compare.out`，脚本 `tools/compare2.py`）

按 `docs/05 §3.2` 的判据"**任何 token 不许消失，搬家只增不减**"，逐函数做多重集比较：

| 函数 | 旧 token 数 | 消失 token 数 |
|---|---|---|
| `box_collides` → 同名 | 265 | **0**（原样搬移） |
| `highest_surface_below` → 同名 | 302 | **0**（原样搬移） |
| `move_axis_y/x/z` → `sweep_axis_y/x/z` | 216/201/201 | 全部逐条记账（见 `compare.out` 的 ledger） |
| 数值字面量 | — | **0 个值消失**（3 处 `0.0` 换位置：`return 0.0`→`AxisSweep::delta` 默认值；`velocity.x = 0.0`→调用方包装） |
| 注释 token | 3 | **0**（搬移的注释逐字保留） |
| `item_sim.hpp` 里手抄的碰撞代码 | — | **1242 token 被删除**（重复消除），且该文件已不再定义/调用 `box_collides` / `highest_surface_below` / `move_axis_*` |

## 3. 实机 A/B（`tools/scenario.py` + `tools/run_scene.sh`）

同一份注入脚本分别驱动两个二进制，世界从**同一颗恒定种子** `srv::WorldSim::kSeed` 从零生成
（每次运行前删 `saves/`），因此两次运行的世界、出生点、朝向完全一致。

**先自证装置**：基线跑两遍（`BASE1` / `BASE2`），静态帧逐像素完全相同、掉落物帧只差在掉落物自身
（见下表逐帧"BASE1 vs BASE2"列）—— 说明脚本的场景是确定性的，A/B 才有意义。

| 帧 | 内容 | BASE1 vs BASE2 | NEW1 vs BASE1 |
|---|---|---|---|
| `S1_pristine` | 干净出生点 | **逐像素相同** | **逐像素相同** |
| `S2_aimed` | 饱和→回退后的 −50° 俯角 | **逐像素相同** | **逐像素相同** |
| `S3_drop_a` | 挖穿后掉落物出现 | 930 px，bbox=(601,509,679,602) | 490 px，bbox=(601,509,**678**,602) |
| `S4_drop_b` | 0.6 s 后（掉落物静止/自转） | 629 px，bbox=(610,523,669,599) | 952 px，同 bbox |
| `S5_after_place` | 放置后（洞被填回、掉落物已不在） | **逐像素相同** | **逐像素相同** |
| （S6） | 走+跳之后 | 全帧不同（位置随时间漂移，**不可比**） | 全帧不同（同因） |

**判据**：S3/S4 的差异 bbox 与"基线 vs 基线"的差异 bbox 一致 → 差异源就是掉落物自身的浮沉/自转
（`docs/05 §3.1` 规则 6 的判据），掉落物**出现在同一位置、同样大小**；除它之外的画面逐像素相同。

**决策日志 A/B（逐字节相同）**——两个二进制、两次基线运行三方一致：

```
item drop sod_loam spawned at (0.50, 131.38, -0.50) [entity 1]
placed stone at (0, 131, -1); consumed 1 from slot 0 (63 left)
autosave: 1 chunk(s) queued for async write, level written (ticks=199)
```

**走 / 跳**（`logs/MNEW2_move.log`、`logs/JNEW_jump.log`、`logs/JBASE_jump.log`）：客户端自己的
`fps ... | pos (x,y,z)` 与跳跃采样：

| | 基线 | 新 |
|---|---|---|
| 走 | 从 `(0.50,132.00,0.50)` 走到 `(0.50,132.00,-13.62)`（早前一次会话） | `(0.50,132.00,0.50)` → `(0.50,134.00,-16.65)` |
| 跳 | y 采样 `132.12 / 133.00 / 133.18`（地面 132.0，弧顶 ~+1.2） | y 采样 `132.0 / 132.42 / 132.8 / 133.02 / 133.17 / 133.25` |

两边都是"离地 → 弧顶 ~+1.25 → 落回地面"的重复弧线（`fps` 行每 ~2 s 一行，采样相位不同，
所以抓到的弧上点不同）。⚠ 本机 HID 注入**会话相关地时灵时不灵**（债务 T-D21）：同一次会话里
60 ms 脉冲完全无效、15 ms 密集脉冲才生效；**疾跑双击 W 本次会话没能复现**（走、跳、挖、放都已复现）。

**存档**：两个二进制都只写了 `saves/world/level.ocd`（149 B）。逐字节比对差 24 字节，
全部落在"玩家位置字段 + 末尾 CRC"上（`cmp -l`：偏移 62–67、86–93、126–131 及尾部），
种子字段 `0x4F50454E43524146` 与玩家 y=132.0 两边相同 —— 差异来自"走/跳落在不同 tick"，
不是碰撞差异。世界方块改动没有落成 region 文件（进程在异步写盘前被 kill），
**世界状态因此以 S5 的逐像素相同为准**（挖出的洞 + 填回的石头，两边像素一致）。

## 4. 目录

| 路径 | 内容 |
|---|---|
| `shots/` | 上表的帧（`BASE1_*` / `BASE2_*` / `NEW1_*`），PNG 原图 |
| `logs/` | 两个二进制的主会话日志、走/跳日志、轨迹 A/B 输出（`ab_base.out` / `ab_new.out`）、逐块 token 账（`compare.out`）、覆盖度（`coverage.out`） |
| `tools/` | 轨迹 A/B 装置（`ab_scenario.cpp`、`build.sh`）、token/字面量账（`compare2.py`）、覆盖度（`coverage.sh`）、实机脚本（`scenario.py`、`run_scene.sh`、`movement.py`、`jump_probe.py`、`run_move.sh`、`run_jump.sh`、`probe_walk.py`）、HID 注入工具源码（`td40input.m`，`clang -framework Foundation -framework CoreGraphics -framework AppKit`） |
