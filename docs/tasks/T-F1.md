# 任务 T-F1：流体地基 + 水桶（阶段 1）

里程碑：M2 前置（内容/系统）　前置：**T-R1（调研已合入）**
基线：**243/243**。运行前请先跑基线确认。
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

把 OpenCraft 的水从「静态布尔标记」升级为**真正的 MC 流体**：建立并行流体层与计划刻调度，
实现元胞自动机转移规则与坡度寻路，让水按 5 tick/格扩散、遇落差优先流向落差口、
按能级渲染不同水位；并提供**水桶**作为入口——右键水面装水、右键地面把水倒在地上，
倒出的水当场按 MC 规则摊开。

用户诉求原话（2026-09-16）：

> "给物品栏加入一桶水，我可以倒在地上，呈现 Minecraft 中相同的流体效果。
> 这一步的目的是将 opencraft 中流体这个大方面的基座给打好。"

## 数值/规格依据

- **主要依据**：`/Users/happy/Desktop/opencraft/docs/research/10-mc-fluid-dynamics.md`（T-R1 产出）
  - §1 数据表示（流体状态与方块状态共存）
  - §2 调度模型（计划刻）
  - §3 元胞自动机转移规则（垂直优先、ΔL 衰减、下落柱）
  - §4 坡度寻路（**核心，必做**）
  - §8 对 OpenCraft 的落地建议（方案 B：并行流体层）
- **裁决**：`/Users/happy/Desktop/opencraft/docs/tasks/T-R1.ruling.md`（4 项，**全部与本卡相关**）
- **PM 已核验的硬数值**（`curl ?action=raw` 拉 wiki 原文，2026-09-16）：

| 项 | 值 | 来源 |
|---|---|---|
| 水平扩散 | 7 格 | `Water`：「7 blocks horizontally from a source block on a flat surface」 |
| 水流速度 | 1 格 / 5 game ticks | `Water`：「1 block every 5 game ticks, or 4 blocks per second」 |

### ⚠ 两条必须遵守的裁决（来自 T-R1.ruling.md）

1. **坡度搜索半径取 5（含起点）**，但**必须做成命名常量**并在代码/报告中写明依据，
   标注"待实机校准"。理由：wiki 自身有两套口径（`Fluid` 页 5/3 vs `Water` 页 "four or fewer"）。
   **校准前不得声称"与 MC 逐格一致"。**
2. **三个数值禁止使用**：水中重力 `0.005`、水/岩浆阻尼 `0.20 / 0.50`、水流推力 `0.014`
   —— T-R1 复核确认 wiki `Entity` 页**无对应行，无法溯源**（债务 T-D23）。
   本卡若需要该量级参数，**沿用本仓已核验的 `PhysicsConfig::water_*`**（T007 遗留），
   或实测后登记新来源。**不得**把它们当作"已对齐 MC"写进任何地方。

> 另：**桌面那份《Minecraft 流体白皮书》已被 PM 判定含错并降级为"素材"**
> （其坡度搜索半径 4/2 是错的，wiki 正文为 5/3）。**不要引用它取数值。**

## ★ 范围（严格，越界即返工）

**做（6 项）**：

1. **并行流体层**：与 `PaletteSection` 平行的 per-voxel 流体通道。
   布局建议（研究员 §8.2，采纳）：1 字节/体素 =
   `kind 2 bit | level 4 bit | - 1 bit | falling 1 bit`；`kind: 0=无, 1=水, 2=岩浆, 3=预留`。
   每 section 额外 4096 字节；复用 Chunk 现有的「空节不落盘」逻辑。
2. **计划刻调度队列 `FluidSim`**：照 `LightEngine` 的模式（`std::deque` +
   跨区块 deferred offer 重放，单线程、调用方串行化）。条目 = (世界坐标, 到期 tick)。
   `TickClock` 每推进一 tick 调用一次 `FluidSim::step()`。
3. **元胞自动机转移规则**：垂直下落优先、水平能级梯度衰减（ΔL：水 = 1）、
   下落柱（falling）行为。
4. **坡度寻路 BFS**（研究员明令"必须在阶段 1 做，否则视觉返工"）：
   搜索半径常量、落差口定义、多方向并列取舍、无落差时的回退。
   详见 research/10 §4.3–§4.6。
5. **水位渲染**：水按能级渲染不同高度（不再是整方块）。需改 T005 mesher。
6. **水桶入口**：快捷栏加水桶物品；右键水面装水、右键地面倒水（放置一个源头）。

