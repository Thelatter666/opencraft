# T-B5 报告 · 美术返工：Mossback 比例对齐原版牛 + Hollow Wretch 三倍加密

> 分支 `task/T-B5-mob-art-rework`，基线 main `ba2f920`。**纯资产卡，零代码改动**（源码/CMake/测试一行未碰）。
> 卡面：`docs/tasks/T-B5.md`　证据目录：`docs/qa/T-B5-2026-09-18/`（索引见该目录 `README.md`）

## 0. 一句话

两只模型都在**同一台相机下**重画并取证：Mossback 只动比例（腿高:躯干高 0.83 → **1.17**，四腿移到躯干外缘，
苔丘从三层悬檐圆顶压成**一格薄盖**）；Hollow Wretch 从 169 体素加密到 **496 体素**
（四肢 2 格宽、胸腔凹 2 格深、锯齿破布下摆、眉弓 + 下坠下颌、**2×2 眼窝**），
并交付卡面新增的**非正交游戏机位渲染图 + 与旧版同角度对照**。
★ **追加需求（用户 2026-09-18 本轮提出）「一定要有明显的面部特征」已落地**：
Mossback 首次用上头的第二色（索引 10）开出**两只 2×1 眼窝 + 一个鼻点**，
Wretch 的颅面补上**鼻窝与口**（连同原有 2×2 眼窝）——两只都有**正面可读的五官**，见 §4.4 特写。

## 1. 产出清单（含路径）

| 文件 | 变化 | 读数（现数） |
|---|---|---|
| `assets/mobs/mossback.vox` | 3784 → **3916** 字节 | 7×17×14 画布 / **705 体素** / 1544 三角形 / 7 关节 / 1.4 格高 |
| `assets/mobs/hollow_wretch.vox` | 1772 → **3080** 字节 | 12×9×18 画布 / **496 体素** / 1592 三角形 / 6 关节 / 1.8 格高 |
| `assets/palettes/mossback.png` | 136 → **142** 字节（**只有第 10 格：U0 → O0**） | md5 `2be25d95e04ce3dba7d65449ccf46ea7` |
| `assets/palettes/hollow_wretch.png` | 130 → 129 字节 | md5 `264c75d7ee0750f47d9653340ad116b7`（只有格 13/14：U0 → S3） |
| `assets/CREDITS.md` | 4 行 | 两行设计稿摘要按新形体重写 + 两行调色板（六字段保持） |
| `docs/art/01-style-guide.md` | §9 新增 1 行 + 变更记录 1 行 | 只动了这两处 |

md5：`mossback.vox fda9ee82116c6a937b163517a38b0149`、`hollow_wretch.vox 2af82806857fa3381e3d6d08d9fb3f27`。

源稿（创作本体，落在证据目录）：`tools/{mossback,hollow_wretch}_layers.txt`、
`tools/{mossback,hollow_wretch}_palette.txt`；工具：`tools/{vox_build,vox_inspect,check_palette_png,face_closeup}.py`。

## 2. 白名单 diff（验收标准 1）

```
$ git diff --name-only main...HEAD

assets/CREDITS.md
assets/mobs/hollow_wretch.vox
assets/mobs/mossback.vox
assets/palettes/hollow_wretch.png
assets/palettes/mossback.png
docs/art/01-style-guide.md
docs/qa/T-B5-2026-09-18/ctest_summary.txt
docs/qa/T-B5-2026-09-18/inspect_hollow_wretch_v4.txt
docs/qa/T-B5-2026-09-18/inspect_hollow_wretch_v5.txt
docs/qa/T-B5-2026-09-18/inspect_mossback_v4.txt
docs/qa/T-B5-2026-09-18/inspect_mossback_v5.txt
docs/qa/T-B5-2026-09-18/palette_png_check.txt
docs/qa/T-B5-2026-09-18/README.md
docs/qa/T-B5-2026-09-18/renders/hollow_wretch_gamecam_ab.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v4_contact_sheet.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v4_gamecam.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v4_silhouette.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_contact_sheet.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_face.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_front_big.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_gamecam.png
docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_silhouette.png
docs/qa/T-B5-2026-09-18/renders/mossback_gamecam_ab.png
docs/qa/T-B5-2026-09-18/renders/mossback-v4_contact_sheet.png
docs/qa/T-B5-2026-09-18/renders/mossback-v4_gamecam.png
docs/qa/T-B5-2026-09-18/renders/mossback-v4_silhouette.png
docs/qa/T-B5-2026-09-18/renders/mossback-v5_contact_sheet.png
docs/qa/T-B5-2026-09-18/renders/mossback-v5_face.png
docs/qa/T-B5-2026-09-18/renders/mossback-v5_front_big.png
docs/qa/T-B5-2026-09-18/renders/mossback-v5_gamecam.png
docs/qa/T-B5-2026-09-18/renders/mossback-v5_silhouette.png
docs/qa/T-B5-2026-09-18/renders/mv_hollow_wretch_v5.png
docs/qa/T-B5-2026-09-18/renders/mv_mossback_v5.png
docs/qa/T-B5-2026-09-18/renders/v4/hollow_wretch.vox
docs/qa/T-B5-2026-09-18/renders/v4/mossback.vox
docs/qa/T-B5-2026-09-18/run1_startup_with_assets.log
docs/qa/T-B5-2026-09-18/run2_startup_no_mobs_dir.log
docs/qa/T-B5-2026-09-18/tools/check_palette_png.py
docs/qa/T-B5-2026-09-18/tools/face_closeup.py
docs/qa/T-B5-2026-09-18/tools/hollow_wretch_layers.txt
docs/qa/T-B5-2026-09-18/tools/hollow_wretch_palette.txt
docs/qa/T-B5-2026-09-18/tools/mossback_layers.txt
docs/qa/T-B5-2026-09-18/tools/mossback_palette.txt
docs/qa/T-B5-2026-09-18/tools/vox_build.py
docs/qa/T-B5-2026-09-18/tools/vox_inspect.py
docs/tasks/T-B5.report.md
```

全部落在卡面 §3 白名单内：两个 `.vox`、两个调色板 PNG、`assets/CREDITS.md`、
`docs/art/01-style-guide.md`（只改 §9 与变更记录）、`docs/qa/T-B5-<日期>/`、`docs/tasks/T-B5.report.md`。
**T-B1/T-B2/T-B2b/T-B3 的目录与文件一字未动**（`git diff main...HEAD -- docs/qa/T-B1-* docs/qa/T-B2* docs/qa/T-B3-*` 输出为空；
v4 旧件是 `git show ba2f920:…`（分支基线）拷进**本卡证据目录**的只读副本，见 §5.2，md5 已核）。

