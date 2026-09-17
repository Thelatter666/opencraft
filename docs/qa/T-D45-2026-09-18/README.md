# T-D45 实机证据　2026-09-18

死亡状态与重生（生存循环闭环）。

> 分支 `task/T-D45-player-death`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-td45`
> 环境：macOS 24.6.0（arm64）；窗口 `1280×720` 客户区（`win` 反查得 `1280×748`，含 28 px 标题栏）
> 单实例确认：每段取证前后 `pgrep -x opencraft` 均为 0（脚本内建 kill）

## 0. 二进制度量（证据 = 交付同一份构建）

| 项 | 值 |
|---|---|
| 取证用 `build/opencraft` md5 | `12c92ef5f6c09b2db4cb7596d8560d61` |
| 临时钩子 / 探针 | **无**。本卡没有为实现取证改过任何产品代码（视角与位置都由**存档字段**设定） |
| 与交付源码的关系 | 取证在 `clang-format` 之后的最终源码上重跑，截图/日志与提交的是同一二进制 |

## 1. ★ 观测装置（决定这些证据能证明什么）

| 项 | 装置 | 能证明 / 不能证明 |
|---|---|---|
| 世界 | 真客户端 + 真 `server::WorldSim`（存档 `build/saves/world/level.ocd`，种子 `0x4F50454E43524146`） | 是生产路径 |
| 死亡场景 | **改写存档**：`player=(24.5, 184, 0.5)`、`on_ground=0`、`pitch=1.35`（`patch_level.py`，含 CRC32 重算） | 证明"摔落致死→死亡画面→掉落→重生"的**生产代码路径**；位置是人为摆的（§6.4 允许） |
| 死亡落点的高度 | `tools/surface_probe.cpp`（`build_surface_probe.sh` 编译）：用**同一 worldgen** 查 `surface_height(24, 0) = 144`，故 184 是 40 格空中 | 排除"落在半空/埋进山里"的猜测 |
| 输入注入 | HID 层（`CGEventPost(kCGHIDEventTap)`，`td45input.m` 由 T-D40 的 `td40input.m` 原样复用，禁用 osascript） | 是真键盘/鼠标事件；**A/D 两组对照**见 §3.3 |
| 截图 | `screencapture -x -o -l<窗口号>`，每次 `front`(AX raise) + `activate` 后连拍两张取第二张 | ⚠ 本机对非前台窗口会返回旧表面（docs/05 §3.1 第 10 条）；§3.2 用**逐像素差异**排除 |
| 判据 | **日志**（客户端 + 权威侧）为主，截图为辅 | 卡面第 10 条要的三种画面各有截图，关键数量由日志给出 |

## 2. 三段会话（脚本可复跑）

```bash
# 1) 造档（新世界 + 一次 autosave），再把死亡场景写进存档
bash tools/run_death_scene.sh <worktree>/build <this dir>
# 2) 用上一步"死亡期间 autosave 落盘的那份存档"（health=0）验证 0 血存档
bash tools/run_zero_health.sh <worktree>/build <this dir>
```

### 2.1 会话 1：新档（`session1_save_fresh.log`）

```
respawn point: (0.5, 132.0, 0.5)          ← 新档：WorldSim::find_spawn() 的结果
autosave: 0 chunk(s) queued for async write, level written (ticks=199)
```

存档里 `spawn_x/y/z = (0.5, 132.0, 0.5)` —— 即**重生点**，不再是玩家位置（本卡 §2.4 修的就是这里）。

### 2.2 会话 2：摔落致死 → 死亡画面 → 掉落 → 重生（`session2_death_scene.log`）

```
save: loaded level.ocd (…, player=(24.50, 184.00, 0.50), hp=20.0)
respawn point: (0.5, 132.0, 0.5)
fall for 20.0 at (24.50, 144.00, 0.50); health now 0.0
player died at (24.50, 144.00, 0.50); 18 stack(s) dropped, 0 refused (slots left: 0)
respawned at (0.50, 132.00, 0.50); health 20.0, 0 slot(s) in hand
```

- `144.00` = 落点正好是 `tools` 预先查出的地表高度（不是负血、不是穿地）。
- `18 stack(s) dropped, 0 refused` = 初始装备 9 快捷 + 9 主背包 = 18 个非空槽**逐个**掉落，全部被权威侧接受；`slots left: 0` = 客户端库存已清空。
- `respawned at (0.50, 132.00, 0.50)` = 回到**重生点**（死亡点 (24.5, 144) 距其 24 格）。

### 2.3 会话 3：0 血存档（`session3_zero_health_save.log`）

存档取自**会话 2 死亡期间那次 autosave 落盘的真实文件**（`level_after_death_autosave.ocd`，`health = 0.0`、`player = (24.5, 144.0, 0.5)`），不是手工编辑：

```
save: loaded level.ocd (…, player=(24.50, 144.00, 0.50), hp=0.0)
respawn point: (0.5, 132.0, 0.5)
save: stored health 0.0; respawning at (0.5, 132.0, 0.5) at full health   ← warning
fps 42.3 | pos (0.50, 132.00, 0.50)                                      ← 玩家在重生点、已满血
```

## 3. 截图与逐像素判据

| 文件 | 对应验收 | 内容 |
|---|---|---|
| `01_death_screen_and_drops.png` | §5.10 第 1、2 条 | 全屏红色遮罩 + `YOU DIED` + `RESPAWN` 按钮；HUD 全隐（`HudState::dead`）；画面中央偏下是**死亡点地面上的掉落堆**（1/4 比例小方块 + 无方块形态物品的着色点，共 18 堆同点生成） |
| `02_dead_inputs_ignored.png` | §5.9 | 与 01 同机位；拍摄前注入 W/A 密集脉冲 + 左键点击 |
| `03_after_respawn.png` | §5.10 第 3 条 | 点击 RESPAWN 之后：回到出生点（草地/水面）、**10 颗满心**、快捷栏**全空** |
| `04_zero_health_save_respawned.png` | §5.8 | 0 血存档重进后：重生点、满心、初始装备（库存不落盘 ⇒ 每次开局发初始装备，与 §2.4 的"不做库存序列化"一致） |
| `05_live_input_control.png` | §5.9 的**对照** | 重生之后注入**同样**的 W/A 脉冲，玩家确实移动了 |

### 3.1 死亡画面与掉落堆

01 里 `RESPAWN` 按钮正下方那一团就是掉落物：小方块各自带不同旋转角（旋转来自每个实体的 `age`），橙/灰色小点是"无方块形态"物品（食物/工具）的着色点回退。

### 3.2 "画面里到底有没有东西"：逐像素（`pixel_diff.txt`）

```
01_death_screen_and_drops.png vs 02_dead_inputs_ignored.png:
  10256 differing pixel(s) of 957440
  bounding box: x 576..702, y 440..570        ← 恰好只有掉落堆那一块
