# T-B1 证据目录 · 生物体素模型（`.vox` 解析器 + 渲染通道）

任务卡：`docs/tasks/T-B1.md`　方案：`docs/research/12-mob-model-formats.md`
取证日期：2026-09-18　分支：`task/T-B1-mob-voxel`

## 这是什么

本目录是 T-B1 的实机证据与取证工具。所有截图都出自**同一个窗口尺寸（1280×748）、
同一个冷启动世界、同一个机位**，两棵树之间唯一的差别写在每段标题里。

产品代码在交付版里**没有任何为取证做的改动**：`git diff --stat` 对
`engine/render/**`、`game/server/**`、`game/common/**`、`engine/{voxel,core,noise,physics}/**`、
`assets/**` 全为空（见 `06_forbidden_paths.txt`）。

## 为什么需要一份 evidence patch

卡面 §7 第 2 条（零变化判据）要求"同一机位、分跑两棵树、逐像素比较"，并且要先自证
**装置能分辨两棵树**。要把这件事做成可判定的，场景必须可复现，而游戏本身给不出一个
"停在已知位置的生物"：

* 实体不落盘（`level_file.hpp` 没有实体表）⇒ 存档里造不出生物；
* 自然刷怪的取样点用 **(seed, chunk, tick)** 作 RNG 流（`mob_spawn.hpp`：
  `chunk_seed(seed, cx, cz) ^ tick`）⇒ 两次运行的刷怪位置不同；
* 会走路的生物会让**同一次运行内的 A/A 两张图**也不同 ⇒ 装置将无法区分
  "代码改了画面"与"时间过去了"。

⇒ 取证时给**两棵树**打同一份补丁（`tools/apply_evidence_patch.py`，输出留档在
`03_evidence_patch_applied.diff`）：关掉自然刷怪，把一只 mossback 钉在玩家正北
3.5 格、脚下高 1.6 格处，朝向玩家（yaw = π）、速度 0。`spawn_pass()` 在
`WorldSim::tick()` 里排在 `step_mob_pass()` **之后**，所以它是该生物每 tick 状态的
最后写者 ⇒ 画面上永远只有这一只、永远在同一个位置、任意 tick 都一致。

补丁在取证后 `git checkout` 还原（两棵树都还原了）。玩家一侧无需任何处理：删掉
`build/saves/` 冷启动，同种子、同出生列，`view_yaw = view_pitch = 0`（main.cpp 从存档
读视角，没有存档就是 0），也没有任何输入注入（HID 全程未使用）。

## 文件

| 文件 | 内容 |
|---|---|
| `run/00_base_nomodels.*` | **改动前**那棵树（`13896c8` 检出）· 无模型文件 · 图 + 日志 |
| `run/01_branch_nomodels.*` | **本分支** · 无模型文件 · 图 + 日志 |
| `run/02_branch_models.*` | **本分支** · `assets/mobs/mossback.vox` = 可辨证据模型 · 图 + 日志 |
| `run/03_branch_palette.*` | **本分支** · 同上再加 `assets/palettes/mossback.png`（§3.3 调色板覆盖）· 图 + 日志 |
| `04_pixel_diff.txt` | 六组逐像素比对的结果（见下表） |
| `05_product_log_check.txt` | **纯产品码**（无补丁）三次冷启动的日志核对：夹具在 / 不在 / 损坏 |
| `03_evidence_patch_applied.diff` | 取证补丁的实际内容（对 `world_sim.cpp` 的完整 diff） |
| `06_forbidden_paths.txt` | 禁碰目录零改动的机器核对 |
| `09_pm_fixture_crosscheck.txt` | ★ 用 **PM 的独立夹具**（`pm_verify/`）跑开发者的解析器，逐条对齐期望 |
| `tools/run_evidence.sh` | 截图取证脚本（单实例纪律、按 PID 反查窗口、尺寸过滤） |
| `tools/apply_evidence_patch.py` | 取证补丁的施加/还原（幂等） |
| `tools/check_product_log.sh` | 产品码日志核对脚本 |
| `tools/pixel_diff.py` | 逐像素比对（解码后比较，非 md5；取自 T-A4 工具） |
| `tools/tb1win.m` | 按 PID 找窗口并置前（取自 T-A4 工具） |
| `tools/make_evidence_model.py` | 生成 `evidence_mossback.vox`（176 体素的人形，**非交付资产**） |
| `tools/evidence_mossback.vox` | 上面那个证据模型；只放进 `assets/mobs/` 用于 02 段 |

