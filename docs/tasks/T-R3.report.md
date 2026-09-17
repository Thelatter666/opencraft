# T-R3 报告：生物模型方案调研（`.vox` 体素模型）

- **任务卡**：`/Users/happy/Desktop/opencraft/docs/tasks/T-R3.md`
- **分支**：`task/T-R3-mob-model`（worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-tr3`）
- **基线**：`36bcc40`（卡面派发时点）
- **角色**：研究员（调研卡，**只产出文档，零实现代码**）
- **日期**：2026-09-18

---

## 1. 变更摘要

**没有改任何现有文件、没有写一行 C++、没有改任何资产。** 新增三处：

| 路径 | 内容 |
|---|---|
| `docs/research/12-mob-model-formats.md` | 调研文档，**868 行**，逐条回答 Q1–Q6 + 许可核验 + 建议表 R-1..R-14 + 合规自检 |
| `docs/tasks/T-R3.report.md` | 本报告 |
| `docs/qa/T-R3-2026-09-18/` | 证据目录，11 个文件（许可正文、格式规范、测量原始输出、本机实机截图等） |

---

## 2. 六个问题的结论（一段话版）

| # | 结论 | 关键论据 |
|---|---|---|
| **Q1** | **体素模型（`.vox`）**，不做盒式组合 | 实测量级：一个人形模型（398 体素）≈ 730 面 / 2 920 顶点 / 1 460 三角形；×70 同屏 = 10 万三角形 ⇒ **顶点数不是瓶颈**，而体素的形体表达力是盒式给不了的 |
| **Q2** | `assets/mobs/<mob_id>.vox`，**纳入 T-A2 资产根解析**（零新增解析逻辑） | `resolve_assets_root()` 返回的是根目录，只需再加一个同型路径函数 |
| **Q3** | **程序化骨骼（分组）**：单份网格 + 调色板索引分段标注关节（1..8 = 关节，9+ = 颜色）。**朝向/位置/受击/蓄爆/幼体只读模拟值**；只有"姿态形变"允许渲染侧推导 | `MobAi` 里有 `yaw`/`pitch`/`hurt_cooldown`/`fuse`/`baby`，**没有**步态相位 ⇒ 相位由客户端时钟推导，但驱动量（速度幅值）来自模拟 |
| **Q4** | **自写解析器**（约 200 行）。不引入 `voxel-io`（129 文件 + 自带 4 个第三方编解码器）或 `VoxReader`（停更 7 年） | `.vox` 是无压缩定长小端整数；只需 `SIZE`/`XYZI`/`RGBA` 三种 chunk |
| **Q5** | **`engine/render/**` 一行不改**。客户端新增：新顶点格式 + 极小 shader + **每类型一份缓存网格**；**回退分支 = 现有代码原样**，判据是 `mob_models.find(type)` 命中与否 | 现有 `MeshVertex` 是区块网格形状、`crack_vao` 的 tile 走 uniform ⇒ 两者都装不下"每面不同调色板格" |
| **Q6** | 引擎侧 **≈ 770 行 C++（单卡可交付，不建议拆）**；美术侧 **≈ 2–4 小时/生物（待校准）**。分批：**B0 手写夹具 → B1 引擎侧 → B2 Mossback → B3 Hollow Wretch → B4 Blastbud** | 实测约 50 分钟/卡；跨层大卡也在一轮内交付（`docs/05 §3.3`） |

---

## 3. ★ 许可核验（三项未核项全部给出结论）

### 3.1 未核项 1：MagicaVoxel 软件许可 —— ✅ 已查实（**PM 的结论已被推翻**）

| 项 | 结果 |
|---|---|
| 许可**存在** | **存在**。不在 `LICENSE.txt`，而在**官网首页正文**的 License 小节 |
| 三条条款 | ①免费用于任何项目 ②鼓励署名 ③**禁止**把软件本体打包进其他发布物转售/再分发 |
| 来源 URL | `https://ephtracy.github.io/mv_main.html`（首页 `index.html` 拉取的内容页） |
| 访问日期 | **2026-09-18** |
| 核验方式 | `curl -sS -x http://127.0.0.1:7890` 取回 `mv_main.html`（64 188 字节），从 HTML 的 `<li><b>License</b>` 列表逐条读出 |
| 留痕 | `docs/qa/T-R3-2026-09-18/qa_mv_main.html`（原始 HTML）+ `qa_license-url-checks.txt`（URL 状态码表） |
| PM 的 `LICENSE.txt` 404 | ✅ **复核成立**：`https://ephtracy.github.io/LICENSE.txt` = **404**（本机实测）。**但 404 的是那个 URL，不是许可本身** |
| 发行包内许可 | ❌ **没有**。Mac 包（3 809 852 字节，sha256 `4ee661e4…e861226`）解包只有 `readme.txt`（含第三方素材署名），全局无 `LICENSE`/`COPYING` |
| 本机可用性 | ✅ **实测可运行**：0.99.6.2 mac 包在本机（macOS 15 arm64）经 Rosetta 2 启动并正常出画面（截图为证 `qa_mv_running_macos15.png`） |
| 版本/平台 | 官网标 0.99.7.2（2025-07-12）；**macOS 包停在 0.99.6.2（2020-09-26）**，0.99.7 只有 Windows |

**结论：许可适合本项目。** 第三条禁的是"分发**软件**"，而我们**不发行 MagicaVoxel**，
只在制作阶段用它生成 `.vox` ⇒ 该条不适用。

> ⚠ **PM 此前在对话中口头说的"`.vox` 可用 CC0 工具链"，按卡面要求如实记录为
> 未经核验的错误说法**（卡面 §3 已自我更正）。本次核验的结论是：
> **工具许可 = "免费用于任何项目"的自定义许可，不是 CC0，也不是开源许可。**

### 3.2 未核项 2：工具许可 ≠ 产出许可 —— ✅ 已写清楚

文档 §8.2 明确：许可三条的主语**全部是"软件"**，没有一个字主张对**产出文件**的权利
⇒ **用 MagicaVoxel 制作的 `.vox` 是 OpenCraft 的原创资产，著作权归本项目**，
受 `docs/04` 分层许可约束（资产 CC BY-SA 或 CC0）。

**但两条红线与工具无关，必须同时满足**（文档已逐条写明）：
1. `docs/04` 红线 2：形体与配色必须**原创**，不得拿原版生物模型/截图当参考画布；
2. `docs/05 §2` 三条硬规则：**AI 输入禁止含 MC 资产**、**提示词不得点名 MC**、
   **`assets/CREDITS.md` 逐条留痕**。

### 3.3 未核项 3：替代制作工具 —— ✅ 已核（**备选登记**，因 3.1 成立而非必需）

| 工具 | 许可 | 核验 | 备注 |
|---|---|---|---|
| **Goxel** | **GPL-3.0**（仓库元数据 + README 的 Licence 节明文） | 源码头部/README 均核 | macOS 原生 **arm64 通用二进制**；`.vox` 读写器 `src/formats/vox.c`（写版本 150 + `SIZE`/`XYZI`/`RGBA`）<br>⚠ **本机实测：装了、进程起来了，但没有窗口**（`brew install --cask goxel` 成功，两次启动均 0 窗口）⇒ 记「待校准」，**不得据此判定不可用** |
| `simlu/voxelshop` | Apache-2.0（**仅** GitHub API） | ❌ **未核**（无源码头部、无发行包核验） | 如实记录为未核 |
| 自制 | 我们自己的代码 | — | 规范 MIT ⇒ 合法 |

**Goxel 的 GPL 不传染我们的产物**（我们只**跑**它，不链接、不发行它的代码）——
与 MagicaVoxel 的分析同构：**两边的许可都只管软件本体。**

### 3.4 已核项的复核（PM 原有结论）

| 项 | PM 结论 | 本次复核 | 一致 |
|---|---|---|---|
| `ephtracy/voxel-model` 仓库 | MIT | ✅ MIT（`LICENSE` 正文，`Copyright (c) 2026 ephtracy`） | ✅ |
| 格式结构（RIFF 式 / `MAIN` / `SIZE` / `XYZI` / `RGBA` / `PACK`） | 已核 | ✅ 一致，**且补完**：规范全文 + 扩展规范（`MATL`/`nTRN`/`nGRP`/`nSHP`/`LAYR`/`rOBJ`…）已存档 | ✅ |
| `voxel-io` | MIT | ✅ MIT（`Copyright (c) 2020 Jan Schultke`）；**补完**：自带 lodepng = **zlib**、miniz/stb_image = **public domain** | ✅ |
| `VoxReader` | MIT | ✅ MIT，但 `Copyright (c) 2017 Jim-Eckerlein`（**第三人**）+ 最后推送 **2019-04-20**（停更 7 年） | ✅ + 补完 |

---

## 4. 新增的实测数据（文档里的"数值不编造"依据）

**A. 体素模型的表面复杂度**（一次性脚本解析真实 `.vox` 样本，原始输出
`qa_vox_geometry_measurements.txt`）：

| 样本 | 体素数 | 暴露面 | 顶点（4/面） | 三角形 |
|---|---|---|---|---|
| `chr_sword.vox` | 334 | 510 | 2 040 | 1 020 |
| `chr_knight.vox`（人形） | 398 | 730 | 2 920 | 1 460 |
| `castle.vox` | 2 628 | 3 696 | 14 784 | 7 392 |
| `teapot.vox` | 28 411 | 55 964 | 223 856 | 111 928 |

⇒ 一个 **398 体素的人形 ≈ 1 460 三角形**；×70 同屏 = **10.2 万三角形** —— 顶点数不是瓶颈。

**B. 真实 `.vox` 的 chunk 构成**（`qa_vox_chunk_structure.txt`，14 个样本全部可解析）：
一个 **3×3×3、20 体素**的模型，文件里还带着 `nTRN`×2 / `nGRP` / `nSHP` /
`LAYR`×8 / `MATL`**×256** / `rOBJ`×11 —— **无关 chunk 占文件体量的 99%**。
⇒ "只读三种 chunk、其余全跳过"是唯一合理做法。

**C. 本机 MagicaVoxel 0.99.6.2 可运行**（`qa_mv_running_macos15.png`，窗口 id 1556）。

---

## 5. 给项目经理的备注

1. **★ PM 的许可结论需要更正**：卡面 §3 写"官网未列明许可条款 ⇒ 不得默认 CC0"，
   实际是**官网首页正文有 License 小节、三条条款俱全**（PM 当时很可能只查了
   `LICENSE.txt`，那个 URL 确实 404）。文档 §8.1 已按"来源 URL + 访问日期 + 核验方式"
   逐条留痕，可直接引用。
2. **★ 死亡动画有一个模拟侧前置**：`kill_mob()` 当场 `store.erase(id)`
   （`mob_sim.hpp:651`），**模拟里不存在"正在死亡"的状态** ⇒ 真倒地动画必须先加模拟字段
   （建议 R-10：先做粒子版 D1，倒地版 D2 另开一张模拟卡）。这不是渲染卡能绕过的。
3. **★ 实现卡不需要拆，但需要一个"手写夹具"前置**（R-9 的 B0）：
   解析器单测不能依赖 MagicaVoxel 自带的示例模型（**那不是我们的资产，不得入仓**）。
   建议在实现卡里先提交一个**手写的、极小的 `.vox`**（我们自己的），
   这样引擎侧不必等美术侧。
4. **既有问题登记（未顺手修，卡面 §7.5 要求）**：
   `mob_skin()` 借**方块 id** 且写错是**硬崩溃**（`inventory_wiring.hpp:245-247`，实测
   crash 消息 `unknown block id: loam_clod`）。模型通道落地后该函数只服务回退分支——
   建议在**回退分支被删除的那张卡**里一并清理（R-14），本卡未动。
5. **一个"跨平台工作流"风险**：macOS 侧工具停在 2020 年的 0.99.6.2，Windows 侧是
   2025 年的 0.99.7.2。`.vox` 版本号仍是 150（实测样本一致），**格式兼容性无碍**，
   但功能差异会影响美术侧协作 ⇒ 建议美术侧卡面约定统一版本。
6. **Q3 的判定标准建议落规格**（R-5）：本项目已有一处相关注释
   （`main.cpp:706` "朝向是模拟值，不是渲染侧动画"）。建议把这条从注释升级为
   `docs/03 §4` 的明文规则 + 一条**可判定的标准**：
   > **凡能改变"玩家看到生物在哪里 / 朝向哪 / 在做什么"的量必须来自模拟；
   > 只有"同一位置与朝向下的姿态形变"允许渲染侧推导。**

---

## 6. 构建 / 运行 / 测试方法

**本卡无代码改动 ⇒ 无需构建与测试。** 为证明"零行为变化"，用以下命令即可自查：

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-tr3
git status --short     # 预期：只有 2 个未跟踪路径 + docs/qa/T-R3-2026-09-18/
git diff --stat HEAD   # 预期：空（无任何已跟踪文件被修改）
```

**结果**：

```
$ git status --short
?? docs/qa/T-R3-2026-09-18/
?? docs/research/12-mob-model-formats.md

$ git diff --stat HEAD
（空）
```

⇒ **已跟踪文件 0 处改动**，仓库仍处于基线 `36bcc40` 的代码状态。

<details>
<summary>（供 PM 复核用：QA 证据目录清单）</summary>

```
docs/qa/T-R3-2026-09-18/
├── qa_license-url-checks.txt        15 个 URL 的 HTTP 状态码与字节数
├── qa_mv_main.html                  MagicaVoxel 官网正文（License 小节所在）
├── qa_mv_running_macos15.png        本机运行的 MagicaVoxel 窗口截图
├── qa_vox_format_spec.txt           .vox 规范原文（MIT 仓库）
├── qa_vox_format_ext.txt            .vox 扩展规范原文
├── qa_vox_chunk_structure.txt       14 个真实 .vox 的 chunk 构成（实测）
├── qa_vox_geometry_measurements.txt 体素数/暴露面/顶点/三角形（实测）
├── qa_voxel-model-repo-LICENSE.txt  ephtracy/voxel-model 的 MIT 全文
├── qa_voxel-io-LICENSE.txt          eisenwave/voxel-io 的 MIT 全文
├── qa_voxreader-LICENSE.txt         Deins/VoxReader 的 MIT 全文
└── qa_goxel-README.md               Goxel 的 Licence 节（GPL-3.0）
```

</details>

---

## 7. 结果（对照任务卡 §6 验收标准）

| # | 验收标准 | 结果 | 证据 |
|---|---|---|---|
| 1 | 新增调研文档 ≥ **400 行**，逐条回答 Q1–Q6 | ✅ **868 行**，Q1–Q6 各有独立小节（§2–§7） | `docs/research/12-mob-model-formats.md` |
| 2 | 三个未核项**全部给出结论**，每条附来源 URL + 访问日期 + 核验方式；查不到如实写"未核到" | ✅ 三项全部有结论。**未核到**的两处已如实标注（`simlu/voxelshop` 的源码许可；Goxel 本机不出窗口的**原因**） | §3.1 / §3.2 / §3.3 与文档 §8.1–§8.4 |
| 3 | 数值不编造：有来源的给来源，无来源的标「待校准」 | ✅ 实测值给测量方法与原始输出文件；无来源的一律 `⚠ 待校准`（步态频率/幅度、死亡时长、调色板过滤模式等） | 文档 §2.3 / §8.1 / §10 / §11.3 |
| 4 | 合规自检：**0 处**逐字复制 | ✅ 机器判定：与 7 份来源的 **8-gram 重合 = 0**；放宽到 6-gram 仅剩 3 处**许可名称/版权行**（属许可声明类，文档 §11.2 已说明边界） | 文档 §11.1 |
| 5 | 结论以 `R-1..R-N` 建议表写入报告，由 PM 决定是否落规格 | ✅ **R-1..R-14**，每条含"落点 / 依据 / 优先级" | 文档 §9 |
| 6 | 动画与模拟的关系写清楚 | ✅ §4.4 给出**逐字段表**（唯一来源 / 允许做 / 禁止做）+ 一条可判定标准 | 文档 §4.4 |
| 7 | 回退策略：缺模型文件时仍画立方体 | ✅ §6.5 七行回退表（缺目录/缺文件/非 `.vox`/解析失败/0 体素/调色板缺失/调色板坏）+ **启动计数日志 `mobs: N/3 mob models loaded`** 作为机器判据 | 文档 §6.5 |
| — | 报告落 `docs/tasks/T-R3.report.md` | ✅ | 本文件 |
| — | 白名单外零改动 | ✅ `git diff --stat HEAD` 为空 | §6 |

**结论：本卡 §6 的 7 条验收标准 + 报告落盘 + 白名单约束，全部满足。**

---

## 8. 已知问题 / 待校准项

| 项 | 性质 | 处置 |
|---|---|---|
| Goxel 在本机**不出窗口** | ❌ 未核（进程存活、无窗口） | 已标"待校准"；**不得**据此判定 Goxel 不可用 |
| `simlu/voxelshop` 源码许可与 `.vox` 支持 | ❌ 未核 | 已如实标注；将来要换工具再核 |
| MagicaVoxel 0.99.7.2 的 macOS 行为 | ⚠ 无 macOS 包 | 以 0.99.6.2 为准 |
| 行走摆动**频率/幅度**、死亡动画时长 | ❌ 无来源 | **待校准**（实机手感） |
| 调色板贴图的采样过滤模式 | ❌ 未核（`rhi.cpp` 的 `Texture2D` 默认过滤） | 实现卡核实；**像素风必须 nearest** |
| 关节标签 8 个是否够 | ⚠ 按首发三种生物够用 | 美术侧反馈后扩到 16（零格式成本） |
| 单模型体素/暴露面**硬上限** | ⚠ §7.2 是**预算**不是对齐值 | 首个模型实测后修订 |
| `.vox` 版本迁移策略（150 之后） | ⚠ 规范未给 | 解析器只接受版本 150，其他拒绝 + WARN |

---

## 9. 接口变更

**无。** 本卡零代码改动，不新增/修改任何 API、文件格式或数据结构。

文档中给出的接口形状（`mob_model_path()` / 顶点格式 / 关节契约 / 启动日志格式）
**均为建议**，需 PM 裁决后才能被后续卡依赖（`docs/05 §3` 审查规则 2）。

---

## 10. 文件列表

**新增（全部在卡面白名单内）**：

```
docs/research/12-mob-model-formats.md          868 行，调研文档（Q1–Q6 + 许可 + 建议表 + 自检）
docs/tasks/T-R3.report.md                      本报告
docs/qa/T-R3-2026-09-18/qa_license-url-checks.txt
docs/qa/T-R3-2026-09-18/qa_mv_main.html
docs/qa/T-R3-2026-09-18/qa_mv_running_macos15.png
docs/qa/T-R3-2026-09-18/qa_vox_format_spec.txt
docs/qa/T-R3-2026-09-18/qa_vox_format_ext.txt
docs/qa/T-R3-2026-09-18/qa_vox_chunk_structure.txt
docs/qa/T-R3-2026-09-18/qa_vox_geometry_measurements.txt
docs/qa/T-R3-2026-09-18/qa_voxel-model-repo-LICENSE.txt
docs/qa/T-R3-2026-09-18/qa_voxel-io-LICENSE.txt
docs/qa/T-R3-2026-09-18/qa_voxreader-LICENSE.txt
docs/qa/T-R3-2026-09-18/qa_goxel-README.md
```

**修改**：无。
**删除**：无。
**未触碰**：`STATE.md`、`docs/00`–`docs/06`、`engine/**`、`game/**`、`assets/**`、`tests/**`。

---

## 变更记录

| 日期 | 内容 | 依据 |
|---|---|---|
| 2026-09-18 | 初版落盘：调研文档 + 证据目录 + 本报告 | 任务卡 `docs/tasks/T-R3.md` |
