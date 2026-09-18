# 任务 T-B2：首个生物体素模型 —— Mossback（美术侧）

里程碑：M2c（内容卡，美术侧）　前置任务：T-B1（`.vox` 通道已合入 `24d4a10`）、T-A2（资产管线）、T-A3/A4（风格规格 + 60 张方块贴图）

> 卡面是派发时点快照，**正文不得修改**；修订一律追加「变更记录」。
> 状态唯一权威是 `STATE.md`，本文件不写状态。
> 本卡编号 B2 沿用 `docs/research/12-mob-model-formats.md` §7.3 的分批顺序（B0/B1 已随 T-B1 交付）。

---

## 0. 为什么是 Mossback 打头

它是三个首发生物里**唯一没有攻击/蓄爆语义**的（被动、吃谷物面包、会跟着你走）——
第一只模型可以把"管子通不通"和"动画语义对不对"分开验。它的身体在 T-B1 之前是
"泥土方柱 + 草顶方块"两立方体占位（`main.cpp` 生物 pass 自述 STAND-IN）。

---

## 1. 目标（一段话）

做一个**原创的 Mossback `.vox` 体素模型**放进 `assets/mobs/`，让游戏里它第一次以
体素模型而不是两个立方体出现；可选再配一张 16×16 调色板 PNG 演示"改配色不碰模型"。
交付物 = 资产文件 + CREDITS 条目 + 自查报告 + 启动日志证据。**不改一行代码。**

设计意象（来自 `game/common/src/mob_type.cpp` 的 `mossback_def` 注释与占位配色，
数值基准 `docs/research/11-mc-survival-systems.md` §6.1"牛"档：四足、 grazing、温顺）：
**一头背部长苔的四足食草动物**。棕色躯干 + 绿色冠背是它的现占位语义，你可以沿用也可以重设计——
但轮廓要让人一眼读出"四条腿 + 一个头 + 背上有苔"。⚠ 数值骨架（碰撞盒/速度/引诱食物）不可改，
你只负责外观。

---

## 2. ★ 接口契约（全部现数自 T-B1 交付代码，冻结项勿自创）

| # | 约定 | 出处（代码为准） |
|---|---|---|
| ① | **Z 轴向上；模型的"脸"朝文件的 +Y；地面 = z 0 层** | `game/client/src/mob_mesh.cpp` 顶部坐标映射注释 `(x, z, −y−1)` |
| ② | **缩放 = 模型 z 总跨度 → 该生物的碰撞高度**（Mossback 现值 **1.4 格**），脚底中心对齐 `Entity::position`；**加高一个附件会把整只压小**；横向（x/y）尺寸不钳制，但按碰撞盒宽 **0.9×0.9** 设计视觉才协调 | `mob_type.cpp` `mossback_def`；`main.cpp` 模型装配段注释 |
| ③ | **关节标签 = 调色板索引 1..8**：1 躯干、2 头、3 左前肢、4 右前肢、5 左后肢、6 右后肢、7 尾/附加件、8 备用（本卡用不到 7/8 可以不出现）。**同一关节只有一种颜色（=标签格的颜色）**；躯干可以用 9..255 的色格加多色细节（9+ 全部归躯干组）。colorIndex **0 永远不得使用** | `mob_mesh.cpp` `mob_joint_of_color` + `mob_pose.hpp` 关节枚举 + `docs/research/12` §4.3 |
| ④ | **调色板 PNG（可选）`assets/palettes/mossback.png`**：严格 **16×16** RGBA PNG（T-A2 通道，不缩放）；**格号 = 颜色索引号**（从图片左上数 (列,行) = 行×16+列；第 1 格 = 索引 1；第 0 格永不画）。1..8 标签格可自由改色，模型不动 | `mob_model.cpp` `load_palette_png` 注释 |
| ⑤ | **预算**（`docs/research/12` §7.2，标注待校准）：体素 ≤1000、暴露面 ≤2000、最长边 ≤32 格、关节 ≤8。解析器硬上限：SIZE 每边 1..256，超限整文件被拒 | `mob_model.cpp` `kMobGridMaxSide` |
| ⑥ | **机器判据（启动日志，产品码，无补丁）**：放入后 `mobs: 1/3 mob models loaded from ../assets/mobs` + `mob model mossback: N voxels, T triangles, J joints, 1.4 blocks tall`（有 PNG 时行尾带 `, palette from palettes/mossback.png`）；坏文件 = **1 条 WARN + 回退两立方体**。文件缺失是静默，不算 WARN | `mob_model.cpp` `load_mob_models`；T-B1 报告 §3.4 |

