# T-B2b · 生物关节标签扩约（1..8 → 1..16 第二色）+ mossback 资产迁移（引擎侧）

> 卡面是派发时点快照，**正文不得修改**；修订一律追加「变更记录」。
> 角色：**开发者**（引擎侧）。裁决出处：`docs/tasks/T-B2.ruling.md` §2（2026-09-18 拍板）。
> 前置：T-B2 已合入（merge `73a37ef`）。本卡完成后才派发 B3（Hollow Wretch 需要第二色画眼睛）。

## 1. 目标（一句话）

把调色板分段契约从"索引 1..8 = 关节标签（每关节仅一色）、9+ 全归躯干"扩为
**v2：`1..16 → 关节 (idx−1) mod 8`（9..16 = 各关节第二色）、`17..255 → 躯干组`**，
并把已交付的 mossback 资产里占用 9..16 的躯干色迁移到 17..24。

## 2. 契约与现状（数字全部现数于 `73a37ef`）

- 唯一要改的函数：`game/client/src/mob_mesh.cpp:54-59` `mob_joint_of_color`
  （现在：`1..kMobJointCount → idx-1`，其余含 `>=9` 一律返回 0=躯干）。
  `kMobJointCount = 8`（`game/client/src/mob_mesh.hpp`，枚举 `MobJoint` 不变）。
- 受影响的存量断言：`tests/test_mob_model.cpp:782` TEST_CASE
  "T-B1 mesh: joint labels are the palette's first eight indices"——**PM 实跑核对：改动后
  存量断言零翻转**（783..790 的 1..8 不变；793/794/795 的 0/9/255 在 v2 下返回值
  **数值不变**，但 9 的语义从"无标签躯干组"变为"躯干第二色"；10..16 区间现无任何存量断言；
  811 行夹具体素色 =9、注释"unlabelled"在 v2 下过时，**该注释须改**）。
  ⇒ 风险不是"存量红"而是"**存量全绿掩盖语义漂移**"——新契约的区分度完全靠 §3.1 的新增用例钉。
  若 ctest 真出现 old→new 翻转，逐条记账（卡面在此显式许可）。
- 回退表、解析器、`mobs: N/3` 判据行、枢轴/包围盒/draw call 逻辑**都不许动**：
  关节数仍 ≤8，第二色只是同关节的另一种 colorIndex。

## 3. 交付内容

### 3.1 引擎侧
1. `mob_joint_of_color`：`if (idx >= 1 && idx <= 2 * kMobJointCount) return (idx - 1) % kMobJointCount;`
   躯干分支不变。函数注释与 `mob_mesh.hpp` / `mob_mesh.cpp` 顶部**契约注释同步改到 v2 口径**。
2. 新增单测（本卡**允许新增用例**，457 这个总数预期会变，报告给出新总数）：
   - 边界：`mob_joint_of_color(8)==kMobJointSpare`、`(9)==kMobJointBody`（v2 的 9=躯干第二色，
     恰好仍落躯干——注意这是 v2 下 9 的**新语义**，别与 v1 的"9→躯干组"混淆，断言消息要写清）、
     `(10)==kMobJointHead`、`(16)==kMobJointSpare`、`(17)==kMobJointBody`、`(255)==kMobJointBody`。
   - 网格级：同一关节两块不同色（例：索引 2 与 10 的头体素）⇒ 顶点分属两块颜色、
     **但落在同一个 part 区段**（同一次 `glDrawArrays`）。
3. 产品日志回归：跑一次产品码，`mobs: 1/3 …` 与 `mob model mossback: … 7 joints …` 行
   **逐字节不变**（mossback 迁移前 9..16 归躯干、迁移后 17..24 归躯干，部件数都是 7）。

### 3.2 mossback 资产迁移（机械操作，禁止任何艺术改动）
1. 迁移规则：`.vox` XYZI 中 colorIndex ∈ **{9..16} → +8**（变 17..24）；RGBA 调色板与
   `assets/palettes/mossback.png` 的格 9..16 内容移到格 17..24，**新格 9..16 填 U0 `#1E1E22`**。
   躯干形状、头、腿、尾一个体素都不动。
2. 做法 = 改 `docs/qa/T-B2-2026-09-18/tools/` 的四件套里**索引分配表**（稿件字符本身不改），
   重跑 `vox_build.py`，重跑 `vox_inspect.py`；`assets/CREDITS.md` 两行的设计稿摘要里
   "9..16 = 躯干组颜色槽"改为 v2 口径（17..24 躯干组、9..16 关节第二色暂空）。
