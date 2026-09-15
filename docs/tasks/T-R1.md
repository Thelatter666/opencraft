# 任务 T-R1：Minecraft 流体系统调研白皮书（技术顾问/研究员）

里程碑：M2 前置（调研卡）　前置：无
基线：当前 main（`b98d504`）。**本卡不改任何 C++ 代码**。
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

产出一份合规、可据以实现 OpenCraft 流体系统的调研文档
`docs/research/10-mc-fluid-dynamics.md`，把 Minecraft Java Edition 流体系统
（有界局部元胞自动机 + 加权图 BFS 混合模型）的机制、数值、调度与边界情形整理清楚，
为后续「水桶倒水」实现卡提供唯一的规格依据。

**你是技术顾问与研究员，不是开发者。本卡的产出物是文档，不是代码。**

## 为什么

用户诉求（2026-09-16 原话）：

> "我想先派一张卡作为'技术顾问与研究人员'，研究 Minecraft 中的流体……
> 研究完成且成果文档落地后你再派一张开发者卡，给物品栏加入一桶水，我可以倒在地上，
> 呈现 Minecraft 中相同的流体效果。**这一步的目的是将 opencraft 中流体这个大方面的基座给打好**。"

当前状态：OpenCraft 的水只有一个 `BlockDef.liquid` 布尔标记（`engine/voxel/include/opencraft/voxel/block_registry.hpp:31`），
是**静态方块**——不会流动、没有水位、没有扩散。债务 T-D12「水的 MC 流体模型」长期 pending。
本卡就是为它打地基。

## 输入材料

**桌面白皮书**：`/Users/happy/Desktop/Minecraft 流体动力学与元胞自动机引擎技术白皮书.md`（23 KB）

### ⚠ 该白皮书的使用限制（PM 已审，必须遵守）

这份白皮书**内容质量不错，但当前形态不能进 `docs/`**：

1. **含约 7 处 FabricMC Yarn 反编译源码片段**（Java + Python 伪代码），
   例如 §1.1 的 `protected void appendProperties(...)`、§4.2 的 `getCacheKey`、
   §6.1 的 `Vec3d vec3d = Vec3d.ZERO; ...`。
   **`docs/04-legal-compliance.md` 红线 1 明文禁止**：不参考 MCP/Yarn 等映射产物写逻辑，
   也不得把其产物喂给 AI 再搬回来。**这些片段一律不得进入文档。**
2. **所有引用链接都套了 `google.com/search?q=` 跳转前缀**，不可直接访问。
   PM 实测：剥掉前缀后 wiki 页面全部 200 可达（见下方「PM 已核验」）。请直接用规范 URL。
3. **它是一份好素材，不是一份合规文档。** 你的工作是把它**消化后用本项目的语言重写**。

处理方式参照本项目先例 `docs/research/08-mc-auto-jump-mechanics.md`
（当时的做法：入库但删源码路径，数学公式与机制结论保留）。

## ★ 合规红线（`docs/04-legal-compliance.md`，触碰即返工）

1. **禁止粘贴任何反编译源码片段**（Java / 映射产物 / 逐行伪代码照抄）。
   机制可以用你自己的话描述，**算法结构可以画示意图，但不得搬运实现代码**。
2. **禁止逐字复制 Minecraft Wiki 文本**（CC BY-NC-SA，且 NC 禁商用、SA 传染）。
   只提炼事实与数值，用自己的话重写。
3. 允许保留的只有：**数学公式**、**数值/常量**、**机制的结构性描述**、
   **来源 URL + 访问日期**、你自己画的结构图。
4. 命名全部用本项目原创命名，不使用 MC 的内部类名/字段记号作为文档骨架
   （提到"社区通常称之为 X"可以，但不要让它成为结构依赖）。
5. 只记公开资料：Minecraft Wiki、公开技术文章。**不用** Yarn / MCP / Forge 反混淆产物。

## PM 已核验的事实（可直接引用，标记来源即可）

PM 于 2026-09-16 用 `curl` 拉 wiki 原文（`?action=raw`）核对过，以下为**原文摘录**，可信：

| 事实 | Wiki 原文依据 |
|---|---|
| 水平扩散 7 格 | `Water`：「spread downward infinitely until stopped by a block, and **7 blocks** horizontally from a source block on a flat surface」 |
| 水流速度 | `Water`：「Water spreads at a rate of **1 block every 5 game ticks**, or 4 blocks per second」 |
| 岩浆扩散（主世界/末地） | `Lava`：「In the Overworld and the End, lava travels **3 blocks** in any horizontal direction from a source block」 |
| 岩浆扩散（下界） | `Lava`：「In the Nether, lava travels **7 blocks** horizontally and spreads 1 block every **10 game ticks**」 |
| 岩浆流速 | `Lava` 模板：`flowrate = 30 ticks/block (Overworld, End) / 10 ticks/block (Nether)` |

⚠ **两处与桌面白皮书不一致，请在文档中如实处理**：

- 岩浆 `flowdistance` 在 wiki **信息框模板**里写的是 `4 blocks (Overworld, End)` / `8 blocks (Nether)`，
  而同一页**正文**写 3 / 7 blocks。这是 wiki 自身的口径差异（"travels N blocks" 与模板参数含义不同）。
  请以**正文**为准（3 / 7），并在文档中注明存在该口径差异——**不要擅自选一个掩盖过去**。

## ★ 范围（严格）

**做**：一份 `docs/research/10-mc-fluid-dynamics.md`，覆盖：

1. **数据表示**：流体状态如何与方块状态共存；水位/能级的取值域与语义；1.13 后从 metadata
   解耦为独立流体状态的设计动机（这条对我们很关键——OpenCraft 现在是 u16 注册表，
   要考虑怎么表达水位）。
