# 任务 T-D4：区块流式加载与卸载策略

里程碑：M2b（权威侧前置的第二张）　前置：**T-A1（已合入 main `98ae090`）**
基线：**317/317**。运行前请先跑基线确认。
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

实现"随玩家移动加载、离开视野卸载"的区块流式管理：
围绕玩家维持一个视距内的区块集合，按每帧预算生成与网格化，**离开视野的区块先落盘再释放**，
并把 T-A1 裁决要求"改成请求形态"的三处维护性调用
（`ensure_chunk` / `unload_chunk` / `autosave_pass`）收进权威侧接口。

## 为什么

这是挂了三天的老债务（T-D4，来源 T009 报告），现在有两条理由必须做：

1. **`unload_chunk` 已实现但从未被驱动**。
   `game/server/sim/src/world_sim.cpp:253` 有 `WorldSim::unload_chunk`（内含
   `light_.forget_chunk`，`engine/voxel/src/light_engine.cpp:35` 的钩子 T006 就实现了），
   但客户端**没有任何"出视野即卸载"的逻辑** ⇒ 内存里的区块只增不减。
2. **T-A1 裁决的连带要求**：客户端现在仍直接调用 `authority.ensure_chunk(...)`
   与 `authority.autosave_pass()`（见下方锚点），这些属于**世界管理**，
   按 T-A1 建立的边界应由权威侧提供请求形态的接口，而不是客户端直接驱动。

## ★ 范围（严格，越界即返工）

**做**：

1. **流式加载**：围绕玩家维持 `kViewRadius` 半径内的区块集合；
   按每帧预算生成（`kGenPerFrame`）与网格化（`kNewMeshPerFrame`）。
   ⚠ **这部分当前已能工作**——本卡的增量是把它**收进权威侧**并**补上卸载**。
2. **卸载策略（本卡的核心增量，当前完全缺失）**：
   - 判定"出视野"的区块（离开视距 + 一点滞回，避免在边界反复加载/卸载）
   - **卸载前先把脏数据落盘**（T006 遗留要求：脏数据先落盘，不能丢）
   - 释放：区块数据 + 光照存储 + 该区块的渲染资源
3. **三处维护性调用改请求形态**（T-A1 裁决要求）：
   `ensure_chunk` / `unload_chunk` / `autosave_pass`
4. **单测**：卸载判定的边界、卸载不丢数据、滞回有效。

**不做**（看到了也不要动）：
- ❌ **不引入网络**（M3 才把进程内通道换成网络）
- ❌ 不做异步/worker 线程生成（当前是同步+每帧预算，**保持现状**）
- ❌ 不做 LOD、不做视距可配置（设置菜单留到 M6）
- ❌ 不改物理数值、不碰 `engine/physics/**`
- ❌ 不改 `docs/01` / `docs/03` 规格（PM 职权）
- ❌ **不修任何流体缺陷**（T-D29 是另一张卡，本卡不要顺手改 `fluid_sim.cpp`）

## 接口契约

### 冻结项（不得改动语义）

- `voxel::Chunk` / `voxel::LightEngine` / `voxel::FluidSim` / `storage::WorldSave` 既有接口
- `IAuthority` 的四个既有虚函数（`submit` / `tick` / `take_changes` / `autosave_pass`）语义
- 既有 317 个测试的行为不变

### 锚点（**用符号定位，不写死行号**）

| 位置 | 符号 |
|---|---|
| 权威侧（新增接口落这里） | `game/server/sim/include/opencraft/sim/world_sim.hpp`（238 行） |
| 已有但未驱动的卸载 | `WorldSim::unload_chunk`（`world_sim.cpp:253`，含 `light_.forget_chunk`） |
| 协议/接口定义 | `game/common/include/opencraft/game/protocol.hpp`（126 行）的 `IAuthority`、`ActionRequest`、`WorldChanges` |
| 客户端现在直接调的三处 | `main.cpp` 中 `authority.ensure_chunk`（第 129/147/455 行附近）、`authority.autosave_pass`（第 694 行附近）、`tick.cpp` 中 `ctx.authority.autosave_pass`（第 358 行附近） |
| 流式常量 | `game/client/src/client_config.hpp`：`kViewRadius = 6`、`kGenPerFrame = 2`、`kNewMeshPerFrame = 4` |
| 渲染资源表 | `main.cpp` 中的 `renderables`（`ChunkRenderableMap`，见 `game/client/src/chunk_renderer.hpp`） |
| 脏区块与耗时हे | `dirty_chunks`、`last_mesh_ms` |

⚠️ 行号会漂；**一律 `grep` 定位符号**。

### 建议形态（你自定）

T-A1 建立了"请求 = 值语义可序列化数据"的范式，本卡**沿用**（M3 换网络时不改调用点）：

```cpp
// 仅示意
struct StreamRequest {
    glm::dvec3 viewer_position;   // 或 int chunk 坐标 + 视距，你定
    int generate_budget = 0;      // 本帧可生成的区块数
};

// IAuthority 上新增/调整（不要破坏既有四个虚函数）
[[nodiscard]] virtual StreamResult stream(const StreamRequest &req) = 0;
// StreamResult 里带回：本帧新生成的区块、被卸载的区块（供客户端清渲染资源）
```

**关键约束**（沿用 T-A1 的范式）：
- 请求与结果是**值语义可序列化数据**（坐标 + int + 枚举），无回调/指针/共享可变状态；
- 客户端**不得**直接驱动 `WorldSim` 的 `ensure_chunk` / `unload_chunk`；
- 渲染资源（`renderables`）的清理由客户端依据结果执行——**渲染资源属客户端，世界数据属权威侧**。

