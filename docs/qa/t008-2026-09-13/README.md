# T008 验收证据（开发者实拍/实测，2026-09-13 → 归档 2026-09-14）

> 来源：开发者 T008 会话留下的 `/tmp/opencraft-t008-accept/`（临时目录，随时可能被系统清理）。
> PM 于 2026-09-14 清理并行 worktree 前归档，**仅去掉 `_small` 缩略图与编译产物**（原件未改）。
> T008 验收结论见 `STATE.md`；卡面原文见 `docs/tasks/T008.md`。

## 截图（按验收项分组）

| 文件 | 内容 |
|---|---|
| `shot1_spawn.png` | 出生点第一人称视角：T004 真实地形 + 线框选取 |
| `shot2_mining.png` | 挖掘中：10 阶段裂纹叠加 |
| `crack_mid.png` / `crack_zoom.png` / `crack_mid_zoom.png` | 裂纹中段特写（第 6+ 阶段） |
| `shot3_broken.png` / `shot3_broken_zoom.png` | 挖破后：露出内部、洞壁变暗（光照随方块修改更新） |
| `shot4_placed.png` | 右键放置方块 |
| `shot4_esc.png` | ESC 暂停菜单 |
| `pause_final.png` / `pause_final2.png` | 暂停菜单 + RESUME 恢复后画面 |
| `pause_v.png` / `pause_v3.png` / `pause_v4.png` / `pause_v5.png` | 暂停菜单迭代过程帧 |
| `aim.png` / `forced.png` / `forced_zoom.png` / `now.png` / `broken_zoom.png` | 取景/调试中间帧 |

## 记录与工具

| 文件 | 内容 |
|---|---|
| `exit_code.txt` | `EXIT_CODE=0` —— **开发者自述的文本记录，非机器产物**（PM 未独立复现） |
| `game.log` | 运行日志（放置/挖掘事件与 `clean shutdown`） |
| `tools/findwin.m` | 窗口定位：`CGWindowListCopyWindowInfo` 找 opencraft 窗口号与 bounds |
| `tools/input.m` | 输入注入器（键盘 `CGEventPostToPid` / 鼠标 / 指针锁定 delta） |
| `tools/probe.cpp`、`tools/probe2.cpp` | 诊断探针 |

> ⚠️ 工具用法注意：`tools/input.m` 的键盘路径（`CGEventPostToPid`）**已被 T009 诊断推翻**为不可靠——
> 会偶发只送 keydown 丢 keyup；且其兄弟路径 `osascript key code` 脉冲仅 2–5 ms，短于每帧轮询。
> 现行定论：**脚本化键盘验收必须用 HID 层**（`CGEventPost(kCGHIDEventTap)`，脉冲 86–87 ms）或按住 ≥50ms。
> 详见 `docs/qa/p2-esc-2026-09-14/` 与 `docs/qa/t009-2026-09-14/`。

## 编译工具（如需复现）

```
clang -fobjc-arc -framework Foundation -framework CoreGraphics -o /tmp/findwin tools/findwin.m
clang -fobjc-arc -framework Foundation -framework CoreGraphics -o /tmp/input   tools/input.m
```
