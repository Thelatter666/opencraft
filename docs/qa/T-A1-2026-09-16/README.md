# T-A1 实机证据（权威侧前置 · 进程内世界模拟分离）　2026-09-16

> 结论先说：**四项世界动作（挖 / 放 / 倒水 / 舀水）与流体推进在实机上全部照旧工作**，
> 但它们现在**全部经由权威侧**（`server::WorldSim::submit()` / `tick()`）；
> 客户端的每一次世界改动都在日志里留下权威侧的裁决（接受或拒绝+理由），
> 且**权威侧的拒绝在实机上真的出现过**（两条 `refused` 警告，理由精确）。
> 全部证据见本目录。

## 环境

- 机器：macOS 24.6.0（arm64，用户实机）；游戏窗口 1280×748 @ (320,89)（**窗口号按 PID 反查 + 尺寸过滤**）
- worktree：`/Users/happy/Desktop/opencraft_worktree/opencraft-T-A1`（分支 `task/T-A1-authoritative-side`）
- 二进制：`build/opencraft`（Release），**md5 `1333ed3f34e551f8d16605d9d3473b71`**
  - 该二进制**不含**临时打点：`strings build/opencraft | grep -c HARNESS` = `0`（打点已还原后重编）
  - 打点版二进制仅用于 session2（见「证据 D」），其补丁归档为 `temporary_harness.patch`
- 运行：`cd build && rm -rf saves && ./opencraft`（种子固定 ⇒ 地形/出生点可复现：spawn (0.5, 132.0, 0.5)）
- 注入：HID 层 `CGEventPost(kCGHIDEventTap)`，工具源码 `tools/ta1input.m`（**未用 osascript**）；
  瞄准用 `kCGMouseEventDeltaX/Y` 增量、点击显式带窗口中心坐标 (960,463)
- **注入校验**（本卡新增）：本机 HID 时灵时不灵（`docs/05` §3.1），因此每次鼠标注入后
  **逐像素比对前后两帧**，未生效就重试，工具在 `tools/ta1_hid.py`（每步打印 `view changed = True`）。
  未生效的尝试整轮作废重跑，其截图不归档。

## 证据 A（验收 9 · 挖）：权威侧接受挖掘，且**同一格**可再次放置

| 文件 | 内容 |
|---|---|
| `G1_placed_one_block.png` | 先放一块 `sod_loam`（= grass_block），可见新方块与选中框 |
| `G2_after_single_dig.png` | 左键按住 1.4 s（恰好破 1 块）后：方块消失，地面留坑 |
| `session2_harness_actions.log` | 决定性日志（见下） |

```
47 [info] placed grass_block at (0, 132, -2); consumed 1 from slot 1 (31 left)
54 [info] HARNESS dig accepted at (0, 132, -2); block there now 0
57 [info] placed grass_block at (0, 132, -2); consumed 1 from slot 1 (30 left)
```

- 第 2 行是**临时打点**打印的权威侧裁决：`submit(Dig)` 返回 `accepted`，
  且**权威侧自己的世界**在那一格报告 `block now 0`（空气）。
- 第 3 行**再次放置到完全相同的坐标**（0, 132, -2）。该格只有在变为空气后才可放置
  （`is_replaceable`），因此这一行是「挖确实生效」的机器证据——
  两次坐标逐字相同，且第 3 行块数 31→30（库存真的被扣）。
- 为什么必须打点：`tick.cpp` 的挖掘分支原本**不打印任何日志**（T-A1 未新增常驻日志），
  故用项目既有手法「临时打点 → 取证 → 还原」（T-F1 同样做法，见 `temporary_harness.patch`）。

`C1_block_present.png` / `C2_after_dig_hole.png` 是**未打点的正式二进制**（session1）里
同一手法的视觉证据：连续挖掉 2 块后地面上出现明暗明显的 1 格深坑（草皮下的棕色泥土侧面外露）。

## 证据 B（验收 9 · 放 + 倒水 + 舀水）：正式二进制，三条日志齐全

| 文件 | 内容 |
|---|---|
| `C1_block_present.png` | 放置后（`grayrock`→`sod_loam` 选中，快捷栏数量 32） |
| `D1_water_vessel_selected.png` | 键 6 选中水容器（手持蓝方块可见） |
| `D2_after_pour.png` | 右键倒水后：地面出现水源 |
| `D3_after_scoop.png` | 键 5 空容器右键舀回后：水源消失，容器回到水态 |
| `session1_pristine_actions.log` | 决定性日志（见下） |

```
51 [info] placed grass_block at (0, 132, -1); consumed 1 from slot 1 (31 left)
63 [info] placed grass_block at (0, 131, -1); consumed 1 from slot 1 (30 left)
82 [info] vessel: poured water source at (0, 132, -1)
109 [info] vessel: filled from (0, 132, -1)
```

