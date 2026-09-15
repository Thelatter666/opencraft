# T-I2 实机证据（库存接线 + 快捷栏）　2026-09-16

> 结论先说：**快捷栏由真库存驱动**——9 格显示的是 `game::Inventory` 的快捷栏段，
> 每格有物品图标与数量（>1 才显示数字），选中/放置/装水/倒水四条路径全部走库存；
> 放置后数量递减、用光后该格变空；T-F1 的水桶两条路径**没坏**，且容器现在**留在手里**
> （同格变状态）。全部证据见本目录。

## 环境

- 机器：macOS 24.6.0（arm64，用户实机）；窗口 1280×720（**窗口号按 PID 反查**）
- worktree：`/Users/happy/Desktop/opencraft_worktree/opencraft-T-I2`（分支 `task/T-I2-inventory-wiring`）
- 二进制：`build/opencraft`（Release），cwd = `build`，每次运行前 `rm -rf saves`（种子固定 ⇒ 地形/出生点可复现）
- 注入：HID 层 `CGEventPost(kCGHIDEventTap)`；工具源码 `tools/ti2input.m`
  （本次自建，未用 osascript；窗口号取 `kCGWindowOwnerPID` 匹配且 800×600 以上的那个，
  菜单栏/状态窗同 PID 也会出现，按尺寸滤掉）。数字键用 18/19/20/21/23/22/26/28/25（US 布局 1–9）。
- 启动后约 8 s 待地形与首帧稳定再注入；鼠标瞄准用 `kCGMouseEventDeltaX/Y` 增量
  （Δpitch = px × 0.0025，向下看 = dy 为正），点击事件**显式带窗口中心坐标**，
  否则实机光标漂到窗口外时点击会被路由到别的窗口（本次踩过，见「过程记录」）。

## 证据 A（验收 3/4）：快捷栏 = 库存段，图标 + 数量

| 文件 | 内容 |
|---|---|
| `A1_hotbar_startup.png` | 启动首帧整屏（未注入任何键） |
| `Z1_hotbar_startup_zoom4x.png` | 同一帧的快捷栏 4× 放大（决定性） |
| `D1_selected_empty_vessel.png` / `D2_selected_water_vessel.png` | 键 5 / 键 6 选槽 |
| `Z4_selection_name_and_count_zoom4x.png` | 上述两帧的「物品名 + 快捷栏」4× 放大 |

- 启动包 `client::kStartingHotbar`（`inventory_wiring.hpp`）9 格依次为
  greyrock×64 / sod_loam×32 / sawn_planks×8 / duskglass×4 / empty_vessel×16 /
  water_vessel×1 / grain_loaf×5 / timber_chisel×1 / rime_block×3。
- 放大图可逐格读出：**64 32 8 4 16 [无数字] 5 [无数字] 3**。
  两格不显示数字的正是数量为 1 的两件（water_vessel、timber_chisel），空槽不显示数字
  （启动包里没有空槽）——与验收 4「数量 > 1 才显示」一致。
- 图标来源：方块类 → 该方块侧面贴图；容器 → **它装的水**（空容器 70 明度、满容器 235，
  即 T-F1 水桶的观感）；食物/工具/盔甲 → 纯色占位块（`item_tint`，见报告「建议表」）。
- `Z4` 上图：键 5 选中第 5 格（15 件，显示数字），名称 `EMPTY VESSEL`；
  下图：键 6 选中第 6 格（1 件，无数字），名称同为 `EMPTY VESSEL`
  —— 该格在倒水后已由水容器变为空容器（见证据 B），选中框与名称都跟着库存走。

## 证据 B（验收 6，T-F1 回归）：满容器倒水 → 空容器装水，且容器留在原格

| 文件 | 阶段 |
|---|---|
| `J1_holding_water_vessel.png` | 键 6 选中水容器（尚未倒） |
| `J2_poured_cell6_now_empty_vessel.png` | 右键倒出后：世界里出现水源、第 6 格由满变空、名称变 `EMPTY VESSEL` |
| `J3_scooped_back_into_cell6.png` | 同一格再右键：舀回水，第 6 格又变回水容器 |
| `Z5_vessel_cell_across_phases.png` / `Z5_vessel_cell_luma.txt` | 第 6 格图标在四阶段的像素亮度对比 |

日志（`session7.log`）：

```
vessel: poured water source at (0, 132, -2)
vessel: filled from (0, 132, -2)
```

第 6 格图标 8×8 角部平均亮度（不含数量文字）：

