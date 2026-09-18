# T-B2b · PM 侧验收装置与证据

> 日期：2026-09-18　执行：PM（第四任）　裁决：`docs/tasks/T-B2b.ruling.md`
> 对象：分支 `task/T-B2b-joint-expansion` @ `d15bb30`（基线 = main `c5a2839`）→ merge `fbef08d`

## 1. 干净检出与回归

`git archive d15bb30` 自行 configure/build（不设 `FETCHCONTENT_BASE_DIR`）→ **ctest 459/459**；
合入后主树重建再跑 **459/459**。用例名集合比对（`ctest -N` 双树取名后 `comm`）：
**存量 457 名零缺失、恰新增 2 个**（`T-B2b contract v2: 1..16 are joint labels, 17..255 the body group`、
`T-B2b mesh: a joint's two colours land in one part range, not two draws`）。

## 2. 迁移复算（第四份实现，`pm_migration_check.py`，不 import 双方任何脚本）

输入 = 分支证据目录归档的 `pre_migration/mossback_v1.{vox,png}`（**先验 md5 为 T-B2 验收物**：
`f8072830…` / `ba69f5a0…`）vs 迁移后入库件。全部判据 PASS（原文 `migration_recheck_output.txt`）：

- 新旧 md5 = `9215d2bef3333821c07c83b20aa1c360` / `74dcc2a82723523d9f1d0b40b92ba7db`（与报告一致）；
- SIZE/体素数不变、**672 条坐标序列逐字节相同**；
- 色字节规则**恰为** `{9..16 → +8, 其余不变}`，violations=0；
- RGBA 与 PNG 细胞：`9..16 = U0`、`17..24 = 旧 9..16`、其余不变（逐格 0 违例）；
- 分层稿数据行（去注释）与 T-B2 原件 diff 为空（PM 复核，非仅采信报告）。

⚠ 留痕（PM 自己的坑）：本脚本第二次跑曾 FAIL——**合并后再拿主仓 `assets/` 当"旧件"**，
两个输入都是迁移后文件。取证脚本引用"旧件"时必须用归档副本 + 先验 md5（本次已如此修正）。

## 3. ★ 语义生效的反证对照（同夹具、两二进制）

夹具 `fixtures/eye_c_second.vox`（头全涂第二色 10）经 `OPENCRAFT_ASSETS_DIR` 指为 mossback：

| 二进制 | 日志 | 判定 |
|---|---|---|
| v2（干净检出） | `6 voxels, 56 triangles, **4 joints**, 1.4 blocks tall` | v2 生效 |
| v1（主树 merge 前，`73a37ef` 版） | `6 voxels, 56 triangles, **3 joints**, 1.4 blocks tall` | 旧行为 = 3（10 归躯干 ⇒ 头无体素） |

两日志：本目录 `v2_binary_eye_c_4joints.log` / `v1_binary_eye_c_3joints.log`。
⇒ **"扩区间零翻转"的存量全绿由这组对照补上区分度**（卡面 §7.1 警示的正是这一点）。

## 4. 日志回归（产品码，干净检出）

`1/3` + `mob model mossback: 672 voxels, 1504 triangles, 7 joints, 1.4 blocks tall,
palette from palettes/mossback.png`（与 T-B2 验收行逐字节同）+ `WARN=0`；移走 `assets/mobs/`
→ `0/3`、WARN=0。

## 5. MagicaVoxel 重开（白捡回归判据）

`mv_reopen_migrated.png`：迁移后资产在 MV 中标题栏 `mossback_v2`、尺寸 `9 14 14`、
形体与 T-B2 期截图（`docs/qa/T-B2-2026-09-18/mv_thirdparty/`）**逐要素相同**（苔丘/四腿/头），
仅调色板格位按 v2 平移——与坐标逐字节相同的机器结论互相印证。
