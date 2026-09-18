# T-B1 报告：生物体素模型 —— `.vox` 解析器 + 渲染通道（引擎侧）

任务卡：`/Users/happy/Desktop/opencraft/docs/tasks/T-B1.md`
方案依据：`/Users/happy/Desktop/opencraft/docs/research/12-mob-model-formats.md`
分支：`task/T-B1-mob-voxel`　取证树：`/Users/happy/Desktop/opencraft_worktree/opencraft-tb1`
基线树（对照）：`/Users/happy/Desktop/opencraft_worktree/opencraft-tb1-base` @ `13896c8`
日期：2026-09-18

---

## 0. 一句话结论

`.vox` 解析器 + 体素网格 + 程序化关节渲染通道已经落地，**没有模型文件时画面与改动前逐像素相同
（0 / 957 440 差异像素，且画面上有生物）**；放一个 `.vox` 进去，生物立刻从"两个立方体"变成
体素模型（日志 `mobs: 1/3`）。`engine/render/**`、模拟层、`assets/**` 零改动。全部 438 张存量
单测未改动且全绿，新增 19 张单测；两棵树从零构建的告警集合**逐条相同**。

---

## 1. 交付物

### 1.1 B0 夹具（本卡第一步，已入仓）

落点 **`tests/fixtures/mobs/`**（卡面允许的两处之一；选此处是因为它是测试数据，
与 `tests/golden/` 同级，且 `tests/CMakeLists.txt` 已经把 fixture 目录烤进测试二进制）：

| 文件 | 内容 | 大小 |
|---|---|---|
| `mob_column.vox` | **手写的最小合法 `.vox`**：`SIZE` 16×16×16，**8 个体素**的竖柱（z=0..7），用三个关节标签上色（z0..1 = 标签5 腿、z2..5 = 标签1 躯干、z6..7 = 标签2 头），调色板 8 个不透明标签色 + 247 个全零未用格 | 1128 B |
| `mob_empty.vox` | 故意做成 **0 体素**的合法文件（回退表第 5 行） | 1096 B |
| `mob_column_palette.png` | 16×16 调色板 PNG（**格号 = 颜色索引号**），**颜色与 `.vox` 内置调色板故意不同**（好让"哪一份在生效"是一像素的问题） | 97 B |
| `make_fixtures.py` | 生成上面三份的脚本，**把每个字节写死在源码里**（B0 的"体素数据可逐字列出"），便于复核 | 133 行 |

**没有引入任何 MagicaVoxel 自带样本**（`docs/04` 红线 2/5）：三份夹具的每个字节都由
`make_fixtures.py` 给出，脚本随夹具一起入仓。

截断 / 错误签名等变体**不另立文件**：单测拿 `mob_column.vox` 的字节就地改（改签名头、
改 `numVoxels`、改坐标、截断），改动点由测试自己的 chunk walker 定位，比多存几份二进制更好审。

### 1.2 B1 引擎侧（新增 8 个文件 + 3 处改动）

| 文件 | 行数 | 职责 |
|---|---|---|
| `game/client/src/mob_model.hpp/.cpp` | 127 + 274 | `.vox` 解析（**纯函数：无 IO、无 GL**）+ 路径/加载通道 + 调色板 PNG 覆盖 + 启动计数日志 |
| `game/client/src/mob_mesh.hpp/.cpp` | 73 + 175 | 体素 → 表面网格（**面剔除、CCW 绕序、按关节分组、关节枢轴**），纯函数 |
| `game/client/src/mob_pose.hpp/.cpp` | 112 + 99 | 姿态数学：模拟值 → 每关节旋转角；行走相位累加器 |
| `game/client/src/mob_render.hpp/.cpp` | 88 + 127 | GL 通道：每**类型**一份 VBO/VAO + 调色板贴图，逐关节 draw |
| `tests/test_mob_model.cpp` | ≈1010 | 19 张新单测（夹具 / 回退表 / 网格 / 姿态 / 短调色板块） |
| `game/client/src/shaders.cpp/.hpp` | +38 / +3 | 新增 `kMobVertexShader` / `kMobFragmentShader`（**纯新增**，既有 12 个符号一字未动） |
| `game/client/src/main.cpp` | +108 / −4 | 模型集装配 + 回退 pass 并入一个早退 + **新增模型 pass** |
| `game/client/CMakeLists.txt` | +31 / −2 | 新静态库 `opencraft_client_mobs`（纯半边，供测试链同一份代码）+ `src/mob_render.cpp` 进可执行文件 |
| `tests/CMakeLists.txt` | +7 / −3 | 新测试文件 + 链新库 + 烤入 `OPENCRAFT_FIXTURE_DIR` |

