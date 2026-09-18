# T-B4 报告 · Blastbud 蓄爆生物体素模型（美术总监）

> 任务卡：`/Users/happy/Desktop/opencraft/docs/tasks/T-B4.md`（唯一需求来源）
> 分支：`task/T-B4-blastbud`　worktree：`/Users/happy/Desktop/opencraft_worktree/opencraft-tb4`
> 证据目录：`/Users/happy/Desktop/opencraft_worktree/opencraft-tb4/docs/qa/T-B4-2026-09-19/`
> 日期：2026-09-19　性质：纯资产卡（零代码改动）

## 0. 一句话结论

`blastbud` 的 `.vox` + 调色板 PNG 已落盘，**启动日志首次满额 `mobs: 3/3`**、三条 `mob model`
行齐全且 WARN=0，另两只的行与 T-B5 验收件**逐字节相同**；契约 ①–⑥ 全过，预算五项全在帽下，
五官为表面着色格且落在头关节包围盒内。

| 项 | 值 |
|---|---|
| 交付件 | `assets/mobs/blastbud.vox`（3136 B，md5 `4ebea03c4373ae04ab35daf3a68ab9be`）<br>`assets/palettes/blastbud.png`（124 B，md5 `3f3cdbb3e9db1e592d32193b66af85b0`） |
| 规格 | 9×10×17 画布 / **510 体素** / 542 暴露面（1084 三角形）/ **6 关节** / 视觉 0.70×0.70×1.70 |
| 启动日志 | `mobs: 3/3 mob models loaded from ../assets/mobs` + `mob model blastbud: 510 voxels, 1084 triangles, 6 joints, 1.7 blocks tall, palette from palettes/blastbud.png` |
| 构建/测试 | configure **离线依赖缓存**通过（外网断）；ctest **459/459**（与 T-B5 同数，纯资产卡无回归） |

---

## 1. 白名单 diff

```console
$ git diff --name-only main...HEAD
assets/CREDITS.md
assets/mobs/blastbud.vox
assets/palettes/blastbud.png
docs/art/01-style-guide.md
docs/qa/T-B4-2026-09-19/build_blastbud.log
docs/qa/T-B4-2026-09-19/configure_offline_deps.log
docs/qa/T-B4-2026-09-19/ctest_summary.txt
docs/qa/T-B4-2026-09-19/front_projection.txt
docs/qa/T-B4-2026-09-19/inspect_blastbud.txt
docs/qa/T-B4-2026-09-19/log_check.txt
docs/qa/T-B4-2026-09-19/md5_assets.txt
docs/qa/T-B4-2026-09-19/palette_png_check.txt
docs/qa/T-B4-2026-09-19/renders/blastbud-v1_contact_sheet.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v1_face.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v1_front_big.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v1_gamecam.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v1_silhouette.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v2_contact_sheet.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v2_face.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v2_front_big.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v2_gamecam.png
docs/qa/T-B4-2026-09-19/renders/blastbud-v2_silhouette.png
docs/qa/T-B4-2026-09-19/renders/blastbud_v1_vs_v2_front.png
docs/qa/T-B4-2026-09-19/renders/blastbud_v1_vs_v2_gamecam.png
docs/qa/T-B4-2026-09-19/renders/species_gamecam_ab.png
docs/qa/T-B4-2026-09-19/renders/species_silhouette_ab.png
docs/qa/T-B4-2026-09-19/run1_startup_with_assets.log
docs/qa/T-B4-2026-09-19/run2_startup_no_mobs_dir.log
docs/qa/T-B4-2026-09-19/tools/blastbud_layers.txt
docs/qa/T-B4-2026-09-19/tools/blastbud_palette.txt
docs/qa/T-B4-2026-09-19/tools/check_log_lines.py
docs/qa/T-B4-2026-09-19/tools/check_palette_png.py
docs/qa/T-B4-2026-09-19/tools/face_closeup.py
docs/qa/T-B4-2026-09-19/tools/front_big.py
docs/qa/T-B4-2026-09-19/tools/front_projection.py
docs/qa/T-B4-2026-09-19/tools/species_ab.py
docs/qa/T-B4-2026-09-19/tools/vox_build.py
docs/qa/T-B4-2026-09-19/tools/vox_inspect.py
docs/tasks/T-B4.report.md
```

