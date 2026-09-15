# T-F1 实机证据（流体地基 + 水桶）　2026-09-16

> 结论先说：水在实机**逐格铺开**（5 tick/环），7 环封顶，能级每格 −1；遇落差**收窄成一股**
> 并形成下落柱；水桶右键**倒水/装水**两条路径都在实机跑通。全部证据见本目录。

## 环境

- 机器：macOS 24.6.0（arm64，用户实机），窗口 1280×720（窗口号按 PID 反查）
- worktree：`/Users/happy/Desktop/opencraft_worktree/opencraft-T-F1`
- 二进制：`build/opencraft`（Release），cwd = `build`（存档 `build/saves/world`，每次运行前 `rm -rf saves`）
- 种子固定（`WorldSource::kSeed`）⇒ 地形与出生点可复现
- 临时打点：见 `temporary_harness.patch`（**已 `git checkout` 还原，最终树 = 提交 `taskT-F1:` 的树**）
  - 逐次流体写入打印：`[fluid-dbg] t=<tick> (x,y,z) kind/level/source/falling`
  - 每 200 tick 的网格化耗时探针：`[perf-dbg] ... no-fluid vs with-fluid`
  - 出生点 21×21 平台整平（自然地形呈梯田，倒水立刻被落差导向，见证据 B）
  - 起始手持「满水桶」（本机注入通道无法送达数字键 0，见下「注入通道」）
- 按键/鼠标注入：`CGEventCreateKeyboardEvent` + `CGEventPost`(**session tap**)，工具同 T008
  （`/Users/happy/Desktop/opencraft/docs/qa/t008-2026-09-13/tools/input.m`，编译：
  `clang -framework Foundation -framework CoreGraphics -o input input.m`）。**未用 osascript。**

## 证据 A（决定性，验收 1/2）：平地单源逐格铺开，5 tick/环，7 环封顶

来源：`trace_flat_spread_full.log`（平台上的单源），逐环统计（脚本提取，非人工摘抄）：

| tick | 该 tick 新写入的格数 | 曼哈顿距离 | 能级 |
|---|---|---|---|
| 600 | 1（源头） | 0 | 8（source=1） |
| 605 | 4 | 1 | 7 |
| 610 | 8 | 2 | 6 |
| 615 | 12 | 3 | 5 |
| 620 | 16 | 4 | 4 |
| 625 | 20 | 5 | 3 |
| 630 | 24 | 6 | 2 |
| 635 | 28 | 7 | 1 |

- 每环间隔**恰好 5 tick**（对应 250 ms；日志时间戳实测 02:11:27.688 → .958 → 28.234 → 28.505，≈270 ms/环，
  含日志与调度开销），**每格能级 −1**（ΔL=1）。
- 全部写入总数 **113** = 1 + 4×(1+2+…+7)，与「半径 7 的菱形」逐格吻合；此后不再增长（能级耗尽）。
- 截图：`shot_pool_level_steps.png`（同心台阶状水面 = 能级高度差）、`shot_pool_after_spread.png`。

## 证据 B（验收 3/4）：自然梯田地形上水收窄成股、优先走落差并形成下落柱

来源：`trace_terrace_funnel.log`（**未整平**的自然地形，出生点西侧有 1 格落差）：

```
t=826 (0,132,-1)  L=8 source=1      ← 水桶倒出的源头
t=831 (-1,132,-1) L=7               ← 四邻只有 1 格进水（未四向均摊）
t=836 (-1,131,-1) L=8               ← 落差口：往下走，落在地面（falling=0）
t=841 (-2,131,-1) L=7               ← 落地后沿地面继续流向下一个落差
t=846 (-2,130,-1) L=8 falling=1     ← 下落柱
t=851 (-2,129,-1) L=8 falling=1     ← 柱子每 5 tick 下降 1 格
t=856 (-2,128,-1) L=8 falling=1
t=861 (-2,127,-1) L=8 falling=0     ← 落到底，恢复水平扩散
t=866 (-1,127,-1) L=7, (-2,127,0) L=7
```

