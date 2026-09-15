# T-I2 开发者报告：库存接线与快捷栏 UI

> 分支 `task/T-I2-inventory-wiring`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-I2`
> → **不做**：E 键背包界面、合成、掉落物、工具属性、饥饿、存档持久化、鼠标拖拽、
>   `engine/**`、`docs/01`/`docs/03`、`STATE.md`。理由：见 §7 的越界自查（结果：未越界）。

---

## 1. 变更摘要

一句话：**快捷栏不再是"9 个硬编码方块 + 第 10 格水桶布尔"，而是真 `game::Inventory` 的
快捷栏段**——每格是真实物品堆（图标 + 数量），选中/放置/倒水/装水四条路径全部走库存，
用光后格变空；T-F1 的水桶两条路径既没坏、还比原来更对（容器留在手里）。

### 1.1 硬前置三条（T-I1 裁决）

| 裁决 | 落地 | 位置 |
|---|---|---|
| **S-2** `ItemDef` 加 `u16 block` | 新增字段 + 哨兵 `game::kNoBlock = 0xFFFF`（**不是 0**） | `game/common/include/opencraft/game/item_registry.hpp` |
| **S-3** 删 `still_water` | 预置表删条目；**水在数据上"无人可放"**（有一条测试专门守这条） | `game/common/src/item_registry.cpp` |
| **S-3 附带** 水桶映射 | `bucket_has_water` 布尔删除，改为 `empty_vessel`/`water_vessel` 两个 item id 的库存状态；**未退化回布尔** | `interaction.hpp` / `inventory_wiring.hpp` |

`ItemDef::block` 的数据来源：预置表按**方块字符串 id**（`"stone"`/`"dirt"`…）写，
`create_default()` 里对一个 `BlockRegistry::create_default()` 做 `id_of()` 解析。

- 为什么不写数字 id：会冻结一份"方块注册顺序"的隐式契约；按名查在重命名时**当场抛异常**，
  比默默错位好。
- 代价（**请 PM 记入架构备注**）：`ItemRegistry::create_default()` 现在**运行时依赖
  `BlockRegistry::create_default()`**。两者都是"首发内容"，本就是配套的，且客户端世界用的
  正是同一个默认注册表（`world.cpp:36`），id 必然一致；但这是 `game/common` 内部新增的一条
  内容耦合，M2c 若有"物品包/方块包分别加载"的设想，需要重新讨论这个解析时机。

### 1.2 接线主体

| 文件 | 改动 |
|---|---|
| `game/client/src/inventory_wiring.hpp`（**新增**，头文件无 GL） | 选中/使用/容器/放置/展示/启动包的全部纯逻辑，可无头测试 |
| `game/client/src/interaction.hpp` | 持 `game::Inventory` + `VesselIds` + 选中缓存（`selected_stack` / `selected_block` / `selected_use`），`select_slot()` / `refresh_selection()` |
| `game/client/src/tick.cpp` | 瞄准过滤、放置、倒水、装水、键 1–9 选槽全部改读库存；存盘字段遇不可放置物品写 0 |
| `game/client/src/hud.cpp` / `hud.hpp` | 快捷栏画 `ItemStack`：图标（方块贴图 / 容器借用水的贴图 / 其余纯色占位）+ **数量（>1 才画）**；空槽不画；名称改为物品 `display_name` |
| `game/client/src/client_config.hpp` | `kBucketSlot` 删除；`kHotbarSlots = game::kHotbarSlots`（9），单一真相源 |
| `game/client/src/main.cpp` | 建 `ItemRegistry`、灌启动包、恢复存档选中、HUD 传 `ItemStack` 段；手持立方体改由 `StandInVisual` 决定（空手/无立方体的物品不画） |
| `tests/` | 新增 `test_inventory_wiring.cpp`（10 例）；`test_item_registry.cpp` 加 1 例 + 去掉 `still_water` |

**删除的东西**（验收 3）：`InteractionState::hotbar`（9 个方块 id）、`bucket_selected`、
`bucket_has_water`、`client::kBucketSlot`、`selected_block` 的方块 id 语义。
`grep -rn "kBucketSlot\|bucket_selected\|bucket_has_water\|\.hotbar\[" game tests` 只剩注释
与 `HudState::hotbar`（新的 `ItemStack` 段）。

### 1.3 启动包（验收 6 的内容口径）

`kStartingHotbar`（可见 9 格）：greyrock×64 / sod_loam×32 / sawn_planks×8 / duskglass×4 /
**empty_vessel×16** / **water_vessel×1** / grain_loaf×5 / timber_chisel×1 / rime_block×3。

- 覆盖三档堆叠**各至少一例**：64（方块）、16（空容器，且正好在上限）、1（水容器 / 木凿）。
- **两种容器状态各一格**：T-F1 的"装水"和"倒水"都是开局一键可达，无需先找水。
- `kStartingMain`（不可见，主 27 格）：4 件盔甲 + 木斧 + 三种矿石 + 食物，为下一张
  E 键界面与盔甲槽约束预置内容。**这是超出验收 6 最小要求的自主增加，在此申报。**

---

## 2. 文件列表

```
新增  game/client/src/inventory_wiring.hpp
新增  tests/test_inventory_wiring.cpp
改    game/client/src/interaction.hpp
改    game/client/src/tick.cpp
改    game/client/src/hud.cpp
改    game/client/src/hud.hpp
改    game/client/src/client_config.hpp
改    game/client/src/main.cpp
改    game/common/include/opencraft/game/item_registry.hpp
改    game/common/src/item_registry.cpp
改    tests/CMakeLists.txt
改    tests/test_item_registry.cpp
新增  docs/tasks/T-I2.report.md（本文件）
新增  docs/qa/T-I2-2026-09-16/**（实机证据 + tools/ti2input.m）
```

白名单核对：改动全部落在 `game/common/**`（只做 S-2/S-3 两件事）、`game/client/**`、
`tests/**`、报告与证据目录内。`git status` 里没有任何白名单外的路径。

---

## 3. 构建 / 运行 / 测试方法

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-I2

# 干净构建（从零 configure + build，约 2.5 分钟，需联网拉依赖）
cmake -S . -B build-clean -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean -j8          # 不要接管道，退出码会被吞

# 测试
ctest --test-dir build-clean           # 期望 100% tests passed out of 308

# 单跑接线测试
./build-clean/tests/opencraft_tests --test-case="wiring*"

# 实机
cd build && rm -rf saves && ./opencraft     # cwd 必须是 build
```

实机取证工具（本次自建，HID 层，无 osascript）：

```bash
clang -framework Foundation -framework CoreGraphics -framework AppKit \
  -o /tmp/ti2input docs/qa/T-I2-2026-09-16/tools/ti2input.m
/tmp/ti2input win <pid> ; /tmp/ti2input activate <pid>
/tmp/ti2input keytap 18 140                  # 键 "1"
/tmp/ti2input clicktap 1 150 <win_cx> <win_cy>   # 右键（带窗口中心坐标）
/tmp/ti2input move 0 200                     # 俯仰增量的 HID 注入
```

---

## 4. 结果

### 4.1 测试：**297 → 308**（+11），全绿

| 项 | 值 |
|---|---|
| 基线（`f0cc57c`，worktree 内自跑复现） | **297/297** ✓ |
| 本卡干净构建（`build-clean`，从零 configure+build） | configure ✓ / build ✓ / **308/308** ✓ |
| 编译告警 | 除既有 `ld: ignoring duplicate libraries` 外 **0 条** |

**297 → 308 的差值逐条交代**（验收 2 要求）：

| 来源 | 例数 |
|---|---|
| `test_inventory_wiring.cpp` 新增 TEST_CASE | **+10** |
| `test_item_registry.cpp` 新增「block link」TEST_CASE | **+1** |
| **删 `still_water`** | **±0**（它只是 `kBlockFormIds[]` 数组里的一个字符串，删条目不改 TEST_CASE 数） |

> 即：**测试数变化全部来自新增用例，删物品本身不改变任何计数**（与 T-I1 那次
> "差值恰等于新 TEST_CASE 数"同一口径，PM 可独立复核）。
> 既有断言**无一条被删除或放宽**；`test_item_registry.cpp` 只做了三处更新：
> ① 去掉 `still_water` 条目；② 用例名 "twenty" → "block"；③ `size() >= 21` → `>= 20`
> （分母从"预留 + 20 个物品形态"变成"预留 + 19 个"，与 S-3 一致）。

### 4.2 clang-format（CLT 17）

`/Library/Developer/CommandLineTools/usr/bin/clang-format --dry-run -Werror` 对 11 个改动/新增
源文件全部 **CLEAN**（证据 `docs/qa/T-I2-2026-09-16/clang-format.txt`）。未用 brew 的 23。测试名不含 `[`。

### 4.3 实机（验收 11）

完整证据见 [`docs/qa/T-I2-2026-09-16/README.md`](../qa/T-I2-2026-09-16/README.md)。摘要：

| 验收 | 证据 | 结果 |
|---|---|---|
| 4 图标 + 数量 | `Z1_hotbar_startup_zoom4x.png` | 逐格读出 **64 32 8 4 16 _ 5 _ 3**；2 件单件不显示数字 ✓ |
| 3 选中跟随库存 | `Z4_selection_name_and_count_zoom4x.png` | 键 5/6 各自选中对应格，名称随格变化 ✓ |
| 5 三种方块放置 | `session5.log` + `H1/H2/H3` | stone / grass_block / planks 各放 1，坐标各不相同 ✓ |
| 6 T-F1 回归（不退化） | `session7.log` + `J1/J2/J3` + `Z5` | 倒水 ✓ 装水 ✓；容器格图标亮度 **75.3 → 35.3 → 75.3**，且**全程留在同一格** ✓ |
| 7 数量递减 / 用光变空 | `session6.log` + `Z6_rime_block_3_to_0.png` | 3 → 2 → 1 → **0（该格空、名称行消失）** ✓ |

实机还跑出**两个真缺陷**（都在本卡新代码里，已修，见 §6）。

---

## 5. 已知问题 / 限制

| # | 项 | 性质 | 建议 |
|---|---|---|---|
| K1 | 非方块物品（食物/工具/盔甲）的图标是**纯色占位块**（`item_tint`，按 id 选色） | 观感缺口，非功能缺口 | 内容卡做真正物品图标；`item_tint` 未知 id 有灰色兜底，新增物品不会"隐形" |
| K2 | 放置/取出时**物品堆不自动整理**：舀水若手里是 16 件空容器，产出的水容器会落到主背包的第一个空格（可能不在快捷栏） | 设计取舍 | 单件容器已改原地换状态（§6 缺陷 2）；>1 件的堆叠无法原地（水容器上限为 1），M2c 有容器系统时再定"是否自动并入手中格" |
| K3 | **中键取物、数字键以外的选槽方式（滚轮）** 未做 | 卡面明确不做 | 无 |
| K4 | 选中**不可放置**的物品（容器/食物/工具）退出再进，选中的是第 1 格而非原格 | 存档字段是"方块 id"，表达不了容器；库存持久化是 M2c | 建议 M2c 把存档字段从 `selected_block`(u16) 改为 `selected_slot`(u8) + 库存序列化，卡片请一并裁决 |
| K5 | 手持立方体只对"有方块形态的物品 + 容器（借用水的贴图）"绘制；食物/工具/盔甲**空手** | 观感缺口 | 与 K1 同一张内容卡 |
| K6 | 快捷栏 9 格在启动包里**全满**，故本轮取证看不到"空槽"（用光场景 `Z6` 第 4 行已覆盖空槽） | 取证局限 | 无 |
| K7 | 未在 Retina（backing scale 2）下验证 | 债务 T-D19 同类 | 本卡未改任何 HiDPI 相关代码 |

---

## 6. 实机跑出的两个真缺陷（本卡新代码，已修）

### 缺陷 1（崩溃，P1）：用光最后一格 → `std::terminate`

- **现象**：霜块 3 件连放第 3 次时进程崩溃，`libc++abi: terminating due to uncaught exception of
  type std::out_of_range: unknown block numeric id`。
- **根因**（我自己的 bug）：放置日志参数在 `refresh_selection()` **之后**求值——
  最后一格被取空后 `selected_block` 变成哨兵 `kNoBlock = 0xFFFF`，被送进
  `BlockRegistry::string_of()`。崩溃报告调用栈：
  `BlockRegistry::string_of(unsigned short) const` ← `client::run_tick(TickContext const&)` ← `main`。
- **为什么单测没抓到**：这是 `run_tick` 内部的**求值顺序**问题，无头测不到（逻辑层的状态是对的：
  空格的 `selected_block` 就该是 `kNoBlock`）。**这正是验收 11 要求的实机取证的直接产出。**
- **修复**：新增 `client::place_one_block()`，**在扣减之前**读出方块，连同 `consumed`/`left`
  一起返回；`run_tick` 的世界写入与日志都用它的返回值，**不再回读选中缓存**。
  该函数带单测（"placing the last unit still reports the block it placed"）。
- **证据**：崩溃报告 `crash_exhaustion_2026-09-16-041925.ips`（修前）；修后 `session6.log` 连放
  三次正常，`Z6` 第 4 行显示该格已空。

### 缺陷 2（UX 语义，P2）：倒水后容器"跑进背包"

- **现象**：验收 6 的字面要求是"空容器右键水面 → **变成**水容器"。朴素实现是"扣 1 件、
  把产出塞进第一个空格"：启动包快捷栏满员时，**产出的容器会落到主背包**（本卡没有 E 键界面，
  玩家看不见）⇒ 读起来像"容器丢了"。
- **修复**：`transform_vessel()` 对**单件**容器做**原地状态替换**（同格 `set_slot`）；
  只有 ≥2 件的堆叠才走"扣 1 + 另找空位"（产出的 1 件装不进留下的堆叠里）。
- **证据**：`Z5`（75.3 → 35.3 → 75.3 全在第 6 格）+ `J1/J2/J3`。

---

## 7. 接口变更

| 接口 | 变更 | 谁需要知道 |
|---|---|---|
| `game::ItemDef` | **+1 字段** `std::uint16_t block = kNoBlock`（聚合初始化向后兼容：老写法少给一个字段仍合法） | 后续所有造 `ItemDef` 的卡 |
| `game::kNoBlock` | 新增常量 `0xFFFF`（物品层内定义，**不引入方块层头文件**） | 同上 |
| `ItemRegistry::create_default()` | **签名不变**，但内部改为对一个默认 `BlockRegistry` 解析方块名（§1.1 的耦合代价） | PM 需记入架构备注 |
| 预置物品集 | **36 → 35 条**（删 `still_water`） | 内容卡 |
| `client::InteractionState` | 构造需传 `const game::ItemRegistry&`（不再可默认构造）；`hotbar`/`bucket_selected`/`bucket_has_water` 删除；新增 `inventory`/`vessels`/`selected_stack`/`selected_use`/`select_slot()`/`refresh_selection()` | 后续所有碰 `InteractionState` 的卡（**注意：它现在有构造参数**） |
| `client::HudState` | 字段完全重排：`hotbar` 变 `std::span<const game::ItemStack, 9>`，新增 `items`/`vessels`；聚合初始化改**指定初始化器** | HUD 相关卡 |
| `client::kHotbarSlots` | `10 → 9`（= `game::kHotbarSlots`）；`kBucketSlot` **删除** | 客户端 |
| 存档字段 `LevelData::selected_block` | **格式未变**（仍是 u16 方块 id）；语义变为"选中物品的方块形态，不可放置时写 0" | M2c 存档卡（见 K4 建议） |

---

## 8. 主动纠正 / 给项目经理的备注

### 8.1 纠错与建议（建议表，请 PM 落盘）

| # | 项 | 建议 | 依据 |
|---|---|---|---|
| S1 | **卡面验收 11 的"数量递减"应扩为"递减 + 用光变空"** | 本卡把"用光"也做了实机（`Z6`），因为它恰好暴露了 P1 崩溃；建议后续涉及容量的卡都带上"边界到 0"的取证要求 | §6 缺陷 1 |
| S2 | **`ItemRegistry::create_default()` 的方块解析时机** | 记入架构备注（§1.1）：M2c 若要分离物品包/方块包，需改为外部传入 `BlockRegistry`（或改存方块字符串 id） | §1.1 |
| S3 | **存档选中字段** | M2c 把 `selected_block`(u16 方块 id) 换成 `selected_slot`(u8) + 库存序列化；否则选中容器/工具永远恢复不了 | K4 |
| S4 | **"物品→方块"以后可能不够用**：某些物品放置的方块取决于上下文（台阶朝向、种子变体等） | `ItemDef::block` 现在够用，但别把它当终局接口；到那时再引入 `place_as(item, context)` | 本卡设计余量 |
| S5 | **建议把本卡的取证工具链固化**（`tools/ti2input.m`：HID 注入 + 按 PID 反查窗口 + 点击显式带窗口坐标） | 本机 T-D21 下，"点击事件带窗口中心坐标"与"窗口按尺寸过滤"两条是**可复用的稳定化手法**，建议并入 `docs/05` §3.1 | §4.3 过程记录 |
| S6 | 债务 T-D21 依旧成立（注入时灵时不灵），本卡另观察到：**同一次会话内命中率也会变化**，未命中只能整轮重跑 | 维持现有取证手法（决定性场景放前列），不建议为取证改产品代码 | 过程记录 |

### 8.2 记功式自查（我自己发现的问题，未掩饰）

1. **P1 崩溃是我写的**（日志求值顺序），已修 + 补单测 + 留崩溃报告；
2. **P2 是我在取证时发现的语义不合卡面**（"变成"应留在手中），已修 + 改单测期望
   （原期望写的是"并回 15 件的那堆"——那是错的，已改为原地替换并注明原因）；
3. 一次会话截图截到菜单栏、一次会话只中 1 次注入：**两批截图都删了**，没有拿来充数；
   为让每张证据都有同会话日志，决定性会话整轮重跑。

### 8.3 已知**未**验证项（不得当作已验）

- Retina / HiDPI（K7）；本卡不涉及。
- 主背包（27 格）内容**玩家看不见**（无 E 键界面），只在单测里断言其落位。
- 存档**跨版本**兼容性：本卡未改存档格式，但未做旧档回归（`selected_block` 语义变化不影响读取）。

### 8.4 下一步建议

1. **E 键背包界面（27 格 + 盔甲 4 + 副手 1）**：模型已就绪（`Inventory` 41 格 + `accepts()` 槽约束），
   且启动包已预置盔甲，可直接验"盔甲槽类型约束"的正例——**这是本卡完成度最高、风险最低的下一张**。
2. 或按路线图转 **M2b 权威侧前置**（`engine/net` 为空、`main.cpp` 695 行（本卡 +1）、
   server 无世界模拟）。
