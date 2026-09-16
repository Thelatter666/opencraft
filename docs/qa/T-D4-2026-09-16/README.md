# T-D4 实机证据（区块流式加载与卸载策略 · 流式加载与卸载 + 请求形态）　2026-09-16

> 结论先说：**出视野的区块真的被卸载了，卸载前脏数据真的落了盘，走回头路时改动真的还在。**
> 全程用两套独立装置取证，互为交叉验证：
> **(A) 游戏自己的日志**（`stream: released … / chunk (cx, cz) loaded from disk / autosave: N chunk(s)`）；
> **(B) 绕开游戏直接读存档文件的独立工具**（`tools/save_read.cpp`，把存档里的方块与同种子 worldgen 对照）。
> B 是这张卡最重要判据（验收 2）的决定性证据：它不经过窗口、不经过输入、不经过渲染。

## 环境

- 机器：macOS 24.6.0（arm64，用户实机）；游戏窗口 1280×748 @ (320,89)（**窗口号按 PID 反查 + 尺寸过滤**）
- worktree：`/Users/happy/Desktop/opencraft_worktree/opencraft-T-D4`（分支 `task/T-D4-chunk-streaming`，提交 `5d49f3e`）
- 二进制：`build/opencraft`（Release），**md5 `c99cd27620bdfd14eacbd4b8e3dfc3b5`**
  - 取证期间工作树**无未提交源码改动**（`git status` 只有本目录未跟踪）⇒ 取证二进制与提交内容一致
- 世界：种子固定 `0x4F50454E43524146`，出生点 (0.5, 132.0, 0.5)（与 T-A1 一致，可复现）
- 注入：HID 层 `CGEventCreate*Event` + `CGEventPost(kCGHIDEventTap)`，工具源码 `tools/td4input.m`（**未用 osascript**）；
  点击显式带窗口中心坐标 (960,463)（`docs/05` §3.1 规则 3）

## 位移怎么造的（为什么不是"按住 W 走很远"）

卡面已预警：本机 HID 注入约 9 tick 后被失焦清掉，"按住 W 走很远"做不到，**改用改写存档坐标或传送**。
本轮实测再次确认：`tools/walk.py`（每 60 ms 重发 keyDown，并按日志 pos 判断是否到点）在 100 秒里
只把玩家挪动了不到 10 格、并反复被清掉 ⇒ **按住走这条路在本机不成立**（记录为方法学结论，非本卡缺陷）。

因此位移一律用卡面许可的**改写存档坐标**：`tools/level_patch.py` 重写 `level.ocd` 的玩家 x/y/z 与 yaw/pitch
（并重算 CRC-32；布局见 `storage/level_file.hpp`），游戏启动即落在指定位置。
它只改游戏自己写的存档文件，**不改产品代码**。

⚠ **另一条方法学教训（本轮踩到）**：`session.py` 的截图/点击会先把游戏窗口激活；
激活期间**机器上真人的键鼠输入会落进游戏**（第 6 次会话 C 里，玩家被真人操作走到 (-4,127,9) 并倒了一桶水）。
此后每个会话都加了 `session.py focus`：注入完立刻把前台交还 Finder，再等 10 s 自动存档窗口。
需要真人操作时**不要**与取证并行。

## 证据 A（验收 2 · 本卡最重要）：改三处 → 走远（300 格）→ 走回来 → 改动仍在

| 会话 | 动作 | 关键日志 | 截图 |
|---|---|---|---|
| p1 | 倒水 | `vessel: poured water source at (0, 132, -1)` | `p1_after_pour.png` |
| p2 | 放方块 | `placed stone at (1, 132, 0); consumed 1 from slot 0 (63 left)` | `p2_after_place.png` |
| p3 | 挖坑 | （挖掘无常驻日志，见截图的坑；`remeshed 4 chunk(s)`） | `p3_before_dig.png` / `p3_after_dig.png` |
| p4b | **走远**：x 0.5 → 300.5 | **`stream: released 25 chunk(s), 2 resident, 0 meshed`** | `p4b_far_away.png` |
| p5 | **走回来**：x 300.5 → 0.5 | `chunk (0, 0) loaded from disk` / `chunk (0, -1) loaded from disk`（共 7 个区块从盘上加载） | `p5_back_with_edits.png` |
| p7 | 回到编辑点旁边的固定机位 | 又 8 个区块 `loaded from disk` | `p7_edited.png` |

**决定性证据（装置 B：直接读存档文件）**——`automated_hid/ondisk_read.txt`：