| 阶段 | 亮度 |
|---|---|
| 启动（水容器） | 74.5 |
| J1 选中水容器 | 75.3 |
| J2 倒水后（空容器） | **35.3** |
| J3 装水后（水容器） | **75.3** |

即：**物品身份真的换了**（不是布尔翻转），而且**换在同一格**——
玩家倒完水手里仍是容器，可以直接再舀。若按「扣 1 + 往空位塞 1」的朴素做法，
启动包快捷栏满员时产出的容器会落进主背包（不可见），玩家会以为容器丢了；
因此 `transform_vessel()` 对**单件**容器做**原地状态替换**（见报告 §6 缺陷 2）。

## 证据 C（验收 5/7）：放置三种方块、数量递减、用光后格变空

| 文件 | 内容 |
|---|---|
| `H1_pillar_1_greyrock.png` / `H2_pillar_2_sod_loam.png` / `H3_pillar_3_sawn_planks.png` | 同一瞄点连放灰岩/草泥/木板，`H3` 一帧内可见三格数字都已递减 |
| `C3_placed_sawn_planks.png` | 另一会话的近距视角，放置后数量同样递减 |
| `I1_before_key9_rime_block3.png` … `I2_after_place_left_0.png` | 霜块 3 件连放 3 次 → 3 / 2 / 1 / **0（该格空）** |
| `Z6_rime_block_3_to_0.png` | 上条四帧快捷栏拼接放大（决定性） |

`session5.log`（三种方块，一次会话内全中）：

```
placed stone at (0, 132, -3); consumed 1 from slot 0 (63 left)
placed grass_block at (0, 132, -2); consumed 1 from slot 1 (31 left)
placed planks at (0, 132, -1); consumed 1 from slot 2 (7 left)
```

`session6.log`（用光）：

```
placed snow_block at (0, 132, -3); consumed 1 from slot 8 (2 left)
placed snow_block at (0, 132, -2); consumed 1 from slot 8 (1 left)
placed snow_block at (0, 132, -1); consumed 1 from slot 8 (0 left)
```

`Z6` 第四行该格**只剩背板**（无图标、无数字、名称行也不再绘制）⇒ 验收 7 后半条实机成立。

## 证据 D（过程）：实机跑出的两个真缺陷

1. **用光最后一格时崩溃**（`crash_exhaustion_2026-09-16-041925.ips`，修前二进制）：
   放置日志在 `refresh_selection()` **之后**再读 `selected_block`，最后一格被取空后
   该值是 `kNoBlock = 0xFFFF`，交给 `BlockRegistry::string_of()` 抛
   `std::out_of_range: unknown block numeric id` → `std::terminate`。
   崩溃报告调用栈（`faultingThread`）：
   `BlockRegistry::string_of(unsigned short) const` ← `client::run_tick(TickContext const&)` ← `main`。
   修复：新增 `client::place_one_block()`，**在扣减之前**读出方块并连同数量一起返回
   （见报告 §6 缺陷 1）；修后 `session6.log` 三连放不再崩，`Z6` 第四行正常。
2. **容器不进背包**（证据 B）：见上，`transform_vessel()` 改为单件原地替换。

## 过程记录（如实交代）

- 本目录的四组证据各来自一次完整会话，**每张截图都有同会话的日志**：
  - 会话 1（`session1.log`）：A/B/C/D 四组截图（倒水、装水、三种方块、选槽名）；
  - 会话 5（`session5.log`）：H 组（同一瞄点连放三种方块）；
  - 会话 6（`session6.log`）：I 组 + `Z6`（霜块 3 → 0，用光后格空）；
  - 会话 7（`session7.log`）：J 组 + `Z5`（容器原地换状态的像素亮度对比）。
- 注入通道（债务 T-D21）本机确实时灵时不灵：会话 1 与 5、6、7 的**决定性动作全部命中**，
  但另有几轮「三种方块」会话三次只中两次、再一轮三次只中一次。
  **未命中一次都没有伪造替代证据**，改为重跑整个会话；决定性场景（Z1/Z5/Z6）均来自
  真实注入触发的运行。
- 一次会话因「窗口号取到菜单栏窗口（同 PID、1920×24）」而截图截到菜单栏，
  该轮截图已删除（未计入证据）。此后窗口按「同 PID 且宽 ≥800 且高 ≥600」过滤。
- 一次会话因注入点击的**光标位置漂到窗口外**而只中 1 次：此后点击事件显式携带
  窗口中心坐标后复现稳定。
- 以上均为实机运行产物：非夹具、非无头回放。日志行即产品代码的既有 `OC_LOG_INFO`，
  本卡**没有为取证改动产品代码**（唯一改动是证据 D 里两个真缺陷的修复本身）。
