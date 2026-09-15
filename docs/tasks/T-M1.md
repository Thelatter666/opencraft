# 任务 T-M1：`main.cpp` 拆分（纯重构，零行为变化）

里程碑：M2a 地基　前置：无（当前 main `fab4d0b`）
基线：**267/267**。运行前请先跑基线确认。
卡面落盘日期：2026-09-16（PM）

---

## 目标（一段话）

把 `/Users/happy/Desktop/opencraft/game/client/src/main.cpp`（**1628 行**）按职责拆成若干
可独立编译的单元，**行为零变化**（267 测试全绿 + 实机表现一致）。

**这是纯重构：不修 bug、不加功能、不改任何数值。**

## 为什么（PM 的结构判断，2026-09-16）

用户裁决「结构优先」后，PM 扫了仓库规模，发现这是**当前第一号冲突源**：

| 事实 | 影响 |
|---|---|
| `main.cpp` 1628 行，全仓最大文件 | T-D1 / T-D7 / T-D8 / T-D13 / T-D14 / T-F1 **每张卡都在它身上动刀** |
| 它同时是：GL 初始化、着色器、字体、UI 绘制、输入映射、tick 循环、区块流式、存档、QA 埋点 | 任何改动都要在 1600 行里找位置；现在只靠"串行发卡"规避冲突 |
| M2 要加库存 UI / 合成 UI / 战斗 HUD / 生物渲染 | 不拆会到 3000+ 行 |

拆完之后，**每张后续卡只碰自己那一块**，冲突概率大幅下降；也才谈得上以后并行。

## ★ 范围（严格）

**做**：按职责拆分为若干文件（建议边界见下，但**具体切法你定**）。

**不做**（越界即返工）：
- ❌ **不修任何 bug**（发现 bug 写进报告的建议表，别顺手改）
- ❌ 不加任何功能/数值/UI 元素
- ❌ 不改任何已有逻辑的分支条件、顺序、常量值
- ❌ 不改 `engine/**`、`game/common/**`、`game/server/**`
- ❌ 不改 `STATE.md` / `docs/` 规格 / 记忆层
- ❌ 不做"顺手美化"：命名可以跟着搬家调整，**逻辑不动**

**核心判据**：`git diff` 应该看起来像"代码搬家"，不像"代码改写"。

## 现状（PM 已核实，可直接用的锚点）

`game/client/src/main.cpp` 结构：

| 行 | 内容 |
|---|---|
| 1–45 | include 与 namespace 别名 |
| 47–60 | 窗口/视角/眼高常量 |
| 62–232 | **10 段 GLSL 着色器源码**（chunk/wire/crack/particle/ui-flat/ui-text） |
| 234–237 | `error_callback` |
| 239–301 | `ChunkLayer` / `ChunkRenderable` / `chunk_key` |
| 303–331 | `CubeGeometry`（线框与裂纹盒几何） |
| 333–455 | **5×7 位图字体**（`kGlyphs` 数据表 + `FontImage` + `draw_text` + `draw_rect`） |
| 457–484 | `block_main_colors`（破坏粒子取色） |
| 486–510 | **破坏粒子系统** |
| 512+ | `int main()` |
| └ 549–566 | 存档 / 世界 / atlas / 几何 / 着色器 / GL 状态 / 玩家状态 / 快捷栏 |
| └ 576–1075 | **主循环**：鼠标视角 → 固定步长 tick → 流式加载 → 相机 → 渲染 → HUD → 暂停菜单 |
| └ 1212+ | 退出 flush |

其中 `run_tick` 是个 lambda（约 345–560 行区间），内含输入映射、Auto-Jump 注入、
选取、挖掘、放置、水桶、流体刻、autosave。

**已有先例可仿**（`game/client/src/` 下）：
- `fov.hpp`（T-D1）、`camera_spring.hpp`（T-D13）—— 纯函数/小类放可测头文件
- `atlas.hpp` / `world.hpp` —— 数据 + 声明分离，`.cpp` 放实现
- `gl.hpp`

## 建议的切分（**仅供参考，你自行判断**）

```
game/client/src/
  main.cpp            ← 只留：main()、主循环骨架、各模块接线
  shaders.hpp/.cpp    ← 10 段 GLSL 常量
  font.hpp/.cpp       ← 5×7 位图字体 + draw_text / draw_rect
  hud.hpp/.cpp        ← 快捷栏 / 生命条 / 准星 / 方块名
  pause_menu.hpp/.cpp ← 暂停菜单（含 Auto-Jump 开关）
  particles.hpp/.cpp  ← 破坏粒子
  chunk_renderer.hpp/.cpp ← ChunkRenderable / ChunkLayer / 区块绘制与流式
```

**不必**把所有东西都拆出去——目标是让每块"职责单一、可独立读懂"，不是追求文件数量。
若你认为某块拆出来反而更绕（比如 `run_tick` 里几个短逻辑），留在 `main.cpp` 完全没问题。

