# T-A1 开发者报告：权威侧前置（进程内世界模拟分离）

> 分支 `task/T-A1-authoritative-side`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-A1`
> 基线：卡面写 297，实测 **308 / 9435 断言**（T-I2 合入后）；本卡交付 **317 / 9719 断言**
> 实机证据：`docs/qa/T-A1-2026-09-16/`（README.md 是索引）
> 二进制：`build/opencraft` md5 `1333ed3f34e551f8d16605d9d3473b71`（**不含**临时打点，见 §7.2）

---

## 1. 变更摘要

一句话：**世界不再住在客户端**。世界（区块存储 + 光照 + 流体 + 地形 + 存档）整体搬到
`game/server/sim` 的 `server::WorldSim`，**它唯一拥有写权限**；客户端只剩一个
**只有 const 方法的只读视图**，以及一条 `game::IAuthority` 请求通道。
挖 / 放 / 倒水 / 舀水四项动作从「客户端直接写世界」变成「发请求 → 权威侧校验并执行 →
客户端按裁决扣物品、按回推重烘焙」。**行为不变**（实机四项逐项复验 + 逐字节单测）。

### 1.1 边界怎么划的（验收 10 要求，本节是重点）

```
       客户端进程（本卡：同一个进程）
  ┌──────────────────────────────┐      ┌─────────────────────────────────────┐
  │  main.cpp 帧循环 / tick.cpp  │      │  server::WorldSim  (权威侧)          │
  │                              │      │                                     │
  │  渲染 / 物理 / 射线 / HUD ───┼─读───▶│  block_at / solid_at / fluid_*      │
  │      client::WorldSource     │      │  （render::IBlockSource 等只读接口）│
  │      （const 转发，无写方法） │      │                                     │
  │                              │      │  ── private ──                      │
  │  动作请求 ───────────────────┼─写───▶│  write_block / set_fluid_at          │
  │      IAuthority::submit()    │      │  place/remove_water_source           │
  │      IAuthority::tick()      │      │   ↑ submit() 与流体模拟（FluidWorld）│
  │      IAuthority::take_changes│◀─回推─│  pending_chunks_（脏区块）           │
  └──────────────────────────────┘      └─────────────────────────────────────┘
