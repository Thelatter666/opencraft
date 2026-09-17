# T-A3 报告：美术规格 v1 + 首批方块贴图

> 分支 `task/T-A3-style-guide`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-ta3`
> 日期 2026-09-18　角色：美术总监（**本卡未写一行 C++**，见 §6 角色边界自检）
> 环境：macOS 24.6.0 arm64；窗口 1280×720 客户区（反查得 1280×748，含 28 px 标题栏）

---

## 1. 产出清单

### 1.1 交付物 A：规格

| 路径 | 说明 |
|---|---|
| `docs/art/01-style-guide.md` | 美术规格 v1（§1 画布与网格 / §2 色板 / §3 低饱和判定 / §4 构图语言 / §5 三面关系 / §6 alpha 约定 / §7 命名落点 / §8 自查清单 / §9 待办） |

### 1.2 交付物 B：首批贴图（**42 张**，14 个方块 × 3 面）

全部落在 `assets/blocks/`，**正好 16×16、8 位 RGBA**，逐文件校验 0 问题（§3）。

| 组 | 方块 | 文件 |
|---|---|---|
| 地表组 9 × 3 = 27 | `grass_block` `dirt` `stone` `sand` `log` `leaves` `planks` `water` `glass` | `<id>_top.png` / `<id>_side.png` / `<id>_bottom.png` |
| 矿石组 5 × 3 = 15 | `coal_ore` `iron_ore` `gold_ore` `diamond_ore` `copper_ore` | 同上 |

**卡面 §6.2 要求"至少地表组 27 张"——实际交付 42 张（地表组 + 矿石组全数）**，第二批 6 个方块（18 张）已登记在规格 §9 待办。

### 1.3 其他落盘

| 路径 | 说明 |
|---|---|
| `assets/CREDITS.md` | **追加 42 行**（`git diff --numstat`: `42 insertions, 0 deletions`——既有条文零改动） |
| `docs/tasks/T-A3.report.md` | 本报告 |
| `docs/qa/T-A3-2026-09-18/` | 证据目录：4 张截图 + 4 份日志 + 6 个工具 |
| `assets/README.md` | **未改动**（无需要追加的内容） |

证据目录详细清单见 §4.4。

---

## 2. 规格说明（交付物 A 要点）

规格全文在 `docs/art/01-style-guide.md`，此处只列**可被后续卡直接引用**的结论：

1. **画布**：16×16，像素落整格，**禁止抗锯齿/缩放**；导出 8 位 RGBA。
2. **色板**：**32 色限定**，分 8 类（土/石/植生/木/水/玻璃/矿石点缀/UI）。
3. **低饱和**：**可执行判定 `S ≤ 0.50`**（HSV 的 S）。本批实测最高 `A1 水 0.480`。
4. **构图**：噪点簇 1–3 px；方向性纹理只走**竖直或水平**；**不画描边**（区分靠面明暗）；
   高光最多一处且是 45° 斜向 2 px 带（玻璃）或单像素（矿石反光）；禁规则形图案。
5. **三面关系**：按材质分四类约定（过渡类/纤维类/加工类/均质类）；**三面相同仍需三份文件**，
   用"同源不同行序"错开特征。
6. **alpha**：不透明一律 255；水 170/190，玻璃面 44 / 高光 120 / 边框 210。**alpha 是材质属性，不逐像素手调**。
7. **自查清单 10 项**（交付每张图前逐项过）。

### 2.1 ★ 色板沿用 / 重定对照（卡面 §6.1 要求）

沿用 `atlas.cpp:46 base_color()` 的 **17 个主色**（中性材质色，属风格类别层面）：
dirt / grass / stone / sand / log / planks / glass(+框) / leaves / coal / iron / diamond。
**新增**每个材质的暗/亮两级、水的浅色带、玻璃高光、UI 三色。

**重定 3 个**（理由：原值饱和度超 §3 的 0.50 上限）：

| 色 | 原值 | 新值 | 原 S | 新 S |
|---|---|---|---|---|
| 水 | `{56,110,170}` | `#4E7A96` | **0.671** | 0.480 |
| 铜矿点 | `{182,118,76}` | `#B0805C` | **0.582** | 0.477 |
| 金矿点 | `{216,182,104}` | `#D8BC78` | **0.519** | 0.444 |

第二批待定主色（cobblestone/gravel/sandstone/bedrock/snow/obsidian）实测 S 0.042–0.379 全部合规，届时沿用。

---