**不做**（看到了也不要动）：
- ❌ **源再生 / 无限水**（§5，需 gamerule 与相邻源头计数）—— 单列后续卡
- ❌ **含水机制（waterlogging）**、**气泡柱**、**岩浆**、**水火相变**、**流动音效/粒子**
- ❌ 不实现物品系统（`game/common/` 目前**没有**物品层，只有 raycast/mining/placement）。
  水桶用**最小形态**：快捷栏一个"水桶"槽位 + 一个 `has_water` 布尔即可。
  **不要**借此搭完整 ItemRegistry——那是 M2 主线的独立卡。
- ❌ 不改 `docs/01` / `docs/03` 规格（PM 职权）

## 接口契约

### 冻结项（不得改动语义）

- `Chunk::kFormatVersion = 1`（`engine/voxel/include/opencraft/voxel/chunk.hpp:35`）
  —— 见下方「序列化」一节的处理要求。
- `IBlockSource::solid_at` / `liquid_at` / `shape_top_at` 的既有语义（T-D7 冻结的适配器契约）。
  **可**为流体层新增查询，但**不得**改变既有三个的行为。
- `PhysicsConfig::water_*`（`water_cruise_speed` / `water_drag` / `water_gravity` /
  `swim_up_accel` / `water_max_up_speed`）：本卡**可读**用于游泳物理，
  但**不得**为了流体方块而改它。

### 现有锚点（PM 已核实在 main 上存在）

| 你要接的地方 | 位置 |
|---|---|
| Chunk 与分节 | `engine/voxel/include/opencraft/voxel/chunk.hpp:25-126`（`kSectionSize=16`、`kSectionCount=24`、`kSectionVolume=4096`、`sections_`） |
| 可照抄的调度模式 | `engine/voxel/include/opencraft/voxel/light_engine.hpp:47-115`（`std::deque` + `record_offer()` + `forget_chunk()`） |
| 半透明渲染分桶 | `engine/render/src/mesher.cpp:11`（`is_translucent_block`，硬编码 9/11/12 = leaves/glass/water） |
| 水位判定的现状 | `engine/voxel/src/block_registry.cpp:37`：`register_block("water", {"Water", false, true, 100.0f, true})` |
| 快捷栏 | `game/client/src/main.cpp` 的 `kHotbarNames`（9 槽位） |
| 放置判定 | `game/common/src/placement.cpp`（`check_placement`，含 `is_replaceable`：空气与液体可替换） |

### 序列化（重要，别破坏旧档）

`Chunk::kFormatVersion` 现为 1。**不要直接改成 2 让旧档不可读。**
要求：
- 新增流体节时，让**无流体的区块**仍产出与 v1 **完全兼容**的字节流；
- 仅在含流体的 section 追加流体节，并提升版本（v2）；
- 反序列化须**同时接受 v1 与 v2**（v1 ⇒ 流体层全空）。
- 旧档可继续加载是**验收项**（见验收 6）。

### 命名常量要求

坡度搜索半径等"口径存疑"的参数一律做成**命名常量 + 注释说明依据与待校准状态**，例如：

```cpp
// docs/research/10 §4.2；取值依据 wiki Fluid 页正文 "up to 5 blocks away"。
// ⚠ wiki Water 页另一处写 "four or fewer blocks"，两者相差 1，
// 公开资料无法判定何者为 1.21.x 当前行为（债务 T-D22，待实机校准）。
constexpr int kSlopeSearchRadiusWater = 5;
```

## 允许触碰的文件/目录（白名单）

- `engine/voxel/**`（流体层 + `FluidSim`，主体）
- `engine/render/**`（水位渲染，仅必要时）
- `game/client/**`（水桶接线、快捷栏）
- `game/common/**`（仅当装水/倒水需要扩展放置判定时；**最小化**）
- `tests/**`（新增流体单元测试；需改 `tests/CMakeLists.txt`）
- `engine/voxel/CMakeLists.txt`（仅当新增 .cpp/.hpp 时，按现有模式加）

⚠️ **禁碰**：`STATE.md`、`docs/**`（`docs/tasks/T-F1.report.md` 是你**必须写**的报告，
属例外）、`engine/{core,physics,noise,net}`、`game/server/`、`cmake/`、根 `CMakeLists.txt`、
`.github/`、`assets/`。
**⚠ 尤其：`engine/physics/**` 一律不碰**（游泳物理本卡不动）。

## 验收标准（逐条可执行）

