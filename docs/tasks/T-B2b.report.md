# T-B2b · 开发报告 — 关节标签扩约（契约 v2）+ mossback 资产迁移

- 分支：`task/T-B2b-joint-expansion`（worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-tb2b`，基线 `main` = `c5a2839`，含 T-B2 merge `73a37ef`）
- 角色：开发者（引擎侧）。卡面：`/Users/happy/Desktop/opencraft/docs/tasks/T-B2b.md`
- 状态：**全部验收标准自测通过**（逐字证据见下）。

---

## 0. 变更摘要

1. `mob_joint_of_color` 改到契约 v2：`1..16 → 关节 (idx−1) mod 8`（9..16 = 各关节第二色），`17..255 → 躯干组`；函数注释与 `mob_mesh.hpp` 声明注释同步改到 v2 口径。枚举 `MobJoint`、`kMobJointCount=8`、回退表、解析器、枢轴/包围盒/draw-call 逻辑一律未动。
2. 新增单测 2 例（边界组 + 网格级双色同 part）；存量用例名零删除、断言值零翻转（§2 点名的两处**注释**已改，见记账表）。**ctest 总数 457 → 459，全绿**。
3. mossback 资产机械迁移：XYZI 色字节 `9..16 → +8`，RGBA/PNG 格 9..16 内容移到 17..24、新 9..16 填 U0。**形体逐字节相同**（机器核验，见 §4）。
4. `assets/CREDITS.md` 两行设计稿摘要改 v2 口径。
5. 产品日志三件套逐字不变；`mobs/` 移除 → `mobs: 0/3`、WARN=0。
6. 眼睛可行性演示：三变体（单色头/双色头/纯第二色头）产品日志 **逐字相同**（`6 voxels, 56 triangles, 4 joints, 1.4 blocks tall`），WARN=0。

## 1. diff 文件清单（验收标准 1）

`git diff --name-only main...HEAD`（提交后跑，实际输出）：

```
assets/CREDITS.md
assets/mobs/mossback.vox
assets/palettes/mossback.png
docs/qa/T-B2b-2026-09-18/ctest_mob_joint_subset.txt
docs/qa/T-B2b-2026-09-18/eye_demo/tb2b_eye_a.log
docs/qa/T-B2b-2026-09-18/eye_demo/tb2b_eye_b_two.log
docs/qa/T-B2b-2026-09-18/eye_demo/tb2b_eye_c_second.log
docs/qa/T-B2b-2026-09-18/fixtures/eye_a_mono.vox
docs/qa/T-B2b-2026-09-18/fixtures/eye_b_two.vox
docs/qa/T-B2b-2026-09-18/fixtures/eye_c_second.vox
docs/qa/T-B2b-2026-09-18/inspect_v2.txt
docs/qa/T-B2b-2026-09-18/mossback_contact_sheet.png
docs/qa/T-B2b-2026-09-18/mossback_silhouette.png
docs/qa/T-B2b-2026-09-18/palette_png_check_v2.txt
docs/qa/T-B2b-2026-09-18/post_migration_md5.txt
docs/qa/T-B2b-2026-09-18/pre_migration/mossback_v1.png
docs/qa/T-B2b-2026-09-18/pre_migration/mossback_v1.vox
docs/qa/T-B2b-2026-09-18/run1_startup_with_assets.log
docs/qa/T-B2b-2026-09-18/run2_startup_no_mobs_dir.log
docs/qa/T-B2b-2026-09-18/tools/check_palette_png.py
docs/qa/T-B2b-2026-09-18/tools/make_eye_demo_fixture.py
docs/qa/T-B2b-2026-09-18/tools/mossback_layers.txt
docs/qa/T-B2b-2026-09-18/tools/mossback_palette.txt
docs/qa/T-B2b-2026-09-18/tools/vox_build.py
docs/qa/T-B2b-2026-09-18/tools/vox_build_v2_run.txt
docs/qa/T-B2b-2026-09-18/tools/vox_inspect.py
game/client/src/mob_mesh.cpp
game/client/src/mob_mesh.hpp
tests/test_mob_model.cpp
```

（`docs/qa/T-B2b-<日期>/` 为自设证据目录；本报告 `docs/tasks/T-B2b.report.md` 随同一提交进仓，实际输出同含此一行。）全部在卡面 §4 白名单内，**零越界**。

### ★ 卡面内部矛盾与处置（需 PM 过目）

卡面 §3.2.2 写「做法 = 改 `docs/qa/T-B2-2026-09-18/tools/` 的四件套里索引分配表」，但 §4 白名单**不含**该目录，且 §5.1 要求 diff 只含白名单文件。两读必取其一：
- 就地改 T-B2 证据目录 → 违反白名单/验收 1，且**破坏 T-B2 报告的 md5 复现链**（重跑 T-B2 原件将产出迁移后资产，不再等于 T-B2 验收物）；
- 四件套拷入本卡证据目录改 → 两判据均成立。

**采后案**：`docs/qa/T-B2b-2026-09-18/tools/` 为 T-B2 四件套的副本，仅索引分配表（`CHAR_TO_INDEX`/`SWATCH_FOR_INDEX`）与口径注释变化；`mossback_layers.txt` 的**逐层字符数据与 T-B2 原件逐行相同**（核验命令与结果见 §4.1）。

## 2. 存量断言 old→new 记账（验收标准 2；卡面 §2 显式许可范围）

| 位置（`73a37ef` 行号） | 类型 | old → new | 值翻转 |
|---|---|---|---|
| `tests/test_mob_model.cpp:782` TEST_CASE 标题「…first eight indices」 | 用例名 | **未动**（验收 2 规定用例名可增不可消失；标题在 v2 下只描述了主标签一半，登记建议表） | — |
| `:783..790`（1..8 → 各关节） | 断言 | 未动（v1=v2 数值相同） | 无 |
| `:791-792` 注释「Unlabelled colours … 9..255 are colour slots」 | 注释 | 改为 v2 口径（9 = 躯干第二色、语义变化靠下方 10/16/17 钉住） | — |
| `:793/794/795`（0/9/255 → Body） | 断言 | 未动（卡面 §2 已预判：数值不变、9 的语义变化由新用例显式断言） | 无 |
| `:811` 夹具体素注释「unlabelled -> body, colour 9」 | 注释 | 「v2 second body colour -> body, colour 9」（卡面点名「该注释须改」） | — |
| 全文件其余 | — | 未动 | 无 |

**断言值翻转：0 条**（与 PM 实跑预判一致）。`git diff` 中 `-TEST_CASE` 行数 = 0。新增 2 例：

```
Start 315: T-B2b contract v2: 1..16 are joint labels, 17..255 the body group
2/3 Test #315: T-B2b contract v2: 1..16 are joint labels, 17..255 the body group ............. Passed
Start 316: T-B2b mesh: a joint's two colours land in one part range, not two draws
3/3 Test #316: T-B2b mesh: a joint's two colours land in one part range, not two draws ........ Passed
```

边界组（卡面 §3.1.2 五点全钉 + 全区间穷举）：`8→Spare`、`9→Body`（消息写明这是 v2 的"躯干第二色"新语义、勿与 v1"9→躯干组"混淆）、`10→Head`、`16→Spare`、`17→Body`、`255→Body`；另加穷举段 `1..8 主 = 9..16 第二色同关节`、`17..255 全归躯干`。网格级例断言 `parts.size()==1`、part 属 `kMobJointHead`、区间内**同时采到索引 2 与 10 两种颜色**（uv→调色板格反解）。

**新总数：459**（v1 的 457 全数在跑 + 2）。全量输出：

```
$ ctest --test-dir build
100% tests passed out of 459
Total Test time (real) =   8.14 sec
```

（`/Users/happy/Desktop/opencraft_worktree/opencraft-tb2b/docs/qa/T-B2b-2026-09-18/ctest_mob_joint_subset.txt` 为 mob 关节子集复跑。）

## 3. 产品日志回归三件套（验收标准 3）

跑法：`cd build`（worktree 内）启动 `./opencraft`，轮询到 `mobs:` 行后再等 1 s 取全启动段，SIGTERM。无其他实例在跑（`pgrep -fl opencraft` 空）。

run1（资产就位，`docs/qa/T-B2b-2026-09-18/run1_startup_with_assets.log`）：

```
[2026-09-18 15:12:38.754] [info] mobs: 1/3 mob models loaded from ../assets/mobs
[2026-09-18 15:12:38.754] [info] mob model mossback: 672 voxels, 1504 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png
```

`WARN count: 0`（`grep -c '\[warning\]\|WARN'` = 0）。两行与 T-B2 交付时**逐字节一致**——mossback 迁移前 9..16 归躯干、迁移后 17..24 归躯干，部件数都是 7，符合卡面 §3.1.3 预期。

run2（`mv assets/mobs` 出去再启动，`run2_startup_no_mobs_dir.log`）：

```
[2026-09-18 15:13:06.120] [info] mobs: 0/3 mob models loaded from ../assets/mobs
```

`WARN count run2: 0`；mossback 目录随后还原（`git status` 仅剩预期的 ` M assets/mobs/mossback.vox`）。

## 4. mossback 迁移复算（验收标准 4；卡面 §3.2.3）

### 4.1 逐字节机器核验（不靠肉眼，卡面 §7.2）

对迁移前备份（`pre_migration/mossback_v1.vox`，md5 与 T-B2 报告记录的 `f8072830088c024aa4ad724465a1b99b` 相符 ⇒ 起点就是已验收资产）与新资产做独立解析比对：

```
geometry identical, color byte rule {9..16->+8 else same}: violations = 0
RGBA palette migration check: violations = 0
```

即：672 条体素的坐标序列逐字节相同；色字节**恰为** `9..16 → +8`、其余不变；调色板 1..256 条逐一等于「新 9..16 = U0 / 17..24 = 旧 9..16 / 其余不变」。稿件核验：`grep -v '^#'` 后新旧 layers 逐行 diff 为空（**LAYERS-DATA-IDENTICAL**）——艺术一格未动。

### 4.2 三项复算

1. **体素总数仍 672**：`vox_build` 复跑输出与 `vox_inspect` 独立解析均报 672（`vox_build_v2_run.txt`、`inspect_v2.txt`）。
2. **每关节包围盒与 `T-B2.report.md` §3 表逐项一致**：见下表（旧 9..16 行与 `inspect_selfcheck.txt` 对应行 x/y/z/体素数**逐字相同**，仅索引号 +8）。
3. **日志行逐字不变**：`672 voxels, 1504 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png`（§3 run1；inspect 独立复算暴露面 752、三角形 1504 相同）。

### 4.3 `vox_inspect.py` 每索引包围盒表（验收 4 要求贴入）

完整表（`inspect_v2.txt` 原样，全部 15 行与 T-B2 原 `inspect_selfcheck.txt` 逐项一致：躯干组行仅索引号 +8，包围盒/体素数/rgb 逐字相同）：

```
  idx  1   219 voxels  x 0..8  y 2..9  z 6..9  rgb (110, 86, 60)  -> joint 0 body 躯干
  idx  2    77 voxels  x 2..6  y 10..13  z 7..11  rgb (168, 132, 92)  -> joint 1 head 头
  idx  3    30 voxels  x 1..2  y 6..8  z 0..4  rgb (66, 52, 38)  -> joint 2 arm_l 左前肢
  idx  4    30 voxels  x 6..7  y 6..8  z 0..4  rgb (66, 52, 38)  -> joint 3 arm_r 右前肢
  idx  5    30 voxels  x 1..2  y 2..4  z 0..4  rgb (66, 52, 38)  -> joint 4 leg_l 左后肢
  idx  6    30 voxels  x 6..7  y 2..4  z 0..4  rgb (66, 52, 38)  -> joint 5 leg_r 右后肢
  idx  7     6 voxels  x 4..4  y 0..1  z 8..11  rgb (70, 112, 60)  -> joint 6 tail 尾
  idx 17    35 voxels  x 2..6  y 2..8  z 10..10  rgb (138, 110, 76)  -> joint 0 body 躯干
  idx 18     3 voxels  x 3..5  y 2..7  z 5..5  rgb (66, 52, 38)  -> joint 0 body 躯干
  idx 19    58 voxels  x 0..8  y 2..8  z 5..8  rgb (100, 76, 60)  -> joint 0 body 躯干
  idx 20   100 voxels  x 0..8  y 2..8  z 11..13  rgb (96, 146, 78)  -> joint 0 body 躯干
  idx 21    27 voxels  x 0..8  y 2..8  z 8..11  rgb (70, 112, 60)  -> joint 0 body 躯干
  idx 22     4 voxels  x 0..8  y 2..8  z 11..11  rgb (46, 74, 40)  -> joint 0 body 躯干
  idx 23    12 voxels  x 2..6  y 2..8  z 12..13  rgb (90, 140, 74)  -> joint 0 body 躯干
  idx 24    11 voxels  x 2..6  y 5..7  z 12..13  rgb (124, 168, 98)  -> joint 0 body 躯干