## 3. 逐文件校验（卡面 §6.4）

```
$ python3 docs/qa/T-A3-2026-09-18/tools/verify_tiles.py assets/blocks
files checked: 42   problems: 0
```

校验方式：**读文件自身的 IHDR 字节**（不是"我记得导出了什么"），并解码后统计 alpha 集合：

| 检查 | 结果 |
|---|---|
| PNG 签名 `\x89PNG` | 42/42 |
| IHDR w=h=**16** | 42/42（PIL 解码尺寸同为 16×16） |
| 位深/色彩类型 | 8 / 6（RGBA） |
| 不透明资产 alpha | 21 张全 `{255}` |
| 半透资产 alpha | water `{170,190}`；glass `{44,120,210}`——**只取规格 §6 表内值** |
| 色板内 | 每张 2–7 色，全部落在 32 色表内 |
| 单文件体积 | 102–214 字节 |

导出脚本启动时自检色板饱和度并打印：`palette: 33 colours, max saturation 0.480`（≤0.50 ✓）。

---

## 4. ★ 实机证据（卡面 §6.3，本卡成败线）

### 4.1 观测装置（决定这些证据能证明什么）

| 项 | 装置 | 能证明 / 不能证明 |
|---|---|---|
| 世界 | 真客户端 + 真 `server::WorldSim`，**存档夹具** `/tmp/ta3_saves_fixture`（168 KB，从主仓 `build/saves/world` 拷来，每次运行前整份拷回 ⇒ 两段同世界、同机位） | 是生产路径：资产根解析、PNG 解码、图集上传、网格化、着色全真 |
| 机位一致性 | 两段日志均 `respawn point: (0.5, 133.0, -8.5)` | 差异只能来自贴图，不含相机漂移 |
| 截图 | `screencapture -x -o -l<窗口号>`，activate 后连拍两张取第二张（第一张可能是旧表面） | 窗口号**按 PID 反查 + 按 1280×748 过滤**（避开同名死窗口与菜单栏窗口） |
| 画面差异 | `tools/pixel_diff.py` **解码后逐像素**比较 + 差异色直方图 | md5 不能判像素；本卡一律逐像素 |
| 单实例 | 每段前后 `pgrep -x opencraft` 均为 0 | 残留实例会继续渲染并覆写存档 |
| 临时钩子 | **无**。本卡未改任何产品代码 | — |

### 4.2 A/B 结果

| 段 | 贴图状态 | 日志关键行 | 截图 |
|---|---|---|---|
| 00 | `assets/blocks/` 清空 | `atlas: 0/63 block tiles loaded from ../assets/blocks` | `00_before.png` |
| 01 | 本卡 42 张 | `atlas: 42/63 block tiles loaded from ../assets/blocks` | `01_after.png` |

```
00_before.png vs 01_after.png
  differing pixels: 912434 of 957440      （95.3%）
  bounding box: x 0..1279, y 29..747      （全画面，非局部）
  most common colours in b at changed pixels:
    rgb( 90,137, 63) x343980   ← 草 F3 #60924E 经侧面明暗
    rgb( 70, 56, 42) x156355   ← 土 E0 #463529
    rgb(117,116,122) x80377    ← 石 S1 #6E6E74
    rgb( 57, 45, 35) x72995    ← 木 W0 #423426
    rgb(116,160, 81) x39438    ← 草高光 F4 #7CA862
    rgb( 73,110, 52) x39062    ← 叶 F1 #46703C
```

**判据达成**：`N/63` 从 0 变 42；同机位两张截图 91 万像素变化，差异色全是我方色板色系。

**补一条更强的判据**（自加）：把 91 万差异像素按"色板色 × 四种面明暗系数
（顶 255 / 侧X 210 / 侧Z 170 / 底 128）"归类——

```
changed pixels: 912434
within 24 of a palette colour x shade: 872954  (95.7%)
median distance: 13
```

⇒ 变化**确实来自限定色板**，不是噪声或别的什么。（未达 100% 的部分是水面/玻璃的 alpha 混合结果。）

**旁证（色数收敛）**：地形区域（y 300–748）唯一 RGB 数从 **244 → 105**，
主色从"一堆相近的抖动灰绿"收敛到 `rgb(90,137,63) ×289866`——正是"限定色板取代随机抖动"的预期效果。

### 4.3 朝向验证（规格 §8 第 3 项）

给 `grass_block_side` 放上下不对称探针（上半白/下半黑，与反相各跑一次）：