## 3. 日志判据（验收标准 2）

**移走资产前（产品码、无补丁；`cd build && ./opencraft`）** —— `run1_startup_with_assets.log`：

```
[2026-09-18 21:10:49.429] [info] mobs: 2/3 mob models loaded from ../assets/mobs
[2026-09-18 21:10:49.429] [info] mob model mossback: 705 voxels, 1544 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png
[2026-09-18 21:10:49.430] [info] mob model hollow_wretch: 496 voxels, 1592 triangles, 6 joints, 1.8 blocks tall, palette from palettes/hollow_wretch.png
```

- `mobs: 2/3` ✓（第三只 `blastbud` 属压后的 T-B4，其资产尚未落盘 ⇒ 2/3 是预期值；
  日志里 `mob blastbud spawned …` 也说明它在名册里、只是没有模型、走回退盒）；
- **两行 `mob model …` 齐全** ✓；**游戏自己的日志行里 WARN/ERROR 各 0 条** ✓
  （`grep -c "WARN\|ERROR" run1_startup_with_assets.log` → `0`）。
  ⚠ **如实登记一处噪声**：run1 日志里有一行
  `opencraft[30825:725421] error messaging the mach port for IMKCFRunLoopWakeUpReliable` ——
  它是 **macOS 输入法框架（IMK/AppKit）直接往 stderr 写的**，行首没有本项目的 `[时间] [级别]` 前缀，
  不是 OpenCraft 的日志通道（它未出现在 T-B2b/T-B3 期的同类日志里，应与本轮系统输入法状态有关，与本卡资产无关）。
- `atlas: 60/63` 与基线一致（本卡未碰方块贴图）✓。
- **数值自洽**：日志的 705 / 1544 / 496 / 1592 与 §5 的独立复查器（`vox_inspect.py`，自写解析 + 邻居剔除）
  **逐个相同** ✓。

**移走 `assets/mobs/` 后** —— `run2_startup_no_mobs_dir.log`：`mobs: 0/3`、无 WARN ✓
（缺目录仍是**静默回退**，T-B2 立的契约未被这版资产破坏）。

**全量测试**：`459/459`（`ctest_summary.txt`）—— 纯资产卡，与 T-B3 基线同数。

## 4. 新判据：非正交游戏机位（验收标准 5）

### 4.1 相机（两只模型、新旧两版**共用**同一组参数）

| 参数 | 值 |
|---|---|
| 距离 | **4.5 格**（45 体素，卡面 §2.5 的"近 4–5 格距离感"） |
| 方位 / 仰角 | 从正前方（+Y）往右 **38°** / 俯视 **10°** |
| 投影 | **针孔透视**：屏幕点 = (x_cam / z_cam, y_cam / z_cam) |
| 缩放 | **16 px / 体素**（在 target 距离上），画布固定 760×620 —— **不按模型自适应缩放** |

> 两条与首轮不同的选择，都写清理由：
> ① **仰角 16° → 10°**：玩家眼高 1.62 格、生物 4.5 格外，真实俯角就是 9–12°；16° 会把脸在画面上压扁成一条，
> 与"要有明显的面部特征"（§4.4）直接冲突。两版（v4/v5）同参数重出，对照关系不变。
> ② **不按模型自适应缩放**：若按"长边 520 px"自适应，v4 与 v5 会被缩到一样大，比例对照就没意义了。

### 4.2 "这张图确实不是正交投影"的机器自证

地面上取**等长**（1 体素）的两小段，一段近端、一段远端，投到屏幕上的像素长度：

| 模型 | 近端 | 远端 | 比值 | 正交相机应为 |
|---|---|---|---|---|
| Mossback v5 | 14.39 px | 8.06 px | **1.786** | 1.000 |
| Hollow Wretch v5 | 12.48 px | 9.22 px | **1.354** | 1.000 |

（工具里是一条 `assert near_px > far_px * 1.02`：不成立就直接中止出图。）

### 4.3 对照图（★ 一图看两条返工结论）

| 文件 | 左（旧） | 右（新） | 一张图里要读出的 |
|---|---|---|---|
| `renders/mossback_gamecam_ab.png` | v4 `672` 体素 | v5 `705` 体素 | **"比例更对了"**：腿明显变长（5→7）、身子变扁平长（体长 7→11）、苔丘由三层悬空壳变一格薄盖 |
| `renders/hollow_wretch_gamecam_ab.png` | v4 `169` 体素 | v5 `496` 体素 | **"不再像棍子"**：四肢有粗细、胸腔有体量、头有骨相 |

### 4.4 ★ 追加需求取证（用户 2026-09-18：「一定要有明显的面部特征」）

| 文件 | 内容 |
|---|---|
| `renders/mossback-v5_front_big.png` | 正视大图（46 px/体素）：**两只 2×1 眼窝 + 正中鼻点**，棕色桥上两眼分开 ⇒ 一眼读出"这是脸" |
| `renders/hollow_wretch-v5_front_big.png` | 正视大图：**2×2 眼窝 + 鼻梁 + 鼻窝 + 口**（亮骨色底上的四处黑），骷髅脸成立 |
| `renders/mossback-v5_face.png` / `renders/hollow_wretch-v5_face.png` | **面部特写**（`face_closeup.py`）：相机贴在脸前 1.6 格、与脸同高（真实玩家眼高与 1.8 格高生物的眼窝基本齐平），px/体素 78 |

落地方式（都用**契约 v2 的关节第二色**，没有新增任何色值）：

| 模型 | 面部特征 | 索引 | 格子 | 机器判据 |
|---|---|---|---|---|
| Mossback | 眼窝 ×2（各 2 宽 × 1 高） | **10** = O0 `#28282C` | `(1,16,11) (2,16,11) (4,16,11) (5,16,11)` | 在占用表内、`+y` 邻居为空（表面着色，不挖空）✓ |
| Mossback | 鼻点 ×1 | **10** | `(3,16,10)`（吻端正中） | 同上 ✓ |
| Wretch | 眼窝 ×2（各 2×2，v4 已有） | 10 = O0 | `(3..4,7,15..16) (7..8,7,15..16)` | 同上 ✓ |
| Wretch | 鼻窝 ×2 | 10 | `(5,7,14) (6,7,14)` | 同上 ✓ |
| Wretch | 口 ×4 | 10 | `(4..7,8,13)`（下颌正面） | 同上 ✓ |

