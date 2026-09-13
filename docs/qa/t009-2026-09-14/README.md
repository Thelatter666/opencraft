# T009 验收证据（开发者实拍/实测，2026-09-14）

> **目录位置**：本目录原为开发者自建的 `artifacts/t009-qa/`（该路径不在 T009 卡白名单内，
> 开发者已在报告中报备）。**PM 于 2026-09-14 裁决：归入 `docs/qa/` 既有惯例**，
> 由 PM 以 `git mv` 移动（无代码依赖）。此后证据目录统一置于 `docs/qa/<task>-<date>/`，
> 见 `docs/05-development-process.md` §2 规则 5 的例外说明与 `docs/tasks/README.md`。
> 移动后路径引用以本目录为准；下方表格内的文件名不变。

> **P2 相关文件已由 PM 交叉引用**：本目录的 `P2_*.png`、`P2_diag_*.log` 是
> `docs/qa/p2-esc-2026-09-14/`（PM 观察记录）的**最终结论依据**，两处请对照阅读。


截图工具：`screencapture -x -o -l<windowID>`（窗口截图，未截取桌面）。
输入注入：`/tmp/t009_tools/inj`（CGEventPostToPid）与 `/tmp/t009_tools/inj2`
（CGEventCreateKeyboardEvent + CGEventPost(kCGHIDEventTap)，系统级、与物理输入同路径）。
构建：`build/opencraft`（worktree `/Users/happy/Desktop/opencraft-t009`，分支 `task/T009-persistence`）。

## HUD（验收标准第 4 条的“各一张”）

| 文件 | 内容 | 机器可复核的判据 |
|---|---|---|
| `A1_hud_hotbar_hearts_full.png` | 快捷栏 9 格 + 满血 10 心 + 选中格白框 + 方块名 | 9 格图标各自平均色不同；第 0 格四周 2px 白框；红色心形像素约 350+ |
| `A2_hud_hearts_after_fall_damage.png` | 摔落 14 格后：**4 满心 + 1 半心 = 4.5 心 = 9 HP** | 与 `floor(14−3)=11` 伤害公式一致（20−11=9）；半心左半红、右半空心 |
| `A3_break_particles.png` | 破坏粒子（圆石主色，约 20 粒，存活 0.4–0.65 s） | 破坏点周围出现灰色点状像素簇（触发方式：轮询日志出现 `remeshed` 的同一帧立刻截图） |
| `A4_swing_rest.png` / `A5_swing_mid.png` | 挥手动画：静止位 vs 摆到位（同一机位连续帧） | 手持方块包围盒 x 从 916 → 792、灰色像素 4520 → 7416（+64%），第 6 帧与第 1 帧逐值相同（周期 0.25 s 闭合） |
| `A6_mining_crack_overlay.png` | 挖掘中：10 阶段裂纹叠加 + 手持方块摆动 | T008 既有能力，本卡回归确认 |

## P2（ESC 暂停/恢复）

| 文件 | 内容 |
|---|---|
| `P2_1_hid_esc_paused.png` | 系统级（HID）ESC 第 1 次 → 菜单出现（白字像素 919、场景亮度 459990→198067） |
| `P2_2_hid_esc_resumed.png` | 系统级 ESC 第 2 次 → **恢复**（白字 0、场景亮度回到 459990 与基线逐值相同） |
| `P2_diag_cursor_disabled.log` | 诊断程序（`/tmp/t009_tools/p2_diag disabled 12`）：System Events 的 ESC 回调 4 次、**轮询边沿仅来自 HID 事件**；脉冲宽 4.0/2.0 ms（System Events）对 87/86 ms（HID） |
| `P2_diag_cursor_normal.log` | 同上，CURSOR_NORMAL：脉冲宽 5.0/2.0/3.0 ms —— 与光标模式无关 |
| `P2_diag_delivery_matrix.log` | 同一进程上四种投递方式对照，含 pid 定向投递出现 REPEAT/丢 keyup 的现象 |

## 存读档（验收标准第 3 条）

`evidence.txt` 汇总以下日志摘录，原始日志在 `/tmp/t009_e2e/*.log`（临时目录，如已清理请按
报告中的命令重放）：

- 新档：`save: no level.ocd, new world with seed 0x4f50454e43524146` + `spawn scan: surface at (0.5, 132.0, 0.5)`
- 读档：`save: loaded level.ocd (seed=..., ticks=..., player=(0.50, 132.00, 0.50), hp=20.0)`
- 脏区块落盘：`autosave: 1 chunk(s) queued for async write, level written (ticks=6588)`
- 退出 flush：`save: flushed on exit (ticks=5990, chunks cached=4)` + `clean shutdown`
- `kill -9` 后重启：`loaded level.ocd` + `chunk (0, -1) loaded from disk`，无 `rejected/corrupt`
- 未修改区块不落盘：区域文件 `r.0.-1.ocr` 内**仅 1 个区块**（正是被修改那块）
