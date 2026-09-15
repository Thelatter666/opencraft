# 任务 T-I1：物品与库存模型（无头库）

里程碑：M2a 地基　前置：无（当前 main `408f96c`）
基线：**267/267**。运行前请先跑基线确认。
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

在 `game/common` 建一套**无头（不依赖 GL/GLFW/渲染）**的物品与库存模型：
物品注册表（字符串 id ↔ 数字 id）、物品堆叠、库存容器（主 27 + 快捷栏 9 + 盔甲 4 + 副手 1）、
堆叠上限三档（64 / 16 / 1）、以及增删/合并/交换/移动的纯逻辑。

**这是 M2c 全部生存内容（合成 / 工具 / 掉落 / 饥饿 / 战斗）的承重墙。**

## 为什么

- 用户裁决「结构优先」后，路线图 M2a 的第二块就是它（`docs/06` 修订记录）。
- 现状：`game/common/` 只有 `raycast` / `mining` / `placement` 三层交互原语，
  **没有任何物品层**。`game/client/src/interaction.hpp:20-28` 的"快捷栏"是一个
  `std::array<std::uint16_t, 9>` 硬编码方块 id + 一个 `bucket_has_water` 布尔
  （T-F1 的水桶最小形态）。
- 没有它，合成/掉落/工具/饥饿每张卡都要自己临时造一套，必然各造各的。

## 数值/规格依据

- `docs/01-gameplay-spec.md` §5「物品/库存/合成」：
  - 库存：**主 27 + 快捷栏 9 + 盔甲 4 + 副手 1**
  - 堆叠：**64 / 16 / 1 三档**
  - 掉落物 5 分钟消失、有重力与弹跳（**本卡不做掉落物实体**）
  - 工具 tiers 耐久/伤害/采掘等级（**本卡不做工具属性**，只留接口空间）
- `docs/03-architecture.md` §7：**方块/物品/配方/群系/结构全部注册表驱动**
  （字符串 ID → 数字运行时 ID），为模组与进度系统铺路。

### 可直接仿的先例（PM 已核实）

| 你要造的 | 照着这个写 | 位置 |
|---|---|---|
| `ItemRegistry` | `BlockRegistry` | `engine/voxel/include/opencraft/voxel/block_registry.hpp`（73 行）+ `.cpp`（104 行） |
| 无头纯逻辑 + 单测 | `Mining` 状态机 | `game/common/include/opencraft/game/mining.hpp` + `tests/test_mining.cpp` |
| 测试风格 | doctest | `tests/test_block_registry.cpp` |

`BlockRegistry` 的既有约定请沿用（**不要另造一套风格**）：
- `kAirId = 0` 保留给空
- `create_default()` 静态工厂预置启动内容
- `register_block(std::string id, Def)` 返回 dense `u16`，重复/空 id 抛异常
- `find_id(string_view) -> optional<u16>` / `id_of()` 抛出变体 / `string_of(u16)`
- 透明哈希 `StringHash{ using is_transparent = void; }` + `std::equal_to<>`（免拷贝查找）
- `def_of(u16) -> const Def&`

## ★ 范围（严格，越界即返工）

**做**：

1. `ItemRegistry`：字符串 id ↔ dense `u16`；`ItemDef` 至少含
   `display_name` + `max_stack`（64/16/1）；预置"启动物品"（内容自定，见下）。
2. `ItemStack`：{item_id, count}，空栈表示；`add` / `can_merge` / `split` 之类纯操作。
3. `Inventory`：四个分区（主 27 / 快捷栏 9 / 盔甲 4 / 副手 1），统一的槽位索引方案。
4. 核心操作（**纯函数或明确无副作用的方法**）：
   - `add_item`：优先合并已有同类堆，再填空格；返回"实际装入 / 剩余"
   - `remove_item`：按 id 扣减，可跨堆
   - `move_slot` / `swap_slots`：槽位间移动与交换
   - 盔甲/副手槽的**槽位类型约束**（盔甲槽只接受盔甲类、副手 1 格）
5. 单测覆盖上述全部行为。

