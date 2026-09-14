# OpenCraft 调研笔记 05：Minecraft Java Edition 移动手感（疾跑 / 疾跑跳 / 空中控制 / FOV）

> 项目：OpenCraft —— C++ 原创体素沙盒引擎（玩法机制对标 Minecraft Java Edition）
> 本文档只记录**数值、机制结论、数学公式、来源 URL 与访问日期**。
> ⚠️ 合规红线（本卡裁决 R6，效力最高）：**不粘贴任何反编译源码片段**，包括从
> wiki / pastebin / github 镜像间接转抄的实现代码。公式本身是数学事实，可以写；
> 实现代码一律不写。
> 调研与访问日期：**2026-09-14**（全部 URL 均为该日访问）。
> 版本基准：Java Edition 1.21.x → 26.x 连续机制；跨版本差异处单独标注。

---

## 0. 本文件的用途与可信度分级

| 级别 | 含义 | 在本文件中的标记 |
|---|---|---|
| A | 官方/主 wiki 明文陈述的可观察行为 | 直接引用数值 |
| B | 社区技术 wiki 的公式/常数（与本引擎口径一致） | 标注 `mcpk` |
| C | 社区实测、论坛问答，样本不确定 | 标注"社区实测"并给区间 |

来源冲突时本文件**并列**并写明取舍理由，不擅自取其一。

---

## 1. 疾跑（Sprinting）

### 1.1 启动条件

| 项 | 值/结论 | 来源 |
|---|---|---|
| 双击前进键窗口 | **7 tick**（0.35 s） | mcpk.wiki（B） |
| 前进输入阈值 | 前进分量 ≥ **0.8**；潜行时该值再 ×0.3 | mcpk.wiki（B） |
| 饥饿门槛 | 饥饿 **> 6** 才可进入；降至 **≤ 6** 时立即退出 | minecraft.wiki/w/Sprinting（A） |
| 失明效果 | 失明时不可疾跑 | minecraft.wiki/w/Sprinting（A） |
| 进食/饮药中 | 按疾跑键**不会**启动疾跑；疾跑中进食会**暂停**疾跑 | minecraft.wiki/w/Sprinting（A） |
| 双击路径是否要求地面 | 需 onGround（地面路径）；疾跑键路径地面/空中皆可，**空中激活/失效延迟 1 tick** | mcpk.wiki（B） |

### 1.2 停止条件

| 条件 | 说明 | 来源 |
|---|---|---|
| 水平碰撞 | 碰撞夹角 **> 8°** 时立即停止（Java 无恢复窗口）；1.18 21w41a 把该阈值由 **5.5° 提高到 8°** | minecraft.wiki/w/Sprinting（A） |
| 饥饿降至 ≤6 | 立即退出 | minecraft.wiki/w/Sprinting（A） |
| 格挡 / 攻击生物 | 二者都会中断疾跑（攻击时为"疾跑击退"消耗疾跑） | minecraft.wiki/w/Sprinting（A） |
| 松开前进键 | 主 wiki **未明文记载**为停止条件（见 §1.4 冲突说明） | — |
| 松开疾跑键 | 主 wiki **未明文记载**；但 1.15 起疾跑输入有 **Hold / Toggle** 两种模式（Hold 模式隐含"按住"语义） | minecraft.wiki/w/Sprinting 历史节（A） |
| 30 秒自动停止 | mcpk 记为 600 tick；主 wiki 历史节记为 **1.9 15w46a 已移除** | 冲突见 §1.4 |

### 1.3 输入模式（Hold / Toggle）

- Java **1.15（19w41a）**起，疾跑输入可在**无障碍设置**里切换 **Hold / Toggle**；
  主 wiki 明确记录该选项存在，但未定义两者行为细节。
- 较早版本只有绑定按键：**1.7.2（13w36a）**加入 Sprint 按钮，默认 **Left Ctrl**。
- 较新版本的 Controls 菜单另有 **"Sprint Window"** 选项可调小/关闭双击疾跑窗。
- 来源：minecraft.wiki/w/Sprinting 历史节、minecraft.wiki/w/Controls（A）。

### 1.4 ⚠ 来源冲突：30 秒自动停止

| 来源 | 记载 |
|---|---|
| mcpk.wiki | 疾跑 30 秒（600 tick）后停止，除非按住疾跑键 |
| minecraft.wiki/w/Sprinting 历史节 | 该 30 秒限制在 **1.9（15w46a）已移除** |

**取舍**：以主 wiki 的历史节为准（时间线更明确、且与现代版本行为一致）——
OpenCraft 目标基准为 1.21.x/26.x，**不实现** 30 秒自动停止。理由写在此处以便复核。

### 1.5 速度