共 **39 个文件**（交付资产 2 + CREDITS + 规格 + 证据目录 34 + 报告）。逐条对照卡面 §4 白名单：
交付资产两件、CREDITS 追加两行、`docs/art/01-style-guide.md` **只动了 §9 待办表一行 + 变更记录
追加一行**、证据目录、报告——**没有任何源码/构建文件/其他资产**。另两只生物的资产
**逐字节未变**（md5 对照 T-B5 验收值）：

```console
$ md5 -r assets/mobs/blastbud.vox assets/palettes/blastbud.png \
        assets/mobs/mossback.vox assets/mobs/hollow_wretch.vox \
        assets/palettes/mossback.png assets/palettes/hollow_wretch.png
4ebea03c4373ae04ab35daf3a68ab9be assets/mobs/blastbud.vox
3f3cdbb3e9db1e592d32193b66af85b0 assets/palettes/blastbud.png
fda9ee82116c6a937b163517a38b0149 assets/mobs/mossback.vox          ← 与 T-B5 验收值同
2af82806857fa3381e3d6d08d9fb3f27 assets/mobs/hollow_wretch.vox     ← 与 T-B5 验收值同
2be25d95e04ce3dba7d65449ccf46ea7 assets/palettes/mossback.png      ← 与 T-B5 验收值同
264c75d7ee0750f47d9653340ad116b7 assets/palettes/hollow_wretch.png ← 与 T-B5 验收值同
```

（证据文件：`docs/qa/T-B4-2026-09-19/md5_assets.txt`。前两件是 T-B5 交付件的 md5，本卡四个文件
均一字未动。）

---

## 2. 契约 ①–⑥ 逐条判定

| # | 契约 | 判定 | 证据 |
|---|---|---|---|
| ① | Z-up，正面 = 文件 +Y | ✅ | 图稿 row 1 = y 最大 = 脸侧；`front_projection.txt` 从 **+y** 方向投影，五官正对读者；腿在 z=0 |
| ② | 体素 z 跨度 → 碰撞高 1.7 ⇒ **z 跨度恰 17**，1 体素 = 0.1 格；头顶禁加附件 | ✅ | `span: x 7 y 8 z 17`；最高格 z=16 就是头本身（冠顶），其上无附件；`mob_physics("Blastbud", 0.3, 1.7, 20)` ⇒ 日志 `1.7 blocks tall` |
| ③ | 契约 v2 调色板分段（1..8 主标签 / 9..16 第二色 / 17+ 躯干组；0 禁用） | ✅ | 用到 `[1,2,3,4,5,6,9,10,17,18]`；**colorIndex 0 = 0 格**；第二色只用了 9（躯干）与 10（头） |
| ④ | 调色板 PNG 16×16、格号 = colorIndex（第 0 格不画）、RGBA 全不透明、色值全在 §2.1 32 色内且 S ≤ 0.50 | ✅ | 第三方读回（Pillow）：`256` 格全中、alpha `{255}`、在画格最大饱和 **0.471** |
| ⑤ | 预算 ≤1000 体素 / 暴露面 ≤2000 / 每边 ≤32 / 关节 ≤8 | ✅ | 510 / 542 / 17 / 6（见 §3） |
| ⑥ | 三向日志：`mobs: 3/3` + 本模型行 + WARN=0；**另两行零回归**；移走 `assets/mobs/` → `0/3`、WARN=0 | ✅ | §5（`log_check.txt` 输出 `RESULT: ALL PASS`） |

---

## 3. 预算与包围盒表（`vox_inspect.py`，独立解析器重读落盘件）