**不做**（看到了也不要动）：
- ❌ **不改 `game/client/**`** —— 本卡是纯库交付，客户端**不接线**
  （接线是下一张卡，本卡保持客户端零 diff 才好验收）
- ❌ 不做 UI / HUD / 渲染 / 拖拽交互
- ❌ 不做掉落物实体、重力与弹跳（M2c）
- ❌ 不做合成表 / 配方 / 烧炼（依赖本卡，但属后续卡）
- ❌ 不做工具属性（耐久/伤害/采掘等级）——只留 `ItemDef` 的扩展空间，不实现
- ❌ 不做存档序列化（M2c 与库存持久化一起做）
- ❌ **不重构现有的 `client::InteractionState` 快捷栏**（下一张接线卡再替换）

**"启动物品"内容**：MC 的原版物品名/外观我们不能用（合规红线 2/5）。
请**原创命名**，覆盖到 20 种左右即可（对应现有 20 个方块的物品形态 + 水桶 + 几种食物/材料雏形）。
重点是**覆盖三档堆叠**（多数 64、可堆叠 16 的如蛋/桶类、不可堆叠 1 的工具类），
让三档都有测试样本。**名称自定，不必与现有方块 id 一一对应。**

## 接口契约

### 命名空间与位置

```
game/common/include/opencraft/game/item_registry.hpp   （ItemRegistry / ItemDef）
game/common/src/item_registry.cpp
game/common/include/opencraft/game/item_stack.hpp      （ItemStack）
game/common/include/opencraft/game/inventory.hpp       （Inventory / 槽位枚举）
game/common/src/inventory.cpp
```

命名空间：`opencraft::game`（与 `mining` / `placement` / `raycast` 一致）。
需改 `game/common/CMakeLists.txt` 把新 .cpp 加进 `opencraft_game`（按现有模式）。

### 冻结项（不得改动）

- `BlockRegistry` 及其 `BlockDef` **一字节不改**
- `client::InteractionState`、`hud`、`tick` 等 T-M1 拆出的模块**一律不碰**
- 既有 267 个测试的行为不变

### 建议形态（**你自定，这只是起点**）

```cpp
namespace opencraft::game {

struct ItemDef {
    std::string display_name;
    int max_stack = 64;   // ⚖ docs/01 §5: 64 / 16 / 1 三档
};

// id 0 保留给"空手/无物品"（对应 BlockRegistry::kAirId 的既有约定）
class ItemRegistry {
public:
    static constexpr std::uint16_t kEmptyId = 0;
    [[nodiscard]] static ItemRegistry create_default();
    std::uint16_t register_item(std::string id, ItemDef def);
    [[nodiscard]] std::optional<std::uint16_t> find_id(std::string_view id) const;
    [[nodiscard]] std::uint16_t id_of(std::string_view id) const;          // 抛出变体
    [[nodiscard]] const std::string &string_of(std::uint16_t id) const;
    [[nodiscard]] const ItemDef &def_of(std::uint16_t id) const;
    [[nodiscard]] std::size_t size() const;
};

struct ItemStack {
    std::uint16_t item = ItemRegistry::kEmptyId;
    int count = 0;
    [[nodiscard]] bool empty() const;
};

// 槽位分区（docs/01 §5：主 27 + 快捷栏 9 + 盔甲 4 + 副手 1）
// 具体索引方案你定；建议给一个统一的 slot 编号 + 分区查询辅助。
class Inventory { ... };
}
```

## 允许触碰的文件/目录（白名单）

- `game/common/**`（主体）
- `tests/**`（**新增** `test_item_registry.cpp` / `test_inventory.cpp` 等；需改 `tests/CMakeLists.txt`）
- `docs/tasks/T-I1.report.md`（你**必须写**的报告）
- `docs/qa/T-I1-2026-09-16/`（如需归档证据；本卡大概率不需要）

⚠️ **禁碰**：`STATE.md`、`docs/**`（上述报告与证据目录除外）、
**`game/client/**`（本卡保持客户端零 diff）**、`engine/**`、`game/server/**`、
`cmake/`、根 `CMakeLists.txt`、`.github/`、`assets/`。

