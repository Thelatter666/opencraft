# T-A4 实机证据　2026-09-18

第二批方块贴图：`cobblestone` / `gravel` / `sandstone` / `bedrock` / `snow_block` / `obsidian`
各 `_top`/`_side`/`_bottom`，共 18 张 16×16 PNG，使全部 20 个方块都有真实贴图。

> 分支 `task/T-A4-blocks-batch2`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-ta4`
> 环境：macOS 24.6.0（arm64）；窗口 `1280×720` 客户区（按 PID 反查得 `1280×748`，含 28 px 标题栏）
> 单实例：每段取证前后 `pgrep -x opencraft` 均为 0（脚本内建 kill + 复检）
> 启动方式：`cd build && ./opencraft`（资产根落在 `../assets`）
> 二进制：`build/opencraft`（本卡 worktree 自行 configure + build，未改任何产品代码 ⇒ 二进制与 main 等价）
> 临时钩子 / 探针代码：**无**

## 0. 为什么本卡要搭"展示台"

卡面 §6 第 3 条要"能看出这 6 个方块确实变了"。但这 6 个方块在出生点视野里看不到：

| 方块 | 世界里有没有 | `region_tool.py hist` 实测（夹具存档） |
|---|---|---|
| `gravel` | 有（海底/地表） | 884 格，全在出生点视野之外 |
| `bedrock` | 有（世界底层 y=0 附近） | 6294 格，同上 |
| `cobblestone` / `sandstone` / `snow_block` / `obsidian` | **一处都没有** | 0 格 |

⇒ 只看日志 60/63 不能证明"这 18 张真的被用上了"（文件名写错是**静默回退**）。
所以本卡沿用 T-D4 卡面授权过的同类手法——**改存档**——在已落盘区块 `(0,-1)` 里搭一块
展示台，把 6 种方块摆到同一机位视野里。**这是证据装置，不是产品改动**：
`patch_showcase.py` 只写在 `build/saves/`（游戏自己写出的存档）上，夹具本身不改。

## 1. 展示台（`patch_showcase.py`）

```
y=199  石台 9×9（玩家站立面）        x 4..12, z -15..-5
y=200  A 排：6 种方块各一块（落地）  x 6..11, z=-14   ⇒ 顶面 + 侧面
y=202  B 排：同样 6 块**悬空**       x 6..11, z=-15   ⇒ y=201 是空气 ⇒ 看得到底面
玩家   (8.5, 200.0, -6.5)  yaw=0（forward=(0,0,-1)）pitch=0  眼高 201.62
```
A 排 7.5 格外（眼下 0.6 格）、B 排 8.5 格外（眼上 0.4 格）⇒ 两排都收在画面中部，
6 块横排约跨 43°，不顶画面边缘；**三面（顶/侧/底）同屏**。
写入后脚本会**从刚写出的文件里回读**每一格并与预期 id 比对（`read-back verified`）。

## 2. 两段会话

| 段 | assets/blocks | 日志关键行 | 截图 |
|---|---|---|---|
| 00 | 只留首批 42 张（本卡 18 张移出） | `atlas: 42/63 block tiles loaded from ../assets/blocks` | `00_before42.png` |
| 01 | 60 张（+本卡 18 张） | `atlas: 60/63 block tiles loaded from ../assets/blocks` | `01_after60.png` |

两段日志的 `respawn point: (0.5, 133.0, -8.5)`、夹具复位、展示台补丁完全一致，
**唯一变量是 assets/blocks 里有没有这 18 张 PNG**。

### 2.1 A/B 判据（`04_pixel_diff.txt`）

```
differing pixels: 70081 of 957440
bounding box: x 442..916, y 293..734
```

分区统计（逐像素比对，`md5` 不能判像素）：

| 区域 | 变化像素 | 占比 | 说明 |
|---|---|---|---|
| 展示台两排（x440–920, y290–500） | 61991 / 100800 | **61.5%** | 6 个方块全变 |
| 石台地面（x0–900, y520–700） | 0 / 162000 | **0.0%** | 对照组：`stone` 属首批，本卡不该动 |
| 物品栏（x440–920, y700–748） | 602 / 23040 | 2.6% | 手上那格的方块图标 |

**自加的更强判据**：展示台区 61991 个变化像素里，有 **61602 个（99.4%）**落在
「规格 §2.1 色板色 × 引擎四档面明暗（顶 255 / 侧X 210 / 侧Z 170 / 底 128）」±24 之内。
例：`rgb(64,64,68)` = `S2 #808085` × 底档 128/255；`rgb(141,142,147)` = `S2` 侧档；
`rgb(106,95,65)` = `E3 #B49E73` 侧档；`rgb(29,26,35)` = `S5 #2E2A3A` 侧档。
⇒ 画面上的新像素**确实来自限定色板**，不是程序化回退图案。