- **Mossback 的调色板因此改了第 10 格**（U0 → O0）——这是本轮唯一改变调色板的地方，
  与首轮的"调色板一字未动"不同，**如实登记**（卡面 §3 白名单本来就允许改 `assets/palettes/mossback.png`）。
- 上一版（无五官）的实机截图已被本轮取代：**面部特征必须由 PM 用暂停帧装置复核才有实机像素证据**（见 §10.5）。

## 5. 验收标准 3：两份预算与包围盒表（`vox_inspect.py` 全文）

### 5.1 Mossback v5（`inspect_mossback_v5.txt`）

```
file        : assets/mobs/mossback.vox (3916 bytes)
version     : 150
chunks      : MAIN(0B), SIZE(12B), XYZI(2824B), RGBA(1024B)
SIZE        : 7 x 17 x 14  (max side 17)
voxels      : 705
colorIndex 0: 0 voxels  (contract: must be 0)
bbox        : x 0..6  y 0..16  z 0..13
span        : x 7  y 17  z 14  (z-span must equal collision height / 0.1)
per-colorIndex bbox and joint mapping:
  idx  1   205 voxels  x 0..6  y 1..12  z 8..12  rgb (110, 86, 60)  -> joint 0 body 躯干
  idx  2    57 voxels  x 1..5  y 13..16  z 9..13  rgb (168, 132, 92)  -> joint 1 head 头
  idx  3    30 voxels  x 0..1  y 9..11  z 0..6  rgb (66, 52, 38)  -> joint 2 arm_l 左前肢/左臂
  idx  4    30 voxels  x 5..6  y 9..11  z 0..6  rgb (66, 52, 38)  -> joint 3 arm_r 右前肢/右臂
  idx  5    30 voxels  x 0..1  y 2..4  z 0..6  rgb (66, 52, 38)  -> joint 4 leg_l 左后肢/左腿
  idx  6    30 voxels  x 5..6  y 2..4  z 0..6  rgb (66, 52, 38)  -> joint 5 leg_r 右后肢/右腿
  idx  7     4 voxels  x 2..4  y 0..0  z 11..12  rgb (70, 112, 60)  -> joint 6 tail 尾
  idx 10     5 voxels  x 1..5  y 16..16  z 10..11  rgb (40, 40, 44)  -> joint 1 head 头
  idx 17   139 voxels  x 0..6  y 1..11  z 10..11  rgb (138, 110, 76)  -> joint 0 body 躯干
  idx 18    22 voxels  x 0..6  y 1..11  z 8..8  rgb (66, 52, 38)  -> joint 0 body 躯干
  idx 19    72 voxels  x 0..6  y 2..10  z 7..9  rgb (100, 76, 60)  -> joint 0 body 躯干
  idx 20    52 voxels  x 0..6  y 1..10  z 11..13  rgb (96, 146, 78)  -> joint 0 body 躯干
  idx 21    11 voxels  x 0..6  y 3..10  z 9..13  rgb (70, 112, 60)  -> joint 0 body 躯干
  idx 22     9 voxels  x 0..6  y 1..10  z 10..13  rgb (46, 74, 40)  -> joint 0 body 躯干
  idx 23     6 voxels  x 2..4  y 2..10  z 13..13  rgb (90, 140, 74)  -> joint 0 body 躯干
  idx 24     3 voxels  x 2..6  y 5..9  z 13..13  rgb (124, 168, 98)  -> joint 0 body 躯干
joint groups actually present (body = index 1 plus every 17+ colour):
  joint 0 body 躯干: 519 voxels
  joint 1 head 头: 62 voxels
  joint 2 arm_l 左前肢/左臂: 30 voxels
  joint 3 arm_r 右前肢/右臂: 30 voxels
  joint 4 leg_l 左后肢/左腿: 30 voxels
  joint 5 leg_r 右后肢/右腿: 30 voxels
  joint 6 tail 尾: 4 voxels
head max z  : 13
  head max z 13 vs torso core (idx 1/17/18/19) max z 12  (centroid 11.10 vs 9.55)
  head max z 13 vs all joint 0 (incl. moss/hump) max z 13  (centroid 11.10 vs 10.01)
lowest layer z=0 colorIndices [3, 4, 5, 6] -> joints [2, 3, 4, 5] (feet/legs only)
lowest layer, per-cell (x, y, index):
  (x=0, y=2, z=0) idx 5
  (x=0, y=3, z=0) idx 5
  (x=0, y=4, z=0) idx 5
  (x=0, y=9, z=0) idx 3
  (x=0, y=10, z=0) idx 3
  (x=0, y=11, z=0) idx 3
  (x=1, y=2, z=0) idx 5
  (x=1, y=3, z=0) idx 5
  (x=1, y=4, z=0) idx 5
  (x=1, y=9, z=0) idx 3
  (x=1, y=10, z=0) idx 3
  (x=1, y=11, z=0) idx 3
  (x=5, y=2, z=0) idx 6
  (x=5, y=3, z=0) idx 6
  (x=5, y=4, z=0) idx 6
  (x=5, y=9, z=0) idx 4
  (x=5, y=10, z=0) idx 4
  (x=5, y=11, z=0) idx 4
  (x=6, y=2, z=0) idx 6
  (x=6, y=3, z=0) idx 6
  (x=6, y=4, z=0) idx 6
  (x=6, y=9, z=0) idx 4
  (x=6, y=10, z=0) idx 4
  (x=6, y=11, z=0) idx 4
  torso x range 0..6; legs x [0, 1, 5, 6]; flush: True
  idx 3 leg bbox z 0..6 (height 7)
  idx 4 leg bbox z 0..6 (height 7)
  idx 5 leg bbox z 0..6 (height 7)
  idx 6 leg bbox z 0..6 (height 7)
second-colour indices painted: [10]
  idx 10 -> joint 1 head 头: cells x 1..5 y 16..16 z 10..11 within joint bbox x 1..5 y 13..16 z 9..13  [OK]
eye voxels (idx 10): 5
  (1, 16, 11) present in occupancy table; +y neighbour (1, 17, 11) empty: True
  (2, 16, 11) present in occupancy table; +y neighbour (2, 17, 11) empty: True
  (3, 16, 10) present in occupancy table; +y neighbour (3, 17, 10) empty: True
  (4, 16, 11) present in occupancy table; +y neighbour (4, 17, 11) empty: True
  (5, 16, 11) present in occupancy table; +y neighbour (5, 17, 11) empty: True
  eye voxels span x [1, 2, 3, 4, 5] z [10, 11]; every eye voxel is a surface colour (not a carved hole): True
exposed faces: 772  (budget 2000)
triangles    : 1544  (what the startup log reports)
faces per colorIndex: 1:116, 2:95, 3:66, 4:66, 5:66, 6:66, 7:14, 10:13, 17:56, 18:30, 19:79, 20:68, 21:10, 22:14, 23:8, 24:5
rendered side-left-nose-right: 390x324
rendered front-face: 170x324
rendered top: 170x390
rendered iso: 358x411
rendered iso-joints: 358x411
rendered sil-side: 390x324
rendered sil-iso: 358x411
rendered gamecam (perspective): 760x620 @ 16.0 px/voxel
gamecam camera: eye=(30.78, 43.42, 14.81) target=(3.50, 8.50, 7.00) dist=45.0 voxels (4.5 blocks) az=38.0 el=10.0 focal=720.0px
perspective self-check: a 1-voxel ground segment projects to 14.39 px at the near end vs 8.06 px at the far end (ratio 1.786; an orthographic camera would give 1.000)
wrote docs/qa/T-B5-2026-09-18/renders/mossback-v5_contact_sheet.png
wrote docs/qa/T-B5-2026-09-18/renders/mossback-v5_silhouette.png
contact sheet tiles: side-left-nose-right | front-face | top | iso | iso-joints
palette png : assets/palettes/mossback.png 16x16 depth 8 color type 6 (142 bytes)
palette png : alpha set [255] (opaque asset -> {255})
  cell  0 (row 0, col  0) png (30, 30, 34)  vox None  sat 0.118  match
  cell  1 (row 0, col  1) png (110, 86, 60)  vox (110, 86, 60)  sat 0.455  match
  cell  2 (row 0, col  2) png (168, 132, 92)  vox (168, 132, 92)  sat 0.452  match
  cell  3 (row 0, col  3) png (66, 52, 38)  vox (66, 52, 38)  sat 0.424  match
  cell  4 (row 0, col  4) png (66, 52, 38)  vox (66, 52, 38)  sat 0.424  match
  cell  5 (row 0, col  5) png (66, 52, 38)  vox (66, 52, 38)  sat 0.424  match
  cell  6 (row 0, col  6) png (66, 52, 38)  vox (66, 52, 38)  sat 0.424  match
  cell  7 (row 0, col  7) png (70, 112, 60)  vox (70, 112, 60)  sat 0.464  match
  cell 10 (row 0, col 10) png (40, 40, 44)  vox (40, 40, 44)  sat 0.091  match
  cell 17 (row 1, col  1) png (138, 110, 76)  vox (138, 110, 76)  sat 0.449  match
  cell 18 (row 1, col  2) png (66, 52, 38)  vox (66, 52, 38)  sat 0.424  match
  cell 19 (row 1, col  3) png (100, 76, 60)  vox (100, 76, 60)  sat 0.400  match
  cell 20 (row 1, col  4) png (96, 146, 78)  vox (96, 146, 78)  sat 0.466  match
  cell 21 (row 1, col  5) png (70, 112, 60)  vox (70, 112, 60)  sat 0.464  match
  cell 22 (row 1, col  6) png (46, 74, 40)  vox (46, 74, 40)  sat 0.459  match
  cell 23 (row 1, col  7) png (90, 140, 74)  vox (90, 140, 74)  sat 0.471  match
  cell 24 (row 1, col  8) png (124, 168, 98)  vox (124, 168, 98)  sat 0.417  match
max saturation over painted cells: 0.471 (cap 0.50)
```

