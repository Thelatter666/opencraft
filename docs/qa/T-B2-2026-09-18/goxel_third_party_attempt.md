# T-B2 · 用第三方工具复核 `mossback.vox` —— 尝试记录（未达成）

**结论：这次没能拿到第三方对 `.vox` 的独立验证。** 下面的尝试都不构成"文件有问题"
的证据——三条路里没有一条真正读到过文件内容。**如实登记**，因为 T-B2 卡面变更记录里
写着"美术侧可放心把 Goxel 作为 MagicaVoxel 的替代路线"，而实测把"启动得起来"
和"能把 `.vox` 读进来"区分开了。

## 环境

- Goxel 0.15.1，`/Applications/Goxel.app/Contents/MacOS/goxel`（二进制名**小写** `goxel`，与 PM 2026-09-18 变更记录一致）
- macOS 15 / darwin 24.6.0 arm64
- 被试文件：`assets/mobs/mossback.vox`（3784 字节，SIZE 9×14×14，672 体素），副本放 `/tmp/mb_pm_copy.vox` 以免污染仓库

## 试过的四条路，逐条结果

| # | 做法 | 观察到什么 | 说明 |
|---|---|---|---|
| 1 | CLI 位置参数：`goxel /tmp/mb_pm_copy.vox` | 起窗口，**画布是空的**（只有淡紫地面网格，无体素）。日志止于 "Read settings file"，**没有任何加载/报错行** | Goxel 的 macOS 构建**忽略命令行参数**。旁证：`goxel --help` 同样只起 GUI、不打印用法。⇒ **不是解析失败，是根本没去读**（截图 `goxel_attempt_1_empty_canvas.png`） |
| 2 | CLI 导出：`goxel -e /tmp/out.png file.vox` | 等 25 s，没有产物；同上空画布 | 同上，参数被忽略 |
| 3 | macOS 文稿打开：`open -a /Applications/Goxel.app /tmp/mb_pm_copy.vox` | **弹窗拒绝**："The document "mb_pm_copy.vox" could not be opened. goxel cannot open files of this type." | 这是**类型/UTI 层面的拒绝**，发生在读文件之前（报的是"files of this type"而不是解析错误）。⇒ 仍**不构成**"我的文件坏"的证据（截图 `goxel_attempt_2_refuses_vox.png`） |
| 4 | 菜单路径：System Events 点 `File > Open…`；再改用真 HID 鼠标点击（Quartz `CGEventPost`，File 菜单在 (140,13)、Open… 在 (150,60)） | 菜单**能打开**（截图 `goxel_attempt_3_menu_open.png`），但点 `Open…` **不弹任何对话框**：`System Events` 枚举进程窗口只有主窗一个；`pgrep openAndSavePanel` 无进程 | Goxel 不走 NSOpenPanel，或该构建的打开路径在本机不工作。**没能进入文件选择那一步** |

## 顺带核对：PM 的验证图也证明不了导入

`docs/qa/T-B2-2026-09-18/pm_goxel_verified.png`（PM 2026-09-18 交）里 Goxel 的
**画布同样为空**。所以那张图证明的是"进程起得来、窗口渲染正常"——**不含**"能读 `.vox`"。
两条结论不冲突，只是口径要分清。

## 那么 `.vox` 的正确性靠什么？

1. **引擎侧 C++ 解析器**（`mob_model.cpp: parse_vox`，与本案无关的第二份实现）读通了，启动日志逐字给出
   `672 voxels, 1504 triangles, 7 joints, 1.4 blocks tall, palette from palettes/mossback.png`，
   且 **WARN = 0**（T-B2 卡面 §6.2 的机器判据）。
2. **本案自写的独立读回器**（`tools/vox_inspect.py`，只读落盘字节、不读分层稿）算出的
   体素数 / 三角形数 / 每索引包围盒，与上面**逐数相同**（672 / 1504）。
3. 两者**独立实现、口径一致**，覆盖了 SIZE / XYZI / RGBA 三个块的字节布局与那处
   "RGBA 第 e 条 = 索引 e+1"的整体错位。

## 给 PM 的建议

- **不要把"Goxel 起得来"当成"Goxel 能收发 `.vox`"。** 本机实测：命令行走不通、
  文稿打开被按类型拒绝、菜单 Open 不弹框。
- 若后续卡（B3/B4）打算**用 Goxel 出 `.vox`**，建议先在真人环境里手工走一遍
  `File > Export` / `File > Open` 确认这条路可用，再写进卡面；
  否则兜底路线（逐层字符稿 + 自写编码器，本案已走通）仍是唯一**可复现**的产出方式。
- 若确实需要第三方 `.vox` 校验器，候选是 **MagicaVoxel**（需自行下载，T-R3 已核许可）
  或 **assimp**（本机未安装，Homebrew 无现成包）。本机没有可用的离线第三方 `.vox` 读取器。