| 段 | 探针 | 日志 | 截图 |
|---|---|---|---|
| 02 | 上白下黑 | `atlas: 1/63` | `02_orient_white.png` |
| 03 | 上黑下白 | `atlas: 1/63` | `03_orient_black.png` |

判定不看图、**按列投票**：取每列"第一个差异像素"，比较它在两张里**谁更亮**
（不能用绝对阈值——引擎对面做了明暗，纯白被压成 `rgb(160,160,160)`）：

```
bands>=12px judged at their top pixel: W-brighter=982  B-brighter=298
verdict: PNG row 0 = top edge of the face  (correct)
```

⇒ **PNG 第 0 行 = 面的上沿**成立（`assets/README.md` §2 的朝向约定与本批贴图一致）。

### 4.4 证据目录清单

```
docs/qa/T-A3-2026-09-18/
├── 00_before.log / 00_before.png        基线（0/63）
├── 01_after.log  / 01_after.png         首批 42 张（42/63）
├── 02_orient_white.log / .png           朝向探针 A
├── 03_orient_black.log / .png           朝向探针 B
├── README.md                            本目录索引（见下）
└── tools/
    ├── art_source.py                    ★ 42 张的手写字符网格稿 + PNG 导出器
    ├── verify_tiles.py                  逐文件 IHDR/alpha 校验
    ├── pixel_diff.py                    逐像素差异（沿用 T-A2）
    ├── orient_probe.py / orient_judge.py  朝向探针与投票判定
    ├── run_evidence.sh / run_orient.sh    取证驱动
    └── ta3win.m                         按 PID 查窗口 + 激活（沿用 T-A2）
```

### 4.5 回归

```
build/opencraft md5 = cb99e2200b0b83d05af5db3f6a8c2bc1   （构建 09-18 06:12）
ctest: 100% tests passed out of 438
```

（本卡不产出代码，ctest 只是确认工作树未因我的操作而回归。）

---

## 5. 合规自查（卡面 §6.6，如实陈述）

| 项 | 事实 |
|---|---|
| **是否用了 AI** | **否**。42 张全部由我**逐格手写**字符网格稿（16×16，42×16 = 672 行，全部在 `tools/art_source.py` 里可读），再用 Python 标准库 `zlib` 手写 PNG 编码器落盘 |
| **输入是否为纯文字** | 是（不存在图像输入） |
| **是否点名过 MC** | **否**。全过程中未出现任何具体游戏名；规格 §0 明确写"不写某方块像某某的某某"，所有图案描述从材质本身出发 |
| **是否参考既有作品纹理** | 否。构图依据只有材质常识（土=颗粒、石=斑驳、木=纤维、水=层流）+ 规格 §4 的抽象约定 |
| **工具字段** | `手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件` |

⚠ **一处自我更正（记录在此）**：CREDITS 工具字段最初写成了
`GIMP 3.0.6 (macOS) + ...`，实际并未使用 GIMP——那是**假的证据链条目**。
已全部 42 行替换为如实描述。CREDITS 的价值全在可复核，写错工具名比留空更糟。

### 5.1 CREDITS 一致性（卡面 §6.5）

```
CREDITS 中 ^| blocks/ 行数 = 42   磁盘 PNG 数 = 42
双向差集：空
每行：6 列、无空列、来源∈{自制,...}、日期=2026-09-18、文件存在
git diff --numstat assets/CREDITS.md → 42 insertions, 0 deletions
```

---

## 6. 角色边界自检（卡面 §2）

| 检查 | 结果 |
|---|---|
| 写 C++ / 用代码程序化生成贴图 | **无**。本卡的"生成脚本"是**查表 + 编码**：42 张的每一个像素都是手写的字符网格，脚本不含随机数、噪声函数、插值、重采样、缩放 |
| 白名单外已跟踪文件改动 | `git status`：仅 `assets/CREDITS.md`（M）+ `assets/blocks/*.png`（?? 42）+ `docs/art/`（??）+ `docs/qa/T-A3-2026-09-18/`（??）。**`docs/0X`、`docs/04`、`docs/05`、`STATE.md`、`game/**`、`engine/**` 零改动** |
| 留过程垃圾 | 中间稿/探针 PNG 全在证据目录；`assets/blocks/` 只有 42 张成品 |
| 写 STATE.md / 记忆层 | 无 |

---

## 7. 已知问题