**逐条对卡面 §4.3（Mossback）**：

| 判据 | 要求 | 实测 | |
|---|---|---|---|
| z 跨度 | 仍**恰 14** | `span z 14`（画布 7×17×14，1 体素=0.1 格） | ✓ |
| 腿包围盒 z 高 | **=7** | 索引 3/4/5/6 **各 z 0..6（高 7）** | ✓ |
| 四腿 x 与躯干外缘对齐 | 逐格打印 | `torso x range 0..6; legs x [0, 1, 5, 6]; flush: True` + 逐格清单（左腿 x0..1 / 右腿 x5..6） | ✓ |
| 苔顶 ≤ 头 max z | 不再"苔比头高" | 苔（索引 20-24）max z = **13**；头（索引 2）max z = **13** ⇒ 取**等号**成立 | ✓（见 §10.1/§10.6） |
| 每索引包围盒表 | 全表贴出 | `per-colorIndex bbox` 全表（16 个索引） | ✓ |
| z=0 层只许腿 | 抗 y-up | 索引 `[3,4,5,6]` → 关节 2/3/4/5 | ✓ |
| ★ 面部特征（追加需求） | 正面可读 | 索引 10 共 5 格，全在头关节包围盒内、全在正面（y=16）、`+y` 邻居为空 | ✓ |

### 5.2 旧版对照读数（证明"独立复查器与产品码同口径"）

`inspect_mossback_v4.txt` / `inspect_hollow_wretch_v4.txt` 是用**同一份工具**跑
`git show ba2f920:assets/mobs/*.vox`（分支基线；md5 mossback `9215d2be…`、wretch `6b5dc511…`）的读数：

| 模型 | 工具读数 | 产品码日志（T-B2b / T-B3 期） |
|---|---|---|
| mossback v4 | 672 体素 / 1504 三角形 | `672 voxels, 1504 triangles` ✓ 逐字相同 |
| hollow_wretch v4 | 169 体素 / 856 三角形 | `169 voxels, 856 triangles` ✓ 逐字相同 |

⇒ 复查器不是"自己说自己的话"：它在旧件上复现了产品码的读数，才用它去核新件的读数。

### 5.3 Hollow Wretch v5（`inspect_hollow_wretch_v5.txt`）