2. **调度模型**：计划刻（scheduled tick）机制；水流/岩浆在不同维度的时间参数表；
   与随机刻的区别；调度队列溢出/背压的行为。
3. **元胞自动机转移规则**：垂直下落优先、水平能级梯度衰减、能量/水位计算公式；
   ΔL 衰减常数（水 1、主世界岩浆 2、下界岩浆 1）。
4. **坡度寻路**：为什么要找"落差口"；搜索半径参数；多方向代价相同时的选择规则；
   找不到落差时的回退行为。这是最容易被漏掉、但对观感影响最大的一条——
   **请把它讲透**，后续实现会直接依赖。
5. **源生成判定**：无限水的形成条件（相邻源头数量、底面约束、gamerule 闸门）；
   岩浆源转换的差异（默认关闭）。
6. **实体交互**：流场推力向量的构造与归一化；浸没实体的阻尼/重力/ pushing 数值；
   气泡柱的正负力场。（**只记数值与公式**，OpenCraft 侧如何接由后续卡定。）
7. **边界情形**：更新抑制、浮空静止流体、 takeover（截至 Discussion 再齐全）。
8. **对 OpenCraft 的落地建议**：现有 `BlockDef.liquid` 布尔标记够不够？
   需要哪些表示层改动与调度层改动？给出分阶段的建议（但**不写代码**）。

**不做**：
- ❌ 不写任何 C++ 代码，不改任何代码文件
- ❌ 不改 `docs/01` / `docs/03` 规格（PM 职权；你的产出是调研文档，规格改动由 PM 裁决后落卡）
- ❌ 不改 `STATE.md`、不改记忆层
- ❌ 不做 OpenCraft 侧实现

## 允许触碰的文件/目录（白名单）

- **唯一产出**：`/Users/happy/Desktop/opencraft/docs/research/10-mc-fluid-dynamics.md`
- 输入（只读）：`/Users/happy/Desktop/Minecraft 流体动力学与元胞自动机引擎技术白皮书.md`

⚠️ **禁碰**：`STATE.md`、除上述文件外的所有 `docs/**`（含 `docs/01`、`docs/03`、
`docs/04`、`docs/05`、`docs/research/0*`）、`docs/tasks/`、
`engine/**`、`game/**`、`tests/**`、`cmake/`、`CMakeLists.txt`、`.github/`、`assets/`。

## 验收标准（逐条可执行）

1. **产出与合规**：文档存在、结构完整（覆盖上述 8 个方面）、**通篇无反编译源码片段**、
   无逐字 wiki 段落。PM 会亲自读全文核对。
2. **数值可溯源**：每个关键数值/参数旁标注来源 URL（规范 URL，**不带 google 跳转前缀**）
   + 访问日期。禁止无来源的数值。
3. **与 PM 核验一致**：上文「PM 已核验」表的 5 条事实须出现在文档中且数值一致；
   岩浆 `flowdistance` 的 **3/7 正文 vs 4/8 模板口径差异须显式注明**（不得隐瞒）。
4. **坡度寻路讲透**（最重要的实现依据）：读者能据它直接写出 BFS 判定，
   含：搜索半径、落差口定义、多方向并列时的取舍、无落差时的回退。
5. **给出落地建议**：明确回答"`BlockDef.liquid` 布尔够不够"以及需要哪些改动；
   建议须针对 OpenCraft 现有结构（注册表 u16、区块调色板、`TickClock`、
   `LightEngine` 的相邻级联模式可作为类比），**不要写通用教科书建议**。
6. **P-001 合规**：未改动本卡白名单外的任何文件。
   你的建议/新发现的登记，一律以**建议表**形式写进**调研文档内**的「建议」章节，交 PM 落盘。
7. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-R1.report.md`，
   并在对话中**整体放单个 markdown 代码块**输出。
8. 完成后置 agentmemory action **`act_mu2wexpf_a1e8fb003b68`** 为 done。

## 已知风险与提示

- **这是调研卡，不是实现卡**：你的价值是把粉乱的材料理成可据以实现的依据，
  不是把论文写长。宁可少而准。
- **纠正 PM 是被鼓励的**：若你核验发现某些数值与 wiki 不符、或白皮书的公式推导有问题，
  **直接写在报告里**。本项目历史上多次由研究者/开发者纠正 PM 的错误并记功。
- **取证手法**：用 `curl -sL "<url>?action=raw"` 拉 wiki 原文比网页渲染稳定（实测有效）；
  比 WebFetch 快且不易超时。
- **access date 一律写 2026-09-16**（本次调研日期）。
- **不要在文档里自称/OpenCraft 称"Minecraft"为商标性用法**；描述机制时可以说
  "对齐基准 MC JE 1.21.x"，这是项目既有口径（`docs/01` 首行已如此表述）。
- **文档模板**：参照 `docs/research/07-physics-knowledge-base.md` 或
  `docs/research/08-mc-auto-jump-mechanics.md` 的头部格式（成文日期 / 状态 / 定位 /
  合规声明 / 落地规则 + 目录）。

## 附：为什么粒度停在这里（避免你发散）

本卡的直接消费者是下一张卡「水桶 + 放置后在地上呈现 MC 同款流体效果」。
因此**需要讲透**的是：能级/水位表达、计划刻调度、元胞自动机转移规则、坡度寻路、源生成。
**不必展开**的是：含水机制（waterlogging）、气泡柱细节、玄武岩生成、
红石与流体的交互——这些 M4+ 才用得上，写一节定值/概述即可，别做成百科。