总计约 **2130 行**（含测试），与 research/12 §7.1 的 ≈770 行引擎侧估算同量级（多出来的是
单测与注释）。

**工程质量**：`clang-format` 用 CLT 的 **17** 检查（不是 brew 的 23），全部新文件与改动文件零差异。

---

## 2. 冻结项逐条对账（卡面 §2）

| 冻结项 | 落点 / 证据 |
|---|---|
| 主方案 = 体素模型 | `mob_mesh.cpp` 全程在体素上做面剔除 |
| 模型路径 `assets/mobs/<mob_id>.vox`，**纳入既有资产根解析** | `mob_model_path()` = `assets_root / "mobs" / (id + ".vox")`，`assets_root` 来自 **`client::resolve_assets_root()` 本身**（零新增解析逻辑） |
| 动画 = 程序化骨骼（单份网格 + 按关节分组顶点 + 每帧关节矩阵） | `MobPartRange` 把每个关节的顶点排成**一段连续区间**（一次 `glDrawArrays`/关节）；`MobRenderer::draw()` 里 `translate(p)·rotate·translate(-p)` |
| ★ 朝向只能读模拟值 | 见 §5 代码审查与单测；渲染侧三段代码里**一次三角函数都没有**，`yaw/pitch` 只被赋值/透传 |
| `engine/render/**` 一行不改 | `06_forbidden_paths.txt`：0 文件改动。新顶点格式与 shader 全在 `game/client/src/` |
| 加载器 = 自写解析器 | `mob_model.cpp`，无第三方库；不引 `voxel-io` |
| 工作量单卡可交付 | 见 §1.2 |

---

## 3. ★ 零变化判据（卡面 §4.1，成败线）

### 3.1 装置

同一份冷启动世界（删掉 `build/saves/` ⇒ 同种子、同出生列、`view_yaw = view_pitch = 0`）、
同一窗口（1280×748，按 PID 反查 + 尺寸过滤）、**没有用任何键盘/鼠标注入**（HID 全程未使用）。
取证脚本：`docs/qa/T-B1-2026-09-18/tools/run_evidence.sh`。

**取证补丁**（`tools/apply_evidence_patch.py`，**两棵树打的是同一份**，取证后 `git checkout` 还原）：
关掉自然刷怪，并把一只 mossback 钉在玩家正北 3.5 格、脚下高 1.6 格处（yaw = π、速度 0）。
理由与必要性写在 `docs/qa/T-B1-2026-09-18/README.md`：实体不落盘、自然刷怪 RNG 含 tick、
会走路的生物会让"同一次运行内的 A/A 两张图"也不同 —— 那装置就无法区分"代码改了画面"与
"时间过去了"。补丁在 `spawn_pass()` 里，而 `spawn_pass()` 在 `WorldSim::tick()` 中排在
`step_mob_pass()` **之后**，故它是该生物每 tick 状态的最后写者。

### 3.2 结果（`04_pixel_diff.txt`）

| # | 比对 | 差异像素 | 含义 |
|---|---|---|---|
| 1 | **基线树 vs 本分支，都无模型文件** | **0 / 957 440** | ★ 零变化判据成立，**且画面上有生物**（回退的两立方体路径真的被执行了） |
| 2 | 本分支 与它自己 3 秒后的第二张 | 0 / 957 440 | 装置自证：场景静止 ⇒ 第 1 条不是运气 |
| 3 | 本分支 无模型 vs 有模型 | **39 176**，包围盒 x 564..715 / y 127..390 | 装置**能**分辨两棵树；差异恰好落在被钉住的生物那一块，且新出现的颜色就是模型调色板的颜色 |
| 4 | 有模型 与它自己 3 秒后的第二张 | 0 / 957 440 | 站立不动的生物姿态不漂（行走相位按速度累积，速度 0 则冻结） |
| 5 | 有模型 vs 有模型+调色板 PNG | **26 634**，同一块包围盒 | §3.3 的覆盖通道在实机上生效，新色就是调色板 PNG 里那些格（青/品红/橙/蓝/黄/紫） |
| 6 | 有调色板 与它自己 3 秒后的第二张 | 0 / 957 440 | 换过调色板后画面同样静止 |