## 接口契约

### 冻结项（不得改动语义）

- 所有 ⚖ 数值与常量：`kMouseSensitivity`、`kMaxPitch`、`kReachDistance`（4.5）、
  `kViewRadius`、`kGenPerFrame`、`kNewMeshPerFrame`、`kEyeStanding`(1.62)、
  `kEyeSneaking`(1.27)、`fov.hpp` 的 FOV、`camera_spring.hpp` 的弹簧参数。
  **搬家可以，改值不行。**
- `physics` / `voxel` / `render` / `game` 各层的公开接口一律不动。
- 着色器源码**逐字节不动**（GLSL 字符串搬家，不重排、不改空格）。

### 可自由决定

- 文件划分粒度、命名、头文件能否 header-only
- 是否把 `run_tick` 提成函数（若提成函数需传的状态过多，留在 lambda 也行）
- 匿名 namespace 的成员如何分配到新文件

## 允许触碰的文件/目录（白名单）

- `game/client/src/**`（主体；可新增文件）
- `game/client/CMakeLists.txt`（新增源文件时按现有模式追加）
- `tests/**`（**仅当**你拆出了值得单测的纯函数时；可选，不强制）

⚠️ **禁碰**：`STATE.md`、`docs/**`（`docs/tasks/T-M1.report.md` 是你**必须写**的报告，
属例外）、`engine/**`、`game/common/**`、`game/server/**`、`cmake/`、根 `CMakeLists.txt`、
`.github/`、`assets/`。

## 验收标准（逐条可执行）

1. **测试全绿**：`267/267`，一条不多一条不少。
2. **零行为变化**（本卡的核心）：
   - 所有 ⚖ 常量值逐字节不变（可用 `git diff` 核对：常量搬家可以有，数值改动必须为零）；
   - 着色器 GLSL 字符串逐字节不变；
   - 字体数据表 `kGlyphs` 逐字节不变。
3. **`main.cpp` 显著变短**：目标 **≤ 700 行**（现 1628）。若你认为合理切法会略超，
   在报告里说明即可，不必硬压。
4. **无新增编译警告**：`-Wall -Wextra -Wpedantic` 下与基线一致（不要求零警告，
   但**不得比基线更多**）。
5. **clang-format 无 diff**（用 **CLT 的 17**：
   `/Library/Developer/CommandLineTools/usr/bin/clang-format`，**不要用 brew 的 23**）。
6. **实机确认**：启动游戏，走/跳/挖/放/倒水/开暂停菜单/AUTO-JUMP 开关/退出存盘，
   与拆分前表现一致。⚠️ 若用脚本注入按键，**必须 HID 层**，禁用 `osascript`；
   本机 HID 注入时灵时不灵（`docs/05` §3.1），把决定性场景放在生效窗口内。
7. 独立 worktree（根 `/Users/happy/Desktop/opencraft_worktree/`）；提交前缀 `taskT-M1:`。
8. **报告**：落 `/Users/happy/Desktop/opencraft/docs/tasks/T-M1.report.md`，
   对话中**只输出简短版 + 该路径**，用 **txt 代码块**包裹（`docs/05` §2 规则 4）。
   报告须给出**拆分前后的文件与行数对照表**。
9. 完成后置 agentmemory action（见文末）为 done。

## 已知风险与提示

- **构建不要接管道**（`| tail` 吞退出码）；判成败用 `cmd > log 2>&1; echo $?` 或搜 `error:`。
- **绝不要把 `FETCHCONTENT_BASE_DIR` 指向主仓库 `build/_deps`**（PM 已两次因此挂掉主仓构建）。
  worktree 里**不设**该变量，让它自行 configure（约 75–90 s，需联网）。
- 可执行文件在 `build/opencraft`。
- **合规红线**：本卡不新增任何外部来源，但**注释搬家时不得引入反编译片段**（沿用即可）。
- **不得直接改状态与规格**（P-001）：建议一律以建议表写进报告交 PM 落盘。
- **建议小步提交**：每拆出一块就提交一次（如 `taskT-M1: 拆出字体模块`），
  这样出问题好二分定位，PM 也好看 diff。**不要**一个大提交把 1600 行全打散。
- **发现 bug 不要顺手修**：写进报告建议表，由 PM 决定是否单列卡。
  理由：混进重构会让"零行为变化"这条判据失效，出事分不清是重构引入还是修 bug 引入。

## 附：PM 为什么坚持"这次玩家看不出变化"也值得做

用户选择了"现在就拆"而非"把拆分藏在一张有产出的卡里"。
理由是：后面每张卡都要在 `main.cpp` 里找位置，早拆早受益；
且纯重构的验收判据（267 全绿 + 常量逐字节不变）足够硬，风险可控。

**但你要意识到**：这张卡做完后，用户看不到任何新东西。这是预期内的，不是失败。

## agentmemory

完成后置 **`act_mu30kxo3_a80cd87120d2`** 为 done。
