# T-I1 报告：物品与库存模型（无头库）

开发者：T-I1-item-inventory　分支：`task/T-I1-item-inventory`（worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-I1`）
基线：`0a96622`（卡面）　卡面落盘日期：2026-09-16
状态：**完成**（297/297 = 存量 267 + 新增 30；纯无头；客户端零 diff；clang-format 17 无 diff）

---

## 0. 结论摘要

- 在 `game/common` 新增一套**无头**物品/库存模型：`ItemRegistry`（字符串 id ↔ dense `u16`）+ `ItemStack` + `Inventory`（41 槽）。命名空间 `opencraft::game`，与 `mining`/`placement`/`raycast` 一致。
- **8 个新文件**（3 个头 + 2 个源 + 3 个测试），共 1673 行；只改 **2 个既存文件**（`game/common/CMakeLists.txt` 加两个源、`tests/CMakeLists.txt` 加三个测试）。**`game/client/**` 与 `engine/**` 零 diff**。
- **297/297 全绿**（存量 267 一条不少、全部仍通过；新增 30 个 test case / 551 条断言 → 断言总数 8498 → 9049）。编译警告数与基线一致（0 条编译器警告，1 条既有链接器重复库警告）。
- **纯无头**：`game/common/**` 全部文件（旧 6 + 新 5）不含任何 GL/GLFW 头；GLFW 仅由 `game/client/CMakeLists.txt:33` 链接。
- **clang-format 17 无 diff**（`/Library/Developer/CommandLineTools/usr/bin/clang-format`），且同目录既存文件复查后仍无 diff（无"顺手重排"副作用）。
- 三处卡面判断我改了或需 PM 明确（详见 §7）：**卡面"20 种左右"与我们实际 36 条的关系**、**`ItemDef` 缺"放置成哪个方块"的链接（下一张卡的接口前置项，需裁决）**、**`still_water` 与 MC 事实的冲突（需裁决是否删）**。

---

## 1. 交付文件与行数

| 文件 | 行数 | 内容 |
|---|---:|---|
| `game/common/include/opencraft/game/item_registry.hpp` | 109 | `EquipSlot` / `ItemDef` / 三档常量 / `is_standard_stack_limit` / `StringHash` / `ItemRegistry` |
| `game/common/src/item_registry.cpp` | 158 | 启动物品表（36 条）+ 注册与双向查询实现 |
| `game/common/include/opencraft/game/item_stack.hpp` | 119 | `stack_limit_of` + `ItemStack`（header-only，卡面契约未给 `item_stack.cpp`） |
| `game/common/include/opencraft/game/inventory.hpp` | 262 | 槽位布局常量 / `InventorySection` / `ArmorSlot` / `SlotRange` / `AddResult` / `TransferResult` / `Inventory` |
| `game/common/src/inventory.cpp` | 200 | 增删/合并/移动/交换/计数实现 |
| `tests/test_item_registry.cpp` | 158 | 7 个 test case |
| `tests/test_item_stack.cpp` | 151 | 6 个 test case |
| `tests/test_inventory.cpp` | 516 | 17 个 test case |
| `game/common/CMakeLists.txt` | +8/−4 | `opencraft_game` 加 `src/item_registry.cpp`、`src/inventory.cpp`（**链接列表一字未动**） |
| `tests/CMakeLists.txt` | +3/−0 | 三个新测试文件加入 `opencraft_tests` |

`Inventory` 通过构造函数持有 `const ItemRegistry *`（对齐 `MiningTracker` 持有 `const BlockRegistry *` 的既有先例），因此 `add_item` / `move_slot` 等不必每次传注册表。代价：注册表必须比库存活得久；若后续卡要让库存脱离注册表序列化，请改回逐调用传参（见 §7 S-5）。

---

## 2. 槽位索引方案（验收 11 要求）

统一平面编号 **0..40**，分区连续，故"分区 = 半开区间"，无需查表：

| 槽位 | 分区 | 数量 | 说明 |
|---|---|---|---|
| `0 .. 8` | Hotbar | 9 | 快捷栏键 k（界面 1 基）↔ 槽位 `k-1` |
| `9 .. 35` | Main | 27 | 主背包 |
| `36 .. 39` | Armor | 4 | 36=Head、37=Chest、38=Legs、39=Feet |
| `40` | Offhand | 1 | 副手（**恰好 1 格**） |

- **快捷栏在前**的理由：让"当前选中的快捷栏格"就是本编号里的一个下标，下一张接线卡不必再做一层映射。
- 辅助：`section_range(InventorySection)`、`section_of_slot(int)`、`storage_range()`（=`[0,36)`，拾取落点）、`armor_slot_index(ArmorSlot)`、`equip_slot_for(ArmorSlot)`、`armor_piece_of(EquipSlot)`、`Inventory::is_valid_slot(int)`、`Inventory::accepts(slot, stack)`。
- 非法槽位号：查询类抛 `std::out_of_range`（`slot()`）；无副作用判断类返回 `false`/`Rejected`。

---

## 3. 启动物品清单（验收 11 要求）

36 条（+ 保留的 id 0）。命名全部原创：用地质/金属加工/甲胄词根，避开红果 2/5 的另一款游戏物品名与其直译。**第一条 20 条 = `BlockRegistry::create_default()` 那 20 个方块的物品形态**（目前只对应"名字与数量"，不含方块 id 链接，见 §7 S-2）。

| # | 字符串 id | 显示名（原创） | max_stack | equip | 归类 |
|---:|---|---|---:|---|---|
| 1 | `loam_clod` | Loam Clod | 64 | — | 方块形态 dirt |
| 2 | `sod_loam` | Sod Loam | 64 | — | 方块形态 grass_block |
| 3 | `greyrock` | Greyrock | 64 | — | 方块形态 stone |
| 4 | `rubble_rock` | Rubble Rock | 64 | — | 方块形态 cobblestone |
| 5 | `fine_grit` | Fine Grit | 64 | — | 方块形态 sand |
| 6 | `pebble_grit` | Pebble Grit | 64 | — | 方块形态 gravel |
| 7 | `grit_slab` | Grit Slab | 64 | — | 方块形态 sandstone |
| 8 | `timber_log` | Timber Log | 64 | — | 方块形态 log |
| 9 | `leaf_canopy` | Leaf Canopy | 64 | — | 方块形态 leaves |
| 10 | `sawn_planks` | Sawn Planks | 64 | — | 方块形态 planks |
| 11 | `clear_pane` | Clear Pane | 64 | — | 方块形态 glass |
| 12 | `still_water` | Still Water | 64 | — | 方块形态 water（见 §7 S-3） |
| 13 | `underrock` | Underrock | 64 | — | 方块形态 bedrock |
| 14 | `char_ore` | Char Ore | 64 | — | 方块形态 coal_ore |
| 15 | `verdigris_ore` | Verdigris Ore | 64 | — | 方块形态 copper_ore |
| 16 | `ferrous_ore` | Ferrous Ore | 64 | — | 方块形态 iron_ore |
| 17 | `auric_ore` | Auric Ore | 64 | — | 方块形态 gold_ore |
| 18 | `lucent_ore` | Lucent Ore | 64 | — | 方块形态 diamond_ore |
| 19 | `rime_block` | Rime Block | 64 | — | 方块形态 snow_block |
| 20 | `duskglass` | Duskglass | 64 | — | 方块形态 obsidian |
| 21 | `empty_vessel` | Empty Vessel | **16** | — | 容器（空） |
| 22 | `water_vessel` | Water Vessel | **1** | — | 容器（盛水，对齐 T-F1 水桶最小形态） |
| 23 | `sunroot` | Sunroot | 64 | — | 食物雏形 |
| 24 | `cave_cap` | Cave Cap | 64 | — | 食物雏形 |
| 25 | `grain_loaf` | Grain Loaf | 64 | — | 食物雏形 |
| 26 | `char_lump` | Char Lump | 64 | — | 材料雏形 |
| 27 | `ferrous_bloom` | Ferrous Bloom | 64 | — | 材料雏形 |
| 28 | `rime_pearl` | Rime Pearl | **16** | — | 材料雏形（蛋类 16 档样本） |
| 29 | `timber_chisel` | Timber Chisel Pick | **1** | — | 工具（第一档 tiers） |
| 30 | `timber_hewer` | Timber Hewing Axe | **1** | — | 工具 |
| 31 | `timber_spade` | Timber Digging Spade | **1** | — | 工具 |
| 32 | `timber_edge` | Timber Edge Blade | **1** | — | 工具 |
| 33 | `timber_headguard` | Timber Headguard | **1** | Head | 盔甲 |
| 34 | `timber_cuirass` | Timber Cuirass | **1** | Chest | 盔甲 |
| 35 | `timber_greaves` | Timber Greaves | **1** | Legs | 盔甲 |
| 36 | `timber_treads` | Timber Treads | **1** | Feet | 盔甲 |

"timber" 取自 `docs/01 §5` 自定 tiers 体系（木质/岩质/精铁/秘银/星钻）的第一档。三档覆盖：64 = 方块/食物/材料；16 = `empty_vessel` / `rime_pearl`；1 = `water_vessel` / 四个工具 / 四件盔甲。保留条目 id 0 = 字符串 `"empty"`、显示名 `Empty`、`max_stack = 1`（对应 `BlockRegistry` 把 `air` 注册在 0 的做法，使 `string_of(0)` / `def_of(0)` 保持全域）。

**加盔甲的原因（卡面自洽性）**：卡面要求"盔甲槽只接受盔甲类"（验收 5），但没有盔甲物品就无法构造该约束的正例与反例。故在"20 方块形态 + 水桶 + 食物/材料雏形"之外补了 4 件盔甲，总数因此是 36 而非卡面写的"20 种左右"。

---

## 4. 接口与语义

### 4.1 新增公开接口

```cpp
namespace opencraft::game {

enum class EquipSlot : std::uint8_t { None, Head, Chest, Legs, Feet };
inline constexpr int kStackLimitLarge = 64, kStackLimitMedium = 16, kStackLimitSingle = 1;
[[nodiscard]] constexpr bool is_standard_stack_limit(int max_stack);

struct ItemDef { std::string display_name; int max_stack = 64; EquipSlot equip = EquipSlot::None; };

class ItemRegistry {
public:
    static constexpr std::uint16_t kEmptyId = 0;
    [[nodiscard]] static ItemRegistry create_default();
    ItemRegistry();
    std::uint16_t register_item(std::string id, ItemDef def);          // 空/重复 id、max_stack<1 抛 invalid_argument；u16 耗尽抛 overflow_error
    [[nodiscard]] bool has_id(std::string_view) const;
    [[nodiscard]] bool has_numeric(std::uint16_t) const;
    [[nodiscard]] std::optional<std::uint16_t> find_id(std::string_view) const;  // 透明哈希，免拷贝
    [[nodiscard]] std::uint16_t id_of(std::string_view) const;                   // 抛 out_of_range
    [[nodiscard]] const std::string &string_of(std::uint16_t) const;             // 抛 out_of_range
    [[nodiscard]] const ItemDef &def_of(std::uint16_t) const;                    // 抛 out_of_range
    [[nodiscard]] std::uint16_t empty() const;
    [[nodiscard]] std::size_t size() const;
};

[[nodiscard]] int stack_limit_of(const ItemRegistry &, std::uint16_t item);   // kEmptyId→0；未知 id 抛 out_of_range

struct ItemStack {
    std::uint16_t item = ItemRegistry::kEmptyId;
    int count = 0;
    [[nodiscard]] static ItemStack of(std::uint16_t item, int count);  // 归一化：空 id 或 count<=0 → 空栈
    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool same_item(const ItemStack &) const;
    [[nodiscard]] static bool can_merge(const ItemStack &, const ItemStack &);
    [[nodiscard]] int stack_limit(const ItemRegistry &) const;
    int merge_from(ItemStack &other, int limit);      // 从 other 取到本栈（空栈会先"认领"item）
    [[nodiscard]] ItemStack split(int amount);        // 取走 amount，剩余留下
    int shrink(int amount);                           // 丢弃 amount
    [[nodiscard]] friend bool operator==(const ItemStack &, const ItemStack &) = default;
};

enum class InventorySection : std::uint8_t { Hotbar, Main, Armor, Offhand };
enum class ArmorSlot : std::uint8_t { Head, Chest, Legs, Feet };
struct SlotRange { int begin, end; constexpr int size() const; };
struct AddResult { int added, remaining; constexpr int requested() const; };
enum class TransferResult : std::uint8_t { Ok, Rejected, NoChange };

class Inventory {
public:
    explicit Inventory(const ItemRegistry &registry);
    [[nodiscard]] const ItemRegistry &registry() const;
    [[nodiscard]] static constexpr bool is_valid_slot(int slot);
    [[nodiscard]] const ItemStack &slot(int slot) const;                       // 非法槽位抛 out_of_range
    [[nodiscard]] const std::array<ItemStack, kInventorySlots> &slots() const;
    [[nodiscard]] std::optional<InventorySection> section_of(int slot) const;
    [[nodiscard]] bool accepts(int slot, const ItemStack &stack) const;
    bool set_slot(int slot, ItemStack stack);
    [[nodiscard]] AddResult add_to_slot(int slot, ItemStack &stack);
    [[nodiscard]] AddResult add_item(ItemStack &stack);
    [[nodiscard]] AddResult add_item(ItemStack &stack, InventorySection section);
    int remove_item(std::uint16_t item, int count);
    int remove_from_slot(int slot, int count);
    [[nodiscard]] TransferResult move_slot(int from, int to);
    [[nodiscard]] TransferResult swap_slots(int first, int second);
    [[nodiscard]] int count_of(std::uint16_t item) const;
    [[nodiscard]] int count_in_section(std::uint16_t item, InventorySection section) const;
    [[nodiscard]] bool is_empty() const;
    [[nodiscard]] int used_slots() const;
    [[nodiscard]] friend bool operator==(const Inventory &, const Inventory &);  // 只比槽位，不比注册表指针
};
}
```

**接线卡会用到的约定**：`add_item(stack)` 的默认落点 = 快捷栏 + 主区（`[0,36)`），**绝不溢进盔甲/副手**；要装盔甲必须显式 `add_item(stack, InventorySection::Armor)`。选中格 `= slot(selected_hotbar_index)`（0..8 直通）。

### 4.2 关键语义（含验收 4、5 的判据）

- **空栈**：`empty() == (item == kEmptyId || count <= 0)`。库产出的栈恒满足 `empty() == (item == kEmptyId)`，因为所有写入路径都会归一化（`count` 归零即写回默认空栈）。
- **合并优先（验收 4）**：`add_item` 走**两遍**：第 1 遍只补"同类未满堆"（按槽位升序），第 2 遍才开新格（按槽位升序）。选两遍而非单遍，是为了让"先填已有未满堆"在**左侧有空格、右侧有半堆**时也成立（单遍会先把东西放进左边的空格）。单测 `add_item tops up a partial stack before opening a new cell` 直接用这个反直觉场景断言。
- **上限（验收 3）**：`add_item` 永不超上限；装不下就继续找下一格，全装不下才把剩余返回到 `AddResult::remaining`（并把用户栈本身改成剩余量）。
- **槽位约束的明确行为（验收 5，即"拒绝"口径）**：
  | 操作 | 违规时的行为 |
  |---|---|
  | `set_slot` | 返回 `false`，单元格**一字不动**（超上限也在这里拒，**不静默裁剪**：裁剪会掩盖调用方 bug，要裁剪请用 `add_to_slot`） |
  | `add_to_slot` / `add_item` | 返回 `AddResult`，`remaining` = 未装入量，`added` = 实际装入量 |
  | `move_slot` | 返回 `Rejected`，两个单元格都不动 |
  | `swap_slots` | 返回 `Rejected`，**双向都合法才动**（原子，不会换一半） |
  约束内容：Hotbar/Main/**Offhand 接受任何物品**；盔甲格只接受"属于本部位的那一件"（放错部位也拒，如 headguard 进 Chest 格）；**空栈对所有合法槽位都合法**（清空永不被拒）；非法槽位号一律 `Rejected`/`false`。副手的约束是**结构性**的（全局只有 1 格，因此最多 1 堆），这一点与盔甲槽的"类型过滤"不同——请 PM 确认这个口径（见 §7 S-4）。
- **`move_slot` vs `swap_slots`**：`move_slot` 只接受"空目标"或"同类目标"（同类则按上限合并，余量留源格），异类目标返回 `Rejected`（要交换请用 `swap_slots`）；`swap_slots` **纯交换、从不合并**（同类两堆交换后数量互换，单测显式断言这一点）。
- **`remove_item(item, count)`**：跨全部 41 格按槽位升序扣减（所以盔甲也能被正常扣掉），返回实际扣掉的数量；`count <= 0` 或 `item == kEmptyId` 扣 0。
- **不变量**：库存里的每个栈都满足 `count <= def_of(item).max_stack`，且空栈恒为默认空栈（`set_slot` 会归一化"手工构造的 `count==0` 栈"）。

### 4.3 冻结项核对

- `BlockRegistry` / `BlockDef`：**未触碰**（`git status` 无该文件）。
- `client::InteractionState` / `hud` / `tick`：**未触碰**（`game/client/**` 零 diff）。
- 既有 267 个测试：**未改动任何既存测试文件**，且全部仍通过（§6 验收 7）。

---

## 5. 构建 / 运行 / 测试方法

```bash
# 1) 在 worktree 里自行 configure（★ 不设 FETCHCONTENT_BASE_DIR，需联网取 doctest/GLFW 等）
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-I1
cmake -S . -B build                 # 退出码 0，约 46 s
cmake --build build -j8             # 退出码 0

# 2) 测试（可执行文件在 build/tests/ 下，不在 build 根）
./build/tests/opencraft_tests       # 297/297

# 3) 只跑本卡新增的
./build/tests/opencraft_tests --test-case="*item*,*inventory*,*stack*"

# 4) 格式检查（必须 CLT 的 17；brew 的 23 会把未改动文件也判违规）
/Library/Developer/CommandLineTools/usr/bin/clang-format --dry-run --Werror \
  game/common/include/opencraft/game/item_registry.hpp \
  game/common/include/opencraft/game/item_stack.hpp \
  game/common/include/opencraft/game/inventory.hpp \
  game/common/src/item_registry.cpp game/common/src/inventory.cpp \
  tests/test_item_registry.cpp tests/test_item_stack.cpp tests/test_inventory.cpp
```

证据归档：`/Users/happy/Desktop/opencraft_worktree/opencraft-T-I1/docs/qa/T-I1-2026-09-16/`
（`env.txt`、`tests-before-after.txt`、`scope.txt`、`clang-format.txt`、`headless.txt`）。

---

## 6. 验收标准逐条核对

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| 1 | 纯无头，不 include GL | ✅ | `grep -rn "glad\|GLFW\|#include <GL" game/common/` = none；GLFW 仅 `game/client/CMakeLists.txt:33`。`game/common/CMakeLists.txt` 的 `target_link_libraries` 块**一字未动** |
| 2 | 对齐 `BlockRegistry` 约定 | ✅ | dense u16、id 0 保留（且是真实注册条目 `"empty"`）、空/重复 id 抛 `invalid_argument`、`find_id`/`id_of`/`string_of`/`def_of` 双向、透明 `StringHash` + `std::equal_to<>`；单测 `test_item_registry.cpp` 7 例（含用 `string_view` 切片查找免拷贝的那条） |
| 3 | 三档堆叠 | ✅ | 三档各有测试样本（64/16/1）；`add_item respects each of the three stack limits` 断言"永不超上限 + 超出的量作为 remaining 返回" |
| 4 | 合并优先 | ✅ | 两遍式 `add_item`；`add_item tops up a partial stack before opening a new cell` 断言槽位占用顺序（含"左空右半"反直觉场景） |
| 5 | 槽位约束行为明确 | ✅ | 见 §4.2 拒绝口径表；`armour cells accept exactly their own armour piece`、`the offhand is a single cell that takes any item` |
| 6 | 移动/交换三类情形 | ✅ | `move_slot` 同类合并（`move_slot moves or merges same items...`）、`swap_slots` 异类交换与同类纯交换、跨分区（主↔副手、盔甲↔主区） |
| 7 | 存量 267 全绿、不多不少 | ✅ | 改前（干净 worktree，commit `0a96622`，同一 build 目录）：267/267；改后：297/297。297−267 = 30 = 三个新测试文件的 `TEST_CASE` 数（7+6+17），且**未修改任何既存测试文件**（`git status`），故 267 条为逐字节同一批源。见 `docs/qa/T-I1-2026-09-16/tests-before-after.txt` |
| 8 | clang-format 无 diff；测试名不含 `[` | ✅ | CLT 17 `--dry-run --Werror` 退出 0；同目录既存文件复查亦 0（无副作用重排）；测试名无 `[` |
| 9 | 客户端零 diff | ✅ | `git diff --stat -- game/client engine` 为空（`scope.txt`） |
| 10 | 独立 worktree + 提交前缀 | ✅ | `/Users/happy/Desktop/opencraft_worktree/opencraft-T-I1`，分支 `task/T-I1-item-inventory`，提交前缀 `taskT-I1:` |
| 11 | 报告落盘（简短版+路径） | ✅ | 本文件；槽位方案见 §2、启动物品清单见 §3 |
| 12 | agentmemory action 置 done | ✅ | `act_mu32p1nn_5f00c39a5e3c` |

---

## 7. 建议表（交 PM 落盘 / 裁决；我未改任何状态或规格文件）

| ID | 建议 | 类型 | 说明与理由 |
|---|---|---|---|
| **S-1** | 卡面"20 种左右"应改为"20 方块形态 + 容器 + 食物/材料雏形 + **4 件盔甲（为验收 5 所必需）**" | 卡面口径 | 验收 5 要求盔甲槽类型约束，无盔甲物品则该约束的正例无法构造。实际 36 条，已按此交付 |
| **S-2** | **下一张接线卡的接口前置项：item → block 的映射**（**需 PM 裁决落法**） | 接口缺口 | `ItemDef` 现在**没有**"拿在手里放置时变成哪个方块"的链接，卡面建议形态也没有，且本卡明确不接线，故我没有加（加了就是造一个本卡无法测的映射）。届时的候选：(a) `ItemDef` 加 `std::uint16_t block`（引用 voxel numeric id）；(b) 另立 `item_block_map.hpp` 表；(c) 存字符串 block id。我倾向 (a)/(b)；请 PM 在派发接线卡前定下，否则接线卡会自造一套 |
| **S-3** | **裁决 `still_water` 是否保留** | 内容口径 | 为满足"20 个方块都有物品形态"我给了水的物品形态 `still_water`；但 MC 事实是水**不能**作为物品拾取（其便携形态是桶）。若要严格对齐 MC，应删掉它（只动 `item_registry.cpp` 的预置表与 `test_item_registry.cpp` 的 20 条 id 列表，**无接口影响**） |
| **S-4** | 确认副手口径：**结构约束（只有 1 格，接受任何物品）**，与盔甲槽的"类型过滤"不同 | 口径确认 | MC 事实是副手可放绝大多数物品（含工具/方块/盔甲），其"限制"只是单格。我按此实现并在 §4.2 写明；若 PM 想要"副手只收特定类别"，请改 `Inventory::accepts` 的 Offhand 分支（一处 switch） |
| **S-5** | `Inventory` 持有 `const ItemRegistry *`（构造注入） | 设计备案 | 对齐 `MiningTracker` 先例，调用点更干净；代价是注册表须活得比库存久。若 M2c 存档要脱离注册表，改回逐调用传参即可（纯机械改动） |
| **S-6** | `StringHash` 现在有 **2 份**（`voxel::StringHash` / `game::StringHash`） | 技术债 | 为不引入 `item_registry.hpp → block_registry.hpp` 的反向依赖而复制了 4 行。可提取到 `engine/core`（**需动 `engine/**`，本卡禁碰**） |
| **S-7** | 非标档位不禁止（只强制 `max_stack >= 1`） | 口径 | 把 64/16/1 写死会把"内容问题"变成破坏性接口变更。提供 `is_standard_stack_limit()`，并单测断言预置集全部落在三档内。若 PM 要求硬约束，改 `register_item` 一行即可 |
| **S-8** | `opencraft_game` 链接列表可瘦身（未动） | 供参考 | 新物品/库存层不依赖 `opencraft_render`/`opencraft_physics`；但链接块是既存共享面，本卡未改。如需"game/common 不得碰 GL"的 CI 门禁，`grep -r "GL" game/common/` 当前结果为空，可直接用 |
| **S-9** | 本卡不适用 `docs/05 §3.2` 三层零变化装置 | 装置边界的建议 | 卡面已声明不适用。本卡"既有行为不变"的判据是验收 7（存量 267 逐字节同一批源 + 全绿）与验收 9（客户端零 diff）——对**纯新增库**这两条比三层装置更贴切；建议 `docs/05 §3.2` 补一句"纯新增卡用：存量测试源零改动 + 存量用例零失败 + 新代码零反向依赖" |

---

## 8. 已知问题、未做项与移交

**已知问题**
1. 「16 档」的语义在 MC 里有两类来源（蛋类 / 空桶），我都放了样本（`rime_pearl` / `empty_vessel`），但**没有**实现"同一物品在不同状态下上限不同"的机制（如桶满水后从 16 变 1）——本项目用两个不同 item id（`empty_vessel` / `water_vessel`）表达，接线卡替换 `bucket_has_water` 时需注意这个映射。
2. `Inventory` 无序列化（卡面明确不做），无 `ItemStack` 的序关系（不需要，故未加 `operator<`）。
3. `remove_item` 的扫描顺序是"全部 41 格升序"，对合成格的"从主区先扣"这类更细的规则，等合成卡出现时再定（届时可加 section 参数重载）。
4. 手写测试期望时我错了两次（`used_slots` 计数、把"超上限"误解为"返回剩余"），**均由测试断言当场暴露**，库行为正确、期望值已修正；记录在此以免 PM 误读为库缺陷。

**未做（严格按卡面范围）**：不做 UI/HUD/渲染/拖拽、不掉落物实体、不合成/烧炼、不工具属性（耐久/伤害/采掘等级）、不序列化、不重构 `client::InteractionState`、不改 `game/client/**`（零 diff 已核）。

**给下一张接线卡的移交清单**
- 快捷栏选中格 → `inventory.slot(selected)`；`hotbar 0..8` 就是槽位号，无需映射。
- `client::InteractionState` 里的 `std::array<std::uint16_t,9>` + `bucket_has_water` 的替换点：`game/client/src/interaction.hpp:20-28`（卡面引用位置已复核，仍在该处）。
- **需要先定 S-2 的 item→block 映射**，否则"拿 Loam Clod 无法放置"。
- 拾取入口调用 `inventory.add_item(stack)`（默认只进快捷栏+主区，不会误装盔甲）。
