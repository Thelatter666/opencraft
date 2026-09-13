# P2 缺陷证据包：ESC 暂停态下再次 ESC 无法恢复（2026-09-14）

缺陷：游戏暂停后，按 ESC 应恢复（main.cpp:697 附近 `paused = !paused` 切换逻辑），实测不恢复。
暂停方向（运行中按 ESC → 出菜单）正常。修复归属：T009。

## 环境

- 构建：main `ffc37f1` 之后的 T008 分支 tip `ba626e5`（构建于 worktree `~/Desktop/opencraft-t008`）
- 进程：`./build/opencraft`，pid 10159；窗口 windowID 3579（`CGWindowList` 查得，bounds 320,89 1280×748）
- 系统：macOS 15，Apple Silicon

## 复现命令（逐条，可直接重放）

```bash
# 0) 前置：游戏运行中（未暂停），基线见 00_baseline_gameplay_running.png
# 1) 确认前台进程是 opencraft（System Events 键盘事件只达前台 app）
osascript -e 'tell application "System Events" to set frontmost of (first process whose unix id is 10159) to true'
osascript -e 'tell application "System Events" to get name of first process whose frontmost is true'
#    → 实际输出：opencraft
# 2) 发送 ESC（key code 53）
osascript -e 'tell application "System Events" to key code 53'
sleep 1
screencapture -x -l3579 shot.png
#    → 实测：暂停菜单出现（01_esc_pause_engaged_SystemEvents_key53.png）
#    → 证明：System Events 键盘事件可达 GLFW（推翻早前"System Events 对 GLFW 不生效"记录）
# 3) 暂停态下再次发送同样的 ESC（重复了 4 次，每次均先确认 frontmost=opencraft）
osascript -e 'tell application "System Events" to key code 53'
screencapture -x -l3579 shot2.png
#    → 实测：菜单仍在（02、03 两张实拍）。恢复方向不复现 = P2 缺陷
# 4) 对照：合成鼠标点击 RESUME 按钮也不生效（CGWarpMouseCursorPosition(992,457) +
#    CGEventCreateMouseEvent kCGHIDEventTap click；指针位置经 CGEventGetLocation 核实为 992,457）
```

## 图片清单

| 文件 | 内容 |
|---|---|
| 00_baseline_gameplay_running.png | 运行中未暂停基线（T008 开发者实拍） |
| 01_esc_pause_engaged_SystemEvents_key53.png | 第 1 次 ESC 后：PAUSED 菜单出现（暂停方向 ✅） |
| 02_after_2nd_esc_still_paused.png | 第 2 次 ESC 后：菜单仍在（恢复方向 ❌） |
| 03_after_4th_esc_frontmost_confirmed_still_paused.png | 第 4 次 ESC（每次均重新确认 frontmost）后：菜单仍在 ❌ |
| dev_evidence_quit_evidence.txt | T008 开发者证据：QUIT 路径退出码 0（RESUME 按钮路径有其 pause_final 实拍） |

三张 01/02/03 字节数一致（438854）——游戏暂停后不跑 tick、画面定格，逐次截图逐字节相同，
本身即是"每次 ESC 后状态未变"的佐证。

## 结论与修复提示

- 暂停方向（运行→菜单）：System Events 键盘可达且生效。
- 恢复方向（菜单→运行）：真键盘 ESC 不响应；代码 `paused = !paused` 看似对称，需诊断
  （怀疑点：暂停分支里 `glfwSetInputMode(CURSOR_NORMAL)` 后按键回调/边沿状态被吞，
  或 `key_pressed` 边沿表在暂停态未被正确轮询/清除）。
- 本证据包由 PM 对话 2026-09-14 生成；复现时 pid/windowID 会变，命令模板不变。
