# 03 · C++ 技术架构

技术依据：`research/03-voxel-engine-tech.md`。目标：桌面三平台（Windows/macOS/Linux）、单机+局域网多人、20 TPS 确定性逻辑。

## 1. 总体形态

```
opencraft/
├── engine/            # 无游戏知识的库层
│   ├── core/          # 数学、AABB、tick 时钟、任务系统、日志、序列化
│   ├── voxel/         # 区块存储、调色板、网格化、光照、区域文件 IO
│   ├── noise/         # 噪声包装（FastNoiseLite）+ fBm + 域扭曲
│   ├── render/        # RHI 抽象 + OpenGL 4.3 后端、着色器、图集
│   ├── physics/       # swept AABB、体素碰撞、射线（DDA）
│   └── net/           # 消息分帧、压缩、可靠/不可靠通道
├── game/              # 游戏层：方块注册表、物品、配方、实体、生物 AI、群系、结构
│   ├── server/        # 世界模拟、权威逻辑、玩家管理
│   ├── client/        # 输入、预测、渲染场景、UI、音频
│   └── common/        # 两端共享：注册表、实体状态、协议消息定义
├── assets/            # 原创纹理/音效/字体（与代码分开许可）
└── tests/             # 单元 + 黄金文件 + 回放测试
```

- **同仓异进程**：`opencraft`（客户端+内嵌服务端）与 `opencraft-server`（独立服务端）链接同一 `game` 库；从第一天就是服务端权威——后补代价极高（Veloren 教训）。
- 双循环：服务端/逻辑 20 TPS 固定步长（决定性，输入在 tick 头生效）；客户端渲染可变帧率 + partial-tick 插值。

### 1.1 权威侧现状（T-A1 建立，2026-09-16）

> T-A1 之前本节只是一条**意图**：权威侧实际不存在（`engine/net` 为空、`game/server`
> 只有 worldgen + storage、客户端直接改世界）。T-A1 建立了它，但**尚未网络化**。

- **权威侧 = `opencraft::sim::WorldSim`**（`game/server/sim/`）：持有区块/光照/流体/地形/存档。
  四个写入口（`write_block` / `set_fluid_at` / `place_water_source` / `remove_water_source`）
  **全部 `private`**；流体经**私有嵌套类**适配 `voxel::IFluidWorld`，
  故引擎要求的 `set_fluid_at` 从外部也不可达 ⇒ **写权限由编译器保证，不是约定**。
  `WorldSim` 禁拷贝与移动（旧 `WorldSource` 可拷贝且含自指 FluidSim，是隐患）。
- **客户端 = `client::WorldSource` 退化为纯 `const` 只读视图**，只持 `const WorldSim*`。
  仍实现 `render::IBlockSource` / `physics::IBlockSource` / `IFluidSource` 的只读部分，
  故渲染/物理/射线/HUD 的调用签名一行未改。
- **通道 = `opencraft::game::IAuthority`**（`game/common/include/opencraft/game/protocol.hpp`）：
  客户端对世界的全部动作都经过**五个动词**：
  `submit`（挖/放/倒水/舀水等动作）、`tick`（权威侧 20 TPS）、`take_changes`（回推）、
  `autosave_pass`（持久化窗口）、**`stream`（区块流式与卸载，T-D4 新增）**。
  请求与结果为**值语义可序列化数据**（坐标 + int + bool + 枚举 + 预留 `sequence`），
  无回调/指针/共享可变状态 ⇒ **M3 换成网络通道时不改调用点**。
- **`stream()` 的顺序约束**（T-D4）：一个动词内按固定顺序做完
  ① 生成（按 `generate_budget` 预算，近者优先）→ ② 释放超出 `unload_radius` 的区块
  （**脏区块先落盘再释放**）→ ③ `persist` 窗口到期则跑自动存档。
  ⚠ 该顺序**由权威侧一处保证**，故持久化**不拆成独立动词**（否则调用方可能先卸后存 ⇒ 丢数据）。
- ⚠ **逐区块的 `ensure_chunk` / `unload_chunk` 已收为 `private`**
  ⇒ 客户端**连编译期都调不到**。世界管理只能经 `stream()`。
- ⚠ **滞回量** `kUnloadHysteresis = 2`（`unload_radius − generate_radius`）：可调常量，
  **不进规格**（不是 ⚖ 对齐数值），只记备忘，避免把调参变成改规格。
- **校验只在权威侧一处**（可达判定按"格子最近点"、replaceable、玩家 AABB、
  hardness<0、`is_water_source`）。客户端已删除自己那份 `check_placement`，
  避免同规则两处漂移。扣物品在 `accepted` 之后 ⇒ 被拒绝不吃物品。
