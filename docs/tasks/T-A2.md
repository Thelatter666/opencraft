# 任务 T-A2：美术资产管线（把"贴图"从代码里解耦出来）

里程碑：M2c（结构卡）　前置任务：T005（图集）、T-I2（库存接线）、T-D45

> 卡面是派发时点快照，**正文不得修改**；修订一律追加「变更记录」。
> 状态唯一权威是 `STATE.md`，本文件不写状态。

---

## 0. 为什么有这张卡（读完再动手）

**本项目目前不存在任何美术资产文件。** 这不是"要替换旧美术"，是**第一次建立美术职能**。

PM 在 HEAD 上核实的事实（不是推测）：

| 事实 | 证据 |
|---|---|
| `assets/` 只有 `.gitkeep`，**零个美术文件** | `find assets -type f` = 1 个（`.gitkeep`） |
| 全部贴图由 **C++ 代码运行时生成** | `game/client/src/atlas.cpp:228 generate_atlas()`，257 行；`base_color()` 里是 `if (id == "dirt") return {100,76,60};` 这样的硬编码 |
| **全仓没有图片加载库** | `stb_image` / `lodepng` 等全仓零命中 |
| 物品图标是**纯色 tint**，按 id 硬编码 | `game/client/src/inventory_wiring.hpp:196 item_tint()` |
| 生物是**占位**：两个立方体 + 借方块图集 | `game/client/src/main.cpp:711` 注释自述 "a STAND-IN"；无模型加载器 |

⇒ **真实阻断点：贴图就是代码。** 美术总监要改一块泥土的颜色，得改 `atlas.cpp` 再重新编译。
**在建成管线之前，美术总监无事可做。**

所以本卡是**纯结构卡**：不产出任何美术，只把"美术资产可以从外部文件加载"这条通路打通。

---

## 1. 目标（一段话）

**让 `assets/` 成为贴图的真实来源**：新增 PNG 加载，图集优先从 `assets/blocks/<id>_<slot>.png`
读取；**文件缺失时回退到现有程序化生成**，保证现有 **21** 个方块（`engine/voxel/src/block_registry.cpp:26-45`，
实测 `register_block` 计数）在没有任何 PNG 的情况下画面与今天
**逐像素一致**。同时为物品图标留出同类通道（本卡只建通道，不产出图标）。

---

## 2. 边界（本卡不做什么）

| 不做 | 理由 |
|---|---|
| **不产出任何贴图/模型** | 那是美术总监的活（T-A3 及之后）。本卡只建管线 |
| **不删 `generate_atlas()` 的程序化代码** | 它是回退路径，且是既有可运行行为 |
| **不做生物模型加载** | 归 T-R3（`.vox` 方案调研），本卡不碰 |
| **不做物品图标的实际绘制** | 只建通道；图标内容由美术总监后续产出 |
| **不做热重载 / 运行时替换** | 超出本卡范围；启动时加载一次即可 |
| **不做 `assets/CREDITS.md` 的实际条目** | 本卡只建模板与规范（见 §3.4），条目由美术总监填 |

---

## 3. 接口契约

### 3.1 目录布局（新建，冻结）

```
assets/
├── CREDITS.md              ← 证据链（本卡建模板）
├── README.md               ← 美术总监的落笔规范（本卡建）
└── blocks/                 ← 方块贴图
    └── <block_id>_<slot>.png      slot ∈ {top, side, bottom}
```

命名示例：`dirt_side.png`、`grass_block_top.png`、`grass_block_side.png`。

**为什么用 `<id>_<slot>` 而非数字**：方块 id 是注册序决定的数字，会随内容卡漂移；
字符串 id 稳定（与 `BlockRegistry::string_of()` 一致）。

### 3.2 图集瓦片索引 —— **冻结项，不得改变**

`/Users/happy/Desktop/opencraft/engine/render/include/opencraft/render/mesher.hpp:157`：

```
tile(block_id, slot) = block_id * 3 + slot     slot 0=top 1=side 2=bottom
裂纹覆盖 = crack_tile_base(registry.size()) + stage(0..9)
每瓦片 16×16，图集是能装下全部瓦片的最小方阵
```

⇒ **加载 PNG 不得改变这个索引，也不得改变图集尺寸算法。**
否则网格化（`engine/render/src/mesher.cpp`）与着色器全部要改，本卡会膨胀成跨层大卡。

**约束**：PNG 必须是 **16×16 RGBA8**。尺寸不符时**拒绝该文件并用回退**（见 §3.5），
不要缩放、不要拉伸——像素风的缩放会引入插值，且掩盖美术总监的落笔错误。