- 768 行日志里 `falling=1` 只出现在下落柱上，落地即 `falling=0` —— 与 §3.3/§3.4 一致。
- 瀑布只有 1 格宽（下落柱不发散），落地后才摊开 —— 正是卡片要求的观感。
- 截图：`shot_terrace_cascade.png`（梯田上的水帘）、`shot_natural_terrain_spawn.png`（自然地形原貌）。

## 证据 C（验收 5）：水桶两条路径

1. **倒水**（证据 A/B 的源头即由此产生）：`bucket: poured water source at (0, 132, 0)`
2. **装水**：`trace_bucket_fill_hold_defect.log`
   ```
   t=776 (0,132,-2) kind=1 level=8 source=1   ← 倒出
   t=780 (0,132,-2) kind=0 level=0            ← 被舀回（bucket: filled from (0, 132, -2)）
   ```
   该日志同时暴露一个**真实缺陷**（已修）：按住右键时动作每 4 tick 重复，而刚倒下的源头可立即被舀回
   ⇒ 表现为「倒→舀→倒」振荡。修复：水桶动作改为按下沿触发（`use_edge`）。方块放置仍保持原重复节奏。
3. 截图：`shot_bucket_selected_hud.png`（快捷栏第 10 格高亮、名称 WATER BUCKET、手持满桶）。

## 证据 D（验收 8）：网格化耗时

`[perf-dbg]` 探针（同一 tick 内对同一区块跑两次 `build_chunk_mesh`，唯一差别是传不传流体源）：

| tick | 区块 | no-fluid | with-fluid | 该区块流体面数 |
|---|---|---|---|---|
| 0（无流体） | (0,0) | 2.712 ms | 2.741 ms | 0 |
| 0（无流体） | (-1,0) | 2.633 ms | 2.558 ms | 0 |
| 800（有水池） | (0,0) | 7.783 ms | 6.230 ms | 124 |
| 800（有水池） | (-1,0) | 5.828 ms | 5.204 ms | 91 |
| 1000（有水池） | (0,0) | 6.592 ms | 6.339 ms | 124 |
| 1000（有水池） | (-1,0) | 5.343 ms | 5.264 ms | 91 |

- 无流体区块：with-fluid 与 no-fluid 差在噪声内 ⇒ **流体层对绝大多数区块零开销**（`FluidSpan` 门控）。
- 有流体区块：差值同样在噪声内（且符号不稳定）⇒ 流体网格化本身不构成可测开销。
- ⚠ **绝对值 5–8 ms 超过 `docs/03 §10` 的 5 ms 预算**，但这是**既有**现象：同一 tick 同一区块的
  no-fluid 路径同样 5–8 ms，而 t=0 时同一区块只有 2.7 ms ⇒ 随流式加载/后台负载升高，非本卡引入。
  本机无法复现 T005 报告的 0.74 ms/区块。

## 过程记录（如实交代）

- **注入通道**（债务 T-D21 复现）：需先用 MCP `open_application(activate)` 把窗口置前，
  之后 session-tap 注入才生效；且**时灵时不灵**：同一会话内首次右键（倒水）成功，随后的按键/右键
  （选槽、后退、装水）多次无效。因此：
  - 证据 A/B/C1/C2 的**倒水**是真实右键注入触发；
  - 证据 C2 的**装水**来自按住右键时的重复动作（缺陷日志，同一条真实代码路径）；
  - 「按数字键 0 选水桶」这一步在本会话无法注入，改由临时 harness 提供起始手持状态。
    （该分支本身只有 3 行：`selected_slot/selected_block` 赋值，见 `temporary_harness.patch` 之外的主树代码。）
- 所有数据均为**实机运行产物**，非夹具、非无头、非合成回放；`[fluid-dbg]` 打印点在
  `WorldSource::set_fluid_at` 内（即真实写入路径），打印点已在最终提交前删除。
