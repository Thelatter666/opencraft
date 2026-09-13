# P2 观察记录：ESC 暂停态下再次 ESC 未恢复（合成事件路径，2026-09-14）

观察：游戏暂停后，按 ESC 应恢复（main.cpp:697 附近 `paused = !paused` 切换逻辑），
经 **System Events 合成键盘事件**触发时未恢复。暂停方向（运行中 → 出菜单）正常。
**真键盘路径未验证（见下文"关键限定"）；修复归属与是否动代码取决于真键盘结果。**

⚠️ 数据来源限定：本包全部键盘证据均为 `osascript ... key code 53`（System Events 合成事件），
**无物理键盘参与**。

## 环境

- 构建：main `ffc37f1` 之后的 T008 分支 tip `ba626e5`（构建于 worktree `/Users/happy/Desktop/opencraft-t008`）
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
#    → 实测：菜单仍在（02、03 两张实拍）。合成事件路径下恢复方向不复现（真键盘待证，见上文限定）
# 4) 对照：合成鼠标点击 RESUME 按钮也不生效（CGWarpMouseCursorPosition(992,457) +
#    CGEventCreateMouseEvent kCGHIDEventTap click；指针位置经 CGEventGetLocation 核实为 992,457）
```

## 图片清单

| 文件 | 内容 |
|---|---|
| 00_baseline_gameplay_running.png | 运行中未暂停基线（T008 开发者实拍） |
| 01_esc_pause_engaged_SystemEvents_key53.png | 第 1 次 ESC 后：PAUSED 菜单出现（暂停方向 ✅） |
| 02_after_2nd_esc_still_paused.png | 第 2 次 ESC 后：菜单仍在（恢复方向 ❌，合成事件路径） |
| 03_after_4th_esc_frontmost_confirmed_still_paused.png | 第 4 次 ESC（每次均重新确认 frontmost）后：菜单仍在 ❌（合成事件路径） |
| dev_evidence_quit_exit_code.txt | **开发者自述的文本记录，非机器产物**：内容为 `EXIT_CODE=0` 一行，由开发者在 T008 验收时提供，PM 未独立复现该退出码、亦无可追改的进程记录。仅供参考，不作机器证据 |

三张 01/02/03 字节数一致（438854）——游戏暂停后不跑 tick、画面定格，逐次截图逐字节相同，
本身即是"每次 ESC 后状态未变"的佐证。

## ⚠️ 关键限定：恢复方向失败仅在"合成事件"路径下观察到（待证）

**已实测（合成路径）**：暂停方向由 `osascript ... key code 53` 触发成功 → 证明该合成键盘
事件**确实到达 GLFW**；同一路径在暂停态下重复 4 次均不能恢复。

**未实测（真键盘）**：**本证据包不含任何物理键盘的验证**。早前 PM 报告中"真键盘 ESC 不响应"
的措辞是叙述错误——当时使用的全部是 System Events 合成事件，无物理按键参与。该结论应读作：
"经 System Events 合成键盘事件路径，恢复方向不复现"。

**为何这一区分决定 T009 是否该动代码**：

| 若真键盘…… | 结论 | T009 动作 |
|---|---|---|
| 也不恢复 | 真实代码缺陷（暂停态按键边沿/焦点处理有 bug） | 必须修代码 |
| 正常恢复 | 问题局限于合成事件路径（可能与 CURSOR_NORMAL 切换后的窗口焦点、事件源可信度有关），非玩家可感知缺陷 | **不应动代码**，只补一条验收注记 |

**因此：T009 开发者不得仅凭本证据包改代码。** 动手前必须先由真人按两次物理 ESC
（运行→暂停→恢复），把结果追加到本文件；若真人复现失败，再按缺陷修。

## 结论（修订）

- 暂停方向（运行→菜单）：System Events 合成键盘可达且生效（已实测）。
- 恢复方向（菜单→运行）：合成键盘路径连续 4 次不复现；**真键盘待证**。
  代码 `paused = !paused` 看似对称，若真人复现失败，怀疑点是暂停分支里
  `glfwSetInputMode(CURSOR_NORMAL)` 之后按键边沿状态未被正确轮询/清除。
- 本证据包由 PM 对话 2026-09-14 生成；复现时 pid/windowID 会变，命令模板不变。