第 3 条是"先自证装置能分辨两棵树"（T-D40 / T-A2 的既有手法）：若它也是 0，第 1 条就什么也证明不了。

### 3.3 实机：生物不再是两个立方体（卡面 §7 第 8 条）

`run/02_branch_models.png`：同一位置同一机位，`assets/mobs/mossback.vox` 用一个 176 体素的人形
证据模型（`tools/make_evidence_model.py`，**非交付资产**）⇒ 画面里是一只绿头、褐身、四肢分色的
体素生物，取代了 `run/00/01` 里的"泥土方柱 + 草顶方块"。

> 说明：截图为取证补丁下所拍，补丁只影响**模拟侧**（多一只钉住的生物 / 关掉自然刷怪），
> 渲染代码在两棵树里逐字节相同。产品码（无补丁）的对应证据是 §4 的日志三连。

### 3.4 启动计数日志（卡面 §7 第 5 条，产品码，无补丁）

`05_product_log_check.txt`，三次冷启动只读日志、不截图：

```
A  assets/mobs/mossback.vox = B0 夹具（md5 c21af626ab28af2f1aab23882ffedf95）
   mobs: 1/3 mob models loaded from ../assets/mobs
   mob model mossback: 8 voxels, 68 triangles, 3 joints, 1.4 blocks tall
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

跑完 `assets/mobs` 已删除：`git status --short -- assets` 为 0 行（本卡不产出生物模型）。

---

## 4. 回退表逐条覆盖（卡面 §4 / research §6.5）

7 行全部有单测，且 4 行另有实机日志（`05_product_log_check.txt`）：

| 情形 | 行为 | 日志 | 单测 | 实机 |
|---|---|---|---|---|
| `assets/mobs/` 目录不存在 | 全部两立方体 | 无 | ✅（空桩 + "无 assets 树"两例） | ✅ B 段 |
| `<mob_id>.vox` 不存在 | 该生物两立方体 | 无（静默） | ✅ | ✅ B 段 |
| 签名错（不是 `.vox`） | 该生物两立方体 | 1 条 WARN | ✅ | — |
| 解析失败（截断/越界/撒谎） | 该生物两立方体 | 1 条 WARN（含原因） | ✅（截断 / `numVoxels` 撒谎 / 坐标越界 / `SIZE` 为 0 / 空输入 五例） | ✅ C 段 |
| 解析成功但 0 体素 | 该生物两立方体 | 1 条 WARN | ✅ | — |
| 调色板 PNG 缺席 | 用内置调色板 | 无 | ✅ | ✅ A 段（日志 0 条 WARN） |
| 调色板 PNG 坏了 | 用内置调色板 | 1 条 WARN | ✅ | — |
| 调色板 PNG 正常 | 覆盖内置调色板 | 无 | ✅ | ✅ `03_branch_palette`（实机：躯干品红、头青、四肢橙/蓝/黄/紫） |

"1 条 WARN"是**断言**不是期望：单测用 spdlog sink 抓取日志，逐例核对条数与内容
（T-A2 的既有手法）。日志条数也覆盖了"静默"的两行（断言行总数 == 1，即只有计数行）。

**另有两条 research §5.4 要求的、回退表之外的边界**：调色板索引 +1（把期望值从**文件自己的
字节**里读出来，不由解析器自证）、`PACK` 多模型文件 ⇒ 只读第一个模型、其余忽略且不失败。
回退表之外还测了格式本身的坑：`RGBA` 缺失 ⇒ 拒绝、`SIZE` 里的轴为 0 ⇒ 拒绝、坐标系越界 ⇒ 拒绝。
解析器走的是**平坦 chunk 扫描**并**不信任 `children_size`**（MAIN 声明的 children 覆盖整个文件，
拿它步进会一步跳过全部内容；一个撒谎的 `children_size` 也无法把我们带出缓冲区——只有
`content_size` 参与边界判定）。

---

## 5. 朝向只读模拟值（卡面 §7 第 6 条）

`08_facing_and_cull_review.txt` 是机器复核记录：

1. **渲染侧三角函数清单**：`mob_model.cpp` / `mob_mesh.cpp` / `mob_render.cpp` **各 0 处**；
   只有 `mob_pose.cpp` 有 `sin/cos`，且全部是**姿态角**（腿摆/臂反摆/身体起伏/尾摆/蓄爆脉动）。
2. **renderer 侧对 `yaw`/`pitch` 的动作**：**一处都没有**（`grep` 结果为空）——它们不进
   `mob_render.cpp` / `mob_mesh.cpp` / `mob_model.cpp`，只在 `main.cpp` 被塞进 `MobPoseInput`、
   在 `mob_pose.cpp` 被逐位抄进 `MobPose`。
3. 单测 `T-B1 pose: facing is copied from the simulation, never invented`：`pose.yaw`/`pose.pitch`
   与输入的 double **逐位相等**（`==`，不是 `Approx`），并枚举 `speed × hurt × fuse × baby`
   共 72 组输入断言这两个值不变。

行走相位是唯一渲染侧自产的量，但它**按模拟速度累积**（`phase += |velocity|·dt·k`）：速度为 0
相位冻结（实机第 4 条比对：0 差异像素），速度越大摆幅越大 —— 满足 §4.4 的判定标准
（"同一位置与朝向下的姿态形变"）。

---

## 6. CULL_FACE 成对（卡面 §4.2）

- **回退分支**：保持今天的"关"（`main.cpp:788` `glDisable` … `:829` `glEnable` 两行都在原位，
  本卡只在循环里加了一行 `if (mob_renderer.has(id)) return;`）。
- **模型分支**：`MobRenderer::begin_pass()` / `end_pass()` **成对**（都是 `glEnable`），且调用点
  被 `if (mob_renderer.size() > 0)` 包住 —— **无模型时整个模型 pass 一次都不执行**，既不 enable
  也不 disable，GL 状态与改动前逐字节相同。
- 两条分支是**两个互斥的循环**（判据 `mob_renderer.has(id)`），不是一个循环里的 if/else：
  这样"回退分支执行的代码与今天逐字节相同"是**结构保证**，不是"另一条长得像的码路"。
- 模型网格的绕序是 **CCW 从头到外**，单测 `inward_facing_triangles()` 对每个三角形算几何法线，
  与"三角形重心 − 网格中心"点乘必须为正（夹具柱、单体力素、双体素、关节分组四例全过）⇒
  开着剔除不会漏面，也不会因关剔除而让 730 面承担 2× 三角形。

---

## 6.5 ★ 回贴前与 PM 的独立夹具交叉核对（新增，抓到两处不一致）

PM 在 main 上同时备了一份**不依赖开发者 B0 夹具**的验收装置（`docs/qa/T-B1-2026-09-18/pm_verify/`，
提交 `e094b65`），其 README §5 逐条列出 9 个独立夹具的期望行为。**同一份错误假设会同时出现在
解析器和它自己的测试数据里**，所以回贴前我把客户端自己的静态库链进一个 `/tmp` 探针，跑了对方的数据。
结果：**两处不一致，都已修**（详见 `09_pm_fixture_crosscheck.txt`）：

1. **对方的 `pm_good.vox` 原本被判为坏文件**：它的 `RGBA` 块只写了模型用到的 3 条颜色，
   而我按规范硬要求 256 条（1024 字节）⇒ `BadChunk`；连带的 `pm_no_size`/`pm_out_of_bounds`/
   `pm_zero_voxels` 三条也报在错误的理由上。修法：**接受更短的调色板块**（读到多少算多少，
   缺的保持"未使用/透明"），只有 0 条目的调色板块才算坏块。截断仍由 `content_size` 边界拦住，
   放宽不会掩盖损坏文件。补了一条单测钉住这个新行为。
2. **调色板 PNG 的格号约定与对方的独立夹具相反**：我原先把 `.vox` 文件里那个"错一位"
   （`RGBA` 块第 0 条 = 索引 1）同样套在 PNG 上，于是 PNG 的第 0 格被当成索引 1；
   而对方的 PNG 把品红/青放在**第 1/2 格**（= 索引 1/2）。修法：**PNG 的格号就是索引号**
   （第 0 格 = 从不使用的索引 0），文件里那个"错一位"只在解析 `.vox` 时发生一次。
   这样美术侧对着自己画的索引配色即可，不必心算一次平移 —— 也正是对方 README 里
   "同一份错误假设会同时在两处出现"提醒的那类坑。

修完后的逐条对齐（左 = 对方期望，右 = 本实现）：9 个夹具**行行一致**
（`pm_good` 加载成功且 8 体素 / 68 三角形，其余 8 条各自以正确的理由回退 + WARN，
`pm_zero_voxels` 由加载器报 0 体素 WARN），调色板 PNG 通道 `colorIndex1 = 品红`、`colorIndex2 = 青`。
对方的 8 体素**不对称**模型还独立验证了面剔除（8×6 − 2×7 = 34 面 = 68 三角形）与
"`RGBA` 第 0 条 = 索引 1"这条最易错的映射。

⇒ 建议 PM 验收时直接用 `pm_verify/` 的夹具跑第 5 条（日志 N 从 0 变 1）：本卡已用产品码跑过
（`05_product_log_check.txt` D 段），日志为 `mobs: 1/3` + `8 voxels, 68 triangles, 2 joints`，0 条 WARN。

## 7. 构建 / 测试 / 运行

```bash
# 工作树（docs/05 §6 硬规则：所有读写都在 worktree 内）
git worktree add /Users/happy/Desktop/opencraft_worktree/opencraft-tb1 task/T-B1-mob-voxel