```

- 死亡期间**相机冻结**（鼠标视角在死亡态被关闭），地形/天空/遮罩/文字全部逐像素相同；
- 唯一在变的是掉落堆（自转 + 上下浮动）⇒ ① 截图不是过期表面；② 那一块确实是"在动的实体"，不是烘进地形的贴图。

### 3.3 死亡态屏蔽交互：A/B 对照（关键）

| 阶段 | 注入 | 客户端位置读数（`fps … pos (x,y,z)`） |
|---|---|---|
| 死亡中 | W/A 密集脉冲 ×6 + 左键点击 | `(24.50, 144.00, 0.50)` → **不变** |
| 重生后（活人） | **完全相同**的注入 | `(0.50, 132.00, 0.50)` → `(0.29, 132.00, -0.13)` **移动了** |

⇒ 死亡期间"位置不变"**不是**注入通道失效造成的假结论（docs/05 §3.1 第 2 条那个坑）：同一条通道、同一段脚本，活人玩家会动。

**这条证据覆盖的范围（诚实说明）**：证明的是**移动**被屏蔽（位置读数）与**没有产生任何挖/放/攻击日志**。挖/放的失败本身无日志可查，其结构性依据是 `run_tick` 的第一分支（`may_act(ctx.life)` 为假时**在读取按键之前**返回），单测见 `tests/test_player_death.cpp` 的 `may_act` 用例。

## 4. 工具

| 文件 | 用途 |
|---|---|
| `td45input.m` | HID 注入（`front`/`activate`/`win`/`keytap`/`clicktap`/`move`/`trust`）。**由 `docs/qa/T-D40-2026-09-17/tools/td40input.m` 原样复制**，只改了头部注释里的卡号；编译：`clang -framework Foundation -framework CoreGraphics -framework AppKit -o /tmp/td45input td45input.m` |
| `patch_level.py` | 读/改/写 `level.ocd`（含 `zlib.crc32` 重算）；`show` / `set key=value` |
| `death_scene.py` | 会话 2 的驱动：等死亡 → 截图 → 注入并对照位置 → 抄出死亡期间落盘的存档 → 点 RESPAWN → 截图 → 活人对照 |
| `run_death_scene.sh` / `run_zero_health.sh` | 三段会话的编排（含单实例检查与 kill） |
| `surface_probe.cpp` + `build_surface_probe.sh` | 用**同一 worldgen** 打印各列地表高度（决定死亡场景把玩家摆多高）。`bash build_surface_probe.sh <worktree> <build> [seed]` → `/tmp/td45_surface`；本世界 `surface(x=24,z=0) = 144`，故 `player_y = 184` 是 40 格空中 |
| `pixel_diff.py` | 解码两张 PNG 逐像素比较（md5 相同 ≠ 像素结论，反之亦然） |

## 5. 本目录没有证明的东西

| 项 | 说明 |
|---|---|
| 生物近战致死 | 未在实机取证：本世界**地表没有敌对生物**（`docs/01 §6` 光照 0，无昼夜），召唤键属 T-M2 的临时钩子、本卡不引入。该路径由单测覆盖（`take_damage` 用于 ActorEvent 的同一入口） |
| 爆炸致死 | 同上（`ActorEventKind::Explosion` 与近战共用入口） |
| 死亡掉落的散落形态 | 本卡就是"全部堆在同一点、初速度 0"（§2.3 **待校准**），截图即现状 |
| 死亡掉经验 | 砍出（无经验系统），见报告"已知问题" |
| keepInventory=true 的实机行为 | 单测覆盖（`take_damage` 与 `respawn_clear_inventory` 两个分支），实机未取证（运行时无 UI 可切该规则） |
| Retina / HiDPI | 本机 1x，未验证（docs/05 §3.1 第 11 条） |