## 允许触碰的文件/目录（白名单）

- `game/server/sim/**`（主体：流式与卸载策略）
- `game/common/**`（`IAuthority` 接口扩展；**最小化**）
- `game/client/**`（接线：改调用点、清渲染资源）
- `tests/**`（新增；需改 `tests/CMakeLists.txt`）
- `docs/tasks/T-D4.report.md`（你**必须写**的报告）
- `docs/qa/T-D4-2026-09-16/`（实机证据）

⚠️ **禁碰**：`STATE.md`、`docs/**`（上述报告与证据目录除外）、
**`engine/physics/**`**、**`engine/voxel/src/fluid_sim.cpp`（T-D29 是另一张卡，别顺手改）**、
`cmake/`、根 `CMakeLists.txt`、`.github/`、`assets/`。
**未引入网络** ⇒ `engine/net` 保持为空。

## 验收标准（逐条可执行）

1. **卸载真的发生**：玩家走远后，原视野内的区块被卸载。
   断言方式：`chunk_ready(...)` 对已离开的区块返回 false，或提供可查询的已加载区块数。
2. **卸载不丢数据**（本卡最重要的判据）：
   卸载前脏数据落盘 ⇒ **走回头路时玩家的改动还在**（挖的坑、放的方块、倒的水）。
   ⚠ 必须实机验证：走开一段再走回来，改动仍在。
3. **滞回有效**：在视距边界来回走动，**不得**反复加载/卸载同一个区块。
   （有明确滞回量，你定，但须可断言并写进报告）
4. **资源被释放**：卸载后该区块的渲染资源（`renderables` 里的条目）也被清掉，
   内存/资源不泄漏。
5. **三处维护性调用已改请求形态**：客户端不再直接调用
   `authority.ensure_chunk` / `unload_chunk` / `autosave_pass`（`grep` 可查）。
6. **存量测试全绿**：317 全绿。**不得**删除或放宽既有断言；
   若某断言与新结构冲突，须**逐条在报告中交代 old → new**。
7. **无网络依赖**：未引入 socket / 网络库；`engine/net` 仍为空。
8. clang-format 无 diff（用 **CLT 的 17**：
   `/Library/Developer/CommandLineTools/usr/bin/clang-format`；**不要用 brew 的 23**）；
   测试名避免含 `[`。
9. 独立 worktree（根 `/Users/happy/Desktop/opencraft_worktree/`）；提交前缀 `taskT-D4:`。
10. **实机证据**（必须）：
    - 走一段长距离，日志显示有区块被卸载（不是只增不减）
    - **走回头路，改动仍在**（验收 2 的实证）
    - 帧率无明显退化（与当前 53–72 fps 同量级）
    ⚠️ 若用脚本注入按键，**必须 HID 层**，禁用 `osascript`；
    本机 HID 注入时灵时不灵（`docs/05` §3.1），把决定性场景放在生效窗口内。
    ⚠️ **HID 注入约 9 tick 后会被失焦清掉**，"按住 W 走很远"做不到 ⇒
    改用**改写存档坐标**或**传送**来制造长距离位移，不要硬撑注入时长。
11. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-D4.report.md`，
    对话中**只输出简短版 + 该路径**，用 **txt 代码块**包裹（`docs/05` §2 规则 4）。
    报告须说明**滞回量取值与依据**。
12. 完成后置 agentmemory action **`act_mu48sely_7999e590ff14`** 为 done。

## 已知风险与提示

- **构建不要接管道**（`| tail` 吞退出码）；判成败用 `cmd > log 2>&1; echo $?` 或搜 `error:`。
- **绝不要把 `FETCHCONTENT_BASE_DIR` 指向主仓库 `build/_deps`**（PM 已两次因此挂掉主仓构建）。
  worktree 里**不设**该变量，让它自行 configure（约 75–90 s，需联网）。
- 可执行文件在 `build/opencraft`；测试在 `build/tests/opencraft_tests`。
- **合规红线**：不粘贴反编译源码；本卡不引用外部资料则无需来源标注。
- **不得直接改状态与规格**（P-001）：建议一律以建议表写进报告交 PM 落盘。
- **最高回归风险**：卸载逻辑若误判，会**丢玩家改动**（最坏情况）。
  验收 2 是硬判据，务必实机验证。
- **卸载与光照的交互**：`LightEngine::forget_chunk` 已存在且 T006 验证过，
  但注意"跨区块 offer 重放"——卸载一个区块后，其邻居若还持有指向它的 deferred offer，
  须按 T006 既有逻辑处理（不要另发明一套）。
- **主动纠正记功**：发现卡面错误、接口不合理，直接写进报告。
  本项目已 8 次由执行方纠正/改进 PM，这是被鼓励的。

## 附：本卡与相邻卡的关系

| 机制 | 本卡 | 后续 |
|---|---|---|
| 加载（生成+网格化，每帧预算） | ✅ 收进权威侧（当前已能工作） | — |
| **卸载（出视野，脏数据先落盘）** | ✅ **本卡核心增量** | — |
| 异步/worker 线程生成 | ❌ | M4+ 性能卡 |
| LOD / 视距可配置 | ❌ | M6 |
| 网络化的区块发送 | ❌ | M3（换通道） |
| **水单向扩散缺陷** | ❌ **别碰** | T-D29（另一张卡） |
