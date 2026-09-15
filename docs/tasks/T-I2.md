# 任务 T-I2：库存接线与快捷栏 UI

里程碑：M2a 地基　前置：**T-I1（已合入 main `e5849b1`）**
基线：**297/297**。运行前请先跑基线确认。
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

把 T-I1 交付的无头物品/库存库**接进客户端**：
用真正的 `Inventory`（41 格）替换 `client::InteractionState` 里硬编码的
`std::array<std::uint16_t, 9>` 方块 id 快捷栏与 `bucket_has_water` 布尔，
让快捷栏显示真实物品堆（含数量）、能选中、能用它放置方块与倒水。

**这张卡玩家能看见东西**——前面 T-M1 / T-I1 都是地基，这一张是地基第一次露出地面。

## 为什么

- T-I1 交付了库但**刻意不接线**（为了让"库是否正确"与"接线是否正确"分开验收）。
- 现状：`game/client/src/interaction.hpp:20-28` 的快捷栏是 9 个硬编码方块 id +
  第 10 格一个水桶布尔（T-F1 最小形态）。
- 不接线的话，T-I1 的库只是死代码；而合成/掉落/工具/饥饿每张卡都要自己造一套临时接线。

## ★ T-I1 裁决的三条硬要求（必须先做）

裁决文件：`/Users/happy/Desktop/opencraft/docs/tasks/T-I1.ruling.md`。**以下三条是本卡的硬前置**：

### 1. 给 `ItemDef` 加 `u16 block`（裁决 S-2，采方案 (a)）

现有 `ItemDef`（`game/common/include/opencraft/game/item_registry.hpp`）只有
`display_name` / `max_stack` / `equip`，**没有"这个物品放置成哪个方块"的链接**。
接线后玩家选中物品要能放下方块，必须有这个映射。

- 加字段：`std::uint16_t block = kNoBlock;`
- **哨兵值必须用 `kNoBlock`（建议 `0xFFFF`）**，**不得用 0** —— 0 是 air 的合法方块 id，
  会产生"这个物品放置成空气"的歧义。
- 该字段只读（注册时确定），运行时不改。
- **不可放置的物品**（食物/材料/工具/盔甲/容器）一律 `kNoBlock`。

### 2. 删除 `still_water` 物品（裁决 S-3）

T-I1 为凑"每个方块都有物品形态"造了一个 MC 里不存在的物品（水不可作为物品拾取）。
**裁决删除**——只动预置表与相关测试，无接口影响。
若删除导致测试数变化，须在报告中交代 297 → N 的差值来源。

### 3. 处理水桶映射（裁决 S-3 附带）

T-I1 用两个 item id 表达容器状态：`empty_vessel`（堆叠 16）/ `water_vessel`（堆叠 1）。
替换 `bucket_has_water` 布尔时注意这个映射——**不要**把它退化回布尔。

## 数值/规格依据

- `docs/01-gameplay-spec.md` §5：库存 = 主 27 + 快捷栏 9 + 盔甲 4 + 副手 1；堆叠 64/16/1
- `docs/03-architecture.md` §7：注册表驱动
- T-I1 裁决 S-4（**已确认**）：副手 = 结构性单格，**接受任何物品**（不是类型过滤）

## ★ 范围（严格，越界即返工）

**做**：

1. `ItemDef::block` 字段 + 哨兵（上述硬要求 1）
2. 删 `still_water`（硬要求 2）
3. `InteractionState`：用 `game::Inventory` 替换硬编码快捷栏与 `bucket_has_water`
4. 快捷栏 UI 显示**真实物品堆**（图标 + 数量），不再是固定 9 个方块 + 第 10 格桶
5. 选中/放置/倒水三条路径全部走库存（键 1-9 选快捷栏槽）
6. 启动默认库存内容（给玩家一点东西，让他**看得见**）：具体给什么你定，
   建议至少覆盖三档堆叠各一例，便于一眼看出"数量/堆叠上限"在起作用

**不做**：
- ❌ **不做背包/容器 UI**（E 键打开的那个 27 格界面）——那是下一张卡。
  本卡只做**快捷栏**这 9 格可见部分。
  *（若你认为顺手做掉更省，可以，但必须在报告里明确申报并说明理由）*
- ❌ 不做合成、掉落物实体、工具属性、饥饿
- ❌ 不做库存持久化（存档）——M2c
- ❌ 不做拖拽/鼠标交互（键盘选槽即可）
- ❌ 不改 `engine/**`
- ❌ 不改 `docs/01` / `docs/03` 规格（PM 职权）

## 现状锚点（PM 已现数核实，基线 `e5849b1`）

| 文件 | 行数 | 要动的地方 |
|---|---|---|
| `game/client/src/interaction.hpp` | 53 | **第 20-28 行**：`hotbar` 数组 + `selected_block` + `bucket_selected` + `bucket_has_water` |
| `game/client/src/tick.cpp` | 279 | 第 142-145（bucket_filling）、216-243（放置/倒水/装水）、255-265（**键 1-9 / 键 0 选槽**） |
| `game/client/src/hud.cpp` | 198 | 第 20-50（快捷栏槽位背板 + 选中高亮）；还有后面的方块图标与物品名绘制 |
| `game/client/src/hud.hpp` | 43 | 第 32 行：`std::span<const std::uint16_t, 9> hotbar` |
| `game/client/src/client_config.hpp` | 33 | 第 30-31 行：`kBucketSlot = 9` / `kHotbarSlots = 10` |
| `game/common/include/opencraft/game/item_registry.hpp` | 109 | `ItemDef`（约第 53-57 行） |
| `game/common/include/opencraft/game/inventory.hpp` | 262 | 槽位枚举：快捷栏 0-8 / 主 9-35 / 盔甲 36-39 / 副手 40 |

