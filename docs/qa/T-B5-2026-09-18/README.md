# T-B5 · 证据目录索引（美术返工：Mossback 比例对齐 + Hollow Wretch 加密）

> 分支 `task/T-B5-mob-art-rework`，基线 main `ba2f920`。纯资产卡，零代码改动。
> 卡面：`docs/tasks/T-B5.md`　报告：`docs/tasks/T-B5.report.md`

## 1. 产出（唯一进 `assets/` 的东西）

| 文件 | 说明 |
|---|---|
| `assets/mobs/mossback.vox` | Mossback v5：7×17×14 画布 / **705 体素** / 1544 三角形 / 7 关节 |
| `assets/mobs/hollow_wretch.vox` | Hollow Wretch v5：12×9×18 画布 / **496 体素** / 1592 三角形 / 6 关节 |
| `assets/palettes/mossback.png` | **一字未动**（md5 `74dcc2a82723523d9f1d0b40b92ba7db` = 仓库原值） |
| `assets/palettes/hollow_wretch.png` | 只有 13/14 两格由 U0 改成 S3（左右脚骨色） |

## 2. 工具（`tools/`，★ 与 T-B2b/T-B3 的四件套同构）

| 文件 | 作用 |
|---|---|
| `vox_build.py` | **机械翻译器**：读手写字符稿 → 写 `.vox` + 调色板 PNG。无几何/无对称展开/无填充/无随机数。`python3 vox_build.py <root> all` |
| `vox_inspect.py` | **独立解析器 + 出图**（不读字符稿，只读落盘 `.vox`）。`model <vox> <out_dir> <tag>` / `compare <out.png> <a> <b>`。★ 含本卡新增的 `render_perspective`（非正交游戏机位） |
| `check_palette_png.py` | 用**第三方**解码器（Pillow）逐格复核两张调色板 PNG |
| `mossback_layers.txt` / `hollow_wretch_layers.txt` | ★ **创作本体**：逐层手写字符稿（mossback 14 层×17 行×7 格；wretch 18 层×9 行×12 格） |
| `mossback_palette.txt` / `hollow_wretch_palette.txt` | 16×16 调色板字符稿 |

复跑（在 worktree 根目录）：

```bash
python3 docs/qa/T-B5-2026-09-18/tools/vox_build.py "$PWD" all
python3 docs/qa/T-B5-2026-09-18/tools/vox_inspect.py model assets/mobs/mossback.vox \
        docs/qa/T-B5-2026-09-18/renders mossback-v5
python3 docs/qa/T-B5-2026-09-18/tools/check_palette_png.py "$PWD"
```

## 3. 日志与表格

| 文件 | 内容 |
|---|---|
| `run1_startup_with_assets.log` | 产品码启动日志：`mobs: 2/3` + 两行 `mob model …`，WARN=0 |
| `run2_startup_no_mobs_dir.log` | 移走 `assets/mobs/` 后：`mobs: 0/3`，WARN=0 |
| `ctest_summary.txt` | 全量测试 **459/459**（纯资产卡，与基线同数） |
| `inspect_mossback_v5.txt` | `vox_inspect.py` 全文（预算/包围盒/逐索引表/z=0 逐格/透视自证/调色板回读） |
| `inspect_hollow_wretch_v5.txt` | 同上（另含契约 v2 专项：第二色落盒、眼格表面着色） |
| `inspect_mossback_v4.txt` | 旧版对照读数（672 体素 / 1504 三角形 —— 与 T-B3 期产品码日志逐字相同，证明工具可信） |
| `inspect_hollow_wretch_v4.txt` | 旧版对照读数（169 体素 / 856 三角形 —— 同上） |
| `palette_png_check.txt` | Pillow 逐格复核输出 |

## 4. 出图（`renders/`）

| 文件 | 内容 |
|---|---|
| `mossback_gamecam_ab.png` | ★ **新判据**：左 = v4、右 = v5，**同一台相机**（4.5 格 / 方位 38° / 仰角 16° / 16 px 每体素 / 固定 760×620 画布） |
| `hollow_wretch_gamecam_ab.png` | ★ 同上（人形） |
| `mossback-v5_gamecam.png` / `hollow_wretch-v5_gamecam.png` | 单张游戏机位出图（非正交，见报告 §5 的机器自证） |
| `*-v5_contact_sheet.png` | 正交五视图：侧视(鼻在右) / 正视 / 俯视 / 等轴 / 等轴-关节分色 |
| `*-v5_silhouette.png` | 剪影（只留轮廓，用来看"四条腿 + 一个头 + 背上有苔"是否一眼可读） |
| `v4/*.vox` | 旧版资产的**只读副本**，来源：`git show ba2f920:assets/mobs/<name>.vox`（分支基线 = 派发时点的 main tip）。md5 已核：mossback `9215d2bef3333821c07c83b20aa1c360`、hollow_wretch `6b5dc511340220c4de29f279389fb16d`，与基线提交逐字节相同。⚠ **取件必须写基线 SHA，不能写 `HEAD`**——本卡提交之后 `HEAD` 指向的是 v5。放这里只为让对照图可一键复跑，不是交付物 |

## 5. 本轮**没有**做的取证（如实登记）

- **实机暂停帧 A/B 未做**：卡面 §2.5 把"暂停帧装置实拍复核"明确划给 PM 侧；
  开发者侧交付的是**非正交渲染图 + 同角度对照**（§4.5 原文）。
- **MagicaVoxel（§4.6 可选加分）未开**：本机 `opencraft_scratch/mv/` 是否仍在需 PM 确认；
  非门槛项，报告里按"未做"记。
- **Retina/HiDPI 无关**（本卡不涉及显示缩放）。

## 6. 目录卫生

只收最终产物：字符稿、脚本、日志、出图。**不含** `__pycache__/`、`*.pyc`、临时中间稿
（`docs/05 §2` 的"过程垃圾不得入仓"）。