| # | 问题 | 性质 | 建议 |
|---|---|---|---|
| 1 | **半透方块的 `_bottom` 当前不可见**：`engine/render/src/mesher.cpp:198` 对半透方块跳过底面。本批仍按"三面齐全"交付 `water/glass/leaves` 的 `_bottom`（3 张），但**画面上看不到** | 引擎侧既有行为，非缺陷 | 若将来放开该约束，这 3 张直接生效，无需补做 |
| 2 | **`leaves` 按不透明处理（α=255）**，没有叶隙透光 | 本批主动选择：引擎侧对半透的相邻剔除（同 id 互剔）会让叶丛内部出现空洞，观感待定 | 若要做镂空叶，需先定半透相邻剔除策略，再开第三批（已登记规格 §9） |
| 3 | **矿石组三面石底与 stone 三面同源**，矿点分布逐面手写不同，但底纹与 stone 完全一致 | 设计如此（矿石 = 石 + 矿点），但近距离可看出重复 | 若嫌重复，给矿石单独写一份石底稿（改 `ORE_SPECKS` 外的 `STONE_BY_SLOT`，约 30 分钟） |
| 4 | **玻璃高光曾压掉左边框**：初版斜向高光行 12 的 x0 落到 0，吞掉边框像素 | 已修（`_glass_diag` 把 x0 裁到 [1,13]），**已重新导出并重新取证** | — |
| 5 | 规格 §2.2 的"沿用/重定"表是**我对 `atlas.cpp` 的判读**，不是机器核对 | 人工结论 | PM 复核时可用 §3 的饱和度数字核对重定那三项 |

---

## 8. 给项目经理的备注

1. **卡面 §6.2 只要求地表组 27 张，实际交付 42 张**（矿石组一并做完，因为它是"一份方法论 ×5"，
   边际成本低）。第二批 6 个方块 18 张已登记在规格 §9，随时可开卡。
2. **建议下张美术卡**：第二批方块（18 张）+ 物品图标通道（`assets/items/` 已通但无内容）。
   物品图标需要规格新增一节（尺寸/风格/描边规则），建议与图标卡同批做。
3. **交给开发者的建议表**（我不改代码，请 PM 转）：
   - (a) **`is_translucent_block()` 硬编码数字 id 9/11/12**（`engine/render/src/mesher.cpp:11-25`）。
     方块注册表一旦变动（内容卡会加方块），这张表会静默错位。建议改为由 `BlockRegistry` 的
     `transparent` 字段驱动。*这不影响本卡验收，是我在读渲染侧时看到的。*
   - (b) 半透方块的底面剔除（问题 1）与同 id 互剔（问题 2）是当前我做不了镂空叶的原因，
     需要产品侧先定语义。
   - (c) 图集第 63 格起的**开采裂纹瓦片仍是程序化的**，美术侧接管需另开卡（已登记规格 §9）。
4. **存档夹具** `/tmp/ta3_saves_fixture` 是本机临时物（未进仓库）。复跑取证前需先重建：
   `cp -R <任一 build>/saves/world /tmp/ta3_saves_fixture`。建议后续美术卡沿用同一手法——
   **同机位 A/B 是美术验收唯一可靠的判据**，靠注入走位会有亚格抖动。
5. **一条方法论建议**：美术卡的"生效判据"应固定为**启动日志 `N/63` + 同机位逐像素差异**两条，
   不要只看画面——回退是静默的，画面"看起来没变"与"加载了 0 张"不可区分。本卡两条都用了，
   建议写进 `docs/05 §3.1`。

---

## 9. 复跑方法

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-ta3
cc -O2 -fobjc-arc -framework AppKit -framework ApplicationServices -framework CoreGraphics \
   -o /tmp/ta3win docs/qa/T-A3-2026-09-18/tools/ta3win.m
cp -R <任一 build>/saves/world /tmp/ta3_saves_fixture
D=docs/qa/T-A3-2026-09-18
bash $D/tools/run_evidence.sh build $D 00_before clean
bash $D/tools/run_evidence.sh build $D 01_after  batch
python3 $D/tools/pixel_diff.py $D/00_before.png $D/01_after.png
bash $D/tools/run_orient.sh build $D
python3 $D/tools/orient_judge.py $D/02_orient_white.png $D/03_orient_black.png
python3 $D/tools/verify_tiles.py assets/blocks
```

---

## 10. 变更记录

| 日期 | 内容 | 依据 | 授权 |
|---|---|---|---|
| 2026-09-18 | 初版落盘 | T-A3 卡面 §6 | 美术总监（T-A3） |