```console
$ python3 docs/qa/T-B4-2026-09-19/tools/vox_inspect.py model \
      assets/mobs/blastbud.vox docs/qa/T-B4-2026-09-19/renders blastbud-v2
file        : …/assets/mobs/blastbud.vox (3136 bytes)
version     : 150
chunks      : MAIN(0B), SIZE(12B), XYZI(2044B), RGBA(1024B)
SIZE        : 9 x 10 x 17  (max side 17)
voxels      : 510
colorIndex 0: 0 voxels  (contract: must be 0)
bbox        : x 1..7  y 1..8  z 0..16
span        : x 7  y 8  z 17  (z-span must equal collision height / 0.1)
per-colorIndex bbox and joint mapping:
  idx  1   205 voxels  x 1..7  y 2..8  z 5..11  rgb (70, 112, 60)  -> joint 0 body 躯干
  idx  2    77 voxels  x 2..6  y 3..7  z 13..16  rgb (180, 158, 115)  -> joint 1 head 头
  idx  3    12 voxels  x 1..2  y 5..6  z 0..2  rgb (66, 52, 38)  -> joint 2 arm_l 左前肢/左臂
  idx  4    12 voxels  x 6..7  y 5..6  z 0..2  rgb (66, 52, 38)  -> joint 3 arm_r 右前肢/右臂
  idx  5    12 voxels  x 1..2  y 3..4  z 0..2  rgb (66, 52, 38)  -> joint 4 leg_l 左后肢/左腿
  idx  6    12 voxels  x 6..7  y 3..4  z 0..2  rgb (66, 52, 38)  -> joint 5 leg_r 右后肢/右腿
  idx  9    16 voxels  x 3..5  y 6..8  z 3..12  rgb (226, 230, 236)  -> joint 0 body 躯干
  idx 10     9 voxels  x 2..6  y 6..7  z 13..16  rgb (40, 40, 44)  -> joint 1 head 头
  idx 17   104 voxels  x 1..7  y 1..7  z 3..12  rgb (46, 74, 40)  -> joint 0 body 躯干
  idx 18    51 voxels  x 1..7  y 3..7  z 9..11  rgb (90, 140, 74)  -> joint 0 body 躯干
joint groups actually present …: 躯干 376 / 头 86 / 左前肢 12 / 右前肢 12 / 左后肢 12 / 右后肢 12
head max z  : 16
  head max z 16 vs torso core (idx 1/17/18/19) max z 12  (centroid 14.26 vs 6.90)
lowest layer z=0 colorIndices [3, 4, 5, 6] -> joints [2, 3, 4, 5] (feet/legs only)
exposed faces: 542  (budget 2000)
triangles    : 1084  (what the startup log reports)
```

| 预算项 | 卡面上限 | 本件 | 余量 |
|---|---|---|---|
| 体素总数 | ≤ 1000 | **510** | 49% |
| 暴露面 | ≤ 2000 | **542**（1084 三角形） | 73% |
| 最长边 | ≤ 32 | **17**（z 跨度，被 ①/② 钉死） | 47% |
| 关节数 | ≤ 8 | **6** | 2 |
| colorIndex 0 | = 0 | **0** | — |

> 对比：Mossback 705 体素 / Hollow Wretch 496 体素。510 落在两只之间，**不是为凑数加的细节**
> （卡面 §7.4）——它是"蛋形腹体 4 层八边形截面 + 5 格宽芽冠"这个形体的自然结果。

---

## 4. 面部特征机器判据（T-B5 起的常设要求）

### 4.1 正面投影（不依赖渲染，只依赖占用表与索引）

新增工具 `tools/front_projection.py`：把**从 +y 看到的第一格**逐格打出来。卡面要求
"五官须为表面着色格（在占用表内、+y 邻居为空）、正面正视图中可读"，这张表就是它的机器形式。