### 3.3 回退语义（本卡的核心判据）

对每一个 `(block_id, slot)`：

```
if assets/blocks/<id>_<slot>.png 存在且合法(16×16 RGBA8):
        用它的像素
else:
        用现有 paint_tile() 的程序化结果（一字不改）
```

⇒ **在 `assets/blocks/` 为空时，图集必须与今天逐像素完全相同。** 这是本卡的验收核心。

### 3.4 `assets/CREDITS.md` 模板（本卡建）

每条资产一行，字段固定：

```
| 文件 | 来源 | 工具 | 提示词摘要 | 日期 | 人工后处理 |
|---|---|---|---|---|---|
| blocks/dirt_side.png | 自制（AI 辅助） | <工具名> | "low-saturation brown 16x16 pixel dirt" | 2026-09-18 | 量化到 16×16 + 限色板 |
```

**合规红线（美术总监同样受约束，本卡须在 README 里写进去）**：

1. **AI 输入禁止含 MC 资产**——不得把原版贴图/截图/官方美术喂给模型做参考重绘
   （`docs/04` 红线 2：改色/翻转/重采样也算衍生）。参考只能以文字描述给出。
2. **提示词不得点名 MC**——不得出现 "Minecraft-style" 等（`docs/04` 红线 5）。
3. **证据链必填**——AI 生成不豁免留痕；因为它更易产出接近原版的东西，更该留痕。

原文见 `docs/05-development-process.md` §2「美术资产三条硬规则」与
`docs/04-legal-compliance.md`「AI 生成资产」节。**本卡只引述，不复述改写。**

### 3.5 失败处理 —— **静默降级，不致命**

| 情况 | 处置 |
|---|---|
| 文件不存在 | 回退（正常路径，不是错误） |
| 尺寸不是 16×16 | `OC_LOG_WARN` 一次 + 回退 |
| 解码失败 / 非 PNG | `OC_LOG_WARN` 一次 + 回退 |
| `assets/` 整个目录不存在 | 全部回退，**不得崩溃、不得启动失败** |

⚠ **不得**因为缺文件而让游戏起不来——`assets/` 现在就是空的，
本卡合入后任何一次运行都必须正常工作。

---

## 4. 依赖选型（**PM 已核验**，二选一，不要引入第三种）

| 库 | 许可 | 核验方式（2026-09-18 curl 直连） | 形态 |
|---|---|---|---|
| **stb_image**（推荐） | **public domain** | `raw.githubusercontent.com/nothings/stb/master/stb_image.h` 第 1 行自述 "public domain image loader - no warranty implied"；34.6k★ | 单头文件 |
| lodepng（备选） | **zlib** | GitHub API `lvandeve/lodepng` `spdx_id: Zlib`；源码头部 "Permission is granted to anyone to use this software for any purpose" | 单 .cpp+.h |

> ⚠ GitHub API 对 stb 返回 `NOASSERTION`（仓库含多个许可），**以源码头部自述为准**。
> 两者都在 `docs/03 §9`「全部宽松许可」的政策内。

**接入方式**：比照 `cmake/deps.cmake` 既有写法（FetchContent + 注释注明用途与许可）。
stb 无 CMakeLists ⇒ 比照 `fastnoise_lite` 的写法（只 `MakeAvailable` 拿源码目录）。

**许可登记**：`docs/03-architecture.md` §9 依赖表**新增一行**（这是新增依赖，PM 要求留痕）。

---

## 5. 允许触碰的文件/目录（白名单）

**允许**：
- `/Users/happy/Desktop/opencraft/assets/**`（新建 `CREDITS.md` / `README.md` / `blocks/`）
- `/Users/happy/Desktop/opencraft/game/client/src/atlas.cpp`
- `/Users/happy/Desktop/opencraft/game/client/src/atlas.hpp`
- `/Users/happy/Desktop/opencraft/game/client/src/**`（若需新增加载器文件）
- **`/Users/happy/Desktop/opencraft/game/client/CMakeLists.txt`** ← 新增 `.cpp` 时登记用
  （注：T-D45 卡面漏了这一条导致开发者被迫写成 header-only；本卡显式列出，不要重蹈）
