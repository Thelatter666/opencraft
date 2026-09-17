# T-A3 实机证据　2026-09-18

美术规格 v1 + 首批方块贴图（地表组 9 + 矿石组 5，共 14 方块 × 3 面 = 42 张 16×16 PNG）。

> 分支 `task/T-A3-style-guide`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-ta3`
> 环境：macOS 24.6.0（arm64）；窗口 `1280×720` 客户区（按 PID 反查得 `1280×748`，含 28 px 标题栏）
> 单实例：每段取证前后 `pgrep -x opencraft` 均为 0（脚本内建 kill + 复检）
> 启动方式：`cd build && ./opencraft`（资产根落在 `../assets`）
> 二进制：`build/opencraft` md5 `cb99e2200b0b83d05af5db3f6a8c2bc1`（构建 09-18 06:12）；`ctest` 438/438
> 临时钩子 / 探针代码：**无**（本卡未改任何产品代码；探针只是放进 `assets/blocks/` 的 PNG）

## 0. 机位一致性装置

存档夹具 `/tmp/ta3_saves_fixture`（168 KB，从主仓 `build/saves/world` 拷来）。
`run_evidence.sh` / `run_orient.sh` 每段开头 `rm -rf build/saves && cp -R 夹具 build/saves`，
⇒ 四段跑的是**同一份世界、同一个出生点与朝向**。四段日志均：

```
respawn point: (0.5, 133.0, -8.5)
```

客户端退出时才落盘，故每次复位就足以消除"世界漂移/玩家走位"这个混淆变量。

## 1. 四段会话

| 段 | 场景 | 贴图 | 日志关键行 | 截图 |
|---|---|---|---|---|
| 00 | 基线：`assets/blocks/` 清空 | 无 | `atlas: 0/63 block tiles loaded from ../assets/blocks` | `00_before.png` |
| 01 | 本卡首批 42 张 | 42 张 | `atlas: 42/63 block tiles loaded from ../assets/blocks` | `01_after.png` |
| 02 | 朝向探针 A：`grass_block_side` = 上白下黑 | 1 张 | `atlas: 1/63 block tiles loaded` | `02_orient_white.png` |
| 03 | 朝向探针 B：`grass_block_side` = 上黑下白 | 1 张 | `atlas: 1/63 block tiles loaded` | `03_orient_black.png` |

日志文件与截图同名，如 `00_before.log`。

### 1.1 A/B 判据（00 → 01）

```
00_before.png vs 01_after.png
  differing pixels: 912434 of 957440      （95.3%）
  bounding box: x 0..1279, y 29..747
  most common colours in b at changed pixels:
    rgb( 90,137, 63) x343980   ← 草 F3 #60924E 经侧面明暗
    rgb( 70, 56, 42) x156355   ← 土 E0 #463529
    rgb(117,116,122) x80377    ← 石 S1 #6E6E74
    rgb( 57, 45, 35) x72995    ← 木 W0 #423426
    rgb(116,160, 81) x39438    ← 草高光 F4 #7CA862
    rgb( 73,110, 52) x39062    ← 叶 F1 #46703C
```

**自加的更强判据**：把差异像素按"色板色 × 四种面明暗系数（顶 255 / 侧X 210 / 侧Z 170 / 底 128）"归类：

```
changed pixels: 912434
within 24 (Σ|ΔRGB|) of a palette colour x shade: 872954  (95.7%)
median distance: 13
```

未达 100% 的部分是水/玻璃的 alpha 混合结果。**旁证**：地形区域（y 300–748）唯一 RGB 数
`244 → 105`，主色收敛到 `rgb(90,137,63) ×289866`——即"限定色板取代随机抖动"。

### 1.2 朝向判据（02 ↔ 03）

不看图、按列投票：每列取"第一个差异像素"，比较它在两张里**谁更亮**。
（不能用绝对阈值判黑白：引擎对面做了明暗，纯白被压成 `rgb(160,160,160)`。）

```
bands>=12px judged at their top pixel: W-brighter=982  B-brighter=298
verdict: PNG row 0 = top edge of the face  (correct)
```

## 2. 工具

| 文件 | 作用 |
|---|---|
| `tools/art_source.py` | ★ **42 张的手写字符网格稿**（16 行 × 16 字符 × 42）+ 查表 + 手写 PNG 编码。**无随机数/噪声/插值/缩放** |
| `tools/verify_tiles.py` | 读 IHDR 字节核 16×16，解码后核 alpha 集合（不透明必须全 255） |
| `tools/pixel_diff.py` | 逐像素差异 + 差异色直方图（沿用 T-A2；md5 不能判像素） |
| `tools/orient_probe.py` / `orient_judge.py` | 朝向探针生成与投票判定 |
| `tools/run_evidence.sh` / `run_orient.sh` | 取证驱动（复位夹具 → 布置贴图 → 启动 → 等 atlas 行 → 激活 → 连拍两张取第二张 → kill + 复检 → 还原） |
| `tools/ta3win.m` | 按 PID 反查窗口 + 激活（沿用 T-A2 的 `ta2win.m`） |

## 3. 复跑

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-ta3
cc -O2 -fobjc-arc -framework AppKit -framework ApplicationServices -framework CoreGraphics \
   -o /tmp/ta3win docs/qa/T-A3-2026-09-18/tools/ta3win.m
cp -R <任一 build>/saves/world /tmp/ta3_saves_fixture     # 夹具是本机临时物，未进仓库
D=docs/qa/T-A3-2026-09-18
bash $D/tools/run_evidence.sh build $D 00_before clean
bash $D/tools/run_evidence.sh build $D 01_after  batch
python3 $D/tools/pixel_diff.py $D/00_before.png $D/01_after.png
bash $D/tools/run_orient.sh build $D
python3 $D/tools/orient_judge.py $D/02_orient_white.png $D/03_orient_black.png
python3 $D/tools/verify_tiles.py assets/blocks
```