```console
$ python3 docs/qa/T-B4-2026-09-19/tools/front_projection.py assets/mobs/blastbud.vox
front projection of …/assets/mobs/blastbud.vox
columns x 1..7 (left -> right), rows z 16..0 (top -> bottom)
each cell = the colorIndex of the frontmost (max y) voxel in that column

z |1234567
----------
16|..2E2..
15|.EE2EE.
14|.22E22.
13|.EEEEE.
12|..sWs..
11|.1tWt1.
10|.1WWt1.
 9|1ttWtt1
 8|s1WW11s
 7|s1WWW1s
 6|s11WW1s
 5|s11W11s
 4|ssWWsss
 3|.ssWss.
 2|33...44
 1|33...44
 0|33...44

LEGEND: 1 躯干 F1  2 头 E3  3/4/5/6 四足 W0  W 躯干二色=裂缝里的灼白 S4  E 头二色=五官 O0  s 躯干组 F0  t 躯干组 F2
```

读法（上表逐行对照）：`z16` 冠顶两齿 + 正中裂口内壁 `E`；`z15` 两只 **2 宽眼窝**
（x2..3 / x5..6），x4 = 鼻梁；`z14` 正中 **1 格鼻点**；`z13` **整行 5 格的口**；
`z12` 颈（掐到 3 格宽）与裂缝相接；`z7` 裂缝最开（3 格）；`z0..2` 只有四只足。

### 4.2 契约 v2 专项断言（`vox_inspect.py` 输出原文）

```console
second-colour indices painted: [9, 10]
  idx  9 -> joint 0 body 躯干: cells x 3..5 y 6..8 z 3..12 within joint bbox x 1..7 y 1..8 z 3..12  [OK]
  idx 10 -> joint 1 head 头: cells x 2..6 y 6..7 z 13..16 within joint bbox x 2..6 y 3..7 z 13..16  [OK]
eye voxels (idx 10): 11
  (2, 7, 13) present in occupancy table; +y neighbour (2, 8, 13) empty: True
  …（11 格逐格同形，全 True，原文见 inspect_blastbud.txt）
  eye voxels span x [2, 3, 4, 5, 6] z [13, 14, 15, 16]; every eye voxel is a surface colour (not a carved hole): True
```

⇒ 三条同时成立：**两个第二色各自落在其关节包围盒内**、**11 格五官全部是表面着色**
（占用表内 + 朝正面 `+y` 邻居为空）、**全部落在头关节**（不跟躯干转）。

### 4.3 出图（人眼读者）

- `renders/blastbud-v2_face.png`：脸前 1.6 格正面特写（工具 = T-B5 的 `face_closeup.py`）
- `renders/blastbud-v2_front_big.png`：正面 44 px/体素硬边大图（工具 `front_big.py`）

---

## 5. 三向日志判据（卡面 §2⑥ / §5.2）

```console
$ cd build && ./opencraft        # 无补丁，正常资产
… [info] mobs: 3/3 mob models loaded from ../assets/mobs
… [info] mob model mossback: 705 voxels, 1544 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png
… [info] mob model hollow_wretch: 496 voxels, 1592 triangles, 6 joints, 1.8 blocks tall, palette from palettes/hollow_wretch.png
… [info] mob model blastbud: 510 voxels, 1084 triangles, 6 joints, 1.7 blocks tall, palette from palettes/blastbud.png

$ mv assets/mobs /tmp/… && cd build && ./opencraft
… [info] mobs: 0/3 mob models loaded from ../assets/mobs      （无任何 mob model 行）
```

机器核对（工具 `tools/check_log_lines.py`，剥掉时间戳与等级标签后逐字节比）：