- **回推只带脏区块集**：进程内客户端渲染的就是权威侧那份存储，方块状态无需转运。
- ⚠ **tick 当前由客户端驱动**。**独立服务端进程仍不存在**，M3 建立时须**自持 20 TPS**。
- ⚠ `kReachDistance` 单一真相源在 `game/protocol.hpp`（客户端不再有自己那份）。

**M3 必做清单**（T-A1 刻意留下的接缝，见 `docs/tasks/T-A1.ruling.md`）：
① 库存上收（需玩家/会话概念）② 挖掘计时上收（反作弊）③ 逐方块事件
（`WorldChanges` 已留加宽点）④ 独立服务端进程 + 自持 20 TPS ⑤ `sequence` 填上（预测/和解）。

### 1.2 ★ 架构约束的编码范式（T-A1 / T-D4 确立，后续架构卡的默认做法）

**把架构约束编码进类型系统 / 访问说明符 / 接口形状，而不是写进注释或文档。**
文档会过期、注释会被忽略，编译器不会。本项目的三个实例：

| 约束 | 编码方式 | 卡 |
|---|---|---|
| 权威侧唯一写权限 | 四个写入口 → `private`；流体写路径 → **私有嵌套类**实现 | T-A1 |
| 世界状态不可拷贝（内含自指） | `WorldSim` 拷贝与移动 `= delete` | T-A1 |
| 客户端不得直接管理区块 | `ensure_chunk` / `unload_chunk` → `private`，只能经 `stream()` | T-D4 |
| "先落盘后释放"的顺序 | 合并为单动词 `stream()`，顺序由实现内部保证（不拆 `persist`） | T-D4 |

判据：**如果一条约束违反了也不会编译失败，它就只是一条建议。**
后续架构卡在写"禁止 X""必须先 A 后 B"时，应先问：能不能让编译器替我们拦住？

## 2. 线程模型

- 主线程（客户端）：输入采样、渲染提交、GPU 上传，**每帧 2–4ms 预算制**，只做调度与结果采纳。
- worker 线程池：地形生成、光照、网格化、存档序列化。任务输入用不可变快照，结果带世界版本号校验，过期结果丢弃。
- 跨线程通信：MPSC 任务队列 + 完成队列；禁止 worker 直接触碰世界写状态。

## 3. 区块生命周期（显式状态机）

```
EMPTY → GENERATING(地形/装饰) → LIGHTING → MESHING → LIVE
                        ↑__________（方块修改后重入 LIGHTING/MESHING）↓→ SAVING → UNLOADED
```

- 生成/光照/网格化均有"邻区就绪"门槛（如网格化需四邻光照就绪），解决树跨区块、光跨区块的依赖。
- 卸载：LIVE→SAIVING→落盘→释放；重新加载时优先读盘。

## 4. 网格化与渲染

- 起步：**culled meshing**（仅生成暴露面），AO 16 级并入合并键后升级贪心网格化；按面朝向分桶 + 顶点池复用。
- 材质：2D 纹理图集（原创 16×16 像素风），块内 UV 收缩 0.5px 防渗色。
- 透明处理：水/玻璃等半透明独立 pass，区块级按距离排序（不做逐面排序）；交叉面用双面渲染+深度偏移缓解。
- 光照呈现：天光/方块光 4-bit 各取 max，逐顶点采样插值 + AO；昼夜通过天光缩放系数实现（不重烘焙）。
- RHI 抽象层先行，OpenGL 4.3 核心作第一个后端；Vulkan/MoltenVK 预留接口不实现。

## 5. 光照引擎

- 双通道：skylight + blocklight，各 4-bit（0–15）。方块光：光源 BFS 外扩每格 −1；天光：列高度图直射 15，向下无衰减、横向 −1。
- 删除走"暗度删除 BFS"（记录旧光边界再重加光），跨区块传播进延迟队列，统一汇入网格化调度去重。

## 6. 物理与实体

- **★ 体素碰撞的唯一入口：`opencraft/physics/sweep.hpp`（T-D40 建立）**。
  公开提供 `physics::Box` / `box_of` / `box_collides` / `highest_surface_below` /
  `sweep_axis_x|y|z`（返回 `AxisSweep`）。
  **任何让 AABB 在体素世界移动的新代码都必须走这里**（生物卡 T-M2 的前置）。
  - 只依赖 `IBlockSource`；依赖方向严格为 **实体 → physics**，不可倒过来。
  - 语义：高度感知（T-D8，用 `shape_top_at`）、"面贴面不算碰撞"、逐轴 Y→X→Z、子步防穿透。
  - **只抽几何，不抽响应**：玩家撞墙清零速度 vs 掉落物乘 `restitution` 属内容差异，各留调用侧。
- ⚠ **`auto_jump.hpp` 的 `overlaps_solid` 是"故意不共享"的第 4 份**，不是漏抽：
  它只用 `solid_at`（**全方块**语义），与 `box_collides`（高度感知）**语义不同**。
  统一会改变含半砖世界的 auto-jump 行为 ⇒ 属语义变更，**不得**在纯重构中顺手合并
  （T-D40 裁决 2）。