```
file        : assets/mobs/hollow_wretch.vox (3080 bytes)
version     : 150
chunks      : MAIN(0B), SIZE(12B), XYZI(1988B), RGBA(1024B)
SIZE        : 12 x 9 x 18  (max side 18)
voxels      : 496
colorIndex 0: 0 voxels  (contract: must be 0)
bbox        : x 1..10  y 0..8  z 0..17
span        : x 10  y 9  z 18  (z-span must equal collision height / 0.1)
per-colorIndex bbox and joint mapping:
  idx  1    32 voxels  x 4..7  y 1..4  z 10..13  rgb (110, 110, 116)  -> joint 0 body 躯干
  idx  2   102 voxels  x 3..8  y 3..8  z 13..17  rgb (226, 230, 236)  -> joint 1 head 头
  idx  3    16 voxels  x 1..2  y 1..3  z 9..11  rgb (74, 74, 80)  -> joint 2 arm_l 左前肢/左臂
  idx  4    16 voxels  x 9..10  y 1..3  z 9..11  rgb (74, 74, 80)  -> joint 3 arm_r 右前肢/右臂
  idx  5    44 voxels  x 4..5  y 1..4  z 1..7  rgb (74, 74, 80)  -> joint 4 leg_l 左后肢/左腿
  idx  6    44 voxels  x 6..7  y 1..4  z 1..7  rgb (74, 74, 80)  -> joint 5 leg_r 右后肢/右腿
  idx  9     4 voxels  x 5..6  y 2..2  z 11..12  rgb (46, 42, 58)  -> joint 0 body 躯干
  idx 10    14 voxels  x 3..8  y 7..8  z 13..16  rgb (40, 40, 44)  -> joint 1 head 头
  idx 11    46 voxels  x 1..2  y 4..7  z 2..8  rgb (166, 166, 172)  -> joint 2 arm_l 左前肢/左臂
  idx 12    46 voxels  x 9..10  y 4..7  z 2..8  rgb (166, 166, 172)  -> joint 3 arm_r 右前肢/右臂
  idx 13     8 voxels  x 4..5  y 1..4  z 0..0  rgb (166, 166, 172)  -> joint 4 leg_l 左后肢/左腿
  idx 14     8 voxels  x 6..7  y 1..4  z 0..0  rgb (166, 166, 172)  -> joint 5 leg_r 右后肢/右腿
  idx 17    56 voxels  x 1..10  y 1..3  z 12..13  rgb (128, 128, 133)  -> joint 0 body 躯干
  idx 18    48 voxels  x 3..8  y 0..5  z 8..9  rgb (74, 74, 80)  -> joint 0 body 躯干
  idx 19    12 voxels  x 4..7  y 0..0  z 11..13  rgb (70, 53, 41)  -> joint 0 body 躯干
joint groups actually present (body = index 1 plus every 17+ colour):
  joint 0 body 躯干: 152 voxels
  joint 1 head 头: 116 voxels
  joint 2 arm_l 左前肢/左臂: 62 voxels
  joint 3 arm_r 右前肢/右臂: 62 voxels
  joint 4 leg_l 左后肢/左腿: 52 voxels
  joint 5 leg_r 右后肢/右腿: 52 voxels
head max z  : 17
  head max z 17 vs torso core (idx 1/17/18/19) max z 13  (centroid 15.29 vs 10.85)
  head max z 17 vs all joint 0 (incl. moss/hump) max z 13  (centroid 15.29 vs 10.87)
lowest layer z=0 colorIndices [13, 14] -> joints [4, 5] (feet/legs only)
second-colour indices painted: [9, 10, 11, 12, 13, 14]
  idx  9 -> joint 0 body 躯干: cells x 5..6 y 2..2 z 11..12 within joint bbox x 1..10 y 0..5 z 8..13  [OK]
  idx 10 -> joint 1 head 头: cells x 3..8 y 7..8 z 13..16 within joint bbox x 3..8 y 3..8 z 13..17  [OK]
  idx 11 -> joint 2 arm_l 左前肢/左臂: cells x 1..2 y 4..7 z 2..8 within joint bbox x 1..2 y 1..7 z 2..11  [OK]
  idx 12 -> joint 3 arm_r 右前肢/右臂: cells x 9..10 y 4..7 z 2..8 within joint bbox x 9..10 y 1..7 z 2..11  [OK]
  idx 13 -> joint 4 leg_l 左后肢/左腿: cells x 4..5 y 1..4 z 0..0 within joint bbox x 4..5 y 1..4 z 0..7  [OK]
  idx 14 -> joint 5 leg_r 右后肢/右腿: cells x 6..7 y 1..4 z 0..0 within joint bbox x 6..7 y 1..4 z 0..7  [OK]
eye voxels (idx 10): 14
  (3, 7, 15) present in occupancy table; +y neighbour (3, 8, 15) empty: True
  (3, 7, 16) present in occupancy table; +y neighbour (3, 8, 16) empty: True
  (4, 7, 15) present in occupancy table; +y neighbour (4, 8, 15) empty: True
  (4, 7, 16) present in occupancy table; +y neighbour (4, 8, 16) empty: True
  (4, 8, 13) present in occupancy table; +y neighbour (4, 9, 13) empty: True
  (5, 7, 14) present in occupancy table; +y neighbour (5, 8, 14) empty: True
  (5, 8, 13) present in occupancy table; +y neighbour (5, 9, 13) empty: True
  (6, 7, 14) present in occupancy table; +y neighbour (6, 8, 14) empty: True
  (6, 8, 13) present in occupancy table; +y neighbour (6, 9, 13) empty: True
  (7, 7, 15) present in occupancy table; +y neighbour (7, 8, 15) empty: True
  (7, 7, 16) present in occupancy table; +y neighbour (7, 8, 16) empty: True
  (7, 8, 13) present in occupancy table; +y neighbour (7, 9, 13) empty: True
  (8, 7, 15) present in occupancy table; +y neighbour (8, 8, 15) empty: True
  (8, 7, 16) present in occupancy table; +y neighbour (8, 8, 16) empty: True
  eye voxels span x [3, 4, 5, 6, 7, 8] z [13, 14, 15, 16]; every eye voxel is a surface colour (not a carved hole): True
exposed faces: 796  (budget 2000)
triangles    : 1592  (what the startup log reports)
faces per colorIndex: 1:48, 2:142, 3:32, 4:32, 5:54, 6:54, 9:4, 10:34, 11:94, 12:94, 13:18, 14:18, 17:74, 18:72, 19:26
rendered side-left-nose-right: 214x412
rendered front-face: 236x412
rendered top: 236x214
rendered iso: 280x411
rendered iso-joints: 280x411
rendered sil-side: 214x412
rendered sil-iso: 280x411
rendered gamecam (perspective): 760x620 @ 16.0 px/voxel
gamecam camera: eye=(33.28, 39.42, 16.81) target=(6.00, 4.50, 9.00) dist=45.0 voxels (4.5 blocks) az=38.0 el=10.0 focal=720.0px
perspective self-check: a 1-voxel ground segment projects to 12.48 px at the near end vs 9.22 px at the far end (ratio 1.354; an orthographic camera would give 1.000)
wrote docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_contact_sheet.png
wrote docs/qa/T-B5-2026-09-18/renders/hollow_wretch-v5_silhouette.png
contact sheet tiles: side-left-nose-right | front-face | top | iso | iso-joints
palette png : assets/palettes/hollow_wretch.png 16x16 depth 8 color type 6 (129 bytes)
palette png : alpha set [255] (opaque asset -> {255})
  cell  0 (row 0, col  0) png (30, 30, 34)  vox None  sat 0.118  match
  cell  1 (row 0, col  1) png (110, 110, 116)  vox (110, 110, 116)  sat 0.052  match
  cell  2 (row 0, col  2) png (226, 230, 236)  vox (226, 230, 236)  sat 0.042  match
  cell  3 (row 0, col  3) png (74, 74, 80)  vox (74, 74, 80)  sat 0.075  match
  cell  4 (row 0, col  4) png (74, 74, 80)  vox (74, 74, 80)  sat 0.075  match
  cell  5 (row 0, col  5) png (74, 74, 80)  vox (74, 74, 80)  sat 0.075  match
  cell  6 (row 0, col  6) png (74, 74, 80)  vox (74, 74, 80)  sat 0.075  match
  cell  9 (row 0, col  9) png (46, 42, 58)  vox (46, 42, 58)  sat 0.276  match
  cell 10 (row 0, col 10) png (40, 40, 44)  vox (40, 40, 44)  sat 0.091  match
  cell 11 (row 0, col 11) png (166, 166, 172)  vox (166, 166, 172)  sat 0.035  match
  cell 12 (row 0, col 12) png (166, 166, 172)  vox (166, 166, 172)  sat 0.035  match
  cell 13 (row 0, col 13) png (166, 166, 172)  vox (166, 166, 172)  sat 0.035  match
  cell 14 (row 0, col 14) png (166, 166, 172)  vox (166, 166, 172)  sat 0.035  match
  cell 17 (row 1, col  1) png (128, 128, 133)  vox (128, 128, 133)  sat 0.038  match
  cell 18 (row 1, col  2) png (74, 74, 80)  vox (74, 74, 80)  sat 0.075  match
  cell 19 (row 1, col  3) png (70, 53, 41)  vox (70, 53, 41)  sat 0.414  match
max saturation over painted cells: 0.414 (cap 0.50)
```

