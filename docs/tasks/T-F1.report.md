# T-F1 开发者报告：流体地基 + 水桶（阶段 1）

> 分支 `task/T-F1-fluid-base`（worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-F1`，基线 `9d2c9bd`）
> 提交：`09e5941`（主体）、`3969524`（性能与交互修复）　报告日期：2026-09-16
> ⚠ 基线说明：期间 `main` 已前进到 `fb9a120`（PM 的 docs/06 路线图与派发说明两次提交，未触碰本卡白名单内文件），
> 本分支基于 `9d2c9bd`，合并时与本卡变更无重叠文件。

---

## 1. 结果摘要（一段话）

水已经从「静态布尔标记」升级为真正的 MC 流体：新增**并行流体层**（每体素一个流体单元，与方块层并存）、
**计划刻队列 `FluidSim`**、**元胞自动机转移规则**（垂直优先 / 下落柱 / ΔL 衰减 / 接收方按发射掩码取最强）、
**坡度寻路 BFS**（半径常量 5，含起点，标"待实机校准"）与**按能级的分档水位渲染**；
快捷栏新增第 10 格**水桶**，右键水面装水、右键地面倒水，倒出的水逐格摊开。
**267/267 单测绿**（243 存量 + 24 新增），clang-format（CLT 17）无 diff，
实机证据（本目录 `docs/qa/tf1-fluid-2026-09-16/`）显示平地单源**每 5 tick 扩张一环、7 环封顶、
每格能级 −1**，梯田地形上**收窄成一股并优先走落差**。

---

## 2. 交付物与文件列表

### 新增

| 文件 | 内容 |
|---|---|
| `engine/voxel/include/opencraft/voxel/fluid.hpp` | 流体单元 `FluidCell`（kind/level/falling/source/spread）+ 打包/解包 + 水参数常量（含坡度半径的取值依据注释） |
| `engine/voxel/include/opencraft/voxel/fluid_sim.hpp` | `IFluidWorld`（注入接口）+ `FluidSim`（到期队列 / 转移规则 / 坡度搜索） |
| `engine/voxel/src/fluid_sim.cpp` | 上述实现（316 行） |
| `tests/test_fluid.cpp` | 24 个用例（680 行），见 §3 |
| `docs/qa/tf1-fluid-2026-09-16/` | 实机证据（README + 3 份原始日志 + 5 张截图 + 临时打点 patch） |

### 修改

| 文件 | 改动 |
|---|---|
| `engine/voxel/include/opencraft/voxel/chunk.hpp` / `src/chunk.cpp` | 并行流体节 `FluidSection`（**惰性分配**，空节不落盘）；`get_fluid/set_fluid/fluid_section_empty/has_fluid`；**格式升 v2**（v1 前置 + 流体节）；反序列化**同时接受 v1/v2** |
| `engine/voxel/CMakeLists.txt` | 加 `src/fluid_sim.cpp` |
| `engine/render/include/opencraft/render/mesher.hpp` / `src/mesher.cpp` | `FluidVertex`（**浮点**高度）+ `FluidBucket` + `MeshData::fluid`；`IFluidSource`（`fluid_height_at` + `fluid_span`）；`build_chunk_mesh(blocks, pos, fluid = nullptr)`（默认参数，旧调用方零改动） |
| `game/client/src/world.hpp` / `world.cpp` | `WorldSource` 增实现 `IFluidWorld` + `IFluidSource`；持有 `FluidSim`；`fluid_step()` 逐 tick 驱动；`place_water_source/is_water_source/remove_water_source`；块改动 → `wake`；区块进内存 → 边界唤醒 |
| `game/client/src/main.cpp` | 快捷栏 9 → **10 格**（键 0 = 水桶）；水桶装/倒接线；每 tick `world.fluid_step(dirty_chunks)`；流体桶上传/绘制（半透明 pass）；脏区块去重 |
| `tests/CMakeLists.txt` | 加 `test_fluid.cpp` |
| `tests/test_chunk.cpp` | **2 处 v1 字节数断言更新为 v2**（见 §6.3，属格式变更的必然结果） |

---

## 3. 验收标准逐条对照

| # | 标准 | 状态 | 证据 |
|---|---|---|---|
| 1 | 扩散 1 格/5 tick、最远 7 格 | ✅ | 单测 `fluid: a flat single source spreads one block per five ticks, at most seven`（t=5 一环、t=35 七环、t=40 不越界）；**实机** `trace_flat_spread_full.log`：t=600 源 → 605(4 格,L7) → 610(8,L6) → … → 635(28,L1)，合计 **113 格 = 1+4×(1+…+7)** |
| 2 | 能级递减 ΔL=1、水位高度随之递减 | ✅ | 单测 `the level drops by one per flowing block`（1..7 格 = 7..1）；`mesher: a fluid surface renders at the level height`（L=3 → h=1+3/9）；实机截图 `shot_pool_level_steps.png` 可见同心台阶 |
| 3 | 垂直优先、下落柱 falling、不向四周摊 | ✅ | 单测 `an open floor wins over spreading sideways`（倒水口下方成 L=8/falling，四邻全干，falling 单元 spread=0）；实机 `trace_terrace_funnel.log` t=846..856 连续 falling=1 单宽下降 |
| 4 | 坡度寻路（崖边收窄、无落差回退、并列规则） | ✅ | 单测 6 例：`water funnels toward a drop-off instead of fanning out`（掩码只含 West，东/南/北**为 0 而非更慢**）、`with no drop-off in range the spread is symmetric`、`a drop-off just outside the search radius does not steer`（半径边界 7 格）、`the radius boundary itself still steers`（5 格）、`tied drop-off distances all flow`（并列全流）、`solids block the search as well as the flow`；实机 terraced 场景收窄成股 |
| 5 | 水桶闭合（装水/倒水/当场摊开） | ✅ | 实机 `bucket: poured water source at (0,132,0)` + 113 行逐格日志；`bucket: filled from (0,132,-2)`（`trace_bucket_fill_hold_defect.log`）；截图 `shot_bucket_selected_hud.png` |
| 6 | 旧档兼容（v1 可读，流体层为空） | ✅ | 单测 `fluid: a v1 payload still loads, with an empty fluid layer`（**手工构造**的 v1 字节流，不是用新序列化器自产自销）+ `serialization rejects malformed payloads`（v3 抛异常）；v2 往返单测 |
| 7 | 无回归（243 存量全绿） | ✅ | **267/267**（243 存量 + 24 新增）；仅 2 处存量断言因格式升 v2 而更新（§6.3） |
| 8 | 单区块网格化 <5 ms | ⚠️ 见 §7 | 流体层**未引入可测开销**（同 tick 同区块 with/no-fluid 差值在噪声内），但绝对数 5–8 ms **超过预算**，且该超标在 no-fluid 路径上同样存在 ⇒ 既有现象，非本卡引入 |
| 9 | clang-format（CLT 17）无 diff、测试名不含 `[` | ✅ | 逐文件 `clang-format --dry-run --Werror` 干净 |
| 10 | 独立 worktree、提交前缀 `taskT-F1:` | ✅ | `/Users/happy/Desktop/opencraft_worktree/opencraft-T-F1`，两个提交均 `taskT-F1:` 前缀 |
| 11 | 报告落盘、对话只给简短版 | ✅ | 本文件；对话用 txt 代码块 |
| 12 | 实机证据（逐格铺开、HID 层、非 osascript） | ✅ | `docs/qa/tf1-fluid-2026-09-16/`（含注入通道的如实交代，见 §5.4） |

**未做（卡面明令）**：源再生/无限水、含水、气泡柱、岩浆、相变、流动音效粒子、完整物品系统、游泳物理（`engine/physics/**` 零 diff）、`docs/01`/`docs/03` 规格。

---

## 4. 实现要点

### 4.1 数据表示（每体素 2 字节 —— ⚠ 与卡面建议的偏离，见 §6.1）

```
bit 15-14 kind(0=无/1=水/2=岩浆/3=预留)   bit 13-10 level(1..8, 0=无)
bit  9    falling                        bit  8     source
bit  7- 4 保留(必须 0，反序列化校验)      bit  3- 0  spread（四向发射掩码）
```
- **惰性分配**：`FluidSection` 只在真正写入流体时分配 4096×2 字节；空节不占内存、不落盘。
- `unpack_fluid()` 把 kind=0 或 level=0 归一化为**全零空单元**，保证「空」的表示唯一。
- `valid_packed_fluid()` 在反序列化时拒收 level>8 / 保留 kind / 保留位非零的载荷（不静默接受脏数据）。

### 4.2 调度（计划刻）

- `std::map<due_tick, vector<Cell>>`（按到期有序）+ `unordered_set` 去重：**同一位置同时最多一条待处理条目**
  （MC 的 per-position scheduled-tick 去重语义），`step()` 一次推进 1 tick 并处理 `due <= tick` 的全部条目。
- 子单元的到期时间 = **父条目到期时间 + 间隔**（不是"当前 tick + 间隔"），这一条让「5 tick/格」在实机上
  恰好表现为「每 5 tick 一环」。队列溢出按 R-5/§8.4 的建议**拒绝入队 + 计数**（`kMaxPending=65536`）。
- 单线程、调用方串行化（由客户端每 tick 调用一次），与 `LightEngine` 同约定。

### 4.3 转移规则（§3）

判定顺序：**(1)** 上方同种流体 → 本格 L=8（下方可通 ⇒ `falling=true` 且**发射掩码置 0**，这就是瀑布 1 格宽的来源）；
**(2)** 桶放源头 → 维持 L=8；**(3)** 否则从「**能有效流向本格**的邻居」取最强：`L = max(L(Q) − ΔL)`，
`L(Q)−ΔL ≤ 0` 即本格无流体（射程耗尽的物理来源，不需要距离计数器）。
「能有效流向本格」= 邻居的 `spread` 掩码包含指向本格的方向 —— 没有这一条，纯 max 规则会把水摊成菱形（§6.2 详述）。

### 4.4 坡度寻路（§4）

- 对每个待流格、每个方向做一次**无权 BFS**（4 向、不穿实体、半径内 11×11 定长栈上缓冲，无堆分配），
  落差口定义 = 候选格**正下方可进流体**（§4.3）。
- 代价从 1 起算（邻格即落差口 ⇒ D=1）；取 `D_min` 的**全部并列方向**发射（§4.5）；全部为 INF 时回退为
  「所有可通行方向对称扩散」。
- 半径 `kSlopeSearchRadiusWater = 5`（含起点），命名常量 + 依据注释 + "待实机校准"（T-D22 / T-R1 裁决 2）。

### 4.5 水位渲染

- `MeshData::fluid` 用**浮点**顶点（`FluidVertex`）承载分数高度；`MeshVertex` 保持整数 10 字节布局不动
  （T005 契约与测试不受影响）。
- 高度 `h = level/9`（level 8 = 1.0）——**推算值**，wiki 只给"渲染高度部分取决于 level"（research/10 §1.3 已声明）。
- 顶面在「上方无同种流体」时才发；侧面墙只覆盖「与邻格水位差」的可见带，因此相邻不同能级之间是**台阶**而非缝隙。
- 流体格**取代**该格的整方块水；方块层水（`water` id 12）而**流体层为空**的格子仍按整方块渲染
  ⇒ **世界生成的海/湖完全不变**（`mesher: worldgen water without a fluid surface keeps meshing as a cube` 钉住）。

### 4.6 客户端接线（防「机制写了但实机永不触发」）

- `WorldSource` 同时实现 `render::IFluidSource` 与 `voxel::IFluidWorld`：**网格化、物理、选取、存档都看同一份世界**。
- `set_fluid_at` 同步方块层（有水 ⇒ placeHolder `water`，水离开且原方块就是 water ⇒ 置空气；
  被玩家放上方块的水格**不会**被误删）、更新光照、标脏存档、标脏网格。
- 每次**方块改动**（挖/放）都 `fluid_.on_block_changed()`（§7.2 的方块更新语义）⇒ 挖穿水池底部会真的漏水
  （单测 `mining a hole under a pool lets the water follow`）。
- **区块进内存**时唤醒边界流体（新块自己的流体单元 + 四邻朝向新块的边界列），避免「跨区块边界处水看不见新世界」
  （未加载区块对流体读作**墙**，§4.7 / R-5；与 `ILightWorld` 的"读作空气"**相反**，已在头文件写明）。

---

## 5. 验证方法与结果

### 5.1 构建/测试

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-F1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # worktree 不设 FETCHCONTENT_BASE_DIR
cmake --build build -j8                            # 不接管道
./build/tests/opencraft_tests                      # 267/267
```

| 阶段 | 结果 |
|---|---|
| 基线（未改动） | 243/243 |
| 本次提交 | **267/267**（assertions 8498/8498） |
| clang-format（`/Library/Developer/CommandLineTools/usr/bin/clang-format`，17.0.0） | 无 diff |

新增 24 例覆盖面：打包往返/坏载荷拒绝、区块流体节存取与空节、v2 往返、**v1 手工载荷兼容**、
平面扩散速率与射程、能级序列、垂直优先与下落柱、坡度寻路 6 例（收窄/回退/半径边界内外/并列/实体阻挡）、
源移除后全量退水且队列收敛、放方块入水、挖洞漏水、队列去重与溢出计数、确定性、水面网格 4 例。

### 5.2 实机（决定性，详见 `docs/qa/tf1-fluid-2026-09-16/README.md`）

平台单源，逐环统计（脚本从原始日志提取，非人工摘抄）：

| tick | 新写入格数 | 曼哈顿距离 | 能级 |
|---|---|---|---|
| 600 | 1 | 0 | 8（source=1） |
| 605 | 4 | 1 | 7 |
| 610 | 8 | 2 | 6 |
| … | … | … | … |
| 635 | 28 | 7 | 1 |

合计 **113 = 1+4×(1+…+7)**，此后不再增长 ⇒ 验收 1 的"1 格/5 tick、7 格封顶"在实机成立。
自然梯田地形上另有「四邻只进水 1 格 → 落差下降 → 落地再摊开」的完整链条（验收 3/4）。

### 5.3 质量自检（本项目历史坑位）

- **构建不接管道**，判成败用 `$?` 与 `grep error:`；重建后核对 `build/opencraft` 的 mtime（02:14）与
  构建日志，确认跑的是新二进制。
- **测试断言先自检**：本轮 4 个先失败的新用例中，3 个是**我的断言写错**（流量、期望值算错），
  1 个（`place_source` 未套用 `open_below`）与 1 个**真 bug**（`source` 位在重算时被清）才是实现问题。
  记录在案：**"测试失败"不能直接当成实现缺陷，也不能直接当成断言错误**。

### 5.4 注入通道（如实交代，债务 T-D21 复现）

- 本机需先 `activate` 窗口，再用 **session-tap**（`CGEventPost(kCGSessionEventTap)`）注入；`CGEventPostToPid`
  与 MCP 的 app-scoped 注入对本进程均无效（前者无效、后者因 bundle_id 为空拒绝）。
- 即便如此仍**时灵时不灵**：同一会话内首次右键（倒水）成功，随后的按键 0/1、S、右键（装水）多次无效。
  因此最终证据的构成是：**倒水 = 真实右键注入**；**装水 = 按住右键时的重复动作（同一条代码路径，也正因如此暴露了缺陷）**；
  **按数字键 0 选水桶**这一步无法注入，由临时 harness 提供起始手持状态（该分支仅 3 行赋值）。
  这三点已在证据 README 与本节写明，**不冒充为"全程真实按键"**。

---

## 6. 主动发现的卡面/文档问题（本项目鼓励项）

### 6.1 ⚠ 卡面建议的 1 字节布局**无法表达** research/10 §3.2+§4.5 的语义（本卡最大偏离）

- §4.5 要求「只让 `D_min` 的方向流，其余**抑制**」，而 §3.2 把接收方的邻域 `N4*` 定义为
  「**能有效流向 P** 的邻居」。要让"抑制"可观测，接收方必须知道每个邻居的发射集合；
  §4.5 又明确「并列时全部并列者都流」，即发射集合可以是四方向的**任意子集**（16 种取值）。
- 与 `kind(2) + level(4) + falling(1) + spread(4)` 相加为 **11 bit**，卡面建议的 1 字节（7 bit 有效）放不下。
- **本卡决定**：改用 **2 字节/体素**（`kind 2 | level 4 | falling 1 | source 1 | 保留 4 | spread 4`），
  并靠**惰性分配**让无流体区块/节的内存开销为 0（实机 perf 探针确认无流体区块零开销）。
- 若 PM 坚持 1 字节：可行方案是 `kind 2 | level 3 | spread 3`，代价是**放弃 2/3 方向并列**
  （只能表达"单方向"或"四方向全发"），会偏离 §4.5 的并列语义。**请裁决**（建议 S-1）。
- 另外新增了文档三字段之外的 **`source` 位**：`source` 不能用「上方无流体」推出——下落柱可以砸在
  已有源头之上，柱子退去后该格必须仍是源头。已写成注释。

### 6.2 ⚠ research/10 §4.6 的手算算例**与它自己的 §4.4 算法矛盾**

- §4.6 声称源 (2,2)、落差口 (1,4) 时只有 W 方向代价 3、S 方向"无落差口"。
  但落差口与源的**曼哈顿距离就是 3**，从 S 方向同样 3 步可达（(2,3)→(1,3)→(1,4)）⇒ **D_S = 3**，
  按 §4.5 的并列规则**发射集合应为 {W, S}**，而非只有 {W}。
- §4.6 的结论（"收窄成股、不四向均摊"）**依然成立**，只是掩码是 2 个方向而非 1 个。
  单测 `fluid: the research document's cliff example` 按 §4.4 **忠实实现**并把这个差异写进注释。
- 影响评估：不算返工级问题（视觉结论一致），但**后续卡不要再把这个算例当数值基准**。

### 6.3 存量断言的更新（old → new，逐条交代）

格式从 v1 升 v2，无流体区块的载荷**多出 1 字节的"非空流体节计数"**，故两处硬编码字节数必然 +1：

| 位置 | 旧值 | 新值 | 原因 |
|---|---|---|---|
| `tests/test_chunk.cpp:161` 全空气区块 | `4+1`（5） | `4+1+1`（6） | v2 追加流体节计数 |
| `tests/test_chunk.cpp:169` 单方块区块 | `4+1+1+1+2+4+4+2048`（2065） | `…+2048+1`（2066） | 同上 |

除这 2 处外**没有任何存量断言被改动**；`test_physics_golden`、光照、存读档、网格化用例全绿（无覆盖损失）。
`tests/test_storage.cpp` 的压缩率打印同步变化（18553 → 18554 字节，ratio 0.05082 → 0.05082，仍为 ×100 级压缩）。

### 6.4 两处"看着像数值漂移、实为别的"的记录

- 本机**无法复现** T005 报告的 0.74 ms/区块（同一场景、同一 Release 构建下实测 ~2.1 ms）——
  预算基线本身就与本机不符，见 §7。

---

## 7. 性能（验收 8）

同 tick、同区块，唯一变量是"传不传流体源"：

| 场景 | no-fluid | with-fluid | 流体面数 |
|---|---|---|---|
| t=0 无流体 (0,0) | 2.712 ms | 2.741 ms | 0 |
| t=0 无流体 (-1,0) | 2.633 ms | 2.558 ms | 0 |
| t=800 有水池 (0,0) | 7.783 ms | 6.230 ms | 124 |
| t=1000 有水池 (0,0) | 6.592 ms | 6.339 ms | 124 |

- **流体层零可测开销**（差值在噪声内、符号不稳定）；这是 `IFluidSource::fluid_span()` 门控的结果：
  修复前每个区块**每体素**都要做一次世界坐标流体查询，含流体区块网格化实测 **0.74 → 6.25 ms**（真回退，已修）。
- ⚠ 绝对值 5–8 ms **超过 `docs/03 §10` 的 5 ms 预算**，但同一时刻的 no-fluid 路径同样 5–8 ms
  （t=0 时同区块仅 2.7 ms）⇒ 判定为**既有网格化/前景负载**在流式加载期升高，**非本卡引入**。
  如实报告，不静默。若 PM 需要严格预算判定，建议单列一张"网格化预算复核"卡（S-4）。

---

## 8. 已知问题与债务

| # | 项 | 说明 |
|---|---|---|
| 1 | **垂直下落速率沿用 5 tick/格** | 与水平同间隔。**没有找到可溯源来源**（wiki 只给水平 flowrate），故不发明数字；观感上瀑布下落比 MC 慢（20 格 ≈ 2 s）。请 PM 决定是否单列"实机校准"（S-2） |
| 2 | 水面**未接光照采样** | 流体桶用固定亮度 220/255（T008 光照路径只走方块桶）。洞内水面会偏亮 |
| 3 | 流体节**不缩容** | 水全部离开后节存储保留（与 `PaletteSection` 同一"no-shrink"政策）；序列化仍按非空节写，zstd 可压。`has_fluid()` 因此可能在水已排空后仍为真（已在头文件写明语义） |
| 4 | `spread` 掩码的**退化情形** | 仅当几何变化时才重算（块改动/邻居变化触发），这是设计如此；若将来支持活塞推方块等非线性几何变化，需要显式唤醒 |
| 5 | 未做**区块卸载时的流体清理** | 卸载区块会丢弃其流体（存档仍在）。与 T-D4 的卸载策略同源 |
| 6 | 水温渲染的图集 | 复用 `water` 的三张既有贴图（顶/侧），水面高度变化时纹理被拉伸；MC 用专门的流水贴图，属 M2+ 美术项 |
| 7 | 注入通道（T-D21） | 本轮取证再受其扰，见 §5.4；建议维持"决定性场景放在生效窗口内"的既有手法 |

---

## 9. 接口变更（供后续卡与 PM 核对）

**新增（引擎内，均已带默认值或可为纯新增，不影响既有调用方）**

| 接口 | 位置 | 语义 |
|---|---|---|
| `voxel::FluidCell` / `pack_fluid` / `unpack_fluid` / `valid_packed_fluid` | `fluid.hpp` | 流体单元与 16 bit 打包；空单元规范化为 0 |
| `voxel::IFluidWorld` | `fluid_sim.hpp` | `fluid_at` / `set_fluid_at` / `fluid_may_enter`（未加载读作**墙**） |
| `voxel::FluidSim` | `fluid_sim.hpp` | `place_source` / `clear_cell` / `on_block_changed` / `wake` / `step` / `tick` / `pending` / `dropped` / `processed` |
| `Chunk::get_fluid/set_fluid/fluid_section_empty/has_fluid` | `chunk.hpp` | 流体层存取（越界**读作空**、不抛异常） |
| `render::IFluidSource` | `mesher.hpp` | `fluid_height_at` + `fluid_span`（后者为性能门控，**新增实现者必须实现**） |
| `render::FluidVertex` / `FluidBucket` / `MeshData::fluid` | `mesher.hpp` | 浮点高度流体几何 |
| `WorldSource::fluid_step/place_water_source/is_water_source/remove_water_source/fluid/water_block_id` | `world.hpp` | 客户端流体入口 |

**冻结项核对（卡面要求）**

- `Chunk::kFormatVersion`：1 → **2**，但**反序列化同时接受 1 与 2**（v1 ⇒ 流体层为空），旧档可读（验收 6）。
  新增 `kMinReadableFormatVersion = 1`。
- `IBlockSource::solid_at` / `liquid_at` / `shape_top_at` 语义**未改**（`engine/physics/**` 零 diff）。
- `render::IBlockSource::block_at` 契约未改；水流体格在方块层仍是 `water` 方块 ⇒ 选取/挖掘/物理/存档一致。
- `PhysicsConfig::water_*` **未改、未被本卡读取**（游泳物理不动）。
- `build_chunk_mesh` **签名向后兼容**（新参数有默认值 `nullptr`），T005/T008 调用方与测试零改动。

---

## 10. 给 PM 的建议表（请裁决后落盘，我不自行改任何规格/状态）

| ID | 类型 | 内容 | 依据 |
|---|---|---|---|
| **S-1** | **需裁决（与卡面偏离）** | 流体单元改用 **2 字节/体素**（卡面建议 1 字节）。理由：§3.2 的接收方语义 + §4.5 的并列语义需要 11 bit。建议采纳 2 字节并把"每 section 额外 4096 字节"更正为"8192 字节（且惰性分配）"；若坚持 1 字节，需接受「放弃 2/3 方向并列」的语义降级 | §6.1 |
| **S-2** | **新债** | **垂直下落速率无来源**：本卡沿用水平的 5 tick/格。wiki 未给下落速率 ⇒ 不得声称已对齐；建议与 T-D22 合并为一次实机对照实验（陡崖下落格/秒） | §8-1 |
| **S-3** | **文档修正** | `docs/research/10 §4.6` 的算例与 §4.4 算法矛盾（S 方向同为 3 步 ⇒ 掩码 {W,S}）。建议在该节加一行勘误，避免后续卡把该算例当基准 | §6.2 |
| **S-4** | **新债/复核** | 网格化预算：本机实测既有 no-fluid 路径 5–8 ms/区块（T005 报告 0.74 ms 无法复现）。建议单列一张"每区块网格化预算复核"卡，先确立**本机可信基线**再谈回归 | §7 |
| **S-5** | **行为决策（已实现，请追认）** | 水桶动作**改为按下沿触发**：原「按住每 4 tick 重复」会让刚倒下的源头被立即舀回，实机抓到 `t=868 倒水 → t=872 舀回` 的振荡。方块放置仍保持每 4 tick 重复 | §5.2、`trace_bucket_fill_hold_defect.log` |
| **S-6** | **口径观察（供 T-D22 校准参考）** | 关于坡度半径的两套 wiki 口径：若 Water 页的 "four or fewer blocks from the block it wants to flow to" 是从**邻格**起算（而非从待流格），则它与 Fluid 页的 "up to 5 blocks away" **是同一个数**，两套口径并不冲突。建议实机校准实验**同时记录起算点**，否则仍无法判定 | `fluid.hpp` 常量注释 |
| **S-7** | **规格建议** | 若后续卡要接红石/流体联动：本卡的流体计划刻**无优先级、纯 FIFO**（§2.2），方块刻与流体刻的执行序尚未在客户端区分（目前方块改动是即时 `on_block_changed`） | `fluid_sim.cpp` |
| **S-8** | **素材提醒** | 桌面那份《Minecraft 流体白皮书》未被本卡引用（仅作背景）；本卡数值全部来自 `docs/research/10` 与其中的 wiki 行号 | 卡面裁决 1 |

---

## 11. 复现命令

```bash
# 构建与测试
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-F1
cmake --build build -j8 && ./build/tests/opencraft_tests

# 实机（正常玩法，无需 harness）
cd build && rm -rf saves && ./opencraft
#   键 0：选中水桶；右键水面 = 装水；右键地面 = 倒水（按下沿触发一次）

# 实机证据的原始吞吐（本轮环境）
#   注入工具：clang -framework Foundation -framework CoreGraphics \
#     -o input /Users/happy/Desktop/opencraft/docs/qa/t008-2026-09-13/tools/input.m
#   临时打点：patch -p1 < docs/qa/tf1-fluid-2026-09-16/temporary_harness.patch（已还原，仅存档）

# 逐环统计复算（从原始日志）
python3 - <<'EOF'
import re, collections
rows = collections.defaultdict(list)
for line in open('docs/qa/tf1-fluid-2026-09-16/trace_flat_spread_full.log'):
    m = re.search(r'\[fluid-dbg\] t=(\d+) \((-?\d+),(-?\d+),(-?\d+)\) kind=(\d+) level=(\d+)', line)
    if not m: continue
    t, x, y, z, k, l = (int(v) for v in m.groups())
    if k: rows[t].append((abs(x) + abs(z), l))
for t in sorted(rows):
    print(t, len(rows[t]), sorted({d for d, _ in rows[t]}), sorted({l for _, l in rows[t]}))
EOF
```