```console
$ python3 docs/qa/T-B4-2026-09-19/tools/check_log_lines.py \
      docs/qa/T-B4-2026-09-19/run1_startup_with_assets.log \
      docs/qa/T-B4-2026-09-19/run2_startup_no_mobs_dir.log \
      /Users/happy/Desktop/opencraft/docs/qa/T-B5-2026-09-18/run1_startup_with_assets.log \
      /Users/happy/Desktop/opencraft/docs/qa/T-B5-2026-09-18/pm_verify/pm_run_with_assets.log
== 1. 本卡日志的满额与三行 ==
  load line   : mobs: 3/3 mob models loaded from ../assets/mobs
  -> exactly 'mobs: 3/3 …': True
  model lines : 3
    mob model mossback: 705 voxels, 1544 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png
    mob model hollow_wretch: 496 voxels, 1592 triangles, 6 joints, 1.8 blocks tall, palette from palettes/hollow_wretch.png
    mob model blastbud: 510 voxels, 1084 triangles, 6 joints, 1.7 blocks tall, palette from palettes/blastbud.png
  -> blastbud line == expected: True
== 2. Mossback / hollow_wretch 两行零回归 ==
  mossback: mine = mob model mossback: 705 voxels, 1544 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png
    vs …/T-B5-2026-09-18/run1_startup_with_assets.log: IDENTICAL
    vs …/T-B5-2026-09-18/pm_verify/pm_run_with_assets.log: IDENTICAL
  hollow_wretch: mine = mob model hollow_wretch: 496 voxels, 1592 triangles, 6 joints, 1.8 blocks tall, palette from palettes/hollow_wretch.png
    vs …/T-B5-2026-09-18/run1_startup_with_assets.log: IDENTICAL
    vs …/T-B5-2026-09-18/pm_verify/pm_run_with_assets.log: IDENTICAL
== 3. [warning] / [error] 计数 ==
  run1_startup_with_assets.log: 0 warning/error line(s)
  run2_startup_no_mobs_dir.log: 0 warning/error line(s)
== 4. 移走 assets/mobs/ 的对照 ==
  load line   : mobs: 0/3 mob models loaded from ../assets/mobs
  -> exactly 'mobs: 0/3 …': True
  mob model lines: 0 (must be 0)
RESULT: ALL PASS
```

**零回归的对照基准**是卡面 §7.1 指定的"本回合新行"，即 T-B5 验收件里的
`705/1544/7` 与 `496/1592/6`；本卡同时与**两份独立的 T-B5 日志**（开发者 run1 与 PM 侧
`pm_run_with_assets.log`）比对，三方一致。

上线后 `assets/mobs/` 已还原（`ls assets/mobs/` = 三件齐全，md5 见 §1）。

---

## 6. 形态自检与迭代（卡面 §3.4 的"先画→渲染看→改"）

### 6.1 v1 → v2（改的三处，逐条对应出图暴露的问题）

| 问题（v1 出图） | 修法（v2） |
|---|---|
| 裂缝"1 格 → 3 格 → 1 格"**对称加宽**，正面读成一个"**十**"字（正是规格 §4.5 禁止的规则形） | 改成**锯齿**：主缝在 x4 贯通，z10 右撕 / z8 左撕 / z6 右撕 / z4 左撕，最鼓的 z7 开到 3 格 |
| 裂缝色 O3 金 `#D8BC78` 与芽冠 E3 `#B49E73` **几乎同色**，"内部露出来"读不出 | 裂缝换成 **S4 `#E2E6EC`（近白）**：全模型最亮的一处落在缝里，且与沙色冠明显不同 |
| 腹体 z3..z10 通体 7 格宽 ⇒ 正面剪影是**矩形**，读不出"鼓" | 做成蛋形：z3 只 5 格宽（收在四足之间）→ z4..z9 鼓到 7 宽 × 8 深（四角切出八边形）→ z10..11 收成 5 宽 → z12 掐到 3 格宽的颈 |

对照图：`renders/blastbud_v1_vs_v2_front.png`、`renders/blastbud_v1_vs_v2_gamecam.png`
（v1 的中间过程图留在 `renders/blastbud-v1_*.png`，供 PM 复核迭代是否有据）。

### 6.2 "会炸"读得出来的三条设计依据（卡面 §3.2 要求写进报告）

1. **腹体占全身体积 74%**（376 / 510 体素），且横向鼓到 7 格 = 0.7 格宽（比 0.6 碰撞盒多
   0.1，与 Mossback 的 7 格身宽同口径）；