**幼体不需要单独模型**（引擎按 `kMobBabyScale` 整体缩小）。受击闪红/行走摆腿由引擎驱动，
你只要把肢体分对关节组，它们自己会动。

---

## 3. 制作工具（三选一，许可均已核实）

| 工具 | 许可 | 本机事实 |
|---|---|---|
| **MagicaVoxel**（推荐） | 免费用于任何项目 / 鼓励署名 / 禁止再分发软件本体（`https://ephtracy.github.io/mv_main.html` License 小节，访问 2026-09-18，T-R3 已核） | macOS 包停在 0.99.6.2，本机经 Rosetta 实测可运行（`docs/qa/T-R3-2026-09-18/qa_mv_running_macos15.png`）；**当前未安装，需自行下载** |
| **Goxel**（备用） | GPL-3.0，**工具许可不传染产物**（T-R3 记录） | 本机已装 0.15.1；⚠ T-R3 实测"进程起来了但 10 秒+ 无窗口"，原因未查——遇到如实上报，**不得据此判定不可用** |
| **字符网格分层稿 + 转换脚本**（兜底，与 T-A3/A4 的"手绘 + 自写 PNG 编码器"同构） | 自产 | 逐格手画每一 z 层的字符稿（这是创作本体），脚本仅把稿子编码成 `.vox` 字节；脚本与逐层稿**都进证据目录**备查 |

⚠ 无论哪条路：**MagicaVoxel/Goxel 自带示例模型不是我们的资产，一个体素都不许抄进仓库**
（`docs/04` 红线 2）。AI 生成不是本卡路线（T-A3/A4 两任都选择了手绘，本卡沿用——如需改用须先报 PM 裁决）。

---

## 4. 允许触碰的文件/目录（白名单）

**工作根 = worktree**（`docs/05 §6` 硬规则；建法见 §7.5）。下表路径均相对
`/Users/happy/Desktop/opencraft_worktree/opencraft-tb2/`，**所有 Read/Edit/Write 用该绝对路径前缀**
（写到主仓会让"构建通过+测试全绿（旧代码）"完全掩盖错位，判据 = 两树 `git status` 一脏一净）。

**允许**（全部是新增或追加）：
- `assets/mobs/mossback.vox`（新）
- `assets/palettes/mossback.png`（可选，新）
- `assets/CREDITS.md`（追加一条，六字段）
- `docs/art/01-style-guide.md`（**只许改 §9 待办与追加登记**，正文其他节不动——T-A4 先例）
- `docs/qa/T-B2-2026-09-18/`（证据目录：最终分层稿/脚本源码/日志摘录/可选实拍）
- `docs/tasks/T-B2.report.md`（报告）

**禁碰**：
- **一切 C++/CMake/构建文件**（`game/**`、`engine/**`、`tests/**`、`CMakeLists.txt`、`cmake/**`）——美术总监禁写代码（`docs/05 §1`；兜底脚本只准放证据目录，不得接进构建）
- `assets/blocks/**`、`assets/items/**`、`assets/README.md`
- `docs/0X-*.md`、`STATE.md`、`docs/research/**`、`docs/tasks/T-B1*`
- `docs/qa/` 下其它任务目录
- **主工作区 `/Users/happy/Desktop/opencraft/` 的一切文件**（验收后由 PM 合入，美术不直接碰主仓）

> 白名单自洽检查（`docs/05 §2`）：本卡**无新增源码文件** ⇒ 不需要任何 CMakeLists；
> 证据目录与报告文件已显式列出。**过程垃圾（`__pycache__/`、`*.pyc`、中间稿）不得入仓**（T-A3/A4 判据沿用）。

## 5. 边界（本卡不做什么）

| 不做 | 理由 |
|---|---|
| 不做 hollow_wretch / blastbud | B3/B4，各自一卡 |
| 不做倒地/死亡动画 | T-D51（模拟里没有"正在死亡"状态），先粒子版另排 |
| 不改 `mob_skin()` 两立方体路径 | T-D52，留给删除回退分支的那张卡 |
| 不动任何引擎数值/物理参数 | 外观与数值分家（`docs/05 §1` 职责切分） |
| 不新增方块贴图 | 方块 20/20 已齐（T-A4） |

---

## 6. 验收标准（逐条可执行）