- 疾跑速度 **5.612 m/s**，为行走 4.317 m/s 的 **130%**（×1.3）。
  来源：minecraft.wiki/w/Sprinting（A）。
- 疾跑可游泳（且显著提高游泳速度）；骑乘矿车/船/猪等会**放大 FOV**但不提高实体速度。
  来源：minecraft.wiki/w/Sprinting（A）。

---

## 2. 疾跑 FOV

| 项 | 值 | 来源 |
|---|---|---|
| 基础 FOV | **70**（"Normal"，垂直视角；options 中该项显示为 Normal） | minecraft.wiki/w/Options（A） |
| 最小可设 FOV | 30（1.7.6-pre1 起由 70 降为 30） | minecraft.wiki/w/Options 历史（A） |
| FOV Effects 滑条 | 无障碍设置内，**默认 100%**，0% 即完全关闭 FOV 效果 | minecraft.wiki/w/Options（A） |
| 疾跑修正量 | 主 wiki **只定性**（"视野略微扩大"），**无数值** | minecraft.wiki/w/Sprinting（A） |
| 疾跑是否改 FOV | 是；wiki 明文把 sprinting 列为"会修改 FOV 的效果"之一 | minecraft.wiki/w/Options（A） |
| 移动速度属性（疾跑） | 玩家基础 **0.1**；`minecraft:sprinting` 修饰符 **+0.3**（`add_multiplied_total` → ×1.3） | minecraft.wiki/w/Attribute（A） |
| 疾跑乘数（推导值） | **×1.15（+15%）** | 由下方公式 + 上表比例推导（见说明） |
| 是否随速度连续变化 | 是——乘数由移动速度属性连续推导（Speed/Slowness 效果会连带改变它） | 公式推论（B） |
| 水下缩窄 | 另有 6/7 的缩窄乘数 | 社区公开资料（B） |

**FOV 乘数的数学形式**（公式是数学事实，非源码）：

```
multiplier = (speed_attribute / walk_speed_attribute + 1) / 2
玩家：speed_attribute = 0.1（基础），疾跑修饰符 +0.3 且为 add_multiplied_total
      → 疾跑速度属性 = 0.1 × 1.3 = 0.13
multiplier = (0.13 / 0.1 + 1) / 2 = (1.3 + 1) / 2 = 1.15
```

即 70° × 1.15 = **80.5°**。

> **来源与可信度说明（R6 合规）**：上表的 0.1 与 +0.3 来自主 wiki 的属性页（A 级）；
> `(比例 + 1) / 2` 这一形式在社区资料中广泛记录，但**主 wiki 未给出该公式，也未给出
> 疾跑 FOV 的具体数值**。因此 ×1.15 属 **B 级推导值**，不是 A 级明证数值。
> 本文件**不引用任何反编译镜像/源码转抄页作为来源**（R6 红线）；公式以数学形式重写，
> 不含实现代码。工程实现以此为默认值，并把 `FOV Effects = 100%` 作为可关闭开关的语义。

**⚠ 与 docs/research/01 §1.1 的出入**：research/01 记"疾跑 FOV 默认约 +10%"，
本文件推导到 **+15%**。二者差 5 个百分点。**建议 PM 裁决以下之一**：
(a) 采纳 +15%（本卡实现所用，`fov.hpp` 已按 1.15 落地）；或
(b) 保留 +10% 并把 `kSprintFovMultiplier` 改为 1.10。
本卡无权改规格，故仅在报告中提出；**若 PM 选 (b)，改动点仅 fov.hpp 一个常数**。

**过渡的数学形式**（每帧一步）：

```
current ← current + (target − current) × 0.5
```

单调、有界（不越过目标），从 70 到 80.5 且阈值 0.5° 时约 **5–6 步**。
过渡系数 0.5 与"每帧"粒度同样为 B 级社区资料，主 wiki 未记载过渡行为。

---

## 3. 疾跑跳与空中模型

### 3.1 水平移动常数（mcpk 口径，B 级）

| 常数 | 值 | 说明 |
|---|---|---|
| 空中动量阻尼 | **×0.91 /tick** | 每 tick 与输入衰减共同作用 |
| 输入衰减（默认） | **×0.98** | 45° strafe 为 1.0；45° sneak 为 0.98√2 |
| 方块滑度 S（默认） | **0.6** | 史莱姆 0.8、冰 0.98、空中 1.0 |
| 移动乘数 M | 疾跑 **1.3**、行走 1.0、潜行 0.3、停止 0.0 | M 再乘输入衰减 |
| 效果乘数 E | `(1 + 0.2×Speed) × (1 − 0.15×Slowness) ≥ 0` | 速度/缓慢效果 |
| 地面加速度 | `0.1 × M × E × (0.6 / S)³` | S=0.6 时 (0.6/0.6)³=1 |
| **空中加速度** | **`0.02 × M`** | 注意：**无** E、**无** (0.6/S)³ 因子 |
| 疾跑跳水平冲量 | **+0.2**（沿朝向，与是否横移无关）；非疾跑跳为 0.0 | 沿朝向施加 |
| 动量阈值 | `|V × S_{t-1} × 0.91| < 0.005` 时水平动量清零；1.9+ 记为 **0.003** | 低于阈值即归零 |
| 撞墙 | 对应轴的动量分量被取消，且玩家停止疾跑 | 与 §1.2 一致 |

