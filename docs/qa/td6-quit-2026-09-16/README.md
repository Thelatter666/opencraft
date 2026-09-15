# T-D6 结案证据：QUIT 按钮真人鼠标点击（2026-09-16）

## 结论

**T-D6 结案（QUIT 按钮已由真人真实鼠标点击验证通过）**。
用户在本地 build 上走完完整会话并以「ESC → 鼠标点 QUIT」退出；存档正确 flush，无残留。

⚠ **本次结案依赖用户口述 + 日志，不含屏幕录像**（见下方「证据边界」）。

## 会话事实（来自日志，非自述）

源日志：`session.log`（客户端 stdout/stderr 原样，未修饰）

| 时间 | 事件 |
|---|---|
| 00:24:55 | 启动，GL 4.1 Metal 89.4，读取已有存档（seed `0x4f50454e43524146`, ticks=19626） |
| 00:24:55 | 启动 gen 24 区块，228.5 ms，avg 9.52 ms/chunk |
| 00:24:58 / 00:25:07 | 两次 `sprint start`（双击 W 进疾跑） |
| 00:25:00–00:25:05 | **6 次疾跑跳弧线**，跨距 2.39–3.56 格，均速 4.44–5.95 m/s |
| 00:25:12–00:25:18 | **4 次 remeshed（6/6/9/6 区块）** ⇒ 期间改了方块（用户：海边挖方块） |
| 00:25:18 | autosave：2 区块异步写盘 + level 落盘（ticks=20025） |
| 00:25:21 | `save: flushed on exit (ticks=20047, chunks cached=4)` → `clean shutdown` |

- PID：12731　cwd：`/Users/happy/Desktop/opencraft/build`（`lsof -a -p 12731 -d cwd` 核实）
- 帧率：53–72 fps，全程无 error 级日志
- 退出后进程消失，`build/saves/world/` 下 `level.ocd` + `r.-1.-1.ocr` 正常存在

## 退出路径的代码事实（关键）

`grep -n glfwSetWindowShouldClose game/client/src/main.cpp` → **只有一处**：

```
1595:            if (clicked && quit_hover) {
1596:                glfwSetWindowShouldClose(window, GLFW_TRUE);
```

即 **QUIT 按钮与窗口红色关闭按钮走同一 `glfwSetWindowShouldClose` 分支**。
日志的 `clean shutdown` + `flushed on exit` 证明该分支被执行且存盘 flush 成功。

`quit_hover` 命中区（`main.cpp:1513`）：`bx0..bx1` × `cy+8 .. cy+44`，
其中 `bx0/bx1 = fb_width*0.5 ∓ 90`、`cy = fb_height*0.5`。
本机为 **1x** 显示（内容像素 == 帧缓冲像素，换算因子 = 1），
故 HiDPI 坐标修正在本会话中**不改变任何行为**——本次通过**不能**作为 T-D19 的 Retina 验证。

## 证据边界（如实）

| 可证明 | 不可证明 |
|---|---|
| 会话正常、走/跳/疾跑/改方块/存盘 flush | 「鼠标点击」这一动作本身 |
| 退出走的是 `glfwSetWindowShouldClose` 分支 | 该分支是由 QUIT 点击、还是窗口关闭按钮触发的 |
| clean shutdown 且无数据损坏 | —— |

日志只有一条 `glfwSetWindowShouldClose` 分支，两种操作在此不可区分。
**本证据链的落点是用户陈述**（原话：「按 ESC 后鼠标点了 QUIT 按钮」）。

对照 T-D14 §5.5（AUTO-JUMP 按钮真实鼠标点翻转）有截图 prove UI 点击链在 1x 上可用；
本次未截图，故不再声称更多。

## 连带

- **T-D6 → done**（结案）
- **T-D19 不变**：HiDPI 修正仍待 Retina 实机验证（1x 下此路径本来就能点中，无信息量）
- 遗留：存档根目录相对 cwd（双击 ⇒ `~/saves/world`，终端 ⇒ `build/saves/world`），
  本机 `/Users/happy/saves` 仍存在（历史双击产物）。未立卡，属 T009 遗留设计。