**逐条对卡面 §4.3（Wretch）**：

| 判据 | 要求 | 实测 | |
|---|---|---|---|
| z 跨度 | 仍**恰 18** | `span z 18`（画布 12×9×18） | ✓ |
| 体素数 | **≥ 450** | **496**（v4 = 169 ⇒ 2.94 倍；上限 1000 内） | ✓ |
| z=0 层只含双脚 | — | 索引 `[13,14]` → 关节 `[4,5]`（两只脚） | ✓ |
| 眼格在占用表内 + `+y` 邻居为空 | 表面着色，不挖空 | 14 格全部 `True/True`（2×2 眼 ×2 + 鼻窝 ×2 + 口 ×4，全在头关节内） | ✓ |
| 索引 9/10/11/12 落在其关节包围盒内 | 契约 v2 | 9→躯干、10→头、11→左臂、12→右臂，**全部 `[OK]`**（另 13/14→左右腿也 `[OK]`） | ✓ |

## 6. 风格合规（验收标准 4）

`§8 十项 + §3 低饱和`（对体素模型适用的是 §2/§3/§6 与 §8 的 1/2/4/5/6/9/10 项，口径见 `docs/art/01-style-guide.md` §9）：

| # | 检查项 | Mossback v5 | Hollow Wretch v5 |
|---|---|---|---|
| 1 | 真文件（`.vox` 头 `VOX `、调色板真 PNG `\x89PNG`） | ✓ 独立解析器读通 | ✓ 同上 |
| 2 | 尺寸正好（画布 ≤32；调色板 PNG **16×16**） | 7×17×14 / 16×16 ✓ | 12×9×18 / 16×16 ✓ |
| 3 | 朝向：PNG 第 0 行 = 面的上沿 | **不适用**（§9：生物模型不是方块贴图） | 不适用 |
| 4 | alpha：不透明全 255 | ✓ `{255}` | ✓ `{255}` |
| 5 | 色板：每个 RGB 都在 §2.1 的 32 色表内 | ✓ 16 个索引全在表内 | ✓ 15 个索引全在表内 |
| 6 | 低饱和：每色 S ≤ 0.50 | 最高 **0.471**（F2） | 最高 **0.414**（E0） |
| 7 | 构图：无 4 px 以上实心块/无规则形/无抗锯齿 | **不适用**（同上：管的是方块贴图内部构图） | 不适用 |
| 8 | 三面（`_top`/`_side`/`_bottom`）齐全 | **不适用**（§9 明写 §5 不约束体素模型） | 不适用 |
| 9 | 原创性：未参考任何既有作品纹理；未用 AI；CREDITS 六字段齐 | ✓ 逐格手写字符稿 | ✓ 逐格手写字符稿 |
| 10 | 生效：启动日志计数与落盘一致 | ✓ `mobs: 2/3` + 两行 `mob model` | ✓ 同上 |

- **第三方复核**（`check_palette_png.py` + Pillow，`palette_png_check.txt`）：两张调色板 **256 格逐格相同、0 不匹配**，
  alpha `{255}`，最大饱和 0.471 / 0.414 ⇒ 自己写的编码器没有"一致地错"。
- **第三方人眼读者**（卡面 §4.6 可选加分）：MagicaVoxel 0.99.6.2 打开两个新件，
  标题栏分别是 `mossback` / `hollow_wretch`、尺寸读数 **`7 17 14`** / **`12 9 18`**
  （与 `SIZE` 一致）—— 截图 `renders/mv_mossback_v5.png`、`renders/mv_hollow_wretch_v5.png`。
- **CREDITS 六字段齐**：`assets/CREDITS.md` 的 4 行（两只 `.vox` + 两张调色板 PNG）逐行点过。

## 7. 交付一：Mossback v5 的比例账（把卡面 §1.1 的表落到格子上）