来源：mcpk.wiki/wiki/Horizontal_Movement_Formulas、mcpk.wiki/wiki/Sprinting（B）。

### 3.2 关键推论：空中稳态 ≈ 4.444 m/s

空中加速度固定 0.02、阻尼 0.91，故空中的**渐近速度**为：

```
v_air_steady = 0.02 / (1 − 0.91) = 0.2222 格/tick ≈ 4.444 m/s
```

**4.444 m/s 略高于行走 4.317 m/s** —— 这正是"疾跑跳在空中不会掉回步行速度"
的机制根源，也是 T007 空中模型（锚定步行速度）与 MC 行为的本质差异所在。

### 3.3 疾跑跳均速 7.127 m/s

| 项 | 值 | 来源 |
|---|---|---|
| 疾跑跳平均水平速度 | **7.127 m/s** | minecraft.wiki/w/Sprinting（A）；docs/research/01 §1.1 |
| 单次疾跑跳水平跨越 | **至多 4 格**（常态跳跃约 **2 格**）；头顶 2 格高时可达 **5 格** | minecraft.wiki/w/Sprinting（A） |
| 社区实测 | 约 **6.99 m/s** | gaming.stackexchange 实测帖（C） |
| 2 格高天花板技巧 | 疾跑跳速度接近**普通疾跑的两倍**，代价约每秒 1 点饥饿 | minecraft.wiki/w/Sprinting（A） |

**⚠ 两份主 wiki 页面的口径差异**：minecraft.wiki/w/Jumping 给出的是"冰道 9 格、
无冰无效果最长 5 格"等**极值**，**未给** 7.127 m/s；7.127 只在 Sprinting 页出现。
"至多 4 格"未说明是"跨越的缺口"还是"水平位移"——**两 wiki 页均未定义该口径**。
OpenCraft 采用"净跨距 = 水平位移 − 0.6（体宽）"作为可复算口径（见 §6）。

**⚠ 公开来源自身不完全自洽（重要）**：按 mcpk 公式独立复算，链式疾跑跳的长程均速
比 7.127 高约 10%。可能原因（未逐一排除）：落地→再起跳 tick 的输入/状态细节、
版本差异（1.21.2 移动管线重构）、社区测量口径含中断。**本文件如实并列，不做臆断**。

### 3.4 饥饿与耗竭

| 项 | 值 | 来源 |
|---|---|---|
| 疾跑门槛 | 饥饿 **> 6** | minecraft.wiki/w/Sprinting（A） |
| 疾跑耗竭 | **0.1 / 米** | minecraft.wiki/w/Hunger（A） |
| 疾跑跳耗竭 | **0.2 / 次**（1.11 16w32a 由 0.8 降至 0.2） | minecraft.wiki/w/Jumping 历史（A） |
| 普通跳耗竭 | **0.05 / 次**（1.11 16w32a 由 0.2 降至 0.05） | minecraft.wiki/w/Jumping 历史（A） |
| 饱和度耗尽后 | 每 **40 米** 或 **7 秒** 扣 0.5 饥饿 | minecraft.wiki/w/Sprinting（A） |

**Trivia（wiki 原文要点，已改写）**：疾跑跳每格的耗竭低于纯疾跑，因为大部分距离
在空中、而耗竭按地面距离计。

---

## 4. 与 OpenCraft 当前实现的对应关系（小结）