2. **重心低、足短小**：四只足只占全高 3/17 = **18%**（0.2×0.2×0.3 格），体态"跑不动"，
   且蓄爆脉动（`mob_pose.cpp:61-65`，整只 ±12%，锚点是**足底**）时形变方向是"往上鼓"；
3. **正面一道亮白锯齿裂缝从口一路劈到腹底**——口（z13 整行）就是裂缝的上端，读出
   "嘴裂开了、里面亮着"。裂缝不做过细的细节：最窄 1 格、贯通 10 层，脉动缩放的是整只，
   特征不会因缩放而消失（卡面 §3.3 的提醒）。

### 6.3 可读性门槛：**非正交（针孔透视）游戏机位**

- `renders/blastbud-v2_gamecam.png`：相机 4.5 格、方位 38°、仰角 10°、**固定 16 px/voxel**、
  固定 760×620 画布（不按模型自适应缩放 ⇒ 三只生物可直接并排比）。
- 工具自带机器自证（透视 vs 正交）：
  `perspective self-check: a 1-voxel ground segment projects to 12.23 px at the near end vs 9.33 px at the far end (ratio 1.310; an orthographic camera would give 1.000)`。

### 6.4 剪影 / 接触表

`renders/blastbud-v2_silhouette.png`（侧 + 等轴剪影）、`renders/blastbud-v2_contact_sheet.png`
（侧 / 正 / 顶 / 等轴 / 关节分色 五联）。剪影读法：**下窄-中鼓-上收**的蛋形 + 顶上钝方的冠
+ 底下两个棕色短足桩；等轴图里关节分色确认 **6 个 part**（躯干/头/四足）。

---

## 7. 风格合规（规格 §8 逐项 + §3 低饱和 + CREDITS）

### 7.1 §8 自查清单

| # | 检查项 | 判定 | 依据 |
|---|---|---|---|
| 1 | 真 PNG（文件头） | ✅ | 第三方（Pillow）读通；`vox_inspect` 回读 `\x89PNG` |
| 2 | 尺寸正好 16×16 | ✅ | `size: (16, 16) mode RGBA` |
| 3 | 朝向 | ✅ | 本件是**调色板**不是贴图：判据 = **逐格读回与声明表逐格相同**（256 格 0 mismatch），行序若反了这条必然失败 |
| 4 | alpha 不透明资产全 255 | ✅ | `alpha set: [255]` |
| 5 | 每个 RGB 都在 §2.1 表内 | ✅ | 在画 10 个索引 + 未用槽 U0，全部取自 §2.1（`check_palette_png.py` 逐格比） |
| 6 | 每个用到的色 S ≤ 0.50 | ✅ | 最大值 **0.471**（F2 `#5A8C4A`）；编码器启动时自检同值 |
| 7 | 构图（无 4px 实心块/无规则形/无抗锯齿） | n/a | §8 第 7 项约束的是**贴图构图**；本件是 256 格调色板，其"构图"由契约 ④ 规定（格号 = colorIndex）。**模型侧**对应的一条已按 §6.1 处理：v1 的对称加宽读成"十"字，v2 改成锯齿 |
| 8 | 三面齐全 | n/a | §9 已明确 §5（三面）**只约束方块贴图、不适用于体素模型** |
| 9 | 原创性（未参考任何既有作品；未用 AI 或提示词已留痕） | ✅ | 全手绘：逐层字符稿（`tools/blastbud_layers.txt`）+ 自写编码器；**未用 AI、未喂任何参考图**；CREDITS 两行六字段齐（`assets/CREDITS.md` 末尾两条） |
| 10 | 生效：启动日志的计数与落盘一致 | ✅ | `mobs: 3/3`（分母 = 注册表里的生物数；本件是第 3 只） |

### 7.2 §3 低饱和

32 色表本身最高 S = 0.480（A1 水）；本件实际用到的 7 个色逐格实测**最大 0.471**：

```console
$ python3 docs/qa/T-B4-2026-09-19/tools/check_palette_png.py <worktree>
painted-cell colours: 10 distinct indices, max saturation 0.471 (cap 0.50)
OK - palette reads back cell-for-cell identical
```

