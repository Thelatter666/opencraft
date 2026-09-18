# T-B3 · Hollow Wretch 人形体素模型（美术总监；契约 v2 首用）

> 卡面是派发时点快照，**正文不得修改**；修订一律追加「变更记录」。
> 前置：T-B2（Mossback 已合入 `73a37ef`）与 T-B2b（契约 v2 已生效，`fbef08d`）。
> 性质：**纯资产卡，零代码改动**（与 T-B2 同型）。这是"每关节第二色"的首个用户——**眼睛**。

## 1. 目标（一句话）

产出第二只生物 `hollow_wretch`（敌对·人形·近战猎手）的 `.vox` 体素模型 + 调色板 PNG，
风格与 Mossback 同源（同一限定色板、同一 1 体素=0.1 格档），**用契约 v2 的第二色给脸画上眼睛**。

## 2. 契约（数字现数于 `fbef08d`；违规任何一条 = 卡不通过）

| # | 契约 | 现数出处 |
|---|---|---|
| ① | **Z-up，正面 = 文件 +Y**；y-up 模型会被"正确解析"却趴地 | `mob_mesh.cpp` 坐标映射；T-B1 两轮踩坑记录 |
| ② | 缩放：**体素 z 跨度 → 碰撞高 1.8 格** ⇒ 本卡画布 **1 体素 = 0.1 格、z 跨度 = 18**（x/y 尽量贴碰撞宽 0.6 ⇒ 6 格）。⚠ 加高附件（角/帽）会把整只压小 | `mob_type.cpp:107` `mob_physics("Hollow Wretch", 0.3, 1.8, 20)`；T-B1.ruling §3 已拍板 |
| ③ | **契约 v2 调色板分段**：`1..8` = 关节主标签（1 躯干/2 头/3 左臂/4 右臂/5 左腿/6 右腿/7 尾/8 备用）；**`9..16` = 对应关节的第二色（10 = 头的第二色 ⇒ 眼睛）**；`17..255` = 躯干组杂色。colorIndex 0 禁用 | `mob_mesh.cpp:mob_joint_of_color`（v2）；`tests/test_mob_model.cpp` 新用例钉死 |
| ④ | 调色板 PNG `assets/palettes/hollow_wretch.png`：16×16、**格号 = colorIndex**（第 0 格不画）；RGBA 全不透明；色值全部取自 `docs/art/01-style-guide.md` §2.1 的 32 色表、最大饱和 ≤ 0.50 | T-B2 同款；`load_palette_png` |
| ⑤ | 预算：**≤ 1000 体素**、暴露面 ≤ 2000、每边 ≤ 32、关节 ≤ 8 | `research/12` §7.2/§6.5 |
| ⑥ | 日志判据（**两向**）：有模型 `mobs: 2/3` + 本模型行 + WARN=0；移走 `assets/mobs/` → `mobs: 0/3`、WARN=0。**且 Mossback 的行逐字节不回归** | T-B2 验收装置 |
| ⑦ | 关节数打点（"6 关节"或你实际用的数）＝ 独立部分数：**眼睛用索引 10 不增加部分数**（同关节两色同 part 区段）；若你的日志行 joints 比预期多一格，先查是不是把第二色误写成了别的关节的主标签 | T-B2b 眼睛演示（v2=4 vs v1=3）证明的判据 |

## 3. 设计与制作（美术侧自由，但四条边界）

1. **原创性（红线 2/5）**：不得参照任何原版生物模型/贴图；"空洞苦役/枯槁者"从**意象**自己拆几何
   （细长四肢、前倾驼背、锁骨下陷的空洞感、低垂的头……你的设计稿说了算但要写进报告）。
   全程不用 AI 生成、不喂任何既有作品（T-A3/T-A4/T-B2 同款路线）。
2. **产线照抄**：`docs/qa/T-B2b-2026-09-18/tools/` 的四件套（**v2 版**：逐层字符稿 + 编码器 +
   独立读回器 + PNG 复核）拷进你的证据目录改内容即可；`mossback_layers.txt` 是稿件式样。
   **逐层字符稿是创作本体**，脚本只做字符→字节翻译（无几何/对称/填充/随机）。
   `vox_inspect.py` 的**每索引包围盒表**直接当方位自检；出 `*_contact_sheet.png` 与 `*_silhouette.png`。
3. **两条抗 y-up 判据**：① 头 z 高于躯干；② **更锐：接触地面的 z=0 层只许是脚**。
4. **MV 可选加分**（非门槛）：`open -a "/Users/happy/Desktop/opencraft_scratch/mv/MagicaVoxel-0.99.6.2-macos-10.15/MagicaVoxel.app" "<交付件绝对路径>"`，
   标题栏/尺寸/形体截图入证据目录（工具许可口径已核，不随仓库分发；它自带的示例模型不得进仓库）。

## 4. 允许触碰的文件（白名单；路径相对 worktree 根 `/Users/happy/Desktop/opencraft_worktree/opencraft-tb3`）