3. **复算校验**：迁移后体素总数仍 672；每关节包围盒与 `T-B2.report.md` §3 表逐项一致；
   日志行 `672 voxels, 1504 triangles, 7 joints, 1.4 blocks tall, palette from …` 逐字不变。

## 4. 允许触碰的文件（白名单；相对 worktree 根 `/Users/happy/Desktop/opencraft_worktree/<自取名>`）

- `game/client/src/mob_mesh.cpp`、`game/client/src/mob_mesh.hpp`（若注释在）
- `tests/test_mob_model.cpp`（按 §2 许可逐条 old→new + 新增用例）
- `assets/mobs/mossback.vox`、`assets/palettes/mossback.png`（迁移后重生成物）
- `assets/CREDITS.md`（仅两行的摘要句口径）
- `docs/qa/T-B2b-<日期>/`（自己的证据目录，新建）
- `docs/tasks/T-B2b.report.md`（报告）
- **禁止**：`engine/**`、`game/client/src/main.cpp`、`mob_model.*`、`mob_render.*`、`mob_pose.*`、
  CMakeLists（无新增源文件）、`STATE.md`、`docs/0*`、`docs/05`、本卡与 T-B2/T-B1 正文、记忆文件。
  **特别禁止"顺手改艺术"**：mossback 外观任何一格都不许变。

## 5. 验收标准（报告逐字贴命令与输出）

1. `git diff --name-only main...HEAD` 只含白名单文件。
2. ctest：v1 的 457 全数在跑（用例名可增不可消失，**除 §2 点名的 old→new 逐条记账外零改写**），
   新总数报出；`mob_joint_of_color` 边界组全绿。
3. 日志回归三件套：`mobs: 1/3` + `mob model mossback: 672 voxels, 1504 triangles, 7 joints,
   1.4 blocks tall, palette from palettes/mossback.png` + `WARN=0`；移走 `assets/mobs/` → `0/3` WARN=0。
4. 迁移核验（§3.2 第 3 条的三项复算）+ `vox_inspect.py` 每索引包围盒表贴进报告。
5. 眼睛可行性演示（**功能判据，非艺术验收**）：用一次性夹具（不入库，留证据目录）
   造一个头 = 索引 2 + 索引 10 两色的微型模型，跑产品码，日志关节部件数与单色头相同，
   证明"同一关节两色走同一次 draw call"。

## 6. 流程与提交

- 开工第一条命令：`git worktree add /Users/happy/Desktop/opencraft_worktree/<名字> -b task/T-B2b-joint-expansion main`
  （`docs/05 §6`；worktree 内构建/测试/提交；不碰主仓）。
- 构建禁管道、不指 `FETCHCONTENT_BASE_DIR`；提交信息 `taskT-B2b: 摘要`。
- 报告 `docs/tasks/T-B2b.report.md`：改动清单、old→new 断言记账表、日志三件套、迁移复算、
  建议表（若有）——**不得自行改 STATE.md/规格/记忆**。
- 交付摘要回贴：简短版 + 报告绝对路径（单代码块）。

## 7. 风险与提醒

1. **v2 的索引 9 落点巧合**：v1 的"9→躯干组"与 v2 的"9→躯干第二色"都落 joint 0——
   若测试只断言 `mob_joint_of_color(9)==Body` 会**假通过**，语义变化不可见。
   新断言必须同时验"17 也归躯干"与"10 归头"，把三段区间钉死。
2. mossback 迁移后旧 PNG/RGBA 若忘了同步移位 ⇒ 躯干色全部变 U0（近黑），一眼能看出，
   但请靠 §3.2 第 3 条的复算抓，别靠眼睛。
3. 暂停帧装置给 B3 预备的账（`docs/qa/T-B2-2026-09-18/pm_verify/README.md` §3/§4）：
   1.4 格高生物召唤距离 4.5 格；暂停态注入鼠标俯仰**无效**，别再试。

## 变更记录

| 日期 | 内容 | 依据 | 授权 |
|---|---|---|---|
| 2026-09-18 | 初版落盘并派发；契约 v2 数字、受影响断言行号（794）、日志回归期望行现数于 `73a37ef` | `docs/tasks/T-B2.ruling.md` §2 | PM |