- 放置：两条 `placed ... consumed 1 ... (31 left / 30 left)` —— 世界写入与库存扣减都在，
  且**先由权威侧接受、客户端才扣物品**（本地顺序：`submit()` → accepted → `place_one_block()`）。
- 倒水/舀水：坐标一致（(0, 132, -1) 倒出、同一格舀回），T-F1 的水桶两条路径未回归。
- 该二进制**未打点**，日志里除上述业务行外没有任何新增调试输出。

## 证据 C（验收 3 · 命令校验在实机上真的会拒绝）：两条 `refused` 警告

`session1_pristine_actions.log`：

```
44  [warning] place refused at (0, 132, 0): cell overlaps the actor
140 [warning] scoop refused at (0, 131, -1): no water source
```

- 第 1 条是「视线俯角过大、瞄准自己脚下那一格」时权威侧的拒绝：**理由为「格子与玩家 AABB 重叠」**，
  且世界里**什么都没变**（该格没有方块出现）。
- 第 2 条是空容器对着没有水源的格子舀水：**理由为「没有水源」**。
- 这两条都来自 T-A1 **新增的**警告分支：客户端不再自己判断，只把结果照抄进日志。
- 四条动作路径**从未出现过 `out of reach` / `chunk not loaded` 的误拒**
  （这是本卡最大的风险点：权威侧的距离判定若比客户端射线更严，会悄悄吃掉一次挖掘）。
  为此另有单测把「客户端射线能命中的每一格都必须被接受」做成扫描断言（`tests/test_authority.cpp`）。

## 证据 D（验收 4 · 结果回推）：流体推进由权威侧 tick 驱动，客户端只按回推重烘焙

| 文件 | 内容 |
|---|---|
| `I1_aim_final_pitch.png` | 倒水前的干燥地面（视野内蓝色占比 10.9%，全是手持水容器方块） |
| `I2_pour_water_pristine.png` | 倒水后 ~1 s（蓝色占比 27.9%）：水源已漫过视野内的地面 |
| `J1b_ground_dry_aim2.png` / `J2b_t0_3s.png` / `J4b_t4s.png` | 第二次会话的同一序列（0.3 s / 4 s） |

决定性的是**日志形状**（`session3_pristine_pour.log` / `session4_pristine_pour_series.log`）：

```
倒水之前：remeshed 行 = 0 条
倒水之后：remeshed 行 = 40 条（session3，跨 04:52:02.8 → 04:52:13.2，约 10.4 s）
                     = 41 条（session4）
```

- `remeshed` 是客户端**唯一**的重烘焙入口，而它的输入只来自
  `IAuthority::take_changes()`（T-A1 前来自 `world.fluid_step(dirty)`）。
- 静止世界里该行为 0 条；倒水之后水每 5 tick 推进一格、每格都产生脏区块，
  于是连续出现 40+ 条 —— 即**流体推进 + 脏区块回推**这条链路在实机上工作。
- 视野内水量：干燥 10.9% → 倒水后 27.9%（蓝色像素采样，含手持方块），与
  「水在 1 秒内铺满近处地面」的观感一致（1 格/5 tick = 4 格/秒）。

## 未归档的失败尝试（如实记录）

- 本机 HID 鼠标注入约 1/3 的调用不生效，导致若干轮「瞄准不对」的空点击
  （日志里没有任何 `placed`/`vessel` 行，只有 `place refused ... overlaps the actor`）。
  这些帧已删除，不入证据目录；正式序列改用 `tools/ta1_hid.py` 的**逐帧校验重试**后一次通过。
- 「水面随时间增长」的分帧曲线**没能拍到**：相机离水面太近（1–2 格），
  视野内的水面在 0.3 s 内就铺满了，后续帧与首帧逐像素相同（`I2` 与 `I3` 的 md5 相同，
  该帧已删除）。流体推进的**时序**证据因此落在上面那 40 条 `remeshed` 行与单测上，
  而不是分帧像素曲线 —— 这一点如实申报。

## 无崩溃

`~/Library/Logs/DiagnosticReports/` 中最新一条 opencraft 崩溃报告为 `04:19`（T-I2 会话遗留），
本卡四次会话（04:46 / 04:49 / 04:51 / 04:53）**未产生任何新报告**。

## 工具

| 文件 | 说明 |
|---|---|
| `tools/ta1input.m` | HID 注入器（T-I2 同款复制而来）。构建：<br>`clang -framework Foundation -framework CoreGraphics -framework AppKit -o /tmp/ta1input ta1input.m`<br>（源注释里的构建行缺 `-framework AppKit`，本机 `NSRunningApplication` 需要它） |
| `tools/ta1_hid.py` | 注入校验封装：每步 `activate` + 逐像素比对 + 重试；`ta1_fluid_series.py` 是倒水序列脚本 |
| `temporary_harness.patch` | 挖掘分支的临时打点（16 行 diff），**已还原**，仅供复核核对日志来源 |