## 验收标准（逐条可执行）

1. **纯无头**：`opencraft_game` 库**不得**依赖 GL/GLFW；新代码不 include 任何 GL 头。
   （现有 `opencraft_game` 已满足，别破坏它。）
2. **注册表行为对齐 `BlockRegistry` 既有约定**：dense u16、id 0 保留、重复/空 id 抛异常、
   双向查询、string_view 免拷贝查找。单测覆盖。
3. **堆叠三档**：64 / 16 / 1 三档各自有测试样本；
   `add_item` 不得超上限，超出部分正确返回"剩余"。
4. **合并优先**：`add_item` 必须先填已有未满堆，再开新槽。单测断言槽位占用顺序。
5. **槽位约束**：盔甲槽只接受盔甲类、副手 1 格；违规操作有明确行为（拒绝或返回失败，
   你定，但**必须可断言且写进报告**）。
6. **移动/交换**：`move_slot` / `swap_slots` 覆盖"同类合并""异类交换""跨分区"三种情形。
7. **存量测试数不变**：**267 个存量测试全绿，一条不多一条不少**
   （本卡是新增库，不改既有行为；新增测试计入 267 之上的增量）。
8. clang-format 无 diff（用 **CLT 的 17**：
   `/Library/Developer/CommandLineTools/usr/bin/clang-format`；**不要用 brew 的 23**）；
   测试名避免含 `[`。
9. **客户端零 diff**：`git diff` 中 `game/client/**` 必须为空。
10. 独立 worktree（根 `/Users/happy/Desktop/opencraft_worktree/`）；提交前缀 `taskT-I1:`。
11. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-I1.report.md`，
    对话中**只输出简短版 + 该路径**，用 **txt 代码块**包裹（`docs/05` §2 规则 4）。
    报告须给出**槽位索引方案**与**启动物品清单**。
12. 完成后置 agentmemory action **`act_mu32p1nn_5f00c39a5e3c`** 为 done。

> 注：本卡**不是**纯重构卡，故不适用 `docs/05` §3.2 的三层零变化装置；
> 但仍须做到"既有行为不变"（验收 7、9 两条即是判据）。

## 已知风险与提示

- **构建不要接管道**（`| tail` 吞退出码）；判成败用 `cmd > log 2>&1; echo $?` 或搜 `error:`。
- **绝不要把 `FETCHCONTENT_BASE_DIR` 指向主仓库 `build/_deps`**（PM 已两次因此挂掉主仓构建）。
  worktree 里**不设**该变量，让它自行 configure（约 75–90 s，需联网）。
- 测试可执行文件默认在 `build/tests/opencraft_tests`（不是 build 根）。
- **合规红线**：
  - 物品名/描述必须**原创**，不得使用 Minecraft 的物品名或直译（`docs/04` 红线 2/5）。
  - 不粘贴任何反编译源码；本卡不引用外部资料则无需来源标注。
- **不得直接改状态与规格**（P-001）：建议一律以建议表写进报告交 PM 落盘。
- **主动纠正记功**：发现卡面错误、接口不合理、数值对不上，直接写进报告。
  本项目已 7 次由执行方纠正 PM，这是被鼓励的。
- **别过度设计**：本卡要的是"能被合成/掉落/工具/饥饿直接复用的一套最小正确模型"，
  不是 ECS 库存框架。若你纠结于是否要加"物品 NBT/组件系统"——**不要**，M2c 用到再说。

## 附：本卡在路线图中的位置

```
M2a 地基
  ├─ T-M1 main.cpp 拆分          ✅ done
  ├─ T-I1 物品与库存无头库        ← 本卡
  └─ 部分高度方块（下一步）

M2c 生存内容（依赖本卡）
  └─ 合成 / 工具 / 掉落 / 饥饿 / 战斗
```

本卡交付后，下一张卡是「把 `client::InteractionState` 的硬编码快捷栏换成真正的库存 + 快捷栏 UI」。
**本卡不接线，是为了让"库是否正确"和"接线是否正确"可以分开验收。**