```
chunk (0, 0): present in the region file
chunk (0, -1): present in the region file

cell                     file                        worldgen (same seed)     verdict
(   1,132,   0)   stone                       air                      PLAYER EDIT survives on disk
(   0,131,   0)   water + flowing water       grass_block              PLAYER EDIT survives on disk
(   0,132,  -1)   water + water source        air                      PLAYER EDIT survives on disk
(   0,132,   0)   water + flowing water       air                      PLAYER EDIT survives on disk
(   3,131,   3)   dirt                        dirt                     same as worldgen
(   3,140,   3)   air                         air                      same as worldgen
```

- (1,132,0)：**放的方块**在盘上是 `stone`，而同种子 worldgen 该格是 air ⇒ 只可能来自玩家放置。
- (0,131,0)：**挖的坑**在盘上已无 `grass_block`（worldgen 该格是 grass_block），且被随后倒的水灌满
  （水是玩家倒的 → 该格的水本身也是玩家改动）⇒ 挖掉的方块确实没回来。
- (0,132,-1)：**倒的水源**在盘上是 `water + water source`，worldgen 该格是 air ⇒ 只可能来自玩家倒水。
- 两行对照格（未编辑的 (3,131,3) / (3,140,3)）与 worldgen **完全一致** ⇒ 上面三行不是"文件整体不同"的假阳性。

**装置 A 与 B 交叉**：p5/p7 的 `loaded from disk` 说明这些区块是从盘上恢复的（而非 worldgen 重生成），
B 说明盘上确实存着改动 ⇒ "卸载前落盘、回头时恢复"这条链闭合。

**附带的物理旁证**：p5 里玩家回到出生点后日志显示 `pos (0.50, 131.00, 0.50)`（静止在 y=131），
而全新世界里出生点是 `pos (0.50, 132.00, 0.50)`（a0/a1/p0 各会话一致）——**玩家掉进自己挖的那一格**，
即"坑还在"由物理引擎自己证明了。

## 证据 B（验收 2 的视觉）：同一机位、同一种子，编辑过的世界 vs 全新世界

- `p7_edited.png` 与 `p8_pristine.png`：玩家都停在 (2.5, 133.00, 2.5)（两侧**同一 y**，
  因为机位离编辑点 2 格以上、脚下地形两世界相同），yaw 0.4636 / pitch 0.26 完全相同。
- 逐像素比对（`ab_mask.png`）：**16.28% 像素不同**，差异**只有两处**：
  1. 画面中下方多出**一块石头方块**（(1,132,0) 的放置）；
  2. 左下方草地被**水覆盖**（(0,132,-1) 倒出的水源顺坡摊开）。
  天空、山体、悬崖、远处水塘轮廓、HUD 逐像素一致 ⇒ 差异就是玩家那三处改动，没有别的副作用。
- 堆叠对照图：`ab_stack_pristine_top_edited_bottom.png`（上=全新世界，下=编辑过的世界）。

## 证据 C（验收 1 + 4）：走远后区块被卸载，内存/资源不再只增不减

```
[23:48:10] stream: released 25 chunk(s), 2 resident, 0 meshed     ← 玩家在 300 格外，出生点一带的区块被释放
[23:48:11] fps 41.9 | pos (300.50, 130.77, 0.50) | chunks 113 | stream-meshed 113
[23:48:17] fps 72.0 | pos (300.50, 118.00, 0.50) | chunks 113 | stream-meshed 113
```

- `released 25 chunk(s), 2 resident`：卸载**真的发生**，且释放后常驻只剩 2 个（正在流式加载中）。
- `chunks 113`（= 渲染资源表 `renderables` 的条目数）在 300 格外**仍是 113**（= 视距 6 的圆 ≈ π·6²），
  **不再单调增长**；被释放区块的 GPU 网格随卸载同帧清掉（验收 4）。
  （改造前该值只增不减：走 300 格会留下两百多个已成网格的区块。）

## 证据 D（验收 3 · 滞回）：在边界来回走不产生加载/卸载抖动

五个会话，玩家位置在 x=0.5 ↔ x=32.5（**2 个区块 = 32 格**）之间来回（`automated_hid/pacing_summary.txt`）：

| 会话 | x | `stream: released` 行数 | 从盘加载区块数 | fps |
|---|---|---|---|---|
| pace1_x0.5 | 0.5 | **0** | 8 | 72.5 |
| pace2_x32.5 | 32.5 | **0** | 8 | 72.5 |
| pace3_x0.5 | 0.5 | **0** | 8 | 72.0 |
| pace4_x32.5 | 32.5 | **0** | 8 | 72.0 |

- 来回两趟、单程 2 个区块：**一次释放都没有** ⇒ 没有"反复加载/卸载同一个区块"。
  （逻辑上闭合：区块离开内存的唯一途径就是释放；释放为 0 ⇒ 不存在被重新加载的同名区块。）
