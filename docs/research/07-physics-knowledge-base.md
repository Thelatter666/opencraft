# 07 · OpenCraft 物理知识库白皮书

> 成文日期：2026-09-15　状态：**调研文档（非实现规格）**
> 定位：本文档是 `01–06` 之外**第二批来源**的消化产物，覆盖 `*research/06` 未触及的四个方向：
> ① 时间调度与主循环架构；② 权威侧碰撞/物理的开源可参照实现；③ 体素刚体与破坏物理（对照系）；
> ④ 学术视角下的离散动力学特性。
> 合规：来源全部为公开 wiki / 开源仓库 / 公开论文。**不引用任何反编译产物或 MCP/Yarn 映射**
> （`docs/04` 红线 1）。凡涉及开源实现，本节只提取**架构与数值事实**，不搬运代码；引用仓库前先核
> 许可证（见 §6 与 §0.2）。
> 落地规则：本文档**不直接改规格**。任何写入 `docs/01` / `docs/03` 的改动须经 PM 裁决并落到
> 对应任务卡（当前相关卡：T-D7 摩擦管线迁移、T-D1 gap #5 自动上台阶）。

---

## 目录

1. [时间调度层：Tick 与执行序](#1-时间调度层tick-与执行序)
2. [权威侧参照实现：Minestom / Glowstone](#2-权威侧参照实现minestom--glowstone)
3. [调试与观测：Carpet Mod 的方法论](#3-调试与观测carpet-mod-的方法论)
4. [体素刚体与破坏物理：VS2 与 Teardown 对照系](#4-体素刚体与破坏物理vs2-与-teardown-对照系)
5. [离散动力学的学术视角：Project Malmo](#5-离散动力学的学术视角project-malmo)
6. [来源核验与许可证台账](#6-来源核验与许可证台账)
7. [对 OpenCraft 的映射：现状对照与建议](#7-对-opencraft-的映射现状对照与建议)
8. [参考文献](#8-参考文献)

---

## 0. 读法说明

### 0.1 本文与 `research/06` 的分工

| 文档 | 覆盖 |
|---|---|
| `research/05-mc-movement-feel.md` | 疾跑/FOV/空中模型的**手感**调研（T-D1 依据） |
| `research/06-mc-movement-physics-whitepaper.md` | **玩家移动**的递推式、闭式解、数值复现（1.8.9 口径） |
| **本文（07）** | **系统层**：调度与执行序、方块实体（下落方块）、载具速度谱、开源参照架构、调试方法论、体素刚体对照 |

`research/06` 回答"玩家每 tick 怎么动"；本文回答"**谁在什么时候驱动它**、**非玩家实体怎么动**、
**出了问题用什么工具观测**、**以及我们不做哪种物理**"。

### 0.2 可信度分级（沿用 `research/05 §0`）

| 级 | 含义 | 本文中的来源 |
|---|---|---|
| **A** | 官方/一手：项目仓库、论文、官方 wiki 原始页面 | Minestom 源码、Carpet 源码、IJCAI 论文页、minecraft.wiki 原始页 |
| **B** | 社区一手：mcpk.wiki（Parkour 社区维护，含自制模拟器验证） | Tiers / Stepping / Ticks 页 |
| **C** | 二手/转述：视频、博客、搜索引擎摘要 | GDC Vault 页（讲者/标题已核，内容未逐帧核）、VS2 Wiki 索引 |

**标记为 C 的条目在写进规格前必须升级为 A/B。**

---

## 1. 时间调度层：Tick 与执行序

### 1.1 20 TPS 的硬事实

- 1 game tick = 50 ms，20 tick/s；一个游戏内日 = 24000 tick = 20 分钟。[[MW:Tick]](https://minecraft.wiki/w/Tick)
- **绝大多数动作按 tick 计数计时，不按真实时间**：TPS 掉下去时，游戏内动作是"变慢"而不是"丢帧"。
  这条对 OpenCraft 的含义是——**逻辑必须完全跑在固定步上，不能把 `dt` 传进玩法逻辑**。
- 红石 tick = 2 game tick（10 rt/s）；随机刻（random tick）由 `randomTickSpeed`（默认 3）驱动，
  每 tick 从每个 16³ subchunk 中随机抽 3 个方块；**间隔中位数 47.30 s，均值 68.27 s**。
- 计划刻（scheduled tick）：JE 分 block tick 与 fluid tick，block tick 按 **priority 再按调度顺序**
  执行（中继器 -3/-2/-1，比较器 -1，其余 0）；**每 tick 上限 65536**。

> ⚠ **来源冲突（记为待证）**：`research/06 §1.1` 写"跳跃冷却 10 tick"。该数值来自 Parkour 社区口径，
> 本次核验未在官方 wiki 找到对应条目。**在写进 `docs/01` 前须补证**，否则保持"未定义"。

### 1.2 JE 主循环执行序（A 级，逐条来自官方 wiki）

每个 game tick 的顺序（节选与物理相关的部分）：

```
1. 若落后预期时间 ≥2s，重置 next tick time          ← 落后保护
2. 计算 next tick time / 递增 tick count
3. tick / load 函数标签
4. 每 20 tick 向玩家发一次时间
5. 逐维度（Overworld → Nether → End → 自定义）：
   a. 世界边界 / 天气 / 昼夜 / 睡觉 / scheduled 命令
   b. 执行 scheduled block ticks → scheduled fluid ticks
   c. 突袭逻辑
   d. 更新区块加载等级
   e. **随机顺序**遍历区块：刷怪 → 冰雪融化 → random ticks
   f. 发方块变更给玩家 / POI / 卸载区块 / 方块事件 / 龙战
   g. **逐非乘客实体**：despawn 检查 → tick 实体 → tick 乘客
   h. tick 方块实体（block entities）
   i. 处理 game events
6. 发包、处理收包
7. 每 6000 tick 自动保存
8. 跑挂起任务，等下一 tick
```

**可提取的架构事实（非表达，可自由对齐）：**

1. **区块遍历是随机顺序**（5e），而实体遍历是线性（5g）。前者的随机性是刻意的均匀化策略
   （避免固定顺序导致某些区块长期抢占），后者意味着**实体 tick 顺序必须被视为确定的**——
   否则物理回放不可复现。
2. **乘客在其载具之后 tick**（5g 的 "tick entity → tick passengers"）：载具先算，乘客继承。
   这条决定了船/矿车/坐骑的实现顺序，是 M2 载具卡的地基。
3. **方块实体在实体之后 tick**（5h）：漏斗等逻辑看到的是**本 tick 已更新的实体位置**。
4. **落后 ≥2s 直接重置时钟**（步骤 1）：即"宁可丢时间，不追赶"。这与 OpenCraft
   `TickClock::kMaxCatchUpTicks = 5`（丢 tick 而非排队）是**同一策略的两种实现**——
   见 §7.1，本仓策略更激进但方向一致，无需改。

### 1.3 Turn Tick：转向延迟（B 级）

mcpk.wiki [[Ticks/en]](https://www.mcpk.wiki/wiki/Ticks/en)：

> 鼠标移动**不绑定 tickrate，而绑定帧率**；但游戏为移动计算保留了一份玩家朝向副本，**每 tick
> 更新一次**，更新的那一刻叫 **turn tick**。玩家无法控制 turn tick 的发生时机，这对"转向跳"
> 影响严重：同样的平滑转向序列，结果轨迹可能差别很大。

**这是 `research/06` 完全未覆盖的一条**，且对 OpenCraft 有直接后果：

- 本仓 `InputState::yaw` 是**每 tick 一个标量**，客户端用 `glfwGetKey` 轮询 + 鼠标位移累积。
  这意味着：**渲染用的相机角（每帧）与物理用的 yaw（每 tick）天然是两个值**——已隐式符合
  turn tick 语义，但是**偶然而非设计**。
- 建议在 T-D7 或网络卡里显式化：物理只读取"tick 开始时的 yaw 快照"，相机角用于渲染插值。
  否则加客户端预测后会出现"转向比现在灵敏"的观感回归（因为预测会用帧率角）。
- Parkour 社区的规避手段（每 50 ms 瞬间转向到目标角）说明：这是**可观测行为**，对齐基准时应保留。

### 1.4 下落方块（Falling Block）：实体化的重力方块（A 级）

[[MW:Falling_Block]](https://minecraft.wiki/w/Falling_Block) 可提取数值事实：

| 项 | 值 |
|---|---|
| 碰撞盒 | 0.98 × 0.98 × 0.98（比整方块略小，避免与相邻方块卡边） |
| 重力 | **−0.04 /tick²**（玩家是 0.08，即**一半**） |
| 阻尼 | ×0.98（水平与垂直同为 0.98；玩家水平是 0.91×S） |
| 落地判定 | 底面中心落在可替换方块、且其下方块可支撑 → 变回方块；否则破碎掉落 |
| 超时 | 存在超过 **600 tick（30 s）** 自动销毁并掉落；在 Y=−64 或超出建筑高度时阈值降为 100 |
| 伤害 | 铁砧造成坠落伤害（随下落距离增长）；沙/砾石等不造成 |

**对本仓的意义**：下落方块是 M2+ 必然会做的实体类型（沙/砾石/混凝土粉末/铁砧）。它的参数
**与玩家完全不同**（重力一半、水平阻尼 0.98 而非 0.91），是一块很好的"物理参数必须 per-entity-type
而非全局单例"的**强制压力测试**。见 §7.3 建议。

---

## 2. 权威侧参照实现：Minestom / Glowstone

### 2.1 为什么值得看

OpenCraft 的关键架构决策之一是"**服务端权威从第一天做起**"（`docs/03 §1`、`research/03 §8`）。
Minecraft 客户端的物理是不可信的，因此**可参照的一手实现只存在于服务端项目**。三个候选：

| 项目 | 许可证 | 状态 | 可用性评价 |
|---|---|---|---|
| **Minestom** | **Apache-2.0** | 活跃（2026-09 仍更新） | ✅ **首选**。无混淆、自研代码、包名结构清晰 |
| Glowstone | MIT 系（GitHub 判为 NOASSERTION，实际正文为 MIT 文本） | 活跃度低 | ⚠ 历史包袱重（继承自 Lightstone，2010 年起），架构参考价值低于 Minestom |
| Carpet Mod | MIT | 活跃 | ✅ 但它是**调试工具**而非实现，见 §3 |

> ⚠ **合规提示（重要）**：Minestom 是 Apache-2.0，**允许我们阅读并在其架构思想上学习**，但其代码
> 若被复制进 OpenCraft，需在 `NOTICE` 中署名并保留许可（Apache-2.0 §4）。本仓代码许可待 PM 定稿
> （`docs/04` 列为 GPL-3.0 或 Apache-2.0/MIT）——**若最终选 GPL-3.0，Apache-2.0 代码可以并入
> （Apache-2.0 与 GPLv3 单向兼容），反之 GPL 代码不可并入 Apache 项目**。定稿前不要复制代码。
> 本文只提取**架构与数值事实**，不复制任何代码。

### 2.2 Minestom 的物理层结构（A 级，读源码所得）

包 `net.minestom.server.collision` 的文件清单本身就是一个**物理层职责划分的参考答案**：

| 类型 | 职责 |
|---|---|
| `PhysicsUtils` | 顶层：`simulateMovement(...)` + `updateVelocity(...)` |
| `CollisionUtils` | `handlePhysics(...)`：对区块内所有相关方块做 sweep |
| `EntityCollision` | 实体↔实体（用 `EntityTracker` 做宽相） |
| `Shape` / `ShapeImpl` | **方块碰撞形状抽象**（不止整方块） |
| `BoundingBox` | AABB |
| `SweepResult` | 扫掠结果：完成比例 `res` + 法线 + 命中的 shape / 方块坐标 |
| `PhysicsResult` | 一步物理的完整输出（record，16 个分量） |
| `Aerodynamics` | **per-entity 三元组**：`(gravity, horizontalAirResistance, verticalAirResistance)` |
| `RayUtils` | 射线 |

#### 2.2.1 三个可直接借鉴的设计

**(a) Aerodynamics 作为参数对象（不是全局常量）**

```
Aerodynamics(gravity, horizontalAirResistance, verticalAirResistance)
```

每个 `EntityType` 提供默认值（`EntityType.horizontalAirResistance()` 委托给注册表），
实体可覆盖（`Entity.setAerodynamics`）。携带三个 `with*()` 变体做不可变派生。

→ **这与 §1.4 下落方块的需求完全吻合**，也印证了 T-D7 要做 `entity-agnostic` 的方向是对的。
本仓目前是 `PhysicsConfig` 单一结构体（`ground_drag/air_drag/gravity/vertical_drag` 全局共用），
M2 加船/矿车/生物时必然要改成 per-entity-type。

**(b) 阻尼在位移之后施加，且区分"位置是否真的变了"**

`updateVelocity` 的语义（自撰伪代码，非源码）：

```
若本 tick 位置没变：
    → 水平速度不动，垂直只按 gravity × verticalAirResistance 更新
否则：
    drag = onGround ? 脚下方块.friction() × horizontalAirResistance : horizontalAirResistance
    vx *= drag ; vz *= drag
    vy = (vy − gravity) × verticalAirResistance      // 先减重力，再乘阻尼
    三分量各自做 |v| < EPSILON → 0                    // EPSILON = 1e-6
```

三点与 `research/06` 独立推出的结论**互相印证**：
- "**先减重力再乘阻尼**"——`research/06 §4.1` 的递推式完全一致；
- "**地面水平阻尼 = 方块摩擦 × 空气阻力**"（而不是二选一）——这就是 T-D7 要迁移的
  `0.91 × S` 模型的参数化形式，且它把"方块摩擦"和"实体空气阻力"**乘性解耦**，比 MC 的
  `0.91 × S` 硬编码更干净；
- "**位置没变则水平速度不动**"——一条 `research/06` 未提及的短路，作用是贴墙时不会因
  反复乘阻尼而"抖动"。本仓目前无此短路，属**可选加固**（非缺陷）。

⚠ 注意其截断阈值 `EPSILON = 1e-6` **远小于** MC 的动量阈值 0.005/0.003。**不要照搬**——
那会导致 `research/06 §4.4` 论证的 1.8/1.9 跳高差消失。这是"开源实现 ≠ 原版行为"的典型例子。

**(c) 结果对象化（PhysicsResult）**

一次物理步返回 16 个分量：新位置、新速度、onGround、X/Y/Z 三轴碰撞标志、原始 delta、
碰撞点数组、命中的 shape 数组、shape 所在方块坐标、是否有碰撞、最终接受的碰撞比例。

→ 本仓 `step_player` 目前只写回 `PlayerState`，碰撞信息只有 `collided_horizontally` 一个 bool。
`PhysicsResult` 这套字段是**十余种玩法的输入**（摔落音效、踩踏压力板、撞墙停疾跑、梯子、
蛛网、蜂蜜块减速……）。**建议在 T-D7 一并引入一个精简结果结构**，否则 M2 每加一个交互都要回改
`step_player` 签名（`docs/05` 强调的接口冻结纪律会被反复打破）。

#### 2.2.2 宽相：实体碰撞的搜索半径

`CollisionUtils.checkEntityCollisions` 的注释给出一条工程数值：扩展半径取
**能碰撞的最大实体盒的"底部中心到上角"距离**，玩家为 `(0.3² + 0.3² + 1.8²)^(1/3) ≈ 1.51`。

→ 可直接用作本仓 M2 实体宽相的初始参数（注意其用立方根而非欧氏范数，是刻意的保守上界估计）。

### 2.3 Glowstone

MIT 系许可，但架构继承自 2010 年的 Lightstone，**代码组织参考价值低于 Minestom**。
唯一值得记的事实：它是**纯净室**立场的长期项目（不依赖 Mojang 代码），这本身就是"不反编译也能
做出可玩服务端"的存在性证明。本轮**不深入**，`research/03 §8` 已有的开源项目经验教训不重复。

---

## 3. 调试与观测：Carpet Mod 的方法论

### 3.1 定位纠正

用户清单把 Carpet 描述为"内部调度探针与调试引擎"——**方向正确，但范围需修正**：

- Carpet 是 **Fabric 侧的 mod**（MIT），通过 **mixin 注入**原版服务端，**不是独立实现**。
- 它的价值在本项目中是**方法论**（怎么观测一套 tick 系统），不是代码（我们无法用 mixin）。

### 3.2 可提取的工具清单（A 级，读源码/README 所得）

| 设施 | 源码位置 | 作用 |
|---|---|---|
| `/tick warp` | README 主推功能 | 以机器最快速度快进 N tick：把"几小时"的观测压到"几分钟" |
| `/profile` | `carpet/commands/ProfileCommand.java` | `healthReport(ticks)` / `healthEntities(ticks)`，ticks ∈ [20, 24000] |
| `CarpetProfiler` | mixin 埋点 | 分 section 计时；`MinecraftServer_tickspeedMixin` 在 run loop 里插桩；**"Autosave" 也是一个独立 section** |
| `/log` | README | 实时输出 mobcap / TPS |

### 3.3 对 OpenCraft 的可执行建议（低优先级，但成本极低）

1. **快进模式（对应 `/tick warp`）**：本仓已有 headless 黄金回放
   （`tests/test_physics_golden.cpp`），缺的是"**一次跑 N tick 不看渲染**"的批量模式。
   建议在 M2 第一张物理相关卡里加一个 `--ticks=N --headless` 开关。**这是 Carpet 最高性价比的
   一条经验**——它把"几小时的农场测试"变成"几分钟"，对摔落伤害、饥饿耗竭、刷怪密度这类
   长周期平衡的调参是刚需。
2. **分 section 计时（对应 `CarpetProfiler`）**：`docs/03 §9` 已列 Tracy。**此处不新增依赖**，
   只建议：在 `TickClock` 层加一个**极简的 per-section 累加器**（物理/光照/网格化/存盘），
   不加第三方库。注意 Carpet 把 **Autosave 单独计一节**——本仓 T-D3 已知"快照写盘会阻塞
   主线程"，把它单列出来能让这个债务**从定性变成定量**。
3. **`/log` 等价物**：HUD 已有（T009）。不重复。

---

## 4. 体素刚体与破坏物理：VS2 与 Teardown 对照系

> **本节是"我们不做什么"的论据，不是路线图。** OpenCraft 对齐的是 MC JE，而 MC JE
> **没有刚体动力学**（船是 AABB + 特殊移动逻辑，不是 6-DoF 刚体）。本节的作用是防止
> 后期有人拿"体素游戏应该有多刚体"来论证改造现有物理层。

### 4.1 Valkyrien Skies 2（VS2）

| 事实 | 值 | 来源/核验方式 |
|---|---|---|
| 仓库 | `ValkyrienSkies/Valkyrien-Skies-2` | GitHub API |
| **许可证** | **LGPL-3.0** | GitHub API `license.spdx_id` |
| 同组织其他仓库 | Eureka / Clockwork / Kelvin / VS-Control 均为 **Apache-2.0**；Addon-Template 为 LGPL-2.1 | GitHub API |
| 目标 MC 版本 | 1.20.1（`gradle.properties`） | 源码 |
| 物理后端 | 配置项 `ConfigPhysicsBackendType`，默认值 **`KRUNCH_CLASSIC`**；可通过 `/valkyrienskies backend engine krunch` 切换 | `BackendCommand.kt` |
| Krunch 可用性 | 运行时探测开关 `KrunchSupport.isKrunchSupported` | `KrunchSupport.kt` |

⚠ **需要修正的两处用户表述**：
1. "VS2 是 Krunch/Jolt/PhysX 的集成"——核验只见 **Krunch**（默认后端）。`Jolt` / `PhysX`
   在仓库中**未找到作为后端的证据**。此项标为**未证实**。
2. 用户给的 "VS2 官方 Wiki" 链接是 `wiki.valkyrienskies.org`；仓库 README 实际指向
   **`docs.valkyrienskies.org`（开发者 wiki）** 与 `wiki.valkyrienskies.org（用户 wiki）`，
   两者并存。文档源码库 `ValkyrienSkies/ValkyrienSkiesDocs` 存在但**无许可证声明**——
   **引用其文本前须确认授权**（`docs/04` 红线 4 的类推）。

**架构上唯一需要记住的一点**：VS2 的核心是"**船（Ship）作为子世界坐标系**"——飞船内部的方块
存于一个独立坐标空间，渲染与碰撞通过变换矩阵映射到主世界。这是**多刚体**问题的标准解法，
与 OpenCraft 的单世界 + 区块存储是**不同问题域**。

### 4.2 Dennis Gustafsson / Teardown / Voxagon

| 事实 | 值 |
|---|---|
| 博客 | `blog.tuxedolabs.com` **已 301 重定向至 `blog.voxagon.se`**（A 级，实测） |
| 作者 | Dennis Gustafsson（Teardown / Smash Hit / Sprinkle，Tuxedo Labs） |
| GDC 讲稿 | **"Physics for Game Programmers: Destruction in Smash Hit"，GDC 2015**，`gdcvault.com/play/1022200`（A 级：标题/讲者/会议已核；**内容未逐帧核，标 C**） |

⚠ 用户描述该讲稿为"连续体素/程序化破碎物理算法"——**核验结果是 Smash Hit 的破坏系统**
（玻璃破碎），**不是 Teardown 的体素**。Teardown 是更晚的项目。用途应改为"**破坏效果与
刚体分解的设计思路**"，不应作为体素物理的直接参考。

博客中**与本项目相关的两篇**（标题与摘要已读，正文未逐篇精读）：
- *The Spraycan*（2020-12-03）：Teardown 用 **8-bit 调色板**表示体素材质（最多 255 种），
  每体素 1 字节；材质不仅定义颜色，还带**粗糙度/自发光/反射率/物理材质类型（木/金属/植被）**；
  所有调色板打包进一张 256 宽纹理常驻 GPU，破碎碎片继承原调色板。
  → **与本仓 T005 图集 + T003 调色板是同构思路**，可作为"调色板携带物理属性"的旁证
  （本仓调色板目前只带渲染信息）。
- *The unlikely story of Teardown Multiplayer*（2026-03-13）：承认"**同步物理到网络本来就难**，
  叠加完全动态可破坏世界 + 完整 mod 支持后，长期被视为不现实"。
  → 这是**给 M3 网络同步的警示**：如果哪天要做可破坏/可推动的方块，物理同步难度会跳一个量级。
  记入风险，不进路线图。

### 4.3 结论：三条边界

1. **不做多刚体**（VS2 域）。船/矿车用 MC 式 AABB + 专用移动逻辑，不用 6-DoF 求解器。
2. **不做程序化破碎**（Teardown 域）。方块破坏 = 整方块消失 + 粒子，不产生刚体碎片。
3. **可借鉴的只有两处工程手法**：调色板携带物理材质类型（§4.2）、以及把"材质"作为
   物理参数的一等输入（对应 T-D7 的 `slipperiness` 钩子）。

---

## 5. 离散动力学的学术视角：Project Malmo

### 5.1 来源核验

| 项 | 值 |
|---|---|
| 论文 | *The Malmo Platform for Artificial Intelligence Experimentation*，**IJCAI 2016** |
| 作者 | Matthew Johnson, Katja Hofmann, Tim Hutton, David Bignell（Microsoft Research） |
| 摘要页 | <https://www.ijcai.org/abstract/16/643> ✅ 可达，标题/作者/摘要已核（A 级） |
| PDF | <https://www.ijcai.org/Proceedings/16/Papers/643.pdf> ✅ 可达（需 PDF 解析，本轮未逐页精读，**正文标 C**） |
| 参考实现 | `Microsoft/malmo`，**MIT**，**已归档**（archived=true，最后 push 2025-09） |

摘要原文要点（转述，非逐字引用——`docs/04` 红线 4）：Malmo 是构建在 Minecraft 之上的 AI 实验
平台，为 AI 研究提供抽象层，支持从导航、生存到协作与问题求解的多种实验场景，在 IJCAI 以
开源形式公开发布。

### 5.2 对 OpenCraft 的三条可迁移认识

1. **离散时间 + 非线性动力学 = 难学的环境**，这是被论文明确作为"研究价值"提出的。反向印证了
   `research/06 §11` 的结论："**数值可以照搬，但时序/几何细节才是手感的真正来源**"——
   如果它只是几个常数，就不会成为 AI 研究的挑战环境。
2. **观测窗口 ≠ 状态**：Malmo 给 agent 的是"观测"（observation）而非完整状态。对本仓的含义是
   M3 网络协议设计时，**客户端拿到的应该是观测而非世界状态**，与"服务端权威"一致。
   （`docs/03 §8` 已定服务端权威，此处只是强化。）
3. **确定性是可研究性的前提**：任何 AI/回放/回归测试都要求同输入同输出。本仓 T007 已把
   "确定性"写成验收标准（#5），`research/06` 也强调平台稳定性。**保持它**，不要为了性能引入
   多线程不确定序的物理（`docs/03 §2` 的线程模型须保证物理在单一确定序上跑）。

### 5.3 不做的事

Malmo 是**研究平台**（带 agent 抽象、任务 DSL、观测/奖励通道）。OpenCraft 是**游戏**。
不引入任何 Malmo 的 API 概念。仅借用其"离散环境难以建模"这一论断作为**保持数值 fidelity 的论据**。

---

## 6. 来源核验与许可证台账

### 6.1 链接可达性核验（2026-09-15 实测）

| # | 来源 | 类型 | HTTP | 判定 |
|---|---|---|---|---|
| 1 | `www.mcpk.wiki/wiki/Vertical_Movement_Formulas` | wiki | 200 | ✅ 已在 `research/06` 使用 |
| 2 | `www.mcpk.wiki/wiki/Horizontal_Movement_Formulas` | wiki | 200 | ✅ 已在 `research/06` 使用 |
| 3 | `www.mcpk.wiki/wiki/Movement_Formulas` | wiki | 200 | ✅ 已在 `research/06` 使用 |
| 4 | `www.mcpk.wiki/wiki/Ticks/en` | wiki | 200 | ✅ **本文 §1.3 新增使用** |
| 5 | `minecraft.wiki/w/Tick` | 官方 wiki | 200 | ✅ **本文 §1.1–1.2 使用** |
| 6 | `minecraft.wiki/w/Falling_Block` | 官方 wiki | 200 | ✅ **本文 §1.4 使用** |
| 7 | `minecraft.wiki/w/Transportation` | 官方 wiki | 200 | ✅ **本文 §7.4 使用** |
| 8 | `github.com/gnembon/fabric-carpet` | 仓库 | 200 | ✅ §3（MIT） |
| 9 | `github.com/Minestom/Minestom` | 仓库 | 200 | ✅ §2（Apache-2.0） |
| 10 | `github.com/GlowstoneMC/Glowstone` | 仓库 | 200 | ⚠ §2.3（MIT 文本，仅作存在性参考） |
| 11 | `github.com/ValkyrienSkies/Valkyrien-Skies-2` | 仓库 | 200 | ✅ §4.1（LGPL-3.0） |
| 12 | `ijcai.org/abstract/16/643` + PDF | 论文 | 200 | ✅ §5（摘要 A / 正文 C） |
| 13 | `gdcvault.com/play/1022200` | 视频 | 200 | ⚠ §4.2（**标题已纠正为 Smash Hit**） |
| 14 | `blog.tuxedolabs.com` | 博客 | 301 | ⚠ **→ `blog.voxagon.se`**（200） |
| 15 | `physicsmod.com` | 商业站 | **000 / 403** | ❌ **不可达**（Cloudflare 拦截 + 域名停放页）。**本文不使用** |

> **#15 的处理**：Physics Mod 站点当前不可达，且其闭源商业属性与本项目无关。
> **从知识库中剔除**，若后续需要粒子/布料/破碎效果，改用 GDC 讲稿与 Voxagon 博客等一手材料。

### 6.2 许可证台账（引用第三方实现前必读）

| 项目 | 许可 | 能否读架构 | 能否复制代码 | 备注 |
|---|---|---|---|---|
| Minestom | Apache-2.0 | ✅ | ⚠ 需在 `NOTICE` 署名 | 与 GPL-3.0 单向兼容 |
| fabric-carpet | MIT | ✅ | ✅（宽松） | 需保留版权声明 |
| Glowstone | MIT 文本（SPDX: NOASSERTION） | ✅ | ✅ | GitHub 无法自动识别，人工确认正文为 MIT |
| **VS2** | **LGPL-3.0** | ✅ | ❌ **动态链接限定** | **不引入依赖**；只读思想 |
| VS 文档库 | **无声明** | ⚠ | ❌ | 引用文本前须取得授权 |
| Microsoft/malmo | MIT（**已归档**） | ✅ | ✅ | 仅论文论据，不引代码 |
| JoltPhysics | MIT | — | — | 仅登记：**当前无计划引入** |
| NVIDIA PhysX | BSD-3-Clause | — | — | 仅登记：**当前无计划引入** |

**红线复核（`docs/04`）**：本轮全部来源为公开 wiki / 开源仓库 / 公开论文，**未触碰**
反编译产物、MCP/Yarn 映射、MC 资产、wiki 逐字文本。✅ 通过。

---

## 7. 对 OpenCraft 的映射：现状对照与建议

> 以下每条都标注了**归属卡**。本文不改规格，只出建议；是否落地由 PM 裁决。

### 7.1 调度层：现状已经合格，补两处文档

**现状**：`TickClock`（`engine/core/include/opencraft/core/tick_clock.hpp`）20 TPS、累加器、
`kMaxCatchUpTicks = 5`（丢 tick 而非排队）、`alpha()` 插值、`dropped_ticks()` 计数。

**对照**：§1.2 步骤 1"落后 ≥2s 重置时钟"证明 MC 官方也是"**不追赶**"策略。本仓更激进（5 tick
上限），但对单机体验更友好（250 ms 内的卡顿不丢时间）。**结论：不改。**

**建议（文档级，成本近乎零）**：
- 在 `tick_clock.hpp` 注释里补一句"与 JE 主循环'落后 2s 重置'同策略"，让后来者知道这是**有意的**
  而不是遗漏了追赶。→ 归入下一张碰 core 的卡。
- §1.2 步骤 5g"**乘客在载具之后 tick**"应写进 `docs/03 §6`（当前只有一句 "AABB + swept
  collision"）。这是 M2 载具卡的**接口冻结项**，先写下来成本最低。→ **PM 直接改 `docs/03 §6`。**

### 7.2 碰撞与结果对象：建议进 T-D7

**现状**：`step_player(state, input, world, config)` 只写回 `PlayerState`；
碰撞对外只暴露 `PlayerState::collided_horizontally` 一个 bool；`IBlockSource` 只有
`solid_at` / `liquid_at`（**整方块 + 是否液体**，无形状概念）。

**参照**：§2.2.1(c) Minestom `PhysicsResult`（16 字段）+ `Shape` 抽象。

**建议**（**归属 T-D7**，因为它已经在动这条管线）：

| 项 | 建议 | 理由 |
|---|---|---|
| `IBlockSource` | 增加"该方块的碰撞形状/摩擦"查询能力（先只返回整方块或空，接口先立） | T-D7 的 `slipperiness` 钩子与 §1.4 下落方块都需要；`research/03 §6.3` 已要求"方块碰撞体积数据驱动" |
| 结果对象 | `step_player` 增加可选出参 `MoveResult{ hit_x, hit_y, hit_z, landed, fall_distance, collided_shape }` | 避免 M2 每加一个交互就改一次签名 |
| 摩擦 | 采用 Minestom 的**乘性解耦**形式 `drag = block.friction × entity.airResistance`，而不是硬编码 `0.91 × S` | 更干净，且天然支持"同一实体在不同方块上"与"不同实体在同一方块上"两个维度 |

⚠ **不要照搬 Minestom 的 `EPSILON = 1e-6`**（§2.2.1(b)）——会破坏 `research/06 §4.4` 论证的
1.8/1.9 跳高差。本仓动量阈值若引入，须取 MC 的 0.005 / 0.003 口径并写成 ⚖。

### 7.3 参数作用域：T-D7 必须一次做到位

**论据**：§1.4 下落方块（重力 0.04 = 玩家一半、阻尼 0.98 ≠ 玩家 0.91）+
§2.2.1(a) Minestom 的 per-entity `Aerodynamics` + §7.4 载具速度谱（船/矿车/坐骑各不同）。

**结论**：`PhysicsConfig` 当前是全局单例结构（`walk_speed / air_drag / gravity / vertical_drag`
平铺），**M2 一定会撞墙**。T-D7 卡面已写 `entity-agnostic`——**请确保它的含义包含
"参数按实体类型实例化"，而不只是"函数不写死玩家"**。这是本轮调研中**最紧迫的一条**：
STATE.md 也已把它标为"唯一的地基决策，拖到 M2 每张生物/载具卡都要改一遍"。

### 7.4 载具与游泳速度谱（A 级数值，供 M2 建卡取用）

来源 [[MW:Transportation]](https://minecraft.wiki/w/Transportation)（`?action=raw` 抓取，A 级）。
**仅登记，不写入 `docs/01`**——等 M2 相应卡落地时再取用。

| 类别 | 条件 | 速度 |
|---|---|---|
| 步行 | 平地 / 斜 45° | 4.317 / 4.405 m/s |
| 疾跑 | 平地 | 5.612 m/s（**与 `docs/01 §2` 一致 ✅**） |
| 疾跑跳 | 平地 / +1 落差 / 2 格高通道 | 7.127 / 7.508 / 9.732 m/s |
| 潜行 | 平地 / 45° | 1.3 / 1.83 m/s（`docs/01` 写 1.295，差 0.4%，**注：口径可能不同，建卡时核**） |
| 矿车 | 动力轨 / 1/4 坡 / 熔炉矿车 | 8.0 / 7.1 / 4.0 m/s |
| **船** | 平地 / 静水 / **冰** / **蓝冰** | 2.00 / 8.0 / **40.0** / **72.73** m/s |
| 船（反向/黏液/蜂蜜） | | 0.24 / 1.18 / 1.06 m/s |
| 坐骑 | 猪 / 骆驼(走-奔) / 马(均-最快) / 驴骡 / 僵尸马 | 2.42 / 3.79-8.0 / 9.49-14.57 / 7.38 / 8.43 m/s |
| 游泳 | 水面 / 水下 / **疾速游泳** / 海豚恩典 | 2.20 / 1.97 / **3.918** / 9.800 m/s |
| 鞘翅滑翔 | 52° 俯冲 / 90° 俯冲 | 67.3 / 78.4 m/s |

**两个立即可用的发现**：

1. **船在冰上 40 m/s、蓝冰 72.73 m/s** —— 这是本仓 `max_substep = 0.5`（`physics_config.hpp`）
   的**真实压力测试**：72.73 m/s = 3.64 b/tick，**一 tick 位移 3.64 格**，需要 ≥8 个子步。
   本仓按 `ceil(max_component / 0.5)` 动态算子步数，**能覆盖**（3.64/0.5 ≈ 8 步），✅ 设计已安全。
   但**值得加一条回归测试**：注入 3.64 b/tick 水平速度，断言不穿墙。
2. **疾速游泳 3.918 m/s** vs 本仓 `water_speed_mult = 0.5`（即步行 4.317 × 0.5 ≈ 2.16 m/s）。
   本仓当前值对应"普通游泳 2.20" ✅ 合理，**但缺疾速游泳档**。→ 归入 T-D1 gap 清单中的
   "游泳"项（原 18 项 gap 之一，M2 归属）。

### 7.5 调试设施：两条低成本建议

| 建议 | 对应来源 | 归属 |
|---|---|---|
| 加 `--ticks=N --headless` 批量快进（类比 `/tick warp`） | §3.2 | M2 第一张物理卡（或 T-D7 顺手） |
| `TickClock` 层加极简 per-section 计时（物理/光照/网格化/**存盘**） | §3.2 | 与 T-D3（写盘阻塞）合并处理，能把定性债务变定量 |

### 7.6 明确不做（防止后期范围膨胀）

| 不做 | 依据 |
|---|---|
| 多刚体 / 6-DoF 船 | §4.3；MC JE 本身没有 |
| 程序化破碎 / 碎片刚体 | §4.3；Teardown 域 |
| 引入 Jolt / PhysX / Bullet | §6.2 仅登记；`docs/03 §9` 依赖表未列，**新增依赖须走 PM 审查** |
| 复用 VS2 代码（LGPL-3.0） | §6.2 |
| 引用 VS 文档库文本 | §6.2 无许可证声明 |

---

## 8. 参考文献

| # | 来源 | 级别 | 用途 |
|---|---|---|---|
| 1 | [MCPK Wiki — Ticks/en](https://www.mcpk.wiki/wiki/Ticks/en) | B | §1.3 turn tick |
| 2 | [MCPK Wiki — Tiers](https://www.mcpk.wiki/wiki/Tiers) | B | 跳高表交叉核对（1.8: 1.2492 / 1.9+: 1.2522，与 `research/06 §4.3` 一致 ✅） |
| 3 | [MCPK Wiki — Stepping](https://www.mcpk.wiki/wiki/Stepping) | B | 步高 0.6、碰撞序 Y-X-Z、"选水平位移最长的方案"；**T-D1 gap #5 自动上台阶的直接依据** |
| 4 | [Minecraft Wiki — Tick](https://minecraft.wiki/w/Tick) | A | §1.1–1.2 |
| 5 | [Minecraft Wiki — Falling Block](https://minecraft.wiki/w/Falling_Block) | A | §1.4 |
| 6 | [Minecraft Wiki — Transportation](https://minecraft.wiki/w/Transportation) | A | §7.4 |
| 7 | [Minestom (GitHub)](https://github.com/Minestom/Minestom) — `server/collision/*`, `server/entity/Entity.java` | A | §2.2 |
| 8 | [fabric-carpet (GitHub)](https://github.com/gnembon/fabric-carpet) — `ProfileCommand.java`, `MinecraftServer_tickspeedMixin.java` | A | §3.2 |
| 9 | [Glowstone (GitHub)](https://github.com/GlowstoneMC/Glowstone) | A | §2.3（存在性 + 许可） |
| 10 | [Valkyrien Skies 2 (GitHub)](https://github.com/ValkyrienSkies/Valkyrien-Skies-2) | A | §4.1（**LGPL-3.0**） |
| 11 | [GDC Vault — Physics for Game Programmers: Destruction in Smash Hit](https://www.gdcvault.com/play/1022200/Physics-for-Game-Programmers-Destruction) | C | §4.2（讲者 Dennis Gustafsson, GDC 2015；**标题已纠正**） |
| 12 | [Voxagon Blog](https://blog.voxagon.se/)（原 tuxedolabs.com，301 至此） | B/C | §4.2（*The Spraycan* / *Teardown Multiplayer*） |
| 13 | [Malmo @ IJCAI 2016 (abstract)](https://www.ijcai.org/abstract/16/643) / [PDF](https://www.ijcai.org/Proceedings/16/Papers/643.pdf) | A/C | §5 |
| 14 | [Microsoft/malmo (GitHub)](https://github.com/Microsoft/malmo) | A | §5（MIT，已归档） |
| 15 | ~~physicsmod.com~~ | — | ❌ **站点不可达（000/403），已剔除** |

**核验方法**：#1–3 用 MediaWiki API（`/w/api.php?action=parse`）拉取正文；#4–6 用 `?action=raw`
拉取维基原始文本；#7–10、#14 用 GitHub API（`repos/*`、`search/code`、`git/trees`）读取元数据与
源码；#11–13 用 HTTP 拉取并解析。**所有链接于 2026-09-15 实测可达性，结果见 §6.1。**

---

## 附录 A：本文产生的"待证 / 待裁决"清单

| # | 事项 | 现状 | 需要谁处理 |
|---|---|---|---|
| A-1 | `research/06` 的"跳跃冷却 10 tick"无官方来源 | 标为**待证**，不得写进 `docs/01` | PM：找官方来源或从规格删除 |
| A-2 | 潜行速度 1.295（`docs/01`）vs 1.3（wiki Transportation） | 差 0.4%，口径可能不同 | M2 建卡时核 |
| A-3 | VS2 后端是否含 Jolt/PhysX | 只见 Krunch，**标未证实** | 无需处理（不影响决策） |
| A-4 | Minestom `EPSILON = 1e-6` 与 MC 动量阈值冲突 | 已在 §2.2.1(b) 标"不要照搬" | T-D7 开发者须知 |
| A-5 | 本仓是否引入动量阈值（0.005/0.003） | 当前**无** | T-D7 裁决（影响跳高 1.2492 vs 1.2522） |
| A-6 | 船在蓝冰 72.73 m/s 的防穿透回归测试 | 分析表明 `max_substep` 可覆盖，**未实测** | 建卡时加测试 |

---

## 附录 B：本轮调研的方法论备注

1. **先核验再引用**：用户给的 15 条链接中，1 条不可达（剔除）、2 条描述与实测不符
   （GDC 讲稿标题、VS2 后端）、1 条已重定向（博客域名）。**若直接照抄清单写文档，会引入 4 处错误。**
2. **许可证先于技术**：VS2 是 LGPL-3.0、文档库无声明——这两条如果没先查，可能会出现"照着文档
   写实现"的合规事故。引用开源实现前**必须先过 §6.2 台账**。
3. **开源实现 ≠ 原版行为**：Minestom 的 `EPSILON = 1e-6` 就是反例。参照实现用于**学架构**，
   数值 fidelity 仍以 `research/06`（独立复现 + wiki 交叉核对）为准。
4. **"不做"也是结论**：§4.3 与 §7.6 明确了四条边界，目的是防止后期以"体素游戏应有 X"为由
   推翻现有物理层。
