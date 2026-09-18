# T-B3 · PM 侧验收装置与证据

> 日期：2026-09-18　执行：PM（第四任）　裁决：`docs/tasks/T-B3.ruling.md`
> 对象：分支 `task/T-B3-hollow-wretch` @ `2a8c914`（基线 = main `8012ce4`）→ merge 见裁决

## 1. 干净检出与回归

`git archive 2a8c914` 自行 configure/build（不设 `FETCHCONTENT_BASE_DIR`）→ **ctest 459/459**（纯资产卡，与基线同数）。

## 2. 产品码日志（无补丁）

`pm_run_with_assets.log`：`mobs: 2/3` + Mossback 行 + `mob model hollow_wretch: 169 voxels, 856 triangles,
6 joints, 1.8 blocks tall, palette from palettes/hollow_wretch.png`，WARN=0；
`pm_run_without_mobs.log`：`0/3`、WARN=0。
**Mossback 行不回归**（独立复核）：本日志与 `docs/qa/T-B2b-2026-09-18/run1_startup_with_assets.log`
的该行剥时间戳后 md5 同为 `615123ee69f5b0bebd0bc8145c6bc16e`。

## 3. 第四份独立实现读回（`pm_readback_b3.py`，不 import 双方脚本）

**ALL PASS**（原文 `readback_output.txt`）：两 md5 中；SIZE 6×6×18；169 体素；每索引计数与报告表逐项同；
**契约 v2 专项**：索引 9/10/11/12 各自落在其关节包围盒内（逐条打印）；眼格恰为 (1,5,15)/(4,5,15)、
**在占用表内**（不是挖空）且 +y 邻居为空（表面着色）；z=0 层只含索引 {5,6}（两脚掌）；
头 max z 17 > 躯干 max z 14（抗 y-up）；z 跨度恰 18；PNG 格 k == RGBA 第 k 条（全部 k≥1）；
格 0 = U0；alpha 全 255；用色 8 种全在 §2.1 表内、最大饱和 0.414。
⚠ 我自己的两处探针 bug 留痕：① 第 0 格比对写成了 `rgba[255]`（自设假定），② "格 0=U0" 应先按字面色值比
——两处都当场改成字面判据后 PASS。

## 4. 暂停帧逐像素（取证树 = 2a8c914 + 三补丁；**新增：暂停前俯仰注入**）

**本卡装置要点（新知识，已回写 `docs/05 §3.1` 第 14 条）**：人形 1.8 > 眼高 1.62 ⇒ 平视时头顶进菜单带。
**修法 = 在按 ESC 之前注入 `ti2input move 0 -60`**（未暂停时鼠标被捕获；+60 是低头、-60 是抬头——
方向就是靠一次探针跑撞出来的）。脚本 `run_scene_pre.sh`（= rev2 装置 + 暂停前 PITCH 预注入）。

| # | 比对 | 结果 | 判定 |
|---|---|---|---|
| [1] | m1 vs m2（同树同资产两跑，含俯仰） | **0 / 957,440** | 俯仰注入**可复现** ⇒ 噪声地板仍为 0 |
| [2] | m1 vs s1（模型 vs 两立方体，yaw=0.7） | **15,374**（x592..689, y443..680） | 模型上屏替换占位 |
| [3] | m1 vs c1（有生物 vs 空） | **14,540** | 灵敏度 |
| [4] | s1 vs c1（两立方体 vs 空） | **17,871** | 对照健全 |
| [5] | m3 vs s2（yaw=π 面向镜头，模型 vs 占位） | **15,109** | 换角重复成立 |
| [6] | m1 vs m3（yaw 0.7 vs π） | 7,385（全在生物 bbox 内） | 朝向变更确实生效 |

目检：`pm_final_m1.png`（模型：**白颅骨 + 黑躯干 + 长臂骨手 + 两条细腿**）vs `pm_final_s1.png`
（占位：灰岩纹理方柱）——"游戏里它不再是一根灰柱"成立；`pm_face_m3.png` 为面向镜头的全身。

### 诚实登记的局限
**引擎内"两只眼窝"的特写未取得**：yaw=0.7 / π 两角度的头面都读成均匀浅色（眼格在头面最外两列，
与暗背景相邻，本分辨率下不能人眼确证）。眼格的正确性由 §3 的机器判据（占用 + 表面 + 关节包盒）
与 MagicaVoxel 渲染（开发者 `mv_orbit_view.png` 与 PM 侧 `pm_mv_reopen.png` 均可读出颅骨上的凹窝）支撑。
⇒ **不构成验收门槛**（契约 v2 的门槛是机器判据），但如实记入裁决。

## 5. MagicaVoxel（可选加分，PM 自跑一份）

`pm_mv_reopen.png`：`open -n -a <app> <绝对路径>` → 标题 `pm_wretch_view`、尺寸 **6 6 18**、直立形体
（此视角为背面，可见背脊锈斑）；另核开发者 `mv_orbit_view.png`（3/4 视角，白颅骨上眼窝清晰）。