**允许**（全部新增或追加）：
- `assets/mobs/hollow_wretch.vox`、`assets/palettes/hollow_wretch.png`（★ 交付资产）
- `assets/CREDITS.md`（**每资产一行**、六字段：文件/来源/制作方式/设计稿摘要/日期/过程；过程列指你自己证据目录的工具）
- `docs/art/01-style-guide.md`（**只许改 §9 待办表与追加变更记录行**）
- `docs/qa/T-B3-<日期>/`（你的证据目录：稿件、脚本副本、日志、截图）
- `docs/tasks/T-B3.report.md`（报告）

**禁止**：一切源码与构建文件（`game/**`、`engine/**`、`tests/**`、`CMakeLists.txt`、`cmake/**`）、
其他资产（`assets/blocks/**`、`assets/mobs/mossback.vox`、`assets/palettes/mossback.png`）、
**T-B1/T-B2/T-B2b 的任何文件（历史 QA 证据目录默认不可变）**、`STATE.md`、`docs/0*`、`docs/05`、
本卡正文、记忆文件。

## 5. 验收标准（报告逐字贴命令与输出）

1. `git diff --name-only main...HEAD` 只含白名单文件。
2. 启动日志（`cd build` 运行 `./opencraft`，无补丁）：
   `mobs: 2/3 mob models loaded from ../assets/mobs` +
   `mob model hollow_wretch: <数> voxels, <数> triangles, <数> joints, 1.8 blocks tall, palette from palettes/hollow_wretch.png`，
   **WARN=0**；Mossback 行与 T-B2 验收行**逐字节相同**；移走 `assets/mobs/` → `0/3`、WARN=0。
3. 预算五项自查表（体素/包围盒/暴露面/关节数/z 跨度）+ `vox_inspect.py` 每索引包围盒表贴进报告。
4. 契约 v2 证据（★ 本卡特有）：**索引 10/或你选的头第二色，其实体素必须落在"头"关节的包围盒内**
   （包围盒表可判）；且报告给出 `mob model … joints` 数与你的部分数一致（眼睛不多出 part）。
5. 风格合规：`docs/art/01-style-guide.md` §8 逐项（含不适用项的说明）+ §3 低饱和 + CREDITS 两行六字段齐。
6. 布局与形态自检：剪影图中"人形 + 前倾 + 细长四肢"可读（对照 T-B2 §4.4 的迭代法：先画→渲染看→改）。

## 6. 流程与提交

- 开工第一条命令：`git worktree add /Users/happy/Desktop/opencraft_worktree/opencraft-tb3 -b task/T-B3-hollow-wretch main`
- 构建禁接管道；**不设** `FETCHCONTENT_BASE_DIR`（会挂掉主仓构建）；GitHub 克隆若 SSL 失败，
  临时 `git config --global http.https://github.com.proxy http://127.0.0.1:7890`，用完撤销。
- 提交信息 `taskT-B3: 摘要`；报告 `docs/tasks/T-B3.report.md`（建议表放报告内，**不得自行改 STATE/规格/记忆**）。
- 回贴：简短版 + 报告绝对路径（单代码块）。

## 7. 已知风险与提示

1. **高度改变 = 整只缩放**：z 跨度必须恰为 18（1 体素 = 0.1 格）；别在头顶加"发冠"再指望头还是那个尺寸。
2. **眼睛别做成深色体素凹陷**：体素模型的"眼睛"= 头部的**第二色体素**（索引 10），
   凹一格会改变剪影且要与索引 2 混涂同一关节——两色都归 head part，不会多出关节。
3. **人形正面朝向是取证难点**：B3 实机 A/B 由 PM 侧装置做（人形 1.8 格高于眼位 1.62，
   **暂停菜单会盖住头顶**；PM 方案：俯仰注入须在**暂停前**做，或换取景几何——你不用管，
   但**别把关键时刻的形体做在头顶小附件上**，摄像机多数时看不到）。
4. **Mossback 的行不许回归**：同一启动里两只模型的行都要对——改动只该新增行。
5. 体量心理预算：人形窄（6 格宽），体素数预计 300–600，比 Mossback 的四足好控制；
   但**不要**为了"细节"上 17–18 档以外的加密（1 体素=0.1 格是本项目最佳性价比档，T-B2 §4.4 实测）。

## 变更记录

| 日期 | 内容 | 依据 | 授权 |
|---|---|---|---|
| 2026-09-18 | 初版落盘并派发。契约数字现数于 `fbef08d`：`mob_type.cpp:107` 高 1.8/宽 0.6；v2 映射与"眼睛=索引 10"出自 T-B2b 合入 + T-B2b.ruling；四件套 v2 版在 `docs/qa/T-B2b-2026-09-18/tools/`；MV 开法在 `docs/qa/T-B2-2026-09-18/mv_thirdparty/README.md` | `T-B2b.ruling.md` §5；`STATE.md` 5g | PM |
