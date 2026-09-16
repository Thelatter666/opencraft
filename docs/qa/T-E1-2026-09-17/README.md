# T-E1 实机证据　2026-09-17（已完成）

> 分支 `task/T-E1-entity-layer`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-E1`
> 二进制：`build/opencraft`（Release）**md5 `2deca4791afd8d1436bc7b4e6773c995`**
> 环境：macOS 24.6.0（arm64）；窗口 `2466 @ (320,89) 1280×748`（按 PID 反查 + 尺寸过滤）；
> 单实例确认：每次运行前 `ps` 无其它 opencraft 进程；取证结束后已 kill 自己的实例。

## 结论

验收 12 的四步链路**在实机上逐帧可见 + 日志可证**：
`挖方块 → 掉落物出现（画面可见的 0.25 立方）→ 掉落物消失 → 快捷栏计数 32 → 33（进库存）`。

## 证据 A：画面（正式二进制，md5 见上）

| 文件 | 内容 |
|---|---|
| `00_pristine_spawn.png` | 干净启动的出生点（早前一次启动，作为环境证据） |
| `A1_pristine_hotbar32.png` / `A1b_hotbar_zoom_32.png` | 挖之前：快捷栏第 2 格 `sod_loam` 计数 **32** |
| `A2_aim_straight_down.png` | 视角压到 −89° 钳位（准星正对脚下那块，选中线框可见） |
| `A3_digging_crack.png` | 按住左键挖掘中（裂纹叠加层 + 破块粒子） |
| **`A4_drop_visible_a.png` / `A5_drop_visible_b.png` / `A6_drop_visible_c.png`** | **掉落物可见**：坑里一个 0.25 立方、草皮纹理、**带旋转**的小方块 |
| `A5b_drop_magnified.png` | 上帧放大（掉落物立方体 + 旋转姿态一目了然） |
| `A7_after_walk.png` | 按 W 之后：掉落物已消失（被拾取） |
| `A8_settled_hotbar33.png` / `A8b_hotbar_zoom_33.png` | **快捷栏第 2 格 `sod_loam` 计数 33** |

判据为什么决定性：起始快捷栏里 `sod_loam` 只有 **32** 个，除了"拾取了刚挖出来的那个掉落物"
之外没有任何来源能让它变成 33；而掉落物在 `A4/A5/A6` 里肉眼可见、在 `A7` 里消失，
两件事发生在同一段时间内，且中间**没有其它世界改动**。

## 证据 B：常驻日志（正式二进制）

`session_final_PRISTINE_binary.log`：
```
spawn scan: surface at (0.5, 132.0, 0.5)
item drop sod_loam spawned at (0.50, 131.38, 0.50) [entity 1]
```
即本卡新增的**唯一**常驻日志（`WorldSim::apply_dig` 成功后一行），它把"哪一个方块被挖"
与"哪一个掉落物被生成"钉在一起。该日志文件里 **PROBE 行数 = 0**
（`grep -c PROBE` = 0，且 `strings build/opencraft | grep -c PROBE` = 0）。

## 证据 C：拾取决策的临时打点（打点版二进制，已还原）

卡面只允许一条常驻日志，拾取路径因此**没有**常驻日志。拾取的内部决策用项目既有手法
（临时打点 → 取证 → 还原）取到，`session_transient_probe.log`：

```
item drop sod_loam spawned at (0.50, 131.38, 0.50) [entity 1]
PROBE pickup: 1 candidate(s) in box, first=(0.50, 131.13, 0.50)
PROBE pickup: took item sod_loam x1 (entity 1)
PROBE render: drops=0 first=(0.00, 0.00, 0.00)
```
四行分别证明：权威侧生成了掉落物 → **客户端的拾取几何把这一个掉落物判为在拾取盒内** →
`submit(PickUp)` 被接受、库存副本提交成功 → 权威侧存储里该实体已消失。
打点位置与还原校验见 `transient_probe.patch.txt`（含 `strings`/md5 双证；还原后 md5 与打点前**逐字节相同**）。

## 取证手法（`tools/`）

- `te1input.m`：HID 层注入（`CGEventPost(kCGHIDEventTap)`，**不用 osascript**；
  鼠标视角用 `kCGMouseEventDeltaX/Y` 增量字段）。本卡新增两个子命令：
  `trust`（`AXIsProcessTrusted()` 探针）与 `front`（AX `kAXFrontmostAttribute` +
  `kAXRaiseAction`）——因为 macOS 14+ 的 `NSApplicationActivateIgnoringOtherApps` 已是空操作，
  旧的 activate 单独用无法把非 bundle 可执行文件的窗口切到前台（本轮第一次取证就卡在这里）。
- `te1_vertical_scene.py`：逐帧校验的取证脚本（每次视角注入比对前后帧像素；
  每次长按每 60 ms 重发 key/button-down，防失焦清状态）。
  **视角标定结论**：灵敏度是文档写明的 0.0025 rad/px，但注入要**分小步**下发；
  本脚本索性把俯仰一路压到客户端自己的 −89° 钳位，从而**确定性地**瞄准"脚下那块"
  （这是唯一保证下方是实心地面的列——前方 2-4 格的地表下是空洞，掉落物会掉进竖井，
  见报告 §6.2）。
- `te1_aimed_scene.py`：第一版脚本（水平前方挖掘场景，含选中线框自动识别），
  用于定位"前方地表下有竖井"这件事。

## 本轮取证暴露并修掉的真问题（报告 §7.6）

**掉落物渲染矩阵错位**：模型矩阵里把缩放后的立方体错误地平移了 0.5（应为 `0.5 × 边长`），
掉落物被画到地面以下 ⇒ 画面里根本看不到它。单测覆盖不到渲染，这一条**只有实机逐帧比对能发现**
（相邻两帧逐像素相同 = 没有东西在动）。修复后同一场景可见 0.25 立方并带旋转姿态（`A5b_drop_magnified.png` 里立方体明显是斜的）。
诚实说明：`A4/A5/A6` 三帧之间整帧差异很大，**主因是玩家挖掉脚下那块后落进坑里导致相机位移**，
因此这三帧**不能**用来单独证明自转/浮动的动画速率（本卡验收只要求"最小可见"）；
动画本身按 `research/11 §4.5` 实现（1 rad/s 自转、Y ∈ [0.0625, 0.2625]、周期 π s），
其**速率**未做独立实测。