## 六组比对（`04_pixel_diff.txt`）

三张分支图取自**同一个二进制**（`build/opencraft` md5 `10499fa55ff030ace648994ee9e45d8a`）。

| # | 比对 | 差异像素 | 含义 |
|---|---|---|---|
| 1 | 00 基线 vs 01 本分支，**都无模型文件** | **0 / 957 440** | ★ 零变化判据成立，且画面上**有生物**（两立方体回退路径真的跑到了） |
| 2 | 01 与它自己 3 秒后的第二张 | 0 / 957 440 | 装置自证：场景静止，第 1 条不是运气 |
| 3 | 01 无模型 vs 02 有模型 | **39 176**，包围盒 x 564..715 / y 127..390 | 装置**能**分辨两棵树；差异恰好落在被钉住的生物那一块（≈150×260 px），新出现的颜色就是模型调色板的颜色 |
| 4 | 02 与它自己 3 秒后的第二张 | 0 / 957 440 | 站立不动的生物姿态不漂（行走相位按速度累积，速度 0 则冻结） |
| 5 | 02 模型 vs 03 模型+调色板 PNG | **26 634**，同一块包围盒 | §3.3 的覆盖通道在实机上生效，且新色就是调色板 PNG 里那些格（青/品红/橙/蓝/黄/紫） |
| 6 | 03 与它自己 3 秒后的第二张 | 0 / 957 440 | 换过调色板后画面同样静止 |

第 3 条是"装置能分辨"的自证：如果它也为 0，那第 1 条就什么也证明不了。
第 5 条同理，它证明"调色板 PNG 覆盖"这一条通道**真的上了屏**，而不只是解析器认了它。

## 产品码日志核对（`05_product_log_check.txt`，无补丁）

```
A  assets/mobs/mossback.vox = B0 夹具（md5 c21af626ab28af2f1aab23882ffedf95）
   mobs: 1/3 mob models loaded from ../assets/mobs
   mob model mossback: 8 voxels, 68 faces, 3 joints, 1.4 blocks tall
   warnings: 0
B  assets/mobs/ 里没有模型文件
   mobs: 0/3 mob models loaded from ../assets/mobs
   warnings: 0
C  assets/mobs/mossback.vox = 故意截断的坏文件
   [warning] mob model ../assets/mobs/mossback.vox was rejected
             (a chunk runs past the end of the file); drawing mossback as boxes instead
   mobs: 0/3 mob models loaded from ../assets/mobs
   warnings: 1
```

```
D  ★ PM 自己的独立夹具 pm_good.vox（pm_verify/fixtures/，8 体素 / SIZE 7x5x3）
   mobs: 1/3 mob models loaded from ../assets/mobs
   mob model mossback: 8 voxels, 68 triangles, 2 joints, 1.4 blocks tall
   warnings: 0
```

跑完 `assets/mobs` 被删掉（本卡不产出生物模型）：`git status --short -- assets` 为 0 行。

## ★ 回贴前用 PM 的独立夹具对过一遍（`09_pm_fixture_crosscheck.txt`）

PM 在 main 上另备了一份**不依赖开发者 B0 夹具**的装置（`pm_verify/`，提交 `e094b65`），
其 README §5 逐条写出了 9 个夹具的期望行为。同一份错误假设会同时出现在解析器和它自己的
测试数据里，所以回贴前把客户端自己的静态库链进一个 `/tmp` 探针，跑了一遍对方的数据。
**抓到两处不一致，都已修**：

1. **`pm_good.vox`（对方那个"合法"夹具）原本被判为坏文件**。原因：它的 `RGBA` 块只写了
   模型用到的 **3 条颜色**（12 字节），而我按规范的"256 条"硬要求 1024 字节 ⇒ `BadChunk`。
   连带 `pm_no_size` / `pm_out_of_bounds` / `pm_zero_voxels` 三条也报错在错误的理由上。
   修法：接受**更短的调色板块**（读到多少算多少，缺的保持"未使用"），零体素条目才算坏块。
   截断仍然抓得住（声明长度超出文件由 `content_size` 拦下）,所以放宽不会掩盖损坏文件。
