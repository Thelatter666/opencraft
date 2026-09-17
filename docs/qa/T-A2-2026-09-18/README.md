# T-A2 实机证据　2026-09-18

美术资产管线（把"贴图"从代码里解耦出来）。

> 分支 `task/T-A2-asset-pipeline`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-ta2`
> 环境：macOS 24.6.0（arm64）；窗口 `1280×720` 客户区（反查得 `1280×748`，含 28 px 标题栏）
> 单实例：每段取证前后 `pgrep -x opencraft` 均为 0（`run_probe.sh` 内建 kill + 复检）
> 启动方式：`cd build && ./opencraft`（即 `saves/` 与 `assets/` 的实际运行时约定）

## 0. 二进制度量（证据 = 交付同一份构建）

| 项 | 值 |
|---|---|
| 取证用 `build/opencraft` md5 | `77ad679ebdcac135baac99196bd8f978` |
| 该二进制的构建时间 | 04:34:00（`asset_atlas.cpp.o` / `atlas.cpp.o` 04:33:58，均**晚于** clang-format 后的源码 04:29:46） |
| 截图时间 | 04:34:14 / 04:34:26 / 04:34:38（全部在最后一次构建之后） |
| 临时钩子 / 探针 | **无**。本卡没有为实现取证改过任何产品代码：探针只是放进 `assets/blocks/` 的 PNG 文件，改完即删 |

## 1. ★ 观测装置（决定这些证据能证明什么）

| 项 | 装置 | 能证明 / 不能证明 |
|---|---|---|
| 世界 | 真客户端 + 真 `server::WorldSim`，存档 `build/saves/world`（首次运行新建，之后各段复用同一份） | 是生产路径：`assets/` 的解析、PNG 解码、图集上传、网格化、着色都是真的 |
| 探针贴图 | `tools/make_probe_png.py`（**只用 zlib 手写 PNG**，不含任何图像库） | 与 `stb_image` 解码器无共同代码路径，探针不可能是"按解码器的脾气造的" |
| 截图 | `screencapture -x -o -l<窗口号>`，`activate`(NSRunningApplication + AX raise) 后连拍两张取第二张 | ⚠ 本机对非前台窗口会返回旧表面（docs/05 §3.1 第 10 条）；窗口号**按 PID 反查 + 按 1280×748 过滤**（同名死窗口会拿错号，菜单栏窗口会被尺寸过滤掉） |
| 画面差异 | `tools/pixel_diff.py`：解码两张图**逐像素**比较并给出差异像素的颜色直方图 | md5 只说明文件是否相同；"哪里变了、变成什么颜色"必须解码像素才能说 |
| 相机一致性 | 每一段都加载**同一份存档**，玩家落在同一出生点、朝向同一方向 | 差异只能来自贴图（本次比较中没有 mob/粒子差异混入：差异像素颜色全部是探针色系） |

## 2. 判据：空目录等价（本卡成败线）

**同一份 dumper 源码**（`/tmp` 编译，绕开 CMake），分别链接**改动前**（`bc20aab` 的
`atlas.cpp`）与**改动后**（本卡最终源码）编译出的图集生成器，在 worktree 根目录运行
（`assets/blocks/` 为空）：

```
old md5: 7d537e85c3f04f4a92cc2a01d532e433    ← 改动前
new md5: 7d537e85c3f04f4a92cc2a01d532e433    ← 改动后
cmp: byte-identical, 20736 pixels (144×144)
```

逐瓦片 FNV-1a 64 摘要（81 个格子，含 8 个背景格）也逐一相同：
`/tmp/ta2_baseline/baseline_digest.txt` 与 `after_clean.txt` 的 md5 都是
`6f6fc963272d8b7b39e96aba3494d1da`。

实机侧：删掉整个 `assets/` 目录后截的图像与"目录在但为空"**逐像素相同（0 / 957440）**，
见 §3.3 的 03 段。可复核的原始产物在 `atlas_equivalence/`（两个版本的摘要表 + 全像素
md5 + 复跑脚本）。

## 3. 五段会话

| 段 | 场景 | 探针 | 日志关键行 | 截图 |
|---|---|---|---|---|
| 00 | 基线（`assets/blocks/` 为空） | 无 | `atlas: 0/63 block tiles loaded from ../assets/blocks` | `00_pristine.png` |
| 01 | 卡面示例：泥土侧面纯品红 | `dirt_side.png` = 品红 | `atlas: 1/63 block tiles loaded` | `01_magenta_dirt.png` |
| 02 | 朝向证明：草方块侧面**上品红/下白** | `grass_block_side.png` = 两段色 | `atlas: 1/63 block tiles loaded` | `02_orient_grass_side.png` |
| 03 | 整个 `assets/` 目录被移走 | 无（目录不存在） | `atlas: no assets/ tree found; every block tile is procedural` | `03_no_assets_dir.png` |
| 04 | 32×32 非法尺寸被拒 | `dirt_side.png` = 32×32 品红 | `[warning] asset tile ../assets/blocks/dirt_side.png is 32x32 px, expected 16x16; painting the procedural texture instead` | `04_reject_32x32.png` |

### 3.1 加载生效 + 只影响该瓦片（01 段，判据 §6.3）

```
00_pristine.png vs 01_magenta_dirt.png
  differing pixels: 16125 of 957440
  bounding box: x 0..798, y 268..485
  most common colours in b at changed pixels:
    rgb(153,  0,164) x14667      ← 品红系（侧面 shade 后）
    rgb(180,  0,193) x608
    rgb(130,  0,140) x530
    rgb(195,  0,209) x301
    rgb(166,  0,178) x13
    rgb(142,  0,153) x6
