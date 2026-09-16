# T-D4 真人验收（用户实跑）2026-09-17

> 合成注入拿不到的证据：本机 HID 约 9 tick 后被失焦清掉，做不到"按住 W 走 200 格"。
> 本轮由**用户真人**完成，日志为唯一留档产物（无截图）。

## 结论：**通过**（四条判据全部成立）

| # | 判据 | 结果 |
|---|---|---|
| ① | 卸载真的发生 | ✅ 32 次 `stream: released` 行 |
| ② | 走回来标记物仍在（卸载不丢数据） | ✅ 用户确认：**放的方块 / 挖的坑 / 倒的水 三个全在** |
| ③ | 确实是从磁盘读回 | ✅ 5 条 `chunk (X, Y) loaded from disk`，含出生点区块 `(0, -1)` |
| ④ | 无崩溃 | ✅ 全程无崩溃；唯一的 error 行是 macOS 输入法噪声（IMKCFRunLoopWakeUpReliable，与游戏无关） |

## 行程

| 项 | 值 |
|---|---|
| 起点 | `(0.50, 133.00, -8.48)` |
| 最远 | `X = 290.01`（约 **290 格**，沿 +X 直线） |
| 卸载阈值 | `unload_radius = 9` 区块 = 144 格 ⇒ 出生点**必然**被卸 |
| 往返 | 走回出生点，标记物全部保留 |
| 帧率 | 全程 **72.0 fps** 稳定 |

关键日志：

```
stream: released 15 chunk(s), 242 resident, 139 meshed     ← 首次释放（32 次）
...
chunk (0, -1) loaded from disk                             ← 走回来时从磁盘读回
chunk (-1, -1) loaded from disk
chunk (-2, 0) loaded from disk
chunk (-2, -1) loaded from disk
chunk (-3, -1) loaded from disk
```

## 验收中发现的独立问题（不属 T-D4 判据）

### ★ 走动/跳跃时卡顿 —— 已定位，非本次引入

用户反馈"移动过程中、尤其跳跃时会卡顿一下"。日志定位：

```
remeshed 3 chunk(s) in 10.89 ms (last mesh 3.05 ms)
remeshed 3 chunk(s) in 11.39 ms (last mesh 3.45 ms)
单块最大耗时：9.82 ms   ← 超过 docs/03 §10 的 5 ms 预算
```

- **真凶是新区块网格化**（单块最高 9.82 ms，逼近/超过一帧预算），
  不是跳跃本身——跳跃只是让你更快进入新区块视野，触发批量网格化，
  恰好落在你最需要流畅的瞬间。
- **与 T-D4 无关**：卸载/加载路径正常，网格化耗时是既有性能问题。
- 已并入 **T-D24（网格化预算待复核）**。

## 文件

- `acceptance-user-walk.log` —— 完整会话日志（原始，未修饰）
- `acceptance-checklist.txt` —— 发给用户的验收清单