- `/Users/happy/Desktop/opencraft/cmake/deps.cmake`（新增依赖）
- `/Users/happy/Desktop/opencraft/docs/03-architecture.md`（**仅** §9 依赖表新增一行）
- `/Users/happy/Desktop/opencraft/tests/test_asset_atlas.cpp`（**新增**单测）
- `/Users/happy/Desktop/opencraft/tests/CMakeLists.txt`（注册新测试文件）
- `/Users/happy/Desktop/opencraft/docs/qa/T-A2-2026-09-18/`（证据目录）
- `/Users/happy/Desktop/opencraft/docs/tasks/T-A2.report.md`（报告）

**禁碰**：
- `engine/render/**`（图集索引与网格化冻结，见 §3.2）
- `engine/voxel/**`、`engine/core/**`、`engine/noise/**`、`engine/physics/**`
- `game/common/**`、`game/server/**`
- `game/client/src/main.cpp` 除必要的调用点外不做重构
- 任何美术产出（本卡不画任何东西）
- `STATE.md`、`docs/tasks/*.ruling*.md`、`docs/01`、`docs/04`、`docs/05`

> **本卡不是纯重构卡**（它是新增能力），所以**允许新增单测**。
> 但**存量测试一律不得改**，且存量测试数不得变化。

---

## 6. 验收标准

**构建**：`cmake --build build -j` 零错误零新增警告；**测试**：`ctest` 全绿。

1. **存量不回归**：既有 **422** 个 `TEST_CASE` **全绿，且存量测试文件零改动**。
2. **★ 空目录等价（核心判据）**：`assets/blocks/` 为空时，生成的图集与改动前
   **逐字节相同**。⇒ 用 md5/逐像素比对证明。**这是本卡的成败线。**
3. **加载生效**：放入 1 张自制 16×16 PNG（如 `dirt_side.png`）⇒ 该瓦片像素变为文件内容，
   其余瓦片不变（单测 + 图集像素断言）。
4. **回退健壮**：放入 32×32 PNG / 非 PNG 文本 / 空文件 ⇒ `OC_LOG_WARN` 且
   **回退到程序化结果**，游戏正常启动（单测）。
5. **缺目录不崩**：整个 `assets/` 删除后游戏仍能启动（实机）。
6. **索引不变**：`tiles_per_row` 与瓦片索引公式与改动前一致（单测断言，见 §3.2）。
7. **实机证据**：放 1 张明显可辨的自制 PNG（例如纯品红的 `dirt_side.png`）→
   游戏内该方块侧面变品红 → 截图。**证明通路真的通了，不是只过了单测。**
   取证手法见 §7。
8. **合规文件就位**：`assets/CREDITS.md`（含模板表头）与 `assets/README.md`
   （含三条红线引述 + 命名规范）已建立。
9. **依赖留痕**：`docs/03 §9` 新增一行，注明库名/许可/用途。

**报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-A2.report.md`；
对话里只输出**简短版 + 完整报告绝对路径**（txt 代码块），不贴全文。

---

## 7. 已知风险与提示

1. **`docs/05 §6` 硬规则**：worktree 根目录固定 `/Users/happy/Desktop/opencraft_worktree/`。
   开工第一条命令：
   ```bash
   git worktree add /Users/happy/Desktop/opencraft_worktree/opencraft-ta2 task/T-A2-asset-pipeline
   ```
   **所有 Read/Edit/Write 一律用 worktree 的绝对路径**，不要写主仓
   `/Users/happy/Desktop/opencraft`。
   **worktree 不设 `FETCHCONTENT_BASE_DIR`**，让它自行 configure。
2. **构建命令不要接管道**。
3. **clang-format 用 CLT 的 17**，不是 brew 的 23。
4. **新增依赖会拉长 configure 时间**（首次 FetchContent）。这是预期的，不是回归。
5. ⚠ **`stb_image` 需要 `#define STB_IMAGE_IMPLEMENTATION`**——只在一个 `.cpp` 里定义，
   不要放进头文件（多 TU 会重复符号）。
6. **不要**为了"顺手"改任何方块的颜色/图案——那是美术总监的活。本卡改了会让
   验收第 2 条（空目录等价）失效，也会让美术总监的规格基线漂移。
7. **发现既有 bug 不顺手修**，写进报告建议表。
8. **本卡不写 `STATE.md`、不改 `docs/` 规格**（§5 单写者原则）。
   `docs/03 §9` 那一行是本卡**唯一**被授权的文档改动。

---

## 变更记录

| 日期 | 内容 | 依据 | 授权 |
|---|---|---|---|
| 2026-09-18 | 初版落盘 | 用户裁决「资产管线卡插在 T-D45 之后」；PM 实证 assets/ 为空、无图片库 | PM |