```

**16 125 个差异像素全部是品红系（六项相加正好等于总数）**：泥土的侧面变成品红，
其余方块、其余面、草丛、水、石头、HUD 一个像素没动。

### 3.2 朝向（02 段，超出卡面要求的一条）

草方块侧面在改动前是"上绿边 + 下泥土"；把 `grass_block_side.png` 换成
**上 8 行品红 / 下 8 行白**后，画面里每一面草方块侧面都是**上品红、下白**
（`02_orient_grass_side.png`），而草方块**顶面仍是绿色**。⇒ PNG 的第 0 行贴在了
面的上沿，`asset_atlas.cpp` 里的上下翻转是对的（这一条在单测里也有断言，
但只有截图能证明"网格化→着色器→屏幕"整条链上的朝向）。

```
differing pixels: 388736 of 957440
  rgb(153,  0,164) x135125     ← 品红（侧面上半）
  rgb(195,  0,209) x131393     ← 品红（另一种光照/朝向）
  rgb(160,160,160) x68898      ← 白（侧面下半，被光照压暗）
  rgb(204,204,204) x43726
```

### 3.3 缺目录不崩（03 段，判据 §6.5）

`mv assets /tmp/ta2_assets_backup` 之后启动，日志：

```
OpenCraft 0.1.0 starting
respawn point: (0.5, 132.0, 0.5)
atlas: no assets/ tree found; every block tile is procedural
atlas: 144x144 px, 9 tiles/row, crack tiles at 63
```

正常进入游戏并渲染。与 00 段基线**逐像素相同**：

```
00_pristine.png vs 03_no_assets_dir.png
  differing pixels: 0 of 957440
```

### 3.4 非法尺寸静默降级（04 段，判据 §6.4 的实机侧）

放 `32×32` 的 PNG：日志恰好 **1 条 warning**（写明文件名与尺寸），加载计数 `0/63`，
画面与基线**逐像素相同（0 / 957440）**——既不缩放也不崩溃。

## 4. 单测（不依赖 GUI）

```
100% tests passed out of 438        # 存量 422 + 本卡新增 16
```

存量测试文件零改动（`tests/` 下除 `CMakeLists.txt` 外没有文件被修改）。

## 5. 复跑方法

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-ta2
cc -O2 -fobjc-arc -framework AppKit -framework ApplicationServices \
   -framework CoreGraphics -o /tmp/ta2win docs/qa/T-A2-2026-09-18/tools/ta2win.m
D=docs/qa/T-A2-2026-09-18
bash $D/tools/run_probe.sh build assets $D 00_pristine
bash $D/tools/run_probe.sh build assets $D 01_magenta_dirt dirt_side=magenta
bash $D/tools/run_probe.sh build assets $D 02_orient_grass_side grass_block_side=orient
bash $D/tools/run_probe.sh build assets $D 04_reject_32x32 dirt_side=size32
# 03 段需要临时移走 assets/：见会话表；跑完记得把探针 PNG 从 assets/blocks/ 删掉
python3 $D/tools/pixel_diff.py $D/00_pristine.png $D/01_magenta_dirt.png
```

`run_probe.sh` 每次都会：杀掉残留实例并复检 → 清空 `assets/blocks/*.png` →
生成探针 → `cd build` 启动 → 等客户端的 `atlas:` 行 → 再等 7 s 让区块网格化稳定 →
按 PID+尺寸取窗口号 → 激活 → 连拍两张取第二张 → 杀进程并复检。