# 构建（不设 FETCHCONTENT_BASE_DIR，让它自行 configure；命令不接管道）
cd /Users/happy/Desktop/opencraft_worktree/opencraft-tb1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 测试
cd build && ctest            # 457/457 通过（存量 438 + 新增 19）

# 干净复验（另一棵 /tmp build，与告警对照同源）
cmake -S . -B /tmp/tb1_clean_build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/tb1_clean_build -j
cd /tmp/tb1_clean_build && ctest     # 457/457

# 实机（cwd 必须在 build 下，资产根走 ../assets）
cd build && ./opencraft
```

| 项 | 基线树 @ 13896c8 | 本分支 |
|---|---|---|
| 构建告警集合 | 6 条存量 `-Wunused-result` + 2 条存量 `ld` 重库告警 | **逐条相同（diff 为空）** |
| `ctest` | 438 / 438 | 457 / 457（+19） |

告警对照方法与原文：`07_build_warnings.txt`（两棵树各自从零 configure+build，去前缀排序 diff）。

---

## 8. ★ 只有实机才能抓到的两个缺陷（都已修）

两个都在**渲染路径**上，单测覆盖不到（`docs/05 §3.1` 第 5 条），是实机截图抓出来的：

1. **调色板贴图顶掉了图集**（第一版 `02_branch_models.png`：模型正确、**整个世界全黑**）。
   `render::Texture2D` 的构造函数在**当前激活的纹理单元**上 `glBindTexture`（它自己不调
   `glActiveTexture`），而图集只在启动时 `atlas.bind(0)` 一次、之后再没重绑 ⇒ 上传调色板时
   激活单元正好是 0，图集被一张 16×16 调色板替换，区块 pass 采到的大多是第 0 号未用格。
   修法：`make_gpu_model()` 上传调色板前显式切到它自己的单元（2），结束后切回 0。
2. **调色板 UV 差一格**（第二版截图：头显示成躯干色、**躯干整块消失**）。
   `palette[colorIndex]` 就是纹理的第 `colorIndex` 号像素，顶点 UV 必须按 `colorIndex` 取格，
   不能按"文件里的 entry 号"（entry = colorIndex − 1，那是**加载时**已经做掉的平移）。
   差一格让 `colorIndex 1`（躯干）采到从不使用的 0 号格（alpha = 0 ⇒ `discard`），其余部位各错一格。
   **教训已落进测试**：原测试只是把 mesher 的公式复述了一遍（"测试复述被测代码就不会失败"）；
   现在的断言先写出纹理布局，再要求"被采样的那格就是该颜色的 RGBA"。

---

## 9. 已知问题 / 边界 / 建议（不顺手改）

1. **关节枢轴是约定，不是数据**：腿/臂取分部包围盒**上缘中点**（肩/胯）、头取**下缘中点**（颈）、
   其余取包围盒中心。`.vox` 里没有枢轴通道，加一个就得再发明一条标注约定（research §4.3 已经
   用满"调色板索引分段"）。若 B2/B3/B4（美术）反馈某生物需要别的枢轴，先登记再动
   `mob_mesh.cpp` 的那三行规则（一处集中）。
2. **模型缩放口径**：统一缩放，使**体素占位高度** = 该生物 `MobDef::physics.height`，脚底中心
   对齐 `Entity::position`（与其他实体同约定）。⇒ 美术侧在体素编辑器里"看着合适"即可，但**加高
   一个附件会把整只生物压小**。若美术侧偏好固定密度（1 体素 = 1/16 格），改一行即可，建议
   在 B2 出货前定案。
3. **行走频率/摆幅全是 待校准**（research §4.5 无来源）：全部集中在 `mob_pose.hpp` 的
   5 个具名常量 + `kMobPhasePerBlock`（当前口径：约 0.8 格一个步态周期）。
4. **`ai.pitch` 的符号**按 `view_dir()` 的口径取负（正 pitch = 低头）；sim 侧只有
   `LookAtPlayer` 会写 `ai.pitch`，若将来出现别的写者，这条要重新核对。
5. **不持久化**：实体仍不落盘（`level_file.hpp` 无实体表）⇒ 存档里存不下生物，重进世界生物是
   重新自然刷的。这不是本卡范围，但会让"给某个生物截图"必须靠 §3.1 的取证装置。
6. **每实例姿态缓存**：`MobAnimClock` 用 `unordered_map<EntityId, phase>`，每帧末清理未被绘制的
   id（槽位 LIFO 复用不会继承上一任的步态）。70 只同屏时是 70 条哈希项，不是瓶颈，但若将来
   上到上千生物，应改成按 store 槽位下标的数组。
7. **调色板 PNG 走的是 16×16 硬约束**（复用 T-A2 的 `load_tile_png`），所以**每个生物最多 255
   个可用颜色**（0 号格是"从不使用"的索引 0）。这与 `.vox` 的索引空间一致，不是新限制。
   PNG 的**格号就是颜色索引号**（第 1 格 = 索引 1），与 `.vox` 文件内部那个"`RGBA` 第 0 条 =
   索引 1"的错位**无关**（那个平移只在解析 `.vox` 时发生一次）。
8. **`RGBA` 块按"读到多少算多少"接受**（可短于 256 条）：这是为最小写手/手写模型放宽的
   （PM 的独立夹具正是 3 条），缺的条目 = 未使用（透明）。**截断仍会被拒**，所以这不是
   "对损坏文件更宽容"。若将来要收紧，先看美术侧是否还会给短调色板的文件。
9. **`mob_skin()` 未清理**（卡面 §5 明令留给"回退分支被删除的那张卡"）：它仍借方块 id，写错是
   硬崩溃（`inventory_wiring.hpp:245`）。本卡的模型分支**不读它**。
10. **建议给 PM 的一条**：`docs/qa/T-B1-2026-09-18/README.md` 里的取证装置（钉住一只生物 +
   关掉自然刷怪）是**可复用**的：B2/B3/B4 每出一只模型都要"同一机位 A/B"来证明"换了模型"，
   建议把 `tools/run_evidence.sh` + `tools/apply_evidence_patch.py` 的用法写进 `docs/05`，
   免得每张美术卡重新发明一遍（并再次踩"HID 注入不稳定"的坑）。

---

## 10. 验收标准逐条对照（卡面 §7）

| # | 条目 | 结果 | 证据 |
|---|---|---|---|
| 1 | 存量 438 全绿、存量测试文件零改动 | ✅ | 基线 438/438、本分支 457/457；`git diff tests/test_*.cpp` 只有 `test_mob_model.cpp` 是**新增**文件，无一处改动存量文件 |
| 2 | ★ 零变化判据（逐像素 + 装置自证） | ✅ | `04_pixel_diff.txt` 第 1/2/3 条；基线树 vs 本分支 **0 / 957 440**，且装置能分辨两棵树（39 176） |
| 3 | B0 夹具入仓 + 解析器单测 | ✅ | `tests/fixtures/mobs/`；断言尺寸/体素数/逐体素坐标与调色板索引 |
| 4 | 回退表 7 行全覆盖 | ✅ | §4 表；单测逐行，含"故意损坏"变体（截断、签名、撒谎、越界、0 体素）；并另用 PM 的 8 个独立损坏夹具交叉核对（§6.5） |
| 5 | ★ 启动计数日志 0 → 1 | ✅ | §3.4 A/B 段：`mobs: 1/3` / `mobs: 0/3` |
| 6 | 朝向只读模拟值 | ✅ | §5：渲染侧 0 处三角函数 + 72 组输入下 `yaw/pitch` 逐位不变的断言 |
| 7 | CULL_FACE 成对 | ✅ | §6：两分支各自成对；无模型时模型 pass 完全不执行 |
| 8 | 实机证据（不再是两个立方体） | ✅ | `run/02_branch_models.png`（对比 `run/00/01`） |
| 9 | `engine/render/**` 零 diff、`game/server/**` 零 diff | ✅ | `06_forbidden_paths.txt` |
| 10 | 禁碰项零改动 | ✅ | 同上（逐目录 0 文件改动） |

---

## 11. 证据清单

`docs/qa/T-B1-2026-09-18/`：

| 文件 | 内容 |
|---|---|
| `README.md` | 装置说明、六组比对、产品码日志、交叉核对、两个实机缺陷的复现与修法 |
| `00_base_nomodels.{png,log,_b.png}` | 基线树 · 无模型 |
| `01_branch_nomodels.{png,log,_b.png}` | 本分支 · 无模型 |
| `02_branch_models.{png,log,_b.png}` | 本分支 · 有模型（可辨证据模型） |
| `03_evidence_patch_applied.diff` | 取证补丁的完整 diff（**还原前**存档） |
| `04_pixel_diff.txt` | 四组逐像素比对 |
| `05_product_log_check.txt` | 产品码日志三连（夹具在/不在/损坏） |
| `06_forbidden_paths.txt` | 禁碰目录零改动核对 + 全量改动清单 |
| `07_build_warnings.txt` | 两棵树从零构建的告警逐条对照 + 打点残留自检 |
| `08_facing_and_cull_review.txt` | 朝向与 CULL_FACE 的机器复核记录 |
| `09_pm_fixture_crosscheck.txt` | 用 PM 的独立夹具跑本实现解析器的逐条对齐记录 |
| `run/` | 各次运行的原始日志与截图（含窗口号、PID）：00 基线无模型 / 01 分支无模型 / 02 分支有模型 / 03 分支有模型+调色板 PNG |
| `tools/` | `run_evidence.sh`、`apply_evidence_patch.py`、`check_product_log.sh`、`pixel_diff.py`、`tb1win.m`、`make_evidence_model.py`、`evidence_mossback.vox` |

**打点残留自检**：`git diff game/server --numstat` = 0 行；`build/opencraft` 内 `EVIDENCE` 字样
计数 = 0；`world_sim.cpp` 与基线树**逐字节相同**。

---

## 12. 给项目经理的备注

1. **本卡的两条自证都做到了**：零变化判据不是"我看了觉得没变"，而是 0/957 440 的逐像素
   A/B，且先用"装置能分辨两棵树"（39 176 像素）自证装置有效；两条比对都必须看。
2. **实机抓到两个仅靠单测抓不到的缺陷**（§8），其中第一个（图集被顶掉、世界全黑）在
   无模型时**完全不会出现** —— 这也说明"零变化判据通过"并不自动覆盖"有模型时正确"，
   B2/B3/B4 每只生物都还需要自己的实机 A/B。
3. **留给美术侧的两个待定案**（§9 第 1、2 条：枢轴约定、缩放口径）建议在 B2 出货前拍板，
   它们决定美术在 MagicaVoxel 里的画法，晚改要重导模型。
4. **本卡不产出任何生物模型**：`assets/**` 零改动（含临时用的 `assets/mobs/` 已删除并核对）。
5. 分支 `task/T-B1-mob-voxel` 已按 task 粒度提交，未动 `STATE.md` / `docs/0X`（单写者原则）。