## 3. 工具

| 文件 | 作用 |
|---|---|
| `tools/art_source.py` | ★ **18 张的手写字符网格稿**（16 行 × 16 字符 × 18）+ 查表 + 手写 PNG 编码。**无随机数/噪声/插值/缩放**；内建两条自检：色板饱和度 ≤ 0.50、无 5×5 同色方块 |
| `tools/verify_tiles.py` | 只读 PNG 字节：PNG 签名 / IHDR 16×16 / alpha 集合 / 是否越出色板 / 饱和度 / 5×5 同色块 / 孤立点逐个列出（输出存 `02_verify_files.txt`） |
| `tools/region_tool.py` | 证据装置：读/写 OpenCraft `.ocr` 存档（格式依据 `region_file.hpp` + `chunk.hpp`），子命令 `hist`/`exists`/`get`/`set` |
| `tools/patch_showcase.py` | 证据装置：把玩家与展示台写进夹具副本，并回读自检 |
| `tools/run_evidence.sh` | 取证驱动（复位夹具 → 补展示台 → 布置贴图 → 启动 → 等 atlas 行 → 激活 → 连拍两张取第二张 → kill + 复检 → 还原贴图） |
| `tools/pixel_diff.py` | 逐像素差异 + 差异色直方图（沿用 T-A3） |
| `tools/ta4win.m` | 按 PID 反查窗口 + 激活（沿用 T-A3 的 `ta3win.m`） |

## 4. 复跑

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-ta4
cc -O2 -fobjc-arc -framework AppKit -framework ApplicationServices -framework CoreGraphics \
   -o /tmp/ta4win docs/qa/T-A4-2026-09-18/tools/ta4win.m
cp -R <任一 build>/saves/world /tmp/ta4_saves_fixture    # 夹具是本机临时物，未进仓库
D=docs/qa/T-A4-2026-09-18
bash $D/tools/run_evidence.sh build $D 00_before42 batch1
bash $D/tools/run_evidence.sh build $D 01_after60  batch2
python3 $D/tools/pixel_diff.py $D/00_before42.png $D/01_after60.png
python3 $D/tools/verify_tiles.py assets/blocks
python3 $D/tools/region_tool.py get build/saves/world/region/r.0.-1.ocr 6 200 -14
```

## 5. 与首批（T-A3）的对照

同一条「不得出现 4 px 以上实心色块」判据（实现为"不存在 5×5 同色正方形"）跑在两批上：

| 批次 | 最大同色正方形 |
|---|---|
| 首批 42 张（已验收） | 玻璃 11 / 8 / 7、水 6 / 5；**不透明颗粒类全部 ≤ 4** |
| 本卡 18 张 | **全部 ≤ 4**（最差 sandstone 顶/底 = 4） |

同一条「孤立点」（四邻域无同色）统计：首批中位数 48、最大 72；本卡除 snow_block（0）
与 bedrock（2–3）外多在 16–102，与首批同量级 —— 本卡的孤立点全部是颗粒材质的正常噪点，
唯一的"单像素亮点"共 3 处（cobblestone/obsidian 每面 1 处），为规格 §4.4 明确允许的镜面反光。