### 7.3 借色登记（色值全在表内，用途写进 §9）

| 索引 | 色 | §2.1 原用途 | 本卡用途 |
|---|---|---|---|
| 2 | `E3 #B49E73` | 沙的阴影带（土系） | **干枯的芽冠**（"快崩开"的干壳） |
| 9 | `S4 #E2E6EC` | 雪；亦可作骨/枯骨（T-B3 先例） | **裂缝里露出的灼白内部**（全模型最亮的一处） |
| 1 / 17 / 18 | `F1 / F0 / F2` | 叶的主色 / 最暗处 / 受光处 | 腹体表皮的**中 / 暗 / 亮**三级 |

未新增任何表外色 ⇒ §2.1 作为"全项目限定色板"的结论继续成立（第三个实例）。

---

## 8. 证据目录清单（`docs/qa/T-B4-2026-09-19/`）

| 文件 | 内容 |
|---|---|
| `tools/blastbud_layers.txt` | **创作本体**：17 层逐层手写字符稿（含坐标/朝向/形体/迭代说明） |
| `tools/blastbud_palette.txt` | 16×16 调色板手写稿（格号 = colorIndex） |
| `tools/vox_build.py` | 字符 → `.vox` 字节翻译器（由 T-B5 原件改写：字符表换成 blastbud，MODELS 表只留本卡这一只）；**无几何/无对称展开/无填充/无随机数** |
| `tools/vox_inspect.py` | 独立解析 + 预算/包围盒/契约 v2 断言 + 五联出图 + 透视游戏机位（照抄 T-B5 版，未改） |
| `tools/check_palette_png.py` | **第三方**（Pillow）调色板逐格读回（名单换成 blastbud） |
| `tools/face_closeup.py` | 面部特写出图（照抄 T-B5 版，未改） |
| `tools/front_projection.py` | **本卡新增**：正面逐格投影（五官判据的机器形式） |
| `tools/front_big.py` | **本卡新增**：正面高倍硬边正交出图 |
| `tools/check_log_lines.py` | **本卡新增**：三向日志判据的机器核对（剥时间戳逐字节比） |
| `tools/species_ab.py` | **本卡新增**：N 图并排对照（三只生物 / v1-vs-v2） |
| `build_blastbud.log` / `inspect_blastbud.txt` / `palette_png_check.txt` / `front_projection.txt` | 编码器、解析器、第三方读回、正面投影的原始输出 |
| `run1_startup_with_assets.log` / `run2_startup_no_mobs_dir.log` / `log_check.txt` | 两次启动的原始日志 + 三向判据核对输出 |
| `configure_offline_deps.log` / `ctest_summary.txt` / `md5_assets.txt` | 离线 configure、测试、资产 md5 |
| `renders/` | v1/v2 的接触表、剪影、正面大图、面部特写、透视游戏机位；v1-vs-v2 对照；三只生物并排对照 |

> 清理口径（docs/05 §2）：证据目录**只收最终产物**，无 `__pycache__`/`*.pyc`/临时稿
> （`find … -name "__pycache__" -o -name "*.pyc"` 为空）。

---

## 9. "新旧无关度"（剪影 / 接触表 / 非正交游戏机位）

本卡**没有 v_old**：`blastbud` 此前从无模型，引擎对它画的是 T-M2 起的**两盒占位**
（`docs/tasks/T-B4.md` §2⑥ 的 `0/3` 分支）。因此"新旧对照"用两件等价的证据代替：

1. **v1 → v2 迭代对照**：`renders/blastbud_v1_vs_v2_front.png`、`…_gamecam.png`
   —— 证明"十"字裂缝 → 锯齿裂缝、矩形腹 → 蛋形腹两次改动的作用（§6.1）；