基准：Mojang `bedrock-samples` 的 `cow.geo.json`（`geometry.cow.v1.8`，卡面派发时已存档在
`docs/qa/T-B5-2026-09-18/pm_verify/cow.geo.json`，本卡只读不改）。1 px = 1/16 格，1 体素 = 0.1 格 = 1.6 px。

| 部件 | 原版牛（格） | 折成体素 | 本稿取整 | v4（返工前） |
|---|---|---|---|---|
| 腿 | 0.25 × 0.75 × 0.25 | 2.5 × **7.5** × 2.5 | **2 × 7 × 2** | 2 × **5** × 2 |
| 躯干 | 0.75 × 0.625 × 1.125 | 7.5 × **6.2** × 11.2 | **7 × 6 × 11** | **9** × 6 × **7** |
| 头 | 0.5 × 0.5 × 0.375 | 5 × 5 × 3.8 | **5 × 5 × 4** | 5 × 5 × 4 ✓ |
| **腿高 : 躯干高** | **12 : 10 = 1.2** | 7.5 : 6.2 | **7 : 6 = 1.17** ✓ | **5 : 6 = 0.83** ✗ |
| 四腿位置 | 与身体外缘齐平 | 贴外缘 | **左 x0..1 / 右 x5..6，躯干 x0..6 ⇒ 同列** ✓ | 内缩 1 格 ✗ |

派生比值（都在原版的 ±5% 内）：体长:体高 1.83 vs 1.80；体宽:体高 1.17 vs 1.20；体长:体宽 1.57 vs 1.50。
**全长** 17 体素 vs 碰撞盒 0.9 格 ⇒ 前后各出 ~0.4 格 —— 原版牛模型全长 26 px = 1.625 格 vs 碰撞盒 0.9 ⇒
前后各出 ~0.36 格，**同一量级、同一做法**。

**外观身份保留清单**（逐条对卡面 §1.2.6 "一律不变"）：

| 特征 | 是否保留 |
|---|---|
| 色板 | ✓ 用的仍是 §2.1 表内那批色（用色集合 = v4 的 15 个 + 本卡新增的索引 10 眼睛色 O0）；调色板第 10 格是本轮唯一改动，理由 = 用户追加的"面部特征"需求 |
| 关节标注 1..7 | ✓ 躯干/头/左右前肢/左右后肢/尾 = 索引 1..7 |
| z=0 层只许腿 | ✓ 只剩索引 3/4/5/6 |
| 正面 +Y | ✓ 鼻端在 y 最大侧；侧视图一律"鼻在右" |
| 苔丘（识别特征） | ✓ 保留，但按卡面压成 **1 格薄盖** |
| 低头吃草姿态 | ✓ 吻部下垂到 z=9、颈部（3 格宽）可见、头前伸到 y=16 |
| ★ 面部特征（追加） | ✓ 新增两只 2×1 眼窝 + 一个鼻点（索引 10） |

## 8. 交付二：Hollow Wretch v5 的加密账

| 卡面 §2 要求 | 落法 | 读数 |
|---|---|---|
| 体素数 ≥ 450（≈3×） | 画布 6×6×18 → 12×9×18，四肢 1→2 格宽、躯干 2×2 → 4×4 | **496**（169 × 2.94） |
| 四肢 ≥ 2 格宽 | 臂 x1..2/x9..10（2 宽 × 3 深）；腿 x4..5/x6..7（2 宽 × 3~4 深） | `per-colorIndex` 表可核 |
| 肘 / 膝弯折 | 肘：上臂 y1..3 → 肘 y2..5 → 前臂 y5..7；膝：大腿 y1..3 → 膝 z=3（y1..4 加厚）→ 小腿 y2..4（前移 1 格） | 侧视出图可见 |
| 保留"长臂垂在身前" | 前臂 + 手在 y5..7（比上臂前 2 格），手垂到 z=2..3 | 臂总高 z2..11（0.9 格） |
| 胸腔 4 宽 × 3 深起步 | x4..7 × y1..4 = **4 × 4** | idx 1 32 格 + 肩带 |
| 锁骨下凹陷用索引 9 做大做深 | 正面 x5..6 **挖掉 2 层**（y4、y3），露出后墙 y2 上的 'H' ⇒ **2 宽 × 2 高 × 2 格深** | idx 9 共 **4 格**（v4 只有 2 格、1 格深） |
| 下摆锯齿破布边 | z=9 一圈：前缘 x3..4 / x7..8 两片、后缘 x4..5 一片 | idx 18 共 48 格（z8..9） |
| 头有骨相：眉弓 | z=17 的颅骨多伸一格到 y=8（眼面在 y=7） | idx 2 的 y 上界 8 > 眼的 7 |
| 头有骨相：下坠下颌 | z=13 的下颌 x4..7 × y6..8（比颅窄 2 格、探到 y=8） | z=13 层可见 |
| 眼窝 2×2（仍 = 索引 10，不挖空） | 正面两角 x3..4 / x7..8 × z15..16 | 8 格 |
| ★ 面部特征（追加） | 鼻窝 x5..6（z=14 正面）+ 口 x4..7（z=13 下颌正面） | 各 2 格 / 4 格，全在头关节内、全为表面着色 |
| 契约 v2 用法照旧 | 1..8 主标签、9..16 第二色、17+ 躯干组 | 见 §5.3 第二色落盒表 |

**v2 第二色本卡用了六个**（v4 是四个）：9 胸前空洞、10 眼睛（眼窝 + 鼻窝 + 口）、11/12 骨手（含前臂）、
**13/14 双脚（本卡新增用）**；它们**不增加关节数**（仍然 6）。

## 9. 迭代记录（先画 → 渲染 → 看 → 改；诚实留痕）

