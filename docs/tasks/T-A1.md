# 任务 T-A1：权威侧前置（进程内世界模拟分离）

里程碑：M2b（★ 从原 M3 提前）　前置：**T-I2（库存接线）**
基线：**297/297**（T-I2 后可能变化，**运行前请先跑基线确认实际数字**）
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

在**不引入网络**的前提下，把"世界模拟"从客户端里分离出来：
建立一个**权威侧（authoritative side）**，所有会改变世界的动作都必须经过它，
客户端**不再直接改世界**，只能通过**命令/事件**提出请求、接受权威侧回推的结果。

**本卡是进程内的职责分离，不是多人。** 网络只是以后把这条通道换掉（M3）。

## 为什么（这是本项目最贵的一次重构，越晚做越贵）

PM 于 2026-09-16 扫仓库得到的实测事实：

| 事实 | 含义 |
|---|---|
| `engine/net` **0 个源文件**（只有 CMakeLists.txt） | 网络层完全不存在 |
| `OPENCRAFT_BUILD_SERVER` 选项**从未被任何目标使用** | 服务端进程是空的 |
| `game/server/` 实际只有 worldgen + storage 两个库 | **没有世界模拟逻辑** |
| 客户端 `tick.cpp` 直接调用 `world.set_block` / `place_water_source` / `fluid_step` | 客户端**既是预测者又是权威** |

`docs/03 §1` 写着「从第一天就是服务端权威（Veloren 教训）」——**现实是权威侧从未建立**。

**为什么现在做**：客户端里"直接改世界"的调用点现在只有 5 处
（`tick.cpp` 的 `set_block` / `place_water_source` / `remove_water_source` / `fluid_step`）。
**每做一张内容卡就多一批**（合成产出、掉落物、生物破坏、爆炸、红石……）。
M2c 做完再拆，改动面会大一个数量级。

用户裁决（2026-09-16）：**结构优先**；路线图已把权威侧从 M3 提前到 M2b
（见 `docs/06-roadmap.md` 修订记录）。

## ★ 范围（严格，越界即返工）

**做**：

1. **权威侧接口**：定义一个世界模拟侧，拥有世界的**唯一写权限**。
   建议落在 `game/server/`（那里已有 worldgen + storage），具体形态你定。
2. **命令/事件通道**：客户端的动作（挖 / 放 / 倒水 / 舀水 / 库存改动）变成**请求**，
   经通道交给权威侧；权威侧执行后回推结果。
3. **收口客户端直写**：`game/client/src/tick.cpp` 里那 5 处直接改世界的调用，
   全部改为发请求。**客户端不得再直接调用 `WorldSource` 的写方法。**
4. **结果回推**：权威侧执行后，客户端的世界表现（方块变化、dirty chunk、流体推进）
   由权威侧驱动，**行为与现在一致**。
5. **单测**：命令可执行性校验、拒绝非法请求、结果回推一致性。

**不做**（看到了也不要动）：
- ❌ **不引入网络**（不写 socket、不引 ENet、不动 `engine/net`）本卡是**进程内**分离
- ❌ 不做客户端预测与和解（那需要权威侧先存在；M3）
- ❌ 不做多客户端、不做会话管理、不做玩家管理
- ❌ 不做权限/反作弊（M3）
- ❌ 不做区块流式加载与卸载（那是 M2b 的第二张卡，债务 T-D4）
- ❌ 不改物理数值、不碰 `engine/physics/**`
- ❌ 不改 `docs/01` / `docs/03` 规格（PM 职权）

## 接口契约

### 冻结项（不得改动语义）

- `engine/physics/**` **零 diff**（`step_player` 及其契约一律不动）
- `voxel::Chunk`、`voxel::FluidSim`、`storage::WorldSave` 的既有接口语义不变
- 既有 297 个测试的行为不变（可能需调整调用方，但**不得放宽或删除断言**）

### 锚点（**用符号定位，不要用行号**）

⚠️ 本卡在 T-I2 之后开工，`game/client/src/tick.cpp` 与 `interaction.hpp` 的行号**会漂移**。
**请一律用 `grep` 定位下列符号**，不要相信本卡写的行号：

| 要收口的调用 | 符号 |
|---|---|
| 挖掘（置空气） | `tick.cpp` 中 `ctx.world.set_block(... , 0, ...)` |
| 放置方块 | `tick.cpp` 中 `ctx.world.set_block(cell.x, cell.y, cell.z, ctx.state.selected_block, ...)` |
| 倒水 | `ctx.world.place_water_source(...)` |
| 舀水 | `ctx.world.remove_water_source(...)` |
| 流体推进 | `ctx.world.fluid_step(ctx.dirty_chunks)` |

`WorldSource` 的写方法定义在 `game/client/src/world.hpp` / `world.cpp`：
`set_block` / `place_water_source` / `remove_water_source` / `fluid_step` / `set_fluid_at`。

**T-I1 裁决提示**：`TickContext`（`game/client/src/tick.hpp`）是天然的分离切口
（其成员全是 main 局部量的引用，不复制）——T-M1 开发者建议以它为边界，PM 记录采纳。

### 建议形态（你自定）

