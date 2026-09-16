# T-D4 开发者报告：区块流式加载与卸载策略

里程碑 M2b（权威侧前置第二张）　前置 T-A1（已合入 main `98ae090`）
交付提交：`5d49f3e`（worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-D4`，分支 `task/T-D4-chunk-streaming`）
基线 317/317 → **324/324**（317 存量全绿 + 7 新增）

---

## 1. 变更摘要

1. **流式与卸载收进权威侧，用一个请求形态的动词表达**：`game::IAuthority::stream(StreamRequest) -> StreamResult`
   （值语义、可序列化；M3 换网络通道不改调用点）。一个调用按**固定顺序**做完三件事：
   生成（按 `generate_budget` 就近）→ 释放（`unload_radius` 之外，**脏数据先落盘再释放**）→ 持久化窗口（`persist`）。
   顺序写在同一个函数里，客户端无法把"先释放后落盘"搞反——这是本卡把三件事收进一个动词的主要理由。
2. **`ensure_chunk` / `unload_chunk` 收为 private**：客户端现在拿不到逐区块的装载/卸载入口
   （T-A1 的"用访问说明符强制边界"，而不是靠 everyone 记得别调）。`autosave_pass()` 语义不变、仍在接口上，
   但客户端不再直接驱动它（请求形态走 `stream(persist = true)`）。
3. **卸载策略**：`resident_` 记录工作集（`ChunkManager` 没有遍历接口，而这一个类就是唯一的装载者），
   每次 `stream()` 扫一遍常驻集合，Chebyshev 距离 > `unload_radius` 的区块走既有 `unload_chunk()`
   （内部就是 T006/T009 那套：脏则 `store_chunk_sync` → `light_.forget_chunk`（含丢弃指向它的 deferred offer）→ 释放方块数据）。
4. **滞回**：`kGenRadius = kViewRadius + 1 = 7`（生成窗口，与改造前的偏移表同形），
   `kUnloadHysteresis = 2`，`kUnloadRadius = 9`。**依据见 §1.1。**
5. **客户端接线**：启动 5×5、每帧流式、tick 的自动存档窗口、退出 flush 四处全部改请求形态；
   释放结果驱动 `renderables.erase(chunk_key)`（渲染资源属客户端，世界数据属权威侧）。
6. **单测 7 例**：窗口生成/预算/不重复生成、越窗释放与上界、释放丢数据与丢光照、脏区块落盘后还原（含倒的水）、
   滞回三段（零抖动 / 无滞回对照 / 越界释放）、持久化窗口、未编辑区块释放后重生成逐字节相同。
7. **实机取证（自动 HID + 独立读盘）**：见 `docs/qa/T-D4-2026-09-16/`（README 已索引）。
   核心两条：`stream: released 25 chunk(s), 2 resident, 0 meshed`（走远后真的释放），
   以及存档文件里 (1,132,0)=stone / (0,131,0) 失去 grass_block / (0,132,-1)=water+source
   （走回来时三处改动仍在，且两行未编辑对照格与 worldgen 完全一致）。

### 1.1 滞回量取值与依据（卡面验收 3 要求写清）

**取 2（`kUnloadHysteresis = 2`，即保留窗口比生成窗口宽 2 个区块 = 32 格；总常驻上界 19×19 = 361 区块）。**

- **为什么不能是 0**：玩家在视距边界来回走是常态。零滞回时，越过一格边界就会释放身后那一列 15 个区块，
   走回来又全部重新生成——而且**每一个脏区块都要再落一次盘**（T009 的写盘代价按区块算）。
   单测里的对照用例（同一节奏、`unload_radius == generate_radius`）实打实地展示这个抖动。
- **为什么是 2 而不是 1**：带宽决定的是"允许的来回幅度"。区块在 `c` 处生成后，只在 `|c_now - c| > unload - gen`
  时才被释放 ⇒ 滞回 H 允许**幅度 ≤ H 个区块的来回**完全无动作。行走速度 ≈ 4.3 格/s（疾跑 5.6），
   一个区块 16 格 ≈ 3–4 秒，所以 H=1 只挡住"来回不到 16 格"的犹豫；H=2 挡住"来回不到 32 格"
  （约 6–8 秒的犹豫），覆盖真实玩家在边界反复试探的做法，并且留出余量给 T-D13 的单帧位移/未来的速度改动。
- **代价与收益**：H=2 比 H=0 多保留一圈（361 vs 225 个区块）。每个区块约 0.6 MiB
  （方块 16×384×16×2 B ≈ 192 KiB + 光照 sky/block 两通道 ≈ 192 KiB + 流体分层按需），一圈 ≈ 80 MiB。
  换来的是：**常驻集合从"只增不减"变成固定上界**（改造前走 1000 格就是上千个区块、数百 MiB 且永不回收），
  以及边界试探不再触发写盘。取 1 可以省下约 40 MiB，但那点收益不值得丢掉一半的滤波带宽；取 3 以上
  收益递减而常驻集合继续变大。**这个数是可调的（改 `client_config.hpp` 一个常量），也不是本卡要固化的规格。**
- **几何形状用 Chebyshev（方窗）而不是欧氏圆**：与客户端原有的偏移表同形，滞回带在四个方向等宽；
  用圆的话对角方向的余量会比正方向小。

### 1.2 主动纠正 / 与卡面不同的判断（3 条）

1. **卡面建议的 `StreamRequest` 里放 `glm::dvec3 viewer_position`，我改成 chunk 坐标 `(center_cx, center_cz)`。**
   理由：客户端本来就在 chunk 空间工作（`Chunk::chunk_coords`），用浮点位置会让"落在哪个区块"在两端各算一次
   （floor + 负数边界），而请求要跨网络；整数坐标没有这个歧义。两个半径（生成/卸载）也显式进请求，
   策略对两端都可见可测。
2. **卡面把"三处维护性调用"改成请求形态，我给的是**一个**动词而不是三个**（`ensure_chunk`/`unload_chunk`
   合成 `stream()`，`autosave_pass` 变成 `stream()` 的 `persist` 字段）。理由：(a) `game/common` 只增加
   2 个结构体 + 1 个虚函数，符合"最小化"；(b) **三件事的顺序是语义的一部分**（脏数据必须先落盘再释放、
   自动存档要在释放之后收尾），分成三个请求就把这个顺序交给调用方去记；(c) M3 迁移最省：
   客户端不再设置 `persist`，服务端自己的循环里跑同一段代码即可。若 PM 更希望"持久化"独立成一个动词，
   改动量约为 30 行（`PersistRequest/PersistResult` + 转调 `autosave_pass`），见 §9 建议表。
3. **`autosave_pass()` 保留在 `IAuthority` 上**（冻结项要求语义不变），但客户端不再调用它。
   卡面验收 5 的 grep 判据在 `game/client/` 下为 0 命中（见证据 E）。

---

## 2. 文件列表

| 文件 | 改动 |
|---|---|
| `game/common/include/opencraft/game/protocol.hpp` | +`StreamRequest` / `StreamResult`；`IAuthority::stream()`；`autosave_pass` 注释说明与 `stream(persist)` 的关系 |
| `game/server/sim/include/opencraft/sim/world_sim.hpp` | +`stream()` 声明与语义注释、+`loaded_chunk_count()`；`ensure_chunk`/`unload_chunk` 移入 private；+`mark_resident()`、`generate_offsets()`；+成员 `resident_`(std::set) / `gen_offsets_` 缓存；类注释补流式一条 |
| `game/server/sim/src/world_sim.cpp` | +`chunk_distance()`（Chebyshev）、+`stream()`、+`generate_offsets()`；`ensure_chunk` 两条装载路径统一走 `mark_resident`；`unload_chunk` 摘掉 `resident_` 条目并补注释 |
| `game/client/src/client_config.hpp` | +`kGenRadius`/`kUnloadHysteresis`/`kUnloadRadius`（含取值依据注释）、+`make_stream_request()` |
| `game/client/src/main.cpp` | 启动 5×5 改一次 `stream()`；每帧流式改 `stream()` + 按结果清 `renderables`；退出 flush 改请求形态；`gen_offsets` 注释改写（现在只服务于客户端网格化节拍） |
| `game/client/src/tick.cpp` | 自动存档窗口改 `stream(persist = true)`，日志值取 `StreamResult::persisted_chunks` |
| `tests/test_chunk_streaming.cpp` | **新增**，7 个 `TEST_CASE` |
| `tests/test_authority.cpp` | 装载辅助 `load_spawn_area` 从逐区块 `ensure_chunk` 改走 `stream()`（**不是放宽断言**，见 §4） |
| `tests/CMakeLists.txt` | +`test_chunk_streaming.cpp` |
| `docs/tasks/T-D4.report.md` | 本报告 |
| `docs/qa/T-D4-2026-09-16/**` | 实机证据 + 取证工具 |

未动：`engine/**`（含 `engine/physics/**`、`engine/voxel/src/fluid_sim.cpp`）、`cmake/`、根 `CMakeLists.txt`、
`STATE.md`、`docs/01`/`docs/03` 及其它规格、`assets/`。

---

## 3. 接口（新增）

```cpp
// game/common/include/opencraft/game/protocol.hpp
struct StreamRequest {
    int center_cx = 0, center_cz = 0;   // 观察者所在区块
    int generate_radius = 0;            // 工作集半径（Chebyshev，单位：区块）
    int unload_radius = 0;              // 释放阈值（>= generate_radius；差值即滞回）
    int generate_budget = 0;            // 本次调用最多生成几个区块（每帧预算）
    bool persist = false;               // 本窗持久化窗口到期（T009 节奏）
};
struct StreamResult {
    std::vector<std::pair<int,int>> loaded_chunks;    // 本次新建，就近序
    std::vector<std::pair<int,int>> unloaded_chunks;  // 本次释放，升序
    std::size_t persisted_chunks = 0;                 // 交给存档的区块数
    std::size_t loaded_total = 0;                     // 调用后常驻区块数（验收 1 的可查询量）
};
class IAuthority {
    [[nodiscard]] virtual StreamResult stream(const StreamRequest &req) = 0;  // 第五个动词
};
```

`server::WorldSim` 侧：

- `stream()` 是 public 的 `override`；`ensure_chunk()` / `unload_chunk()` 变 **private**（唯一调用者 `stream()`）。
- 新增 `[[nodiscard]] std::size_t loaded_chunk_count() const`（= `ChunkManager::loaded_count()`，验收 1 的第二种查询方式）。
- 冻结项未动：`Chunk`/`LightEngine`/`FluidSim`/`WorldSave` 接口、`IAuthority` 四个既有虚函数语义、
  `WorldSim` 的读接口与 `submit()`/`tick()`/`take_changes()`。

**客户端不再依赖**：`authority.ensure_chunk` / `authority.unload_chunk` / `authority.autosave_pass`（编译期即可发现越界调用）。

---

## 4. 测试（新增 7 例 / 324 全绿）

```
$ cd build && ctest
100% tests passed out of 324
Total Test time (real) =   4.93 sec
```

| 新增用例 | 断言要点 |
|---|---|
| 窗口生成/预算/不重复 | 预算 1 → 只有中心区块；预算 4 → 恰为 Chebyshev=1 的 4 个；再要一次 → 0 个（常驻区块绝不重新生成） |
| 越窗释放与上界 | 移位 3 格 → 旧方窗 9 个全部释放、新方窗 9 个到位、`loaded_total == 9`（不是 18） |
| 释放丢数据与光照 | 释放后 `chunk_ready` 假、该格读作 air、`light().chunk_initialized` 假；回来光照重新初始化且柱顶 sky > 0 |
| **脏区块落盘后还原** | 挖坑 + 倒水 → 移位释放 → `is_dirty` 已清、`load_chunk` 有值；**未编辑的邻块没有被写盘**；回来两处改动都在，且内存中的区块与盘上**逐字节相同** |
| **滞回** | 半径 2/卸载 4：来回 2 区块 ×8 次 → 零加载零释放、`loaded_total` 恒 25；**对照**：卸载 2（零滞回）同节奏立刻"释放一列 → 再生成"；出带后照常释放且常驻 ≤ 保留方窗 |
| 持久化窗口 | `persist = true` + 预算 0 → 生成/释放均为空、`persisted_chunks == 1`、区块仍常驻且改动在内存里；未挂存档时同一请求是 no-op |
| 未编辑区块释放后重生成 | 逐字节相同，且 `load_chunk` 为 nullopt（证明它没走盘——T009"未修改不落盘"的前置假设） |

**`tests/test_authority.cpp` 的既有断言一条没动**，只改了装载辅助（§7.1）：
`load_spawn_area` 原来逐区块调 `ensure_chunk`（该函数现在 private），现在发一个
`{radius 1, unload 1, budget 9}` 的 `StreamRequest` 并 `CHECK(loaded_total == 9)`。
9 个区块、同一方窗、同样的装载顺序，`test_authority.cpp` 的 9 个用例逐条保持原断言与语义。

---

## 5. 构建 / 运行 / 测试方法

```bash
# 构建（禁接管道；worktree 里不要设 FETCHCONTENT_BASE_DIR，让它自行 configure）
cmake -S /Users/happy/Desktop/opencraft_worktree/opencraft-T-D4 -B /Users/happy/Desktop/opencraft_worktree/opencraft-T-D4/build -DCMAKE_BUILD_TYPE=Release
cmake --build /Users/happy/Desktop/opencraft_worktree/opencraft-T-D4/build -j8 > /tmp/build.log 2>&1; echo $?

# 全量测试
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-D4/build && ctest            # 324/324

# 格式（必须用 CLT 的 17，不要用 brew 的 23）
/Library/Developer/CommandLineTools/usr/bin/clang-format --dry-run <改动文件>       # 无输出

# 实机
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-D4/build && ./opencraft

# 独立读盘工具（证据 A 的装置 B；链接行复用构建目录里的 flags.make）
B=/Users/happy/Desktop/opencraft_worktree/opencraft-T-D4/build
clang++ -std=c++20 -O2 $(grep -h '^CXX_INCLUDES' $B/game/client/CMakeFiles/opencraft.dir/flags.make | sed 's/^CXX_INCLUDES = //') \
  -o /tmp/save_read docs/qa/T-D4-2026-09-16/tools/save_read.cpp \
  $B/game/server/libopencraft_storage.a $B/game/server/libopencraft_worldgen.a $B/engine/voxel/libopencraft_voxel.a \
  $B/engine/noise/libopencraft_noise.a $B/engine/core/libopencraft_core.a $B/_deps/spdlog-build/libspdlog.a $B/_deps/zstd-build/lib/libzstd.a
/tmp/save_read <saves_root> world 1,132,0 0,131,0 0,132,-1 3,131,3
```

---

## 6. 结果

- 构建：`build exit=0`，0 个 `error:`（链接期仅有基线已有的 `ignoring duplicate libraries` 警告）。
- 测试：**324/324**（317 存量 + 7 新增）；`test_authority.cpp` 既有断言零改动。
- 格式：改动文件 `clang-format --dry-run`（CLT 17.0.0）无 diff；新增测试名不含 `[`。
- 无网络：`engine/net/` 仍只有 `CMakeLists.txt`；`game/`+`engine/` 下 `sys/socket|arpa/inet|netinet` 命中 0。
- 实机（详见 `docs/qa/T-D4-2026-09-16/README.md`）：
  - **卸载真的发生**：`stream: released 25 chunk(s), 2 resident, 0 meshed`（玩家在 300 格外）。
  - **卸载不丢数据**：改三处（放 stone / 挖坑 / 倒水）→ 位移 300 格 → 回来，日志出现
    `chunk (0, 0) loaded from disk`、`chunk (0, -1) loaded from disk`；**独立读盘**确认文件里
    (1,132,0)=stone、(0,131,0) 已无 grass_block（且被倒的水灌满）、(0,132,-1)=water+source，
    两行未编辑对照格与 worldgen 完全一致。视觉上：同一机位下编辑过的世界与全新世界**只差**"多一块石头 + 左下方多一片水"
    （16.28% 像素差异，天空/山体/水塘轮廓/HUD 逐像素一致）。
  - **资源释放**：300 格外 `chunks 113`（= 视距圆的网格数），不再单调增长。
  - **滞回**：x=0.5 ↔ 32.5（2 区块）来回两趟，五个会话 `stream: released` 行数均为 **0**。
  - **fps**：各会话逐秒读数在 **72.0–88.6** 之间，末次读数多为 72.0（会话首帧 41–46 是首帧加载）。

---

## 7. 已知问题与边界外残留（如实申报）

### 7.1 为改 private 而改了一行既有测试的装载辅助（唯一一处触碰存量测试）

改的是 `tests/test_authority.cpp` 的 `load_spawn_area` 辅助（不是断言）：`sim.ensure_chunk(cx, cz)` ×9
→ 一个 `stream({1,1,9})`。**old → new**：装载入口变了、装载集合与顺序不变；
该文件 9 个用例的断言一行未动（对比提交 `5d49f3e` 的 diff 只有这一处辅助函数）。
理由：把"客户端不得直接驱动装载"变成编译器强制（否则 `authority.ensure_chunk(...)` 在客户端仍然可编译）。

### 7.2 日志格式两处变化（非断言，供 QA 脚本注意）

- 启动行：`startup gen: N chunks, total X ms, avg Y ms/chunk, max Z ms` → **删去 `max`**
  （25 个区块现在是一次 `stream()` 调用，逐区块耗时不再单独计时）。N/X/Y 语义不变。
- 新增一行（仅在有释放时打印）：`stream: released N chunk(s), M resident, K meshed`。

### 7.3 fps 证据的强度限制：本机帧率被上限锁住，未做基线二进制 A/B

各会话逐秒读数在 **72.0–88.6**（末次读数多为 72.0），卡面给的当前基线是 53–72 ⇒ 同量级或更好。
但 `72.0` 这个"精确值反复出现"说明本机帧率**被 vsync 上限锁住**（144 Hz 显示器的 1/2），
**这种读数对小幅回归不敏感**，所以"无退化"这条我只敢报量级、不敢报差异。
本卡新增的每帧成本是"遍历常驻集合（≤361 项，`std::set`）做 Chebyshev 比较"，**不做任何世界查询、不碰盘**；
生成预算仍是 2 区块/帧，与改造前逐区块 `ensure_chunk` 的调用次数相同。若 PM 需要严格数字，
建议单独开一张性能卡用 `-DCMAKE_BUILD_TYPE=Release` + 关 vsync 做 A/B（本卡的验收标准没有要求）。

### 7.4 释放后"迟到的渲染网格"不会被重新网格化（既有行为，本卡未扩大）

区块释放时客户端清掉它的 `renderables` 条目。但它**邻块**那张已经烘好的网格仍然缓存着
（烘的时候邻块还在，所以朝向邻块的面被裁剪掉了），此后既不在视距内也不会被重烘——
现象是"回头看向 9 区块外的世界边缘时，地形比实际多出约 0~48 格"。
这不是本卡引入的：改造前客户端**从不**删除任何 `renderables` 条目，"身后的网格一直留着"是老行为
（本卡只让"被释放区块自己的网格"跟着消失）。真要修，属于"网格资源生命周期"话题，建议单开。
**风险等级：视觉、仅在 144 格外、不影响任何验收项。**

### 7.5 卸载与光照/流体的交互：沿用 T006/T009 既有逻辑，无新增机制

- 光照：`unload_chunk` 里照旧 `light_.forget_chunk(cx, cz)`——它同时丢弃**指向该区块的 deferred offer**，
  所以重新初始化时不会重放陈旧 offer（证据：单测"释放丢数据与光照"里重新初始化后柱顶 sky > 0；
  实机 p5/p7 每个会话都有 8 个区块从盘加载且光照正常，未出现暗块）。
- 流体：释放后 `fluid_may_enter` 对未加载区块返回 false（既定 R-5 语义），模拟把它当墙；
  区块回来时 `wake_fluid_at_chunk_border` 唤醒其流体单元。**本卡没有改 `fluid_sim.cpp`**，
  水单向扩散缺陷（T-D29）原样保留。

### 7.6 实机取证的三个方法学限制（不是产品缺陷，但影响证据强度）

1. **本机 HID 长按不可用**：`tools/walk.py`（每 60 ms 重发 keyDown + 按日志判到位）在 100 秒内只挪动 < 10 格
   并反复被清掉 ⇒ "连续走动中逐次释放"的日志拿不到（与 `docs/05` §3.1 规则 2 一致）。
   位移改用卡面许可的**改写存档坐标**（`tools/level_patch.py` + 重算 CRC-32）。
2. **新会话的常驻集合只含"启动 5×5 + 玩家自己的生成窗口"**，所以"新会话里越过滞回带"不会有东西可释放
   （`pace5_x64.5` 那行 released = 0 就是这个原因）⇒ 滞回的**量化对照**只能在单测里做（已做，见 §4）。
3. **真人输入会污染取证**：截图/点击会先激活游戏窗口，激活期间机器上真人的键鼠输入会落进游戏
   （第一轮会话 c 就被污染：玩家被走到 (-4,127,9) 并倒了一桶水，该会话日志已作废不归档）。
   第二轮加了"注入完立刻把前台交还 Finder 再等 10 秒存档窗口"。

### 7.7 `StreamRequest` 的 `unload_radius = 0` 是个尖锐边（已文档化，未加运行期保护）

`unload_radius = 0` 的语义是"只保留中心区块，其余全释放"。例如退出路径若写成
`stream(StreamRequest{.persist = true})`（其余字段默认 0），会在退出时把整个世界卸掉
（脏数据仍会先落盘，不丢数据，但行为出乎意料）。**当前四处调用点都通过
`client::make_stream_request(position, budget)` 带上真实窗口**，不会踩到；
头文件里对此有明确注释。是否要加运行期断言（如 `unload_radius >= generate_radius` 或"未显式给窗口就拒绝"），
见 §9 建议表。

---

## 8. 接口变更（供 PM 落规格）

1. **`game::IAuthority` 新增第五个动词** `StreamResult stream(const StreamRequest &)`；
   四个既有动词（`submit` / `tick` / `take_changes` / `autosave_pass`）语义不变。
2. **新增值语义类型** `game::StreamRequest` / `game::StreamResult`（字段见 §3）。
3. **`server::WorldSim`：`ensure_chunk` / `unload_chunk` 由 public 变 private**；
   新增 `loaded_chunk_count()`。逐区块的装载/卸载不再是任何一端可以直接驱动的操作。
4. **`client_config.hpp` 新增常量** `kGenRadius = kViewRadius + 1`、`kUnloadHysteresis = 2`、`kUnloadRadius = kGenRadius + kUnloadHysteresis = 9`，
   以及 `make_stream_request(feet, budget)`；`kViewRadius` / `kGenPerFrame` / `kNewMeshPerFrame` 值未动。
5. `docs/03` 若记录"客户端-服务端边界动词表"，需要把"世界管理"从"客户端直接调用"改成 `stream()`（PM 职权）。

---

## 9. 给 PM 的备注（**建议表，请由你落盘**）

| # | 建议 | 依据 / 代价 | 优先级 |
|---|---|---|---|
| 1 | `docs/03 §1.1` 的边界动词表补第 5 个动词 `stream()`，并写明"逐区块装载/卸载已是 private，客户端不可直接驱动" | §3/§8；30 行文档 | 高 |
| 2 | 滞回量 2 区块记入规格或备忘录（**可调，不必是规格**）；若要玩家可见的视距设置，M6 一起做 | §1.1 | 中 |
| 3 | 是否要把"持久化"从 `stream()` 的 `persist` 字段拆成独立动词 `persist(PersistRequest)` | §1.2 第 2 条；约 30 行，纯风格取舍——现在这个形态的好处是**顺序由权威侧一处保证** | 中（等你裁决） |
| 4 | 要不要给 `StreamRequest` 加运行期保护（`unload_radius >= generate_radius` 断言 / 未给窗口则拒绝） | §7.7；三行，可防未来调用点踩坑 | 中 |
| 5 | 建议单开"网格资源生命周期"卡：释放区块的**邻块**迟到网格不重烘（144 格外视觉边界） | §7.4；既有行为，本卡未扩大 | 低 |
| 6 | 建议单开"性能基准"卡：关 vsync 做基线二进制 A/B（本卡 fps 读数被 72 上限锁住，只能报量级） | §7.3 | 低 |
| 7 | 债务：本机"连续走动中逐次释放"的实机日志仍缺（HID 长按不可用），已在 QA README 记明取证装置 | §7.6；若 PM 要，需要真人手跑一次 200 格往返 | 低 |
| 8 | T-D29（水单向扩散）与 T-D4 无耦合，本卡未碰 `fluid_sim.cpp`；但**水灌进坑**这类"改动相互作用"会在存档里一起持久化（属正常行为） | §7.5 | 信息 |

---

## 10. 验收标准逐条核对

| # | 标准 | 结果 | 证据 |
|---|---|---|---|
| 1 | 卸载真的发生（`chunk_ready` 假 / 可查已加载区块数） | ✅ | `StreamResult::unloaded_chunks` + `loaded_total`、`loaded_chunk_count()`；实机 `stream: released 25 chunk(s), 2 resident` |
| 2 | **卸载不丢数据**（走开再走回，改动仍在） | ✅ | 单测"脏区块落盘后还原"（含倒的水、逐字节相同）；实机三处改动 + 独立读盘 `ondisk_read.txt` + 同机位 A/B 截图 |
| 3 | 滞回有效（边界来回不反复加载/卸载） | ✅ | 单测三段（零抖动 / 无滞回对照 / 越界释放）；实机四个会话 pacing 释放行数 = 0（滞回量 2 区块，依据见 §1.1） |
| 4 | 资源被释放（`renderables` 条目清掉、不泄漏） | ✅ | 客户端按 `unloaded_chunks` 清条目；实机 300 格外 `chunks 113` 不再增长（改造前只增） |
| 5 | 三处维护性调用已改请求形态（grep 可查） | ✅ | `grep -rn "ensure_chunk\|unload_chunk\|autosave_pass" game/client/` = 0 |
| 6 | 存量测试全绿、不删不放宽断言 | ✅ | 324/324；`test_authority.cpp` 仅装辅助改动（§7.1 old→new），断言零改动 |
| 7 | 无网络依赖（`engine/net` 仍为空） | ✅ | 无 socket 头/调用；`engine/net/` 仅 CMakeLists.txt |
| 8 | clang-format 无 diff（CLT 17）、测试名不含 `[` | ✅ | `--dry-run` 无输出；新增用例名无方括号 |
| 9 | 独立 worktree、提交前缀 `taskT-D4:` | ✅ | `/Users/happy/Desktop/opencraft_worktree/opencraft-T-D4`；提交 `5d49f3e` |
| 10 | 实机证据（走远有卸载 / 走回改动仍在 / fps 同量级） | ✅（1 项受限） | §6 与 QA README；**fps 只能报量级**（vsync 上限锁住读数，§7.3）；"连续走动中逐次释放"缺实机日志（§7.6 第 1/2 条，故用量化单测 + 300 格位移的释放日志替代） |
| 11 | 报告落盘 + 对话只出简短版（txt 代码块） | ✅ | 本文件；对话末尾简短版 |
| 12 | 完成后置 agentmemory action `act_mu48sely_7999e590ff14` 为 done | ✅ | 见 §11 |

---

## 11. 收尾状态

- 提交：`5d49f3e taskT-D4: 区块流式加载与卸载策略（stream() 请求形态 + 出视野卸载体 + 滞回 2 格）`（单提交，按任务粒度）。
- agentmemory：`act_mu48sely_7999e590ff14` 已置 `done`（result 里附提交号与本报告路径）。
- 取证二进制 md5 `c99cd27620bdfd14eacbd4b8e3dfc3b5`，与提交内容一致（工作树无未提交源码改动）。
- 未越权：没有改 `STATE.md`、`docs/` 规格、任何记忆文件；本报告与 `docs/qa/T-D4-2026-09-16/` 为卡面白名单内的产物。