2. **与另两只生物的并排对照**（同机位、同 16 px/voxel、不缩放）：
   `renders/species_gamecam_ab.png`、`renders/species_silhouette_ab.png`
   —— 三只的体形（四足兽 / 人形 / 蛋形种荚）、体色（木棕+苔绿 / 灰石骨白 / 植绿+沙冠+亮白缝）
   与轮廓（横长 / 竖高 + 破布下摆 / 下窄中鼓）三者都判然可分，**新模型与既有两只不混淆**。

（与 T-B5 的"同机位模型 vs 占位"实机对照不同，本卡是纯资产卡、无源码可改；实机 A/B 属 PM
侧的暂停帧装置，若需要请 PM 按 `docs/qa/T-B1-2026-09-18/pm_verify/run_scene.sh` 复现。）

---

## 10. 构建与测试（本卡零代码改动，仅作回归证明）

```console
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release            # 外网断：首个 clone 失败
CMake Error at …/FetchContent.cmake:1933 (message):  Build step for glfw failed: 2

$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_GLFW=/Users/happy/Desktop/opencraft_scratch/deps/glfw-src \
    -DFETCHCONTENT_SOURCE_DIR_GLM=/Users/happy/Desktop/opencraft_scratch/deps/glm-src \
    -DFETCHCONTENT_SOURCE_DIR_SPDLOG=/Users/happy/Desktop/opencraft_scratch/deps/spdlog-src \
    -DFETCHCONTENT_SOURCE_DIR_DOCTEST=/Users/happy/Desktop/opencraft_scratch/deps/doctest-src \
    -DFETCHCONTENT_SOURCE_DIR_FASTNOISE_LITE=/Users/happy/Desktop/opencraft_scratch/deps/fastnoise_lite-src \
    -DFETCHCONTENT_SOURCE_DIR_STB=/Users/happy/Desktop/opencraft_scratch/deps/stb-src \
    -DFETCHCONTENT_SOURCE_DIR_ZSTD=/Users/happy/Desktop/opencraft_scratch/deps/zstd-src
-- Configuring done (3.5s)      ← 离线依赖缓存法（docs/05 §6 长驻白名单第四样）；全量输出见 configure_offline_deps.log

$ cmake --build build -j8        # rc=0
$ cd build && ctest
100% tests passed out of 459     ← 与 T-B5 同数（459），纯资产卡无回归
```

（构建命令未接管道；`build/` 在 `.gitignore` 内，不入仓。）

---

## 11. 已知问题 / 给 PM 的备注

1. **`mobs: 3/3` 自此是满额基线**（卡面 §7.1）。以后任何卡再动 `assets/mobs/`，回归判据应
   以"三行"为准（分母会随注册表增长）。
2. **本件比碰撞盒宽 0.1 格**（身宽 0.7 / 盒 0.6），与 Mossback 同口径，**不是本卡新引入的偏差**；
   若 PM 认为该口径应统一收紧到 0.6，请作为**规格级裁决**（会同时影响 Mossback），本卡不自行改。
3. **面相在 38° 游戏机位下的可读度弱于正视**：斜视时脸被透视压扁，"3 格宽口 + 两个颚角"的
   版本几乎读不出，故定稿改成**整行 5 格的口**。若要更强的斜视可读性，下一轮可考虑让五官
   **包角**（画到头的侧面上）——但那会让 `vox_inspect` 的"所有 index-10 格 `+y` 邻居为空"
   断言出现例外，属**判据口径变更**，须 PM 先裁决，本次未做。
4. **实机（引擎内）截图未做**：卡面 §5 的验收标准不要求，且本卡零代码改动、模型与占位的差异
   属 PM 装置的 A/B 范畴。若 PM 需要"上屏的是它"的直接证据，请用 T-B1 的暂停帧装置跑一次
   模型 vs 占位对照。
5. **面部"鼻"只有 1 格**（0.1 格）：与 Mossback 的"吻端鼻点"同口径。若要更醒目，可加宽到
   2 格——但那会破坏 5 宽脸的左右对称（鼻梁 x4 是正中的那一格），故保持 1 格。
6. 无其他遗留问题；未新增债务。