1. **零代码改动**：`git diff --name-only origin/main...HEAD` 只含白名单文件（报告贴结果）。
2. ★ **日志判据达成**（产品码，无补丁）。先建 worktree 并构建一次（构建命令**不接管道**、
   不设 `FETCHCONTENT_BASE_DIR`，见 §7.5）；然后逐字执行：
   ```bash
   cd /Users/happy/Desktop/opencraft_worktree/opencraft-tb2/build
   rm -rf saves
   mkdir -p ../docs/qa/T-B2-2026-09-18
   ./opencraft > ../docs/qa/T-B2-2026-09-18/run_with_model.log 2>&1 &
   PID=$!
   for i in $(seq 1 60); do grep -q "spawn scan" ../docs/qa/T-B2-2026-09-18/run_with_model.log && break; sleep 0.5; done
   sleep 1
   kill $PID; sleep 1
   grep -E "mobs:|mob model" ../docs/qa/T-B2-2026-09-18/run_with_model.log
   ```
   期望恰好读到：`mobs: 1/3 mob models loaded from ../assets/mobs` 与
   `mob model mossback: ...` 一行，且日志中 **mob model 相关 WARN = 0 条**。再删掉
   `../assets/mobs/` 用同样命令跑一次，应为 `mobs: 0/3`、0 条 WARN（证明两边都活）。
   两份日志留证据目录。
3. **预算与契约自查表**（报告内填，逐项给数）：体素数（≤1000）/ 包围盒 x×y×z（每边 ≤32，z≥2）/
   使用的关节标签集合（⊆{1..8}）/ 是否出现 colorIndex 0（必须"无"）/ 9+ 色格用量（若有，说明用途）/
   估算暴露面（≤2000）/ 调色板 PNG 格号用法（若提供）。
4. **CREDITS 一条**：工具+版本、提示词或设计稿摘要、日期、人工后处理、署名（MagicaVoxel 鼓励署名）；
   六字段零空缺，风格对齐现有条目。
5. **风格合规**：过 `docs/art/01-style-guide.md` §8 自查清单 + §3 低饱和判定
   （适用于调色板 PNG 与 MagicaVoxel 内所选颜色，索引 1..8 的标签格颜色同样受此约束）；
   并在报告里声明"轮廓与配色未参照任何原版生物贴图"（红线 2/5）。
6. **报告**：正文落在工作根下的 `docs/tasks/T-B2.report.md`
   （= `/Users/happy/Desktop/opencraft_worktree/opencraft-tb2/docs/tasks/T-B2.report.md`，随分支提交）；
   对话里只回**简短版 + 完整报告绝对路径**（txt 代码块），含"需 PM 裁决项"与"已知问题"。
7. **实机截图为尽力项、非门槛**：冷启动后苔背兽会自然刷在地表（被动生物），走近拍到"它不再是
   两个立方体"最好；**拍到就交，拍不到不算失败**——逐像素与召唤取证由 PM 验收侧装置负责
   （`docs/qa/T-B1-2026-09-18/` 两套现成装置）。报告里写明拍到/没拍到即可。

---

## 7. 已知风险与提示

1. **y-up 是这张卡最容易犯的错**（`.vox` 与 MagicaVoxel 都是 Z 向上，直觉一致；
   但**分层稿兜底路线**如果按"图片思维"把 y 当高度，模型会被**正确解析**却趴在地上——
   自查表里 z 跨度 ≥2 就是防这条的）。判据：头（标签 2）的 z 应高于躯干主体。
2. 关节标签 1..8 的格子**同时是该关节的颜色**（§2 表 ③）。想换色走调色板 PNG（④），
   不要重导出模型。
3. MagicaVoxel 导出的 `.vox` 会带一堆场景图 chunk（`nTRN`/`MATL`…），**无关 chunk 占文件 99%**
   是正常的——解析器只读 `SIZE`/`XYZI`/`RGBA`，其余跳过；**不要**为此手工剪文件。
4. `RGBA` 块短于 256 条也被接受（T-B1 已放宽），所以 MagicaVoxel 导出的满 256 条与手写稿的
   短块都能过——**不要拿"解析成功"当调色板正确的证据**，颜色对不对看日志行 + 截图/自查表。
5. worktree 根目录固定：`git worktree add /Users/happy/Desktop/opencraft_worktree/opencraft-tb2 -b task/T-B2-mossback-model main`
   （`docs/05 §6`；所有读写在 worktree 绝对路径内）。
6. 若发现本卡契约与代码现实不符：**报告里标出来交 PM 裁决，不要自行改代码或改本卡正文**。

## 变更记录

| 日期 | 内容 | 依据 | 授权 |
|---|---|---|---|
| 2026-09-18 | 初版落盘并派发 | 用户"好"（B2 派发批准）；契约六条现数自 `372b373` 交付代码，两条美术定案拍板于 `docs/tasks/T-B1.ruling.md` §3 | PM |