```cpp
// 仅示意，形态你定
namespace opencraft::server {

// 客户端 → 权威侧
enum class ActionKind { Dig, Place, PourWater, ScoopWater, ... };
struct ActionRequest {
    ActionKind kind;
    glm::ivec3 target;
    std::uint16_t item_or_block;   // 放置用
    std::uint32_t sequence = 0;    // 为 M3 预测/和解预留（本卡可不填）
};

// 权威侧 → 客户端
struct ActionResult {
    bool accepted = false;
    // 被拒绝的原因（用于日志与以后给玩家反馈）
    const char *reason = "";
};

class WorldSim {   // 名字你定
public:
    [[nodiscard]] ActionResult apply(const ActionRequest &req);
    void tick();                   // 权威侧的 20 TPS：流体推进等
};
}
```

**关键约束**：
- 权威侧**唯一持有世界写权限**；客户端侧的世界对象退化为**只读视图 + 接收回推**。
- 通道是**进程内**的（直接函数调用即可），但**接口形状要能容纳将来的网络**
  ——即：请求是**值语义、可序列化的数据**，不是回调、不是指针、不跨侧共享可变状态。
- 本卡**不做**预测与和解，但 `sequence` 字段建议预留。

## 允许触碰的文件/目录（白名单）

- `game/server/**`（主体：新增世界模拟）
- `game/common/**`（仅当命令/事件结构需要共享定义时；**最小化**）
- `game/client/**`（收口直写 + 接线；**必要**）
- `tests/**`（新增；需改 `tests/CMakeLists.txt`）
- `game/server/CMakeLists.txt`（新增源文件时按现有模式加）
- `docs/tasks/T-A1.report.md`（你**必须写**的报告）
- `docs/qa/T-A1-2026-09-16/`（实机证据）

⚠️ **禁碰**：`STATE.md`、`docs/**`（上述报告与证据目录除外）、
`engine/**`（尤其 **`engine/physics/**` 与 `engine/net`**）、
`cmake/`、根 `CMakeLists.txt`、`.github/`、`assets/`。

## 验收标准（逐条可执行）

1. **客户端零直写**：`game/client/**` 中不再出现对 `WorldSource` 写方法的直接调用
   （`set_block` / `place_water_source` / `remove_water_source` / `set_fluid_at`）。
   **唯一例外**：权威侧回推后的**应用**（若由客户端执行，必须明确标注为"应用回推"）。
   `grep` 可查 —— 这条会用 `grep` 验收。
2. **行为不变**：挖 / 放 / 倒水 / 舀水 / 流体推进，实机表现与当前一致
   （这是本卡的核心判据——**重构，不是改行为**）。
3. **命令校验可测**：非法请求被拒绝（如：对不可替换的格子放置、超出到达距离、
   对非水源舀水）。单测断言 `accepted=false` 且 `reason` 有值。
4. **回推一致**：权威侧执行一次动作后，客户端世界状态与"直接执行"的结果**逐字节一致**
   （可写测试对比两条路径）。
5. **存量测试全绿**：297（或 T-I2 之后的实际数字）全绿。
   **不得**删除或放宽既有断言；若某断言与新结构冲突，须**逐条在报告中交代 old → new**。
6. **无网络依赖**：本卡不得引入任何 socket / 网络库；`engine/net` 保持为空。
7. clang-format 无 diff（用 **CLT 的 17**：
   `/Library/Developer/CommandLineTools/usr/bin/clang-format`；**不要用 brew 的 23**）；
   测试名避免含 `[`。
8. 独立 worktree（根 `/Users/happy/Desktop/opencraft_worktree/`）；提交前缀 `taskT-A1:`。
9. **实机证据**（必须）：挖 / 放 / 倒水 / 舀水 四项**逐项**验证仍工作，
   含截图或日志。⚠️ 若用脚本注入按键，**必须 HID 层**，禁用 `osascript`；
   本机 HID 注入时灵时不灵（`docs/05` §3.1），把决定性场景放在生效窗口内。
10. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-A1.report.md`，
    对话中**只输出简短版 + 该路径**，用 **txt 代码块**包裹（`docs/05` §2 规则 4）。
    报告须说明：**你划分的权威/客户端边界是什么，以及为什么**。
11. 完成后置 agentmemory action **`act_mu347urt_99a403c1f15e`** 为 done。

## 已知风险与提示

- **这是全项目最贵的一次重构**，也是本卡被提前的唯一理由。**慢一点没关系，
  但边界要想清楚**——边界定错了，后面每张卡都会别扭。
- **构建不要接管道**（`| tail` 吞退出码）；判成败用 `cmd > log 2>&1; echo $?` 或搜 `error:`。
- **绝不要把 `FETCHCONTENT_BASE_DIR` 指向主仓库 `build/_deps`**（PM 已两次因此挂掉主仓构建）。
  worktree 里**不设**该变量，让它自行 configure（约 75–90 s，需联网）。
- 可执行文件在 `build/opencraft`；测试在 `build/tests/opencraft_tests`。
- **合规红线**：不粘贴反编译源码；本卡不引用外部资料则无需来源标注。
- **不得直接改状态与规格**（P-001）：建议一律以建议表写进报告交 PM 落盘。
- **最高回归风险**：T-F1 的水桶路径与流体推进。本卡改的是它们的**调用方式**，
  务必实机验证装水/倒水/水流仍工作（验收 9）。
- **主动纠正记功**：发现卡面错误、接口不合理、边界划错更省事，直接写进报告。
  本项目已 7 次由执行方纠正 PM，这是被鼓励的。**边界怎么划你比我有发言权。**

## 附：为什么不做预测与和解

客户端预测需要"权威侧存在"作为前提。本卡只建立这个前提：
请求有 `sequence`、结果是数据（可网络传输），但**本卡不做预测、不做回滚、不做插值**。
那些是 M3（把进程内通道换成网络时）才需要的东西。一次做两件事会不可验收。