- 对照：同一世界里位移 300 格（p4b）立刻产生 `released 25 chunk(s)` ⇒ 卸载机制不是"从不触发"。

**滞回量的可断言边界（在单测里做，原因见下）**：`tests/test_chunk_streaming.cpp` 的三段用例
——2 区块来回 8 次零加载零卸载；无滞回配置下同样节奏立刻抖动（对照）；越界后照常释放。
⚠ 实机上**做不到**"越界释放"的量化对照：本机位移只能靠改写存档（卡面许可），
而**新会话的常驻集合只含"启动 5×5 + 玩家自己的生成窗口"**，所以新会话里没有"身后的旧区块"可释放
（`pace5_x64.5` 这一行 released = 0 就是这个原因，不是缺陷）。实机可观测到的释放，
必然发生在"同一会话内离开本会话加载过的区域"，即 p4b 那种 300 格位移。
要拿到"连续走动中逐次释放"的实机日志，需要 HID 长按可用的机器或真人操作（见文首方法学说明）。

## 证据 E（三处维护性调用已改请求形态）

```
$ grep -rn "ensure_chunk\|unload_chunk\|autosave_pass" game/client/ | wc -l
0
```

- 客户端现在只发 `authority.stream(client::make_stream_request(...))`：
  每帧流式（带 `kGenPerFrame` 预算）、tick 的自动存档窗口（`persist = true`）、退出 flush（`persist = true`）、
  启动 5×5（一次性预算 25）。
- 日志里的持久化窗口走的正是这条新路：`autosave: 4 chunk(s) queued for async write`（p1，倒水后水流扩散污染 4 个区块）
  / `autosave: 1 chunk(s) …`（p2 放置；p3 挖坑）——**数值来自 `StreamResult::persisted_chunks`**。

## 证据 F（fps 与无网络）

- fps：各会话逐秒读数在 **72.0–88.6**（末次读数多为 72.0），会话首帧 41–46 是首帧加载。
  卡面给的当前基线是 53–72 fps ⇒ 处于同量级或更好。**注意**：本机帧率疑似被 vsync 上限锁住
  （144 Hz 显示器的 1/2 = 72），因此 fps 对小幅回归不敏感，本卡未做基线二进制 A/B（见报告 §7.3）。
- 无网络：`engine/net/` 仍只有 CMakeLists.txt（无源文件）；`game/`+`engine/` 下
  `sys/socket|arpa/inet|netinet` 出现 0 次。

## 工具（`tools/`，均可离线复跑）

| 文件 | 作用 |
|---|---|
| `td4input.m` | HID 注入（激活/取窗口/按键脉冲/点击/鼠标增量/光标就位），`clang -framework AppKit` 构建 |
| `session.py` | 启动游戏并重定向日志、按窗口号截图、点击、按键、交还前台、杀进程 |
| `level_patch.py` | 读/写 `level.ocd` 玩家字段（位移与朝向），含 `--create` 从零造存档；重算 CRC-32 |
| `walk.py` | 每 60 ms 重发 keyDown 的长按尝试（本轮证明**本机不可靠**，留作复现证据） |
| `save_read.cpp` / `save_read` | **独立读存档并对照 worldgen**（证据 A 的装置 B）；构建命令见报告 §5 |

## 目录内容

- `automated_hid/`：
  - 会话日志 16 份（第一轮 a0–a3 + b；第二轮 p0–p8；滞回 pace1–pace5），截图 21 张
  - `ondisk_read.txt`（装置 B 输出）、`pacing_summary.txt`（验收 3 四会话 + 说明）
  - `ab_mask.png` / `ab_stack_pristine_top_edited_bottom.png`（证据 B 的逐像素差异与堆叠对照）
  - `saves_after_round1/`（第一轮取证结束时磁盘上的世界）、`saves_edited_snapshot/`（第二轮结束时磁盘上的世界，
    含 region 文件——装置 B 就是读它得到证据 A 的）
- `tools/`：`td4input.m`、`session.py`、`level_patch.py`、`walk.py`、`save_read.cpp`（+ 已构建的 `save_read`）

> 第一轮（a0–b）是自动化 HID 的早期取证：验证了"放方块 → 自动存档写 1 区块 → 下一会话从盘加载"的链路
> 和 `stream: released 25 chunk(s)`；第二轮（p0–p8）在同一装置上补齐了"三处改动 + 走远 + 走回 + 独立读盘"。
> 第一轮的会话 c 被真人输入污染（玩家被演示操作走到 (-4,127,9) 并倒水），其日志未归档。