⚠️ **行号会随你的改动漂移**；以符号名为准（`grep` 定位）。

## 允许触碰的文件/目录（白名单）

- `game/common/**`（`ItemDef::block` + 删 `still_water`；**只做这两件事**，其余不动）
- `game/client/**`（接线主体）
- `tests/**`（更新受影响的测试 + 新增接线相关测试；需改 `tests/CMakeLists.txt`）
- `docs/tasks/T-I2.report.md`（你**必须写**的报告）
- `docs/qa/T-I2-2026-09-16/`（实机证据归档）

⚠️ **禁碰**：`STATE.md`、`docs/**`（上述报告与证据目录除外）、
`engine/**`、`game/server/**`、`cmake/`、根 `CMakeLists.txt`、`.github/`、`assets/`。

## 验收标准（逐条可执行）

1. **`ItemDef::block` 就位**：可放置物品有正确方块 id，不可放置物品为 `kNoBlock`；
   哨兵**不是 0**。单测断言。
2. **`still_water` 已删除**，且报告中交代测试数变化（297 → N）。
3. **快捷栏由库存驱动**：`InteractionState` 不再有硬编码的 9 个方块 id；
   `bucket_has_water` 布尔**已移除**（由 `empty_vessel`/`water_vessel` 物品堆表达）。
4. **UI 显示数量**：快捷栏每格显示物品图标 + **数量**（数量 > 1 时才显示数字，
   与 MC 一致）；空槽不显示数量。
5. **放置仍可用**：选中方块类物品 → 右键能放下对应方块（**逐方块验证至少 3 种**）。
6. **水桶仍可用且不退化**：空容器右键水面 → 变成水容器；水容器右键地面 → 倒出水并摊开。
   **实机验证**（这是 T-F1 已交付的功能，本卡不得弄坏它）。
7. **数量会减少**：放置方块后该槽数量 −1；用光后槽位变空。
8. **存量测试全绿**：297（或按第 2 条调整后的 N）全绿；
   **不得**因为接线而删除或放宽既有断言。若某既有断言与新模型冲突，
   须**逐条在报告中交代 old → new**，不得静默改。
9. clang-format 无 diff（用 **CLT 的 17**：
   `/Library/Developer/CommandLineTools/usr/bin/clang-format`；**不要用 brew 的 23**）；
   测试名避免含 `[`。
10. 独立 worktree（根 `/Users/happy/Desktop/opencraft_worktree/`）；提交前缀 `taskT-I2:`。
11. **实机证据**（本卡玩家能看见，所以必须有）：
    - 截图：快捷栏显示物品与数量
    - 截图/日志：放置方块后数量递减
    - 截图/日志：装水 → 倒水仍然工作（回归 T-F1）
    ⚠️ 若用脚本注入按键，**必须 HID 层**，禁用 `osascript`；
    本机 HID 注入时灵时不灵（`docs/05` §3.1），把决定性场景放在生效窗口内。
12. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-I2.report.md`，
    对话中**只输出简短版 + 该路径**，用 **txt 代码块**包裹（`docs/05` §2 规则 4）。
13. 完成后置 agentmemory action **`act_mu33jwrn_345388491d66`** 为 done。

## 已知风险与提示

- **构建不要接管道**（`| tail` 吞退出码）；判成败用 `cmd > log 2>&1; echo $?` 或搜 `error:`。
- **绝不要把 `FETCHCONTENT_BASE_DIR` 指向主仓库 `build/_deps`**（PM 已两次因此挂掉主仓构建）。
  worktree 里**不设**该变量，让它自行 configure（约 75–90 s，需联网）。
- 可执行文件在 `build/opencraft`；测试在 `build/tests/opencraft_tests`。
- **合规红线**：物品名/图标必须原创；不粘贴反编译源码；不使用 MC 物品名。
- **不得直接改状态与规格**（P-001）：建议一律以建议表写进报告交 PM 落盘。
- **回归风险最高的一条**：T-F1 的水桶路径（装水/倒水）。本卡换掉了它的状态表示，
  **务必实机验证还能用**（验收 6）。
- **主动纠正记功**：发现卡面错误、接口不合理、数值对不上，直接写进报告。
  本项目已 7 次由执行方纠正 PM，这是被鼓励的。
- **别过度设计**：本卡是"接线 + 快捷栏可见"，不是"完整背包系统"。
  若你纠结要不要做 E 键背包界面——**不要**（见范围里的"不做"）。

## 附：本卡在路线图中的位置

```
M2a 地基
  ├─ T-M1 main.cpp 拆分            ✅ done
  ├─ T-I1 物品与库存无头库          ✅ done
  └─ T-I2 库存接线与快捷栏 UI       ← 本卡（玩家可见）

随后（未定顺序）
  ├─ 部分高度方块（半砖地基，解锁 step-assist 实机可达）
  └─ M2b 权威侧前置
```