```

**权威侧 = `opencraft::server::WorldSim`（`game/server/sim/`）**，它持有
`ChunkManager` / `LightEngine` / `FluidSim` / `TerrainGenerator` / `WorldSave*`，
以及**全部四个写入口，且它们都是 private**：
`write_block` / `set_fluid_at` / `place_water_source` / `remove_water_source`。

其中 `set_fluid_at` 是引擎接口 `voxel::IFluidWorld` 的成员，本该是 public
（接口要求）。为不让它从外部可达，`WorldSim` 用一个 **private 嵌套类 `FluidWorld`**
实现 `IFluidWorld` 并转调私有 `set_fluid_at`（C++ 里嵌套类共享外层访问权）。
于是「唯一写权限」不是约定，而是**编译器保证**：客户端拿着 `WorldSim&` 也写不了世界。

**客户端 = `client::WorldSource`**，从「世界的实现」退化为**只读视图**：
全部成员是 const 转发（`block_at` / `solid_at` / `liquid_at` / `fluid_height_at` /
`fluid_span` / `registry()` / `light()` / `water_block_id()` / `chunk_ready()` /
`neighbors_ready()`），**类里没有任何写方法**。它继续实现 `render::IBlockSource` /
`physics::IBlockSource` / `render::IFluidSource`，所以 **渲染、物理、射线、HUD 的签名与调用
一行没改**（`mesh_chunk(..., const WorldSource&, ...)`、`HudResources{world,...}` 等原样）。

**通道 = `game::IAuthority`（`game/common/include/opencraft/game/protocol.hpp`）**，
只有四个动词：`submit(ActionRequest) -> ActionResult`、`tick()`、
`take_changes() -> WorldChanges`、`autosave_pass()`。请求与结果是
**值语义、可序列化**（glm 向量 + `u16` + 枚举 + `u32 sequence` 预留），
**无回调、无指针、不共享可变状态** —— M3 把 `submit()` 换成一次 socket 写即可，
调用点不变。

为什么这么切（逐条理由）：

1. **为什么不把世界留在客户端、只约定"别调写方法"**：卡面验收 1 要的是"客户端不再直接改世界"。
   只靠约定，后面每张内容卡（合成、掉落、爆炸、红石）都有机会绕过；现在绕过需要改
   `WorldSim` 的访问权限，**不可能悄悄发生**。这也是本卡被提前的唯一理由。
2. **为什么校验只有一处**：放置/挖掘/倒水/舀水的合法性全部搬到权威侧
   （`gate()`：界内 → 区块已加载 → 可达；再加 `check_placement` / `is_water_source` /
   `hardness < 0`）。客户端**删掉了自己那份 `check_placement` 调用**——
   本项目已记录过「同一条规则写在两处必然漂移」，所以宁可多发一次请求。
3. **为什么"扣物品"跟着裁决走**：客户端在 `submit()` 返回 accepted 之后才
   `place_one_block()`。这样**拒绝永远不会吃掉物品**（T-I2 的崩溃修复点：读方块要在格还满着的时候，
   顺序原样保留）。实机日志 `placed ... consumed 1 ... (31 left)` 同时含两侧证据。
4. **为什么 `can_transform_vessel`（舀水前的干跑）仍留在客户端**：它是**库存约束**（放不下就不该舀），
   不是世界约束；权威侧随后仍会独立拒绝（"没有水源"）。这条干跑是 T-I2 已确立的规则，未动。
5. **为什么回推只带「脏区块」**：进程内客户端渲染的**就是权威侧那份存储**（视图），
   方块/流体的*状态*不需要转运；唯一必须由权威侧告知的派生信息是"哪些区块的网格过期了"。
   逐方块事件在**没有预测与回滚**之前没有消费者（会是死代码），故不建；
   `WorldChanges` 这个结构就是为 M3 留的加宽点（那时客户端才有副本）。**若 PM 要求现在就带逐方块事件，一句话即可加。**
6. **为什么生命周期（`ensure_chunk` / `unload_chunk` / `autosave_pass` / `attach_save`）放在
   权威侧、却仍由客户端驱动**：区块流式加载与卸载是 M2b 的第二张卡（债务 T-D4），本卡不搬；
   但它们已经**放在 `WorldSim` 上而不是视图上**——语义是"客户端向权威侧请求"，不是"客户端改世界"。
7. **为什么库存没搬进权威侧**：卡面建议的 `ActionRequest` 就是
   `{kind, target, item_or_block, sequence}`——**放置请求自带方块 id**，权威侧不需要查库存。
   库存随玩家/会话一起搬属于 M3。本卡据此保持库存原样（T-I2 的模型一行未改）。
8. **为什么请求里多了 `ActorPose`**（`feet` / `height` / `eye_height`）：卡面验收 3 要求
   权威侧能拒绝"超出到达距离"，而权威侧手上没有玩家状态。三个字段是值语义、可序列化，
   M3 服务端自持玩家状态后由服务端填/忽略即可。**这是对卡面建议结构的一处主动扩充，在此申报。**

### 1.2 五处直写怎么收口的

| 原调用（`tick.cpp`） | 现在的形态 |
|---|---|
| `ctx.world.set_block(..., 0, ...)`（挖） | `submit({Dig, hit.block_pos, actor})`；裁决 + 打点（§7.2） |
| `ctx.world.set_block(cell, selected_block, ...)`（放） | `submit({PlaceBlock, cell, block, actor})`，accepted 后才 `place_one_block()` 扣物品 |
| `ctx.world.place_water_source(cell)`（倒水） | `submit({PourWater, cell, actor})`，accepted 后才换容器 |
| `ctx.world.remove_water_source(pos)`（舀水） | 先 `can_transform_vessel` 干跑 → `submit({ScoopWater, pos, actor})` |
| `ctx.world.fluid_step(ctx.dirty_chunks)`（流体） | `tick()` + `take_changes()` → 追加进 `ctx.dirty_chunks` |

`run_tick` 里**顺序、分支、⚖ 数字、既有日志文本全部未变**；新增的只有
"权威侧拒绝时打一条 warning"（T-A1 新增，见 §5 与 §7.1）。

### 1.3 附带纠正与加固

- **`WorldSource` 原本可拷贝且内含自指（隐患）**：`fluid_(*this)` 持有指向外层的指针，
  一旦拷贝，副本的流体模拟会指向原对象。旧类从未被拷贝所以没暴露；现在 `WorldSim`
  显式 `= delete` 拷贝与移动（`DECLARED` 在头文件里，附理由注释）。**这是本卡主动加固，建议记入架构备注。**
- **`kReachDistance` 单一真相源**：原本只在 `client_config.hpp`（4.5）。现在它实现于
  `game/protocol.hpp`（权威侧复核用的就是它），`client::kReachDistance` 变成它的别名，
  数值未变（`docs/01 §4` ⚖ 未动）。

---

## 2. 文件列表

```
新增  game/common/include/opencraft/game/protocol.hpp   请求/结果/回推/通道 词汇表（值语义）
新增  game/common/src/protocol.cpp                      ActionReject -> 理由字符串
新增  game/server/sim/include/opencraft/sim/world_sim.hpp   server::WorldSim（权威侧）
新增  game/server/sim/src/world_sim.cpp                     世界实现（自 client/src/world.cpp 整体搬迁）
新增  tests/test_authority.cpp                          9 例 / 284 断言
改    game/client/src/world.hpp                         世界实现 -> 只读视图（写方法全部消失）
改    game/client/src/world.cpp                         只剩 shade_mesh_with_light（表现层）
改    game/client/src/tick.hpp                          TickContext 增 authority 引用
改    game/client/src/tick.cpp                          5 处直写 -> 请求；末尾 drain 回推
改    game/client/src/main.cpp                          建 WorldSim + 视图；生命周期调用改走权威侧
改    game/client/src/client_config.hpp                 kReachDistance 改为别名（数值不变）
改    game/common/CMakeLists.txt                        + src/protocol.cpp
改    game/server/CMakeLists.txt                        + opencraft_sim 静态库（含注释说明）
改    game/client/CMakeLists.txt                        链接 opencraft_sim
改    tests/CMakeLists.txt                              + test_authority.cpp，链接 opencraft_sim
新增  docs/tasks/T-A1.report.md                         本文件
新增  docs/qa/T-A1-2026-09-16/**                        实机证据 + 工具 + README 索引
```

**迁移量**（`wc -l`）：`client/src/world.cpp` 530 行 → `server/sim/src/world_sim.cpp` **588 行**
（多了 §3 的校验与 §1.3 的加固，少了注释里的客户端措辞），`client/src/world.hpp` 从 178 → **77 行**
（去掉实现只剩 const 转发），`client/src/world.cpp` 剩 **78 行**（只有明暗折叠 `shade_mesh_with_light`）。

**禁碰项自查**：`engine/**` 零 diff（`git status` 可查，仅 `game/**` 与 `tests/**` 与 `docs/`）；
`docs/01`/`docs/03` 未动；`STATE.md` 未动；未引入任何网络库（`engine/net` 仍只有 CMakeLists.txt）。

---

## 3. 权威侧接口（新增，接口变更在 §8）

```cpp
// game/common/include/opencraft/game/protocol.hpp
enum class ActionKind : std::uint8_t { Dig, PlaceBlock, PourWater, ScoopWater };

struct ActorPose { glm::dvec3 feet; double height; double eye_height; };
struct ActionRequest {
    ActionKind kind; glm::ivec3 target; std::uint16_t item_or_block;
    ActorPose actor; std::uint32_t sequence;   // 预留，本卡不填（卡面允许）
};

enum class ActionReject : std::uint8_t {
    None, OutOfWorld, ChunkNotLoaded, OutOfReach, NothingToDig, Unbreakable,
    UnknownBlock, CellOccupied, IntersectsActor, NotAWaterSource,
};
[[nodiscard]] const char *action_reject_reason(ActionReject);   // 稳定字符串，测试与日志共用

struct ActionResult { bool accepted; ActionReject reject; [[nodiscard]] const char *reason() const; };

struct WorldChanges { std::vector<std::pair<int,int>> dirty_chunks; };

class IAuthority {
    virtual ActionResult submit(const ActionRequest &) = 0;
    virtual void tick() = 0;
    virtual WorldChanges take_changes() = 0;
    virtual std::size_t autosave_pass() = 0;
};
```

校验规则表（权威侧唯一判定点，全部有单测）：

| 规则 | 拒绝码 | 备注 |
|---|---|---|
| `target.y ∉ [0, 384)` | `OutOfWorld` | `Chunk::kSizeY` |
| 目标区块未加载 | `ChunkNotLoaded` | 写入本会被丢弃 |
| 眼到**格子最近点**的距离 > 4.5 | `OutOfReach` | 用最近点而非格心，见下 |
| 挖空气 / 挖未知 id | `NothingToDig` | |
| 挖 `hardness < 0` | `Unbreakable` | 基岩；与挖掘状态机同规则 |
| 放空气 / 未知 id | `UnknownBlock` | |
| 放/倒到不可替换格 | `CellOccupied` | `is_replaceable`（空气或液体） |
| 放与玩家 AABB 重叠 | `IntersectsActor` | 严格不等式（`check_placement` 原样） |
| 舀非水源 | `NotAWaterSource` | `is_water_source` 单实现 |

**可达判定为什么用"格子最近点"**：客户端射线以 4.5 为上限，命中格子时其进入点必然
≤ 4.5，而进入点属于该格 AABB 的边界 ⇒ 最近点距离 ≤ 进入距离 ≤ 4.5。
放置格（命中方块再往前一格）与命中格共享被穿过的那个面，同理通过。
因此这条判定**不可能拒绝客户端射线已经命中的目标**——这是本卡最大的风险点，专门有一个
扫描式单测守着（见 §4 第 5 例），实机四次会话也从未出现 `out of reach` 误拒。

---

## 4. 测试（新增 9 例 / 284 断言）

`tests/test_authority.cpp`：

1. **dig 破块并回推精确的脏区块集**：`(0,132,0)` 左下表 → 期望脏区块恰为
   `{(-1,-1),(-1,0),(0,-1),(0,0)}`（自身 + 光照可达的邻+对角），并断言 `take_changes()` 只能取一次。
2. **逐字节一致（验收 4）**：权威侧 `Dig → PlaceBlock` 之后的区块序列化字节，
   与「同一 seed 的 `TerrainGenerator` 重新生成 + 引擎 `Chunk::set_block` 直接写」逐字节相同。
   参考路径**完全绕过模拟**（没有 `WorldSim` 参与），所以这是真的两条路径对比。
3. **被拒绝的请求不碰世界**：放置到玩家自身格子 → `IntersectsActor`，前后区块字节相同，脏区块为空。
4. **拒绝表**：空气挖 / 基岩挖 / 界外 / 未加载区块 / 超距 / 未知 id（0 与超界）/ 占用格（放+倒）/
   玩家自身格 / 非水源舀 —— 逐条断言 `accepted == false`、`reject` 精确、`reason()` 非空。
5. **★ 客户端射线能命中的每一格都必须被接受**：以同一 eye 与同一 `kReachDistance`
   对 48×13 个方向做 `raycast_voxel`，对每个可挖命中提交 `Dig` 并 `REQUIRE(accepted)`，
   再放回原方块继续扫描（`checked > 100` 保证扫描真的看过世界）。
6. **`ActionReject` 每个码都有非空理由**（验收 3 的"reason 有值"逐码覆盖）。
7. **倒水 → 流体 tick → 舀水**：挖坑 → 倒水（`is_water_source` 真、`fluid_height_at > 0`、
   此刻 `take_changes()` 为空——流体脏区块由 `tick()` 冲刷，与 T-F1 时同序）→ 舀回
   （水源消失、占位方块回到空气、再舀被拒 `NotAWaterSource`）→ 再倒一次后 `tick()` ×6，
   坑底变湿（下落水）且回推非空。

---

## 5. 构建 / 运行 / 测试方法

```bash
# 构建（禁接管道；worktree 里不要设 FETCHCONTENT_BASE_DIR）
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-A1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # 45 s（已 configure 过则秒过）
cmake --build build -j8                             # 100 s 级

# 全量测试
./build/tests/opencraft_tests                       # 317 cases / 9719 assertions

# 干净检出复验（另建目录，源目录仍是本 worktree）
cmake -S . -B /tmp/ta1_clean_build && cmake --build /tmp/ta1_clean_build -j8
/tmp/ta1_clean_build/tests/opencraft_tests

# 实机
cd build && rm -rf saves && ./opencraft              # 固定 seed ⇒ spawn (0.5, 132.0, 0.5)

# 格式（CLT 的 17，不要用 brew 的 23）
/Library/Developer/CommandLineTools/usr/bin/clang-format --dry-run --Werror \
  game/common/include/opencraft/game/protocol.hpp game/common/src/protocol.cpp \
  game/server/sim/include/opencraft/sim/world_sim.hpp game/server/sim/src/world_sim.cpp \
  game/client/src/world.hpp game/client/src/world.cpp game/client/src/tick.hpp \
  game/client/src/tick.cpp game/client/src/main.cpp game/client/src/client_config.hpp \
  tests/test_authority.cpp                            # 无输出 = 无 diff
```

---

## 6. 结果

| 项 | 结果 |
|---|---|
| 构建 | 成功，**0 error / 0 warning**（除 §7.3 的链接期既有警告） |
| 测试 | **317 / 317 通过，9719 / 9719 断言**（基线 308/9435 → 新增 9 例 / 284 断言） |
| 干净检出 | ✅ 另建空目录 `/tmp/ta1_clean_build`（源为本 worktree，依赖现场下载）：configure + build **0 error**，全量 **317/317**（独立 build 目录，不污染 worktree） |
| clang-format | CLT 17 `--dry-run --Werror` 无输出 |
| 验收 1（客户端零直写） | `grep -rn "\.set_block(\|\.place_water_source(\|\.remove_water_source(\|\.set_fluid_at(\|\.fluid_step(" game/client/` → **无匹配** |
| 验收 2（行为不变） | 四项实机逐项复验（证据 A/B）+ §4 第 2 例逐字节一致 + 存量 308 例全绿 |
| 验收 3（拒绝可测） | §4 第 4/6 例 + 实机两条真实 `refused`（证据 C） |
| 验收 4（回推一致） | §4 第 2/1 例（逐字节 + 脏区块集）+ 实机 40 条 remesh（证据 D） |
| 验收 6（无网络） | 未新增任何 include/库到 socket/ENet；`engine/net` 仍只有 CMakeLists.txt |
| 验收 9（实机证据） | `docs/qa/T-A1-2026-09-16/`（四项 + 流体 + 拒绝 + 无崩溃） |

---

## 7. 已知问题与边界外残留（如实申报）

### 7.1 挖的"何时破"仍在客户端（本卡边界内，但 M3 必须上收）

`MiningTracker`（T008，`game/common`）仍由客户端驱动：**客户端决定"这一刻破了"**，
权威侧只复核"该不该破"（可达 / 已加载 / 可破坏 / 非空气）。所以严格说，
"挖掘时长"目前不是权威判定。真正服务端权威应由服务端计时（M3 的反作弊范围）。
把它上收需要把每 tick 的挖掘进度也变成请求/回推（形态与"20 TPS 服务端"耦合），
**本卡不做**（卡面明确不做预测/和解），在此列为已知残留。

### 7.2 挖掘成功没有常驻日志（证据用临时打点取的，已还原）

`tick.cpp` 挖掘分支原本不打印任何东西，本卡也**没有**为它新增常驻日志
（保持"行为不变"，也让实机日志与基线可逐行对比）。实机证据因此用了项目既有手法
**临时打点 → 取证 → 还原**：补丁 16 行落在 `docs/qa/T-A1-2026-09-16/temporary_harness.patch`，
取证后 `tick.cpp` 逐字节还原并重编，最终二进制已验证 `grep -c HARNESS` = 0。
**如果 PM 希望以后实机验收不必打点，建议单独开一张"挖掘日志"小卡**（属行为可观测性，不属本卡）。

### 7.3 链接期 `ignoring duplicate libraries` 警告（2 条，基线 1 条）

`opencraft_sim` PUBLIC-link 了 `opencraft_game/worldgen/storage/physics`，而
`opencraft`（客户端）与 `opencraft_tests` 也显式列了这些库，于是 macOS `ld` 报重复。
**该类警告基线已存在**（客户端链接 physics+render 时就有 1 条），本卡使其变成 2 条。
未清理的理由：删掉显式依赖会让"谁需要什么"变得不可读（那几行都带注释说明用途）。
**请 PM 裁决**：保留（现状）或改为只依赖 `opencraft_sim` 的传递依赖。

### 7.4 其它残留（都不影响本卡验收）

- 独立服务端进程仍不存在（`opencraft_server` 只是 INTERFACE 目标）；`WorldSim` 由客户端固定步驱动。
  **M3 需要它自持 20 TPS** —— 接口已经是"`tick()` 由外部调用"的形态，换驱动方不改调用点。
- 客户端仍有 3 处"非世界内容"的权威调用：`ensure_chunk`（流式生成）、`autosave_pass`（存档）、
  `unload_chunk`（本卡未被调用）。建议 **T-D4（区块流式）** 顺手改成请求/事件形态。
- 逐方块事件未建（无消费者，见 §1.1 第 5 条）。
- `sequence` 字段预留但从不填（卡面允许）。
- 实机证据的一个缺口：**"水面随时间增长"的分帧曲线没拍到**（相机离水太近，视野内水面
  0.3 s 就铺满，后续帧与首帧逐像素相同）。流体推进的时序证据落在 40 条 `remeshed` 回推行与
  单测上。详见 `docs/qa/T-A1-2026-09-16/README.md`「未归档的失败尝试」。

---

## 8. 接口变更（供 PM 落规格）

| 新增/变更 | 位置 | 后续卡可依赖？ |
|---|---|---|
| `game::ActionKind` / `ActionRequest` / `ActorPose` / `ActionReject` / `ActionResult` / `WorldChanges` / `IAuthority` | `game/common/include/opencraft/game/protocol.hpp`（**两端共享**，符合 `docs/03 §1`「common 放协议消息定义」） | 是（M3 网络化直接复用） |
| `game::kReachDistance = 4.5` | 同上（单一真相源） | 是 |
| `server::WorldSim`（`kSeed` / `kWorldHeight` / 全部只读查询 / `submit` / `tick` / `take_changes` / 生命周期） | `game/server/sim/include/opencraft/sim/world_sim.hpp` | 是 |
| `opencraft_sim` 静态库（`opencraft_server` 现指向它） | `game/server/CMakeLists.txt` | 是 |
| `client::WorldSource` 语义变更：**从"世界的实现"变为"只读视图"**（构造参数由 seed 变为 `const WorldSim&`；`kSeed`/`kWorldHeight` 移到 `WorldSim`；`set_block`/`place_water_source`/`remove_water_source`/`fluid_step`/`set_fluid_at`/`ensure_chunk`/`unload_chunk`/`find_spawn`/`autosave_pass`/`attach_save`/`surface_height` 全部移除） | `game/client/src/world.hpp` | **是，且是破坏性变更**；`mesh_chunk` / `HudResources` 签名未变 |
| `TickContext` 增 `game::IAuthority &authority`（新增成员，非破坏） | `game/client/src/tick.hpp` | 是 |

**未放宽/未删除任何既有断言**：`git diff --name-only tests/` 只有 `tests/CMakeLists.txt`，
既有 30 个测试文件**一个字节未改**（新增的 `test_authority.cpp` 是全新文件）。

---

## 9. 给 PM 的备注（建议表，请由你落盘）

| # | 建议 | 落点 |
|---|---|---|
| S-1 | `docs/03 §1` 补一句现状：权威侧**已存在**（`game/server/sim`，`server::WorldSim`），客户端持只读视图 + `IAuthority` 通道；"同仓异进程"仍待 M3 | `docs/03-architecture.md §1` |
| S-2 | `docs/03 §2`/§8 可记：`WorldSim` 目前由客户端固定步驱动 `tick()`，独立服务端须自持 20 TPS（调用点不变） | `docs/03 §2` / `§8` |
| S-3 | T-D4（区块流式）卡面**加一条**：把 `ensure_chunk` / `unload_chunk` / `autosave_pass` 的客户端调用改成请求/回推形态（本卡已把它们放在权威侧对象上，只差形态） | `docs/tasks/` 与债务表 |
| S-4 | M3 卡面须包含三件本卡**故意没做**的事：①库存搬入权威侧（`item_or_block` 改为只带槽位/意图）②挖掘计时上收（§7.1）③逐方块事件（客户端出现副本时，`WorldChanges` 加 `blocks`） | M3 规划 |
| S-5 | 架构备注补两条加固：`WorldSim` 禁止拷贝/移动（自指流体适配器）；`kReachDistance` 单一真相源在 `game/protocol.hpp` | `docs/03 §10` 或 §7 |
| S-6 | 请裁决 §7.3 的链接警告（保留 / 改为传递依赖），以及 §7.2 是否单独开"挖掘日志"小卡 | STATE.md |
| S-7 | 本卡对卡面结构的**主动扩充**需追认：`ActionRequest` 增 `ActorPose`（验收 3 要求权威侧能判可达，而权威侧手上没有玩家状态）；卡面 §"接口契约"里列了"库存改动"为动作之一，但同段建议结构把方块 id 放在请求里，两者不自洽——本卡按"库存留客户端、M3 随玩家上收"理解 | `docs/tasks/T-A1.md` 变更记录 |

---

## 10. 验收标准逐条核对

| # | 验收项 | 结果 | 证据 |
|---|---|---|---|
| 1 | 客户端零直写（grep） | ✅ 无匹配 | §6 命令行；另：视图类**没有**写方法（比 grep 更强） |
| 2 | 行为不变（四项实机一致） | ✅ | 证据 A/B；§4 第 2 例逐字节 |
| 3 | 命令校验可测（拒绝 + reason 有值） | ✅ | §4 第 4/6 例；实机两条 `refused`（证据 C） |
| 4 | 回推一致（逐字节） | ✅ | §4 第 1/2 例；实机 40 条 `remeshed`（证据 D） |
| 5 | 存量测试全绿（308 → 317） | ✅ | 既有测试文件零改动；317/317、9719 断言 |
| 6 | 无网络依赖 | ✅ | `engine/net` 仍空；无 socket include |
| 7 | clang-format（CLT 17）无 diff；测试名无 `[` | ✅ | §5 命令无输出；新测试名无 `[` |
| 8 | 独立 worktree + `taskT-A1:` 提交前缀 | ✅ | `/Users/happy/Desktop/opencraft_worktree/opencraft-T-A1`，分支 `task/T-A1-authoritative-side` |
| 9 | 实机证据（四项逐项，含截图或日志） | ✅ | `docs/qa/T-A1-2026-09-16/`（README 索引 + 4 份会话日志 + 19 张截图 + 工具/补丁） |
| 10 | 报告落盘 + 说明边界与理由 | ✅ | 本文件 §1.1 |
| 11 | agentmemory action 置 done | ✅ | `act_mu347urt_99a403c1f15e` |