| 轮 | 改了什么 | 出图后的判定 |
|---|---|---|
| Mossback v5a | 只改腿高（5→7）与腿位（移到外缘），躯干 y 仍 7 长 | 侧视变成"长腿短身"，**肚子比腿窄一圈的反向病**：腹线左右各收 1 格 ⇒ 四条腿顶在外侧悬着 |
| Mossback v5b | 腹线 z=7 改成**整宽 x0..6**（只把 y 两端各收 1 格），头改成"颅 + 前额 + 吻"三级台阶 | 腿"从身体四角长出来" ✓；头不再是平板方块 ✓ |
| Mossback v5c | 背线 z=12 整层由受光色 '9' 改回主色 '1' | 头与苔都更跳；体素数 705 不变 |
| Mossback v5d | 下腹两侧竖边（z=8 的 x0/x6）由泥色 'b' 改回暗色 'a'（W0） | 下腹有暗边、形体更立体；用色集合补回与 v4 相同的 15 个索引 |
| **Mossback v5e**（★ 追加需求） | 头的第二色（索引 10 = O0）画出**两只 2×1 眼窝 + 正中鼻点**；调色板第 10 格 U0 → O0 | 先试 2×2 眼窝（正面两只黑洞、像空心骷髅 ✗ 与"温顺食草兽"不符），reduced 到 2×1 ✓；鼻孔一开始做成两只、正好落在眼睛内列的正下方 ⇒ 与眼睛糊成阶梯状 ✗，改成吻端正中一格 ✓ |
| Wretch v5a | 四肢加粗到 2 格宽、画布加宽 | **正面剪影从肩到手仍是一整块**（臂与腿在 x 上相邻）—— v1 的教训换了个位置复发 ✗ |
| Wretch v5b | 臂外移 + 腿收进躯干正下方（x4..7），留出 x=3/x=8 两条竖向空隙 | 正面读出「臂 \| 躯干+腿 \| 臂」三段 ✓ |
| Wretch v5c | 胸腔 4×4、锁骨下凹 2 格深、锯齿破布边、眉弓 + 下坠下颌、眼窝 1×1 → 2×2、双脚上骨色 | 定稿 |
| **Wretch v5d**（★ 追加需求） | 颅面补**鼻窝**（z=14 正面 x5..6）与**口**（z=13 下颌正面 x4..7） | 正面：眉弓 → 黑眼窝 ×2 → 鼻梁 → 口，骷髅脸一眼成立 ✓ |

> ★ 附带发现并修掉**我自己工具里的一个 bug**（重要，属"探针也会伪造结论"）：
> `vox_inspect.py` 的透视渲染里，背面剔除写成了 `n · fwd > 0`（`fwd` 是"相机看向场景"的方向），
> 结果是**把正面剔掉、渲染了模型的内表面** —— 症状是"五官全不见、颜色偏暗"。
> 是这次做面部特写时抓出来的（见 §4.4）；修正为 `n · toward > 0`（toward = −fwd）后所有出图重跑。
> **本卡 §4 的全部对照图都是修正后重出的**；修正前的旧图只作废、未归档。

## 10. 已知问题（含需 PM 裁决/知悉项）

### 10.1 ★ 需裁决：卡面 §1.2.2 的"苔最多中部 2 格"与验收判据互斥

卡面 §1.2.1 写死"腿 7 + 躯干 6 + 苔 1 = 14"，§4.2 又要求"z 跨度仍恰 14"；而 §1.2.2 的括号里允许苔"最多中部 2 格"。
**两条同时满足时苔顶会到 z=14 ⇒ z 跨度 15**。本卡按**验收判据**执行：苔只有 z=13 一层。

### 10.2 需 PM 知悉：高度预算会"锁死"头位（卡面算术的必然结果）

腿 7 + 躯干 6 ⇒ 背线 z=12，苔占 z=13；验收判据"苔顶 ≤ 头 max z" ⇒ **头必须够到 z=13**，
头高 5 ⇒ 头只能落在 **z 9..13**。副作用是"头比背线高一格"（原版牛的头其实**低于**背线）。
这是卡面两条要求联立的结果，不是美术侧的自由发挥。

### 10.3 需 PM 知悉：Mossback 躯干宽取 7（卡面给的是"7~8"区间）

取 7 的唯一理由是**头要居中**：躯干 7 与头 5 同为奇数 ⇒ 头左右各让 1 格完全对称；
取 8 时 5 宽的头只能偏 0.5 格。副作用：腿粗/躯干宽 = 2/7 = 0.286（原版 4/12 = 0.333；取 8 时 0.25）——7 更贴原版。

### 10.4 需 PM 知悉：Wretch 的躯体比碰撞盒宽

实体占 x1..10 = **1.00 格宽**（v4 是 0.60），碰撞盒仍 0.6 宽 ⇒ **手臂外缘探出碰撞盒 0.2 格**
（与原版人形的臂/盒关系一致）。若 PM 理解为"必须恰好 0.6"，需把臂收回一格（会重新踩到 v5a 的坑）。

### 10.5 诚实登记：本轮**没有**做的取证

- **实机暂停帧 A/B**：卡面 §2.5 明确划给 PM 侧。开发者侧只做了启动日志 + 渲染出图；
  **"游戏里它长这样（含五官）"尚未经过实机像素判据** —— 建议 PM 复核时把机位放在生物正面偏下
  （仰角 ≤10°），否则眉弓会挡眼。
- 渲染器是**自写的简易光栅器**（面明暗固定 6 档，非引擎光照）：服务"形状可读性"，不能替代实机像素证据；
  两者冲突时以实机为准。
- MagicaVoxel 出图是**默认视角**（未转轨），它是加分项、不是门槛。

### 10.6 一处口径提示：Mossback 的"躯干最高点"

`head max z 13 vs all joint 0 (incl. moss/hump) max z 13` 是**取等号**：躯干组的最高点就是苔盖（z=13）。
契约自查表里"头应高于躯干"那条，本稿按**躯干主体（索引 1/17/18/19）**成立（`max z 12 < 13`）。

## 11. 给项目经理的备注

1. **两只模型各自独立可验** ✓：mossback 的判据全在 §5.1；wretch 的全在 §5.3（外加 §4.4 的面部判据）。
2. **调色板通道**：Mossback 的 `palettes/mossback.png` **本轮改了第 10 格（U0 → O0）**——
   这是"明显面部特征"需求的直接后果；Wretch 的调色板与首轮相比未再动（第 10 格本来就是 O0）。
   若 PM 侧有历史 md5 断言，两张都会翻。
3. **本卡未碰任何代码/构建/测试**：`ctest` 459/459 与基线同数；`atlas: 60/63` 一字未变。
4. **待 PM 侧补的证据**：暂停帧装置实拍。建议沿用
   `docs/qa/T-B3-2026-09-18/pm_verify/run_scene_pre.sh` 的"暂停前俯仰"手法，且**俯仰取正对脸或略仰**
   （人形 1.8 格高、四足兽 1.4 格高，两只都要拍正面）。
5. **T-B4（Blastbud）**：本卡未创建、未触碰 `assets/mobs/blastbud*` ✓；
   若要沿用"游戏机位 + 面部特征"门槛，`vox_inspect.py` 的 `render_perspective` 与 `face_closeup.py` 可直接复用
   （相机参数在文件顶部常量里）。