- 实体：AABB + swept collision（先轴分离滑动，防穿墙）；方块命中用 DDA voxel raycast（与渲染选取共用，保证"指哪挖哪"）。
- 实体管理：**实体层已建立（T-E1）**：类型注册表 + 稠密槽池（升序 id = 确定顺序遍历）+ 每类型
  `EntityDef` 物理参数 + 权威侧持有实体状态。**目前未引入 EnTT**（避免与网络化边界复杂化）。
- 执行序（对齐 JE 主循环，来源 `research/07 §1.2`）：乘客在其载具**之后** tick（载具先算、乘客继承）；
  方块实体在所有实体**之后** tick；区块遍历为随机顺序、实体遍历为确定顺序。载具/坐骑实现须遵守此序。
- 物理参数**按实体类型实例化**，非全局单例（下落方块重力 0.04 为玩家一半、船/矿车/坐骑速度各异，
  见 `research/07 §1.4 / §7.4`）。
- **摩擦契约（T-D7 冻结，M2 生物/载具复用）**：
  `k = entity.horizontal_drag × block.slipperiness`（**乘性解耦**，支持"同实体不同方块"与
  "不同实体同方块"两个维度）；**空中 `S ≡ 1.0`**（取脚下采样，忽略方块）；
  **阻尼施加在位移之后**（先移动后衰减，顺序反了则跳跃高度与水平距离全错）；
  地面加速度 `a = 0.1 × M × E × (0.6/S)³`（**三次方**，不可降为线性）；
  单轴动量 `|v| < 0.003` 截断归零（阈值选择见 `docs/tasks/T-D7.ruling.md` A-5）。
  详见 `docs/research/06-mc-movement-physics-whitepaper.md` §2、§10。

## 7. 存储与注册表

- 区域文件：32×32 区块/文件、扇区制、zstd 压缩（块头带未压缩长度）；脏区块标记 + 定时批量异步回写 + 原子替换。格式细节自定，不兼容 Anvil。
- 方块/物品/配方/群系/结构全部**注册表驱动**（字符串 ID → 数字运行时 ID），数据包式 JSON 定义（原创 schema），为模组与进度系统铺路。

## 8. 网络（M3 起）

- 传输：TCP 或 ENet 可靠通道起步，消息分帧 + zstd；区块异步发送 + 增量方块消息（批量 compact）。
- 客户端预测：输入序号化，服务端权威重放和解（位置误差阈值内不拉扯）；远程实体快照插值（100ms 缓冲）；动作（挖/放/攻击）服务端校验。

## 9. 依赖选型（全部宽松许可）

| 用途 | 库 | 许可 |
|---|---|---|
| 窗口/输入 | GLFW | zlib |
| 数学 | glm | MIT |
| ECS | EnTT | MIT |
| 噪声 | FastNoiseLite | MIT |
| 贴图/图标 PNG 解码（美术资产，T-A2） | stb_image | public domain（源码头部自述） |
| 脚本嵌入（后期） | sol2 + Lua | MIT |
| 压缩 | zstd | BSD |
| 日志 | spdlog | MIT |
| 性能分析 | Tracy | 自带许可（可用） |
| 音频 | OpenAL Soft | LGPL（动态链接）|
| 测试 | Catch2 / doctest | BSL / MIT |
| GUI | Dear ImGui | MIT（调试工具与主 UI 均可用）|

构建：CMake ≥3.24 + FetchContent/vcpkg；编译器 MSVC/Clang/GCC 三端 CI；C++20。

> **本表只登记"代码依赖"**（会被链接进产物、须在提交信息注明许可）。
> **美术制作工具不进本表**（T-R3 R-13）：它们不被链接、不随产物分发，
> 其许可只约束"能不能用它制作"，不约束产出——**工具许可 ≠ 产出许可**。
> 例：MagicaVoxel（免费用于任何项目 / 鼓励署名 / 禁止转售或再分发软件本体，
> 来源 <https://ephtracy.github.io/mv_main.html> License 小节，访问 2026-09-18）
> 与 Goxel（GPL-3.0）均为制作工具；用它们产出的 `.vox` 是我们自己的原创资产，
> 受 `docs/04` 红线 2/5 与 `docs/05` §2 三条硬规则约束。
> 若有工具**被链接进产物**（如某个解析库），则必须回到本表登记。

## 10. 工程纪律

- 每次交付可编译、现有测试全绿；`clang-format` 统一风格。
- 黄金文件测试覆盖：物理回放、种子世界快照、光照传播用例。
- 性能预算：单区块网格化 <5ms、光照重算 <3ms（i5 级基线），Tracy 持续盯。
