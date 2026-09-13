# P2 观察记录：ESC 暂停态下再次 ESC 未恢复（**已结案：测试工具伪影，非游戏缺陷**）

> ## ✅ 结论（2026-09-14，T009 排查后定案）
>
> **P2 不是玩家可感知的代码缺陷，是"合成输入脉冲宽度 vs 每帧轮询"的采样竞态。**
> 真机等价输入（HID 层，86 ms 脉冲）在 **T008 与 T009 两个构建上双向均正常**；
> 而 `osascript ... key code 53` 的脉冲仅 **2–5 ms**，短于一帧（≈11 ms），
> 按下沿常整段落入两次采样之间 → 波尔切换不触发。
>
> **T009 未改一行 ESC 逻辑**（见 §「代码等价性」），符合"先验证再决定改码"的前置要求。
>
> 证据依据：`docs/qa/t009-2026-09-14/`（`P2_*.png` 双向截图 + `P2_diag_*.log` 双路诊断日志）。
> 下文原始观察记录保留，作为"结论如何被推翻"的过程留痕。

## 原始观察（2026-09-14，PM，合成事件路径）

观察：游戏暂停后，按 ESC 应恢复（main.cpp:697 附近 `paused = !paused` 切换逻辑），
经 **System Events 合成键盘事件**触发时未恢复。暂停方向（运行中 → 出菜单）正常。

⚠️ 数据来源限定：本包键盘证据均为 `osascript ... key code 53`（System Events 合成事件），
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

## ✅ 最终诊断（2026-09-14，T009 开发者排查 + PM 复核）

### 机制：输入脉冲宽度 vs 每帧轮询的采样竞态

客户端用**每帧轮询**读按键（`game/client/src/main.cpp:770`
`glfwGetKey(window, key) == GLFW_PRESS`），配合 `prev_esc` 边沿变量做切换。

诊断程序（GLFW 键盘回调 + 轮询边沿**双路记录**，仅存 `/tmp`、未入库）的直接观测：

| 注入方式 | 按键脉冲宽度 | 是否产生轮询边沿 |
|---|---|---|
| `osascript ... key code 53`（本包原用） | **2–5 ms** | **多数没有**——3 次按键只产生 2 个边沿（丢 1 次） |
| HID 层注入（`CGEventPost(kCGHIDEventTap)`） | **86–87 ms** | 每次都产生 |

本机帧率 ≈90 FPS → 帧间隔 ≈11 ms。**4 ms 脉冲大概率整段落在两次采样之间**，切换不触发。
`CURSOR_DISABLED` 与 `CURSOR_NORMAL` 两种模式下脉冲宽度都是 2–5 ms →
**与暂停分支里的光标模式切换无关**（原怀疑方向排除）。

### 代码等价性（T008 ↔ T009 无功能改动）

`git diff ba626e5 HEAD -- game/client/src/main.cpp` 中与 `esc`/`paused`/`prev_esc` 相关的
增删行只有一条：删除重复的 `prev_esc = esc_down;`（同一赋值连写两次，**空操作**）。
`paused = !paused` 与两个分支体一字未动 → 不存在"T009 顺手修好了 P2"的可能。

### 双向功能验证（T008 与 T009 两个构建，HID 层）

HID 连按 5 次 ESC：playing→PAUSED→playing→PAUSED→playing 全部正确。
判据为像素级：暂停帧白字像素 919、场景亮度 459990→198067；恢复帧白字 0、
亮度回到 459990（与基线**逐值相同**）。两个构建（T008 的 `ba626e5`、T009 的 HEAD）结果一致。

### 附带的工具层观察

`CGEventPostToPid` 偶发**只送达 keydown、丢 keyup**（诊断日志中下一次同键变为 REPEAT），
此时应用侧 `prev_esc` 会一直停在 `true` —— 这可解释 PM 当时"暂停方向只成功一次、
之后怎么按都不恢复"的观感，**同属投递层现象，不是游戏逻辑问题**。

## 结论

- **P2 结案：非玩家可感知的代码缺陷**，是合成输入脉冲宽度小于轮询周期的采样盲区。
  T009 按变更记录**未改任何 ESC 逻辑**，处理正确。
- 可靠性排序（updated）：**HID 层注入 ≥ 物理键盘** ≫ System Events `key code`（脉冲过短）
  > `CGEventPostToPid`（会丢 keyup）。
  **脚本化 GUI 键盘验收必须用 HID 层，或按住 ≥50 ms 后再释放**；`osascript` 不适合。
- 物理键盘验证仍未做（本包始终用合成输入）；但机制已直接观测、两个构建双向正常、
  代码未变，故**不再作为验收前置**。真人若愿意可按两次 ESC 做最终确认。
- **可选加固（非本卡必须，列入债务）**：把暂停切换从"每帧轮询 + 边沿变量"改为
  `glfwSetKeyCallback` 按下回调，从此免疫短脉冲。真人按键 50–150 ms，**对玩家无影响**，
  故优先级低。
- 本证据包由 PM 对话 2026-09-14 生成，同日随 T009 排查结案；复现时 pid/windowID 会变，
  命令模板不变。