2. **调色板 PNG 的格号约定与对方的独立夹具不一致**。我原先按 `.vox` 文件里那个"错一位"
   （`RGBA` 块第 0 条 = 索引 1）同样映射 PNG，于是 PNG 的第 0 格被当成"索引 1"，而对方的
   PNG 把品红/青放在**第 1/2 格**（= 索引 1/2）。修法：**PNG 的格号就是索引号**
   （第 1 格 = 索引 1，第 0 格 = 从不使用的索引 0），文件里那个"错一位"只在解析 `.vox` 时
   发生一次，不在 PNG 通道里再发生一次 —— 这样美术侧对着自己画的索引配色，不必心算平移。

修完之后的逐条结果（左 = 对方期望，右 = 本实现）

| 夹具 | 期望 | 实际 |
|---|---|---|
| `pm_good.vox` | 加载成功、8 体素 | OK · SIZE 7×5×3 · 8 体素 · **68 三角形**（= 34 暴露面，独立算得）· `palette[1]/[2]/[3]` = 红/绿/蓝 |
| `pm_bad_signature.vox` | 回退 + 1 WARN | REJECT（签名） |
| `pm_truncated.vox` | 回退 + 1 WARN | REJECT（chunk 越过文件尾） |
| `pm_chunk_overrun.vox` | 回退 + 1 WARN、不越界读 | REJECT（chunk 越过文件尾） |
| `pm_count_lies.vox` | 回退 + 1 WARN | REJECT（XYZI 谎报体素数） |
| `pm_out_of_bounds.vox` | 回退 + 1 WARN、不静默钳制 | REJECT（坐标越出 SIZE） |
| `pm_no_size.vox` | 回退 + 1 WARN | REJECT（缺 SIZE/XYZI） |
| `pm_huge_size.vox` | 回退 + 1 WARN | REJECT（SIZE 越出 1..256） |
| `pm_zero_voxels.vox` | 回退 + 1 WARN | 解析成功、0 体素 ⇒ 加载器 1 条 WARN |
| `pm_palette_magenta_cyan.png` | 1/2 格 ⇒ 躯干品红、头青 | `colorIndex1 = ffff00ff`（品红）、`colorIndex2 = ffffff00`（青）、`index0 = 0` |

对方的 8 体素不对称模型还独立验证了**面剔除**：8×6 − 2×7（7 对相邻） = 34 面 = 68 三角形，与实测一致；
`palette[1]` = 红也独立验证了"`RGBA` 块第 0 条 = 索引 1"这个最易错的映射。

## 两个只有实机才能抓到的缺陷（已修）

1. **调色板贴图顶掉了图集**：`render::Texture2D` 的构造在**当前激活的纹理单元**上
   `glBindTexture`（它自己不调 `glActiveTexture`），而图集只在启动时 `atlas.bind(0)`
   一次、之后再没重绑 ⇒ 上传调色板时若激活单元正好是 0，图集就被一张 16×16 调色板
   替换，**整个世界采样全黑**，而模型本身画得好好的（第一版 `02_branch_models.png`
   正是这个画面）。修法：上传调色板前显式切到它自己的单元（`mob_render.cpp`）。
2. **调色板 UV 差一格**：`palette[colorIndex]` 就是纹理的第 `colorIndex` 号像素，
   顶点 UV 必须按 `colorIndex` 取格，不能按"文件里的 entry 号"（entry = colorIndex − 1）。
   差一格的后果是 `colorIndex 1`（躯干）采到从不使用的 0 号格（alpha = 0 ⇒ 被丢弃），
   整个躯干不可见，其余部位各错一格（第二版截图：头显示成躯干色、躯干消失）。
   修法与"测试只复述公式"的教训一起落在 `tests/test_mob_model.cpp` 里：现在的断言
   先写出纹理布局，再要求"被采样的那格就是该颜色的 RGBA"。

两条都是**渲染路径**的问题，单测覆盖不到（`docs/05 §3.1` 第 5 条），由实机截图抓出。