```

关节聚合与两条防趴地判据同步复算：joint 0..6 = 469/77/30/30/30/30/6（与 T-B2 相同）；`head vs torso core (idx 1/18/19): centroid 8.70 vs 7.08, max z 11 vs 9 [OK]`；`lowest layer z=0 colorIndices [3, 4, 5, 6] -> legs only`。Pillow 第三方复核（`palette_png_check_v2.txt`）：格 1..8、9..16(=U0)、17..24 逐格 match，max saturation 0.471 ≤ 0.50，VERDICT 通过。

### 4.4 资产 md5（迁移前→后）

```
mossback.vox : f8072830088c024aa4ad724465a1b99b → 9215d2bef3333821c07c83b20aa1c360 （3784 B 不变）
mossback.png : ba69f5a0ade80bb44fc0291fe3c9f5b9 → 74dcc2a82723523d9f1d0b40b92ba7db （134 B → 136 B，zlib 对变更格数的正常长度波动）
```

复跑 `vox_build.py` 产出与入库资产 md5 相同（幂等，`post_migration_md5.txt` 对照 §4.4）。

## 5. 眼睛可行性演示（验收标准 5；功能判据，非艺术验收）

夹具生成器 `docs/qa/T-B2b-2026-09-18/tools/make_eye_demo_fixture.py`（一次性，不入 `assets/`；产物与日志留证据目录）。三变体形体逐格相同，只换头部涂色；经 `OPENCRAFT_ASSETS_DIR` 指临时资产根跑**产品码**（blocks/items 以符号链接指回，避免 atlas 噪声）：

| 变体 | 头部涂色 | 产品日志 `mob model mossback:` | WARN |
|---|---|---|---|
| A（基线） | 索引 2 单色 | `6 voxels, 56 triangles, 4 joints, 1.4 blocks tall` | 0 |
| B（B3 形态） | 索引 2 + 索引 10 双色 | **逐字相同** | 0 |
| C（判据放大器） | 头全部涂 10、2 缺席 | **逐字相同（joints 仍 = 4）** | 0 |

- **B 与 A 逐字相同** ⇒ 第二色并入同一 part 区段、没有多出一帧 draw（`joints` = `mesh.parts.size()`）。
- **C 提供区分力**（B 单独看不出的原因见卡面 §7.1 同款巧合：v1 下 B 也是 4 parts，但第二色格会归躯干）：C 在 v2 下 joints=4；若是 v1 行为（10→躯干），head 将无体素、日志会是 **3 joints**。产品路径实跑 4 ⇒ v2 在真实管线成立。
- 单测侧另有等强度证明：网格级用例直接断言 `parts.size()==1` 且同一区段内 uv 反解出 2 和 10 两色。
- 日志留档：`docs/qa/T-B2b-2026-09-18/eye_demo/`。

## 6. 已知问题与建议表（不自行落盘，交 PM）

| # | 事项 | 建议 |
|---|---|---|
| 1 | 存量用例名「T-B1 mesh: joint labels are the palette's first eight indices」在 v2 下只覆盖主标签一半；因「用例名不可消失」未改名 | PM 裁定：后续卡（若允许用例名换代）或规格里标注该标题为 v1 历史名 |
| 2 | `assets/CREDITS.md` 两行**过程列**仍指 `docs/qa/T-B2-2026-09-18/tools/`（T-B2 手稿原件），而入库资产的直接生成器现为 `docs/qa/T-B2b-2026-09-18/tools/` | 卡面白名单限定「仅两行的摘要句口径」，未越权改过程列；建议 PM 收口时决定是否把过程列改指 T-B2b 目录（设计链仍在 T-B2） |
| 3 | §3.2.2「就地改 T-B2 tools」与 §4 白名单/§5.1 的矛盾及取位（§1 末） | PM 追认「副本迁移」口径，或修订变更记录 |
| 4 | 暂停帧装置账（卡面 §7.3）本卡未触碰：迁移后 mossback 外观逐字节同形，T-B2 的暂停帧证据继续有效 | 无需重拍；B3 卡直接复用 `run_scene.sh` 即可 |

## 7. 复现命令

```bash
# 构建（禁管道；不指 FETCHCONTENT_BASE_DIR）
cmake -S /Users/happy/Desktop/opencraft_worktree/opencraft-tb2b -B /Users/happy/Desktop/opencraft_worktree/opencraft-tb2b/build
cmake --build /Users/happy/Desktop/opencraft_worktree/opencraft-tb2b/build -j8
ctest --test-dir /Users/happy/Desktop/opencraft_worktree/opencraft-tb2b/build        # 459/459
# 资产复算
python3 docs/qa/T-B2b-2026-09-18/tools/vox_build.py <worktree根>
python3 docs/qa/T-B2b-2026-09-18/tools/vox_inspect.py <worktree根> <出图目录>
python3 docs/qa/T-B2b-2026-09-18/tools/check_palette_png.py <worktree根>
```