1. **扩散速度**：平地单源，水以 **1 格 / 5 tick** 向外扩散；最远 **7 格**。
   单测断言：t=5 覆盖 1 格、t=35 覆盖 7 格、t=40 仍为 7 格（不超界）。
2. **水位递减**：离源越远能级越低（ΔL = 1），水位高度随之递减；
   单测断言能级序列，非仅"有水/无水"。
3. **垂直优先**：下方可流时优先向下（下落柱 `falling=true`），不向四周摊。
4. **坡度寻路（关键，最易漏）**：
   - 崖边单源：水**优先流向落差口**，而非四向均摊（research/10 §4.6 有算例，可照它写断言）；
   - 搜索半径内无落差 ⇒ 回退为四向对称扩散；
   - **并列方向的取舍**须有明确规则并单测固定。
5. **水桶闭合**：右键水面 → 桶装满；右键地面 → 放置源头 → 水按上述规则摊开。
   实机：站在平地按右键，能看见水从落点向外逐格铺开（不是一帧出现）。
6. **旧档兼容**：用 v1 格式写出的存档（可现造一个）能被新二进制正确加载，流体层为空。
   单测或集成测试覆盖。
7. **无回归**：**243 存量测试全绿**。若影响黄金回放（`test_physics_golden.cpp`）
   或光照测试，须**逐 leg 交代旧值→新值**，并**先排查覆盖率回退**再谈数值漂移。
8. **性能**：单区块网格化仍 <5 ms（`docs/03 §10` 预算）。若水位渲染导致明显退步，
   在报告中如实报告数字，不要静默。
9. clang-format 无 diff（用 **CLT 的 17**：`/Library/Developer/CommandLineTools/usr/bin/clang-format`；
   **不要用 brew 的 23**）；测试名避免含 `[`。
10. 独立 worktree（根 `/Users/happy/Desktop/opencraft_worktree/`）；提交前缀 `taskT-F1:`。
11. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-F1.report.md`，
    对话中**只输出简短版 + 该路径**，用 **txt 代码块**包裹（`docs/05` §2 规则 4）。
12. **实机证据必须给**（这是本项目最贵的一课）：截图序列或逐帧日志证明水在**逐格铺开**
    而非一帧出现。⚠️ 若用脚本注入按键，**必须 HID 层**，禁用 `osascript`。
    ⚠️ 本机 HID 注入**时灵时不灵**，且按键约 9 tick 后会被 GLFW 因失焦清除
    （`docs/05` §3.1，债务 T-D21）——把决定性场景放在生效窗口内，别指望"按住走很远"。

## 已知风险与提示（每条都踩过）

- **构建不要接管道**（`| tail` 吞退出码）；判成败用 `cmd > log 2>&1; echo $?` 或搜 `error:`。
- **绝不要把 `FETCHCONTENT_BASE_DIR` 指向主仓库 `build/_deps`**（PM 已两次因此挂掉主仓构建）。
  worktree 里**不设**该变量，让它自行 configure（约 75–90 s，需联网拉依赖；
  若网络受限先告知 PM）。
- 可执行文件在 `build/opencraft`。
- **合规红线**：只记数值/公式/来源 URL/访问日期；**禁止粘贴任何反编译源码片段**；
  禁止逐字复制 wiki 文本。
- **不得直接改状态与规格**（P-001）：建议一律以建议表写进报告交 PM 落盘。
- **「已实现」≠「可达」**：单测全绿**不代表**实机真的会流。必须给实机证据（验收 12）。
  特别留意：流体层若只加在 `Chunk` 而**客户端 `WorldSource` 没接查询**，
  就会重演 T-D8/T-D17 那类"机制写了但实机永不触发"的事故。
- **主动纠正记功**：发现卡面错误、数值对不上、接口不合理，直接写进报告。
  本项目历史上 5 次由执行方纠正 PM，这是被鼓励的。

## 附：本卡与相邻卡的关系

| 机制 | 本卡 | 后续 |
|---|---|---|
| 水位表达 / 调度 / 转移 / 坡度寻路 | ✅ 做 | — |
| 源再生（无限水） | ❌ | 单列（需 gamerule + 相邻源头计数） |
| 岩浆 / 相变 / 含水 / 气泡柱 | ❌ | M4+ |
| 完整物品与库存系统 | ❌（只做水桶最小形态） | M2 主线独立卡 |
| 游泳物理（浸没动力学） | ❌ 不动 | 债务 T-D12（数值待溯源，见 T-D23） |