| MC 机制 | 本引擎实现 | 落点 |
|---|---|---|
| 双击窗前 7 tick | `PhysicsConfig::sprint_toggle_window_ticks = 7` | 已对齐 |
| 饥饿 > 6 门槛 | `sprint_min_hunger = 6.0` + PlayerState::hunger 注入 | 已对齐（完整饥饿模型留 M2） |
| 疾跑键路径 | `InputState::sprint` + forward | 已对齐 |
| 撞墙停疾跑 | `collided_horizontally` 下一 tick 生效 | 已对齐（未做 8° 角度判定） |
| 空中加速度 0.02 | `PhysicsConfig::air_accel = 0.02` | 已对齐 |
| 空中阻尼 0.91 | `air_drag = 0.91` | 已对齐 |
| 疾跑跳 +0.2 | `sprint_jump_boost`（**校准值 0.1842**，非 0.2） | **简化管线下的校准**，退役条件见 §5 |
| FOV ×1.15 | `game/client/src/fov.hpp` `kSprintFovMultiplier = 1.15` | 已对齐 |
| FOV 过渡 ×0.5 | `fov_step()` | 已对齐 |
| 地面摩擦 0.546（=0.6×0.91） | 本引擎 `ground_drag = 0.9`（**不同模型**） | **未对齐**，迁移卡 T-D7 |
| 输入衰减 0.98 / 动量阈值 0.005 | 未实现 | gap |

---

## 5. `sprint_jump_boost` 为何是校准值（本引擎口径）

MC 的 +0.2 是**机制常数**，7.127 m/s 与"约 4 格"是**可观察行为**。在本引擎的
简化管线（地面 drag 0.9 而非 MC 的 0.546）下，这两个可观察量无法由 +0.2 同时命中，
故取"对齐可观察行为"并由 `sprint_jump_boost` 承担校准（详见
`docs/tasks/T-D1.report.md` §3）。

**退役条件**：T-D7 迁移 MC 摩擦管线（0.546/0.91 + 输入衰减 0.98 + 动量阈值 0.005）后，
MC 原始 **+0.2 应自然命中 7.127 ±1%**；届时本校准值退役。该条已写入
`physics_config.hpp` 的注释与被指派卡 T-D7 的验收项。

---

## 6. 本引擎的测量口径（供复核）

- **弧的长度 = 12 个 move**（起跳 tick + 11 个滞空 tick），实测确定，见 T-D1 报告 §3。
- **弧均速** = 弧内水平位移 / 12 × 20（m/s）。
- **净跨距** = 弧内水平位移 − 0.6（体宽）。

在固定 12-move 窗口下二者**仿射锁定**：`净跨距 = 0.6 × 弧均速 − 0.6`。
因此 `弧均速 = 7.127` 对应净跨距 3.676（低于 ⚖ 下限 3.7），而"净跨距 4.03"要求
弧均速 7.717（+8.3%，远超 ±1%）。**两个 ⚖ 指标不能同时取端值**，只能在交叠区间内
取折中——本卡据此选定校准值，实测同时满足两条 ⚖ 验收。

---

## 7. 参考来源清单（全部访问日期 2026-09-14）

| # | 来源 | URL | 级别 |
|---|---|---|---|
| 1 | Minecraft Wiki — Sprinting | https://minecraft.wiki/w/Sprinting | A |
| 2 | Minecraft Wiki — Controls | https://minecraft.wiki/w/Controls | A |
| 3 | Minecraft Wiki — Options（FOV 表 / FOV Effects 滑条） | https://minecraft.wiki/w/Options | A |
| 4 | Minecraft Wiki — Jumping（跳跃高度、耗竭历史） | https://minecraft.wiki/w/Jumping | A |
| 5 | Minecraft Wiki — Hunger | https://minecraft.wiki/w/Hunger | A |
| 6 | Minecraft Wiki — Attribute（movement_speed 0.1、sprinting +0.3） | https://minecraft.wiki/w/Attribute | A |
| 7 | mcpk.wiki — Horizontal Movement Formulas | https://www.mcpk.wiki/wiki/Horizontal_Movement_Formulas | B |
| 8 | mcpk.wiki — Sprinting（窗口/冲量/乘数） | https://www.mcpk.wiki/wiki/Sprinting | B |
| 9 | Gaming StackExchange — 疾跑跳实测帖 | https://gaming.stackexchange.com/questions/174761 | C |

**来源可信度与合规声明**：

- 上表 1–6 为主 wiki（A 级），7–8 为社区技术 wiki（B 级），9 为社区实测（C 级）。
- **FOV ×1.15 无 A 级来源**：主 wiki 只定性记载"疾跑会修改 FOV"，未给数值。本文件
  用 A 级的属性数值（0.1 / +0.3）代入 B 级公认公式推导得到 1.15，**属 B 级推导值**，
  已在 §2 显式标注。
- **本文件不含任何反编译源码片段**，也不把反编译镜像/源码转抄页列为来源（R6 红线）。
  公式均以数学形式重写；wiki 内容均为改写，无逐字复制。
- `https://gaming.stackexchange.com/questions/174761` 在 2026-09-14 的脚本化访问被
  返回 403（反爬），其数值来自调研过程中的搜索引擎摘要，**标 C 级、仅作旁证**，不作为
  任何实现数值的依据。
- 其余所有 URL 均于 2026-09-14 以 HTTP 200 复验可达。
