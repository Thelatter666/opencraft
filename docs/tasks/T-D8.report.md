# 任务 T-D8 报告：自动上台阶（step height 0.6）

- 分支：`task/T-D8-auto-step`　提交：`636e9ba`（前缀 `taskT-D8:`）
- worktree：`/Users/happy/Desktop/opencraft_worktree/opencraft-td8`
- 基线：开工前 **184/184 绿** → 交付 **203/203 绿**（+19 新增）
- 黄金回放文件：**零改动**（无需重生成 ⇒ 不存在覆盖率回退风险）

---

## 0. 一句话结论

step-assist 已按 research/06 §6.3 实现并逐条验收：玩家 0.6 / 生物 1.0 可配，
越 0.5 坎水平速度与平地**完全一致（差 0.0000%）**，「取最低增益高度」这一
★ 性质已用**变异测试**证明真被测试约束。

**但卡面验收 #1 有一处数学上不成立的表述需要 PM 裁决**（见 §1），它决定用户
实机抱怨是否已被真正修复。

---

## 1. ⚠ 卡面缺陷：验收 #1「1 格高台阶」与 `step_height = 0.6` 互斥（需裁决）

卡面 §7.1 写「**1 格高（含 ≤0.6）台阶** → 玩家连续通过」，同时 §硬规则写
「**不要为了让测试变绿而放宽 step_height**；数值依据以 research/06 §6.3 为准」。

二者不能同时成立：

| 事实 | 出处 |
|---|---|
| 玩家步高 = **0.6 b**，可跨地毯/半砖/床 | research/06 §6.3；MCPK Stepping（2026-09-15 实测 HTTP 200，原文"maximum step height is 0.6b … carpets, slabs, and even beds"） |
| 整方块高差 = **1.0 b** > 0.6 | 体素几何 |
| MC 原版玩家**走不上整方块，必须跳**；生物步高 1.0 才走得上去 | research/06 §6.3（"玩家 0.6；生物多为 1.0"） |

**推论（重要）**：用户 T-D1 gap #5 原话是「被 **1 格高坎**卡住、必须手动跳」——
这**本身就是 MC 原版正确行为**。因此本卡按 0.6 实现后，**该类 1 格整方块坎仍
需要手动跳，用户的原始观感不会被本卡消除**。本卡真正修复的是「≤0.6 的高差
（半砖/地毯/床一类）此前完全过不去」这个真实缺陷。

**本卡处理方式**：严守 0.6（未放宽任何数值），并按开发者提问后 PM 选定的
**方案 B** 加最小高度通道使机制可测（见 §2）。验收 #1 以 **0.5 台阶**为基准
达成（速度与平地差 0.0000%）。

**请 PM 裁决其一**（本卡不擅自决定）：
- **(a) 维持 0.6**：验收 #1 措辞改为「≤0.6 台阶」，并知悉用户抱怨的 1 格坎
  属 MC 正确行为 → 若仍要满足，需另立「自动跳跃 auto-jump」卡（范围外，
  research/06 未记载该机制的 MC 数值）；
- **(b) 玩家步高改 1.0**：用户诉求直接满足，但**偏离 MC 原版**、与 docs/00
  「对齐 MC JE」总原则冲突，需先改 research/06 §6.3 依据链。

---

## 2. 接缝：`shape_top_at`（方案 B，最小高度通道）

**为什么必须有**：纯整方块世界里可表达的高差只有 1.0 / 2.0 / …，全部 > 0.6。
⇒ 玩家 0.6 的跨步**永远不会触发**，本卡机制无法验收。卡面「只用整方块地形
做验收」与「step_height=0.6」在这一点上也是互斥的。

```cpp
// block_source.hpp（新增默认虚函数；未改任何已有虚签名）
[[nodiscard]] virtual double shape_top_at(int wx, int wy, int wz) const {
    return shape_at(wx, wy, wz) != BlockShape::Empty ? 1.0 : 0.0;
}
```

- 默认由 `shape_at` 派生 ⇒ **整方块适配器逐位不变**（已断言，见 §5 用例 17）；
- **不是**半砖/楼梯形状系统：只有一个「顶面高度」标量，无 AABB、无多边；
  M2 内容卡仍可自由替换为真实形状描述，本卡不预设立场；
- `box_collides` / `has_support_at` / Y 轴向下夹持三处改为按实际顶面高度计算。
  整方块下 `by + 1.0` 与旧实现同值 ⇒ 黄金文件零 diff（实测）。

**踩坑记录（值得进 lessons）**：`has_support_at` 最初漏改，仍用
`shape_at != Empty`。后果是跨上 0.25 台后，脚下「没有整方块」被判为无支撑 →
`on_ground` 逐 tick 在 true/false 之间抖动，实体从自己站着的方块上掉下去。
高度感知碰撞必须**同时**覆盖 `box_collides`、支撑探测、向下夹持三处，改一处
不够。

---

## 3. 算法实现（research/06 §6.3 四步，逐条对应）

```
1. 扁平尝试（不跨步）求解 → d_flat
2. 收集候选抬升高度（≤ step_height 的 Y 面），升序排序、去重
3. 逐个候选重试「完整求解」，取第一个水平位移更大者   ← ★ 最低增益
4. 采纳结果并向下 settle 落到新支撑面
```

实现中三个**不直观但决定正确性**的点：

**(1) 候选取自「扫掠 footprint」，不是当前 footprint。**
扁平解算会把实体夹到贴墙；贴墙后盒面与墙面严格相切，而本引擎碰撞是**严格
重叠**判定（相切不算碰撞）⇒ 用当前 footprint 取候选**必然取空**，表现为
「跨步从不触发」。修法：候选区 = 起点盒 ∪ 终点盒。

**(2) 比较量是沿位移方向的带符号水平投影，Y 不参与。**
若把 Y 折进距离（三维距离），则**任何抬升都天然"更大"** ⇒ 「最低增益」会被
悄悄退化成「最高可得」，而且测试照样全绿（单候选场景下看不出来）。这条是
本卡最容易写错且最隐蔽的一处。

**(3) 重试的是「本 tick 剩余的全部水平位移」，并在成功后跳出子步循环。**
否则子步循环会对已消费的位移重复施加。

另外三个副作用处理：
- **成功跨步不算碰撞**：`hit_x/hit_z` 取跨步解算结果，故 `collided_horizontally`
  为 false → **不打断疾跑**（MC 走上下楼梯不中断疾跑）；
- **跨步后重置 `fall_peak_y` / `fall_distance`**：抬升不是坠落，留着旧 apex 会让
  下一次落地按虚高起点误算摔落伤害（docs/01 §2 ⚖ 会破）；
- **settle 只允许向下**：允许向上找面会在 footprint 内找到更高的顶面，把实体
  瞬移到方块**顶**而不是它刚跨上的**内部台阶**（实测踩过，正是 ★ 性质的反面）。

**接口契约自查**：
- `step_player(state, input, world, config, result)` **五参数签名未变** ✅
- `step_height` 挂在既有 `PhysicsConfig`，经既有 `for_entity()` + `EntityKind`
  取用；**未新建全局单例** ✅
- **未改** `IBlockSource` 任何已有虚签名（只加了一个带默认实现的） ✅
- `EntityKind` 按枚举注释预留的扩展位加 `Mob = 2`（M2 内容卡可继续扩展）

---

## 4. 验收实测（PM 可逐条重跑）

### 4.1 核心手感（验收 #1，以 ≤0.6 为基准）

| 指标 | 平地 | 越 0.5 坎 | 差 |
|---|---|---|---|
| 疾跑稳态 | 5.6123 m/s | **5.6123 m/s** | **0.0000%** |
| 走路稳态 | 4.3172 m/s | **4.3172 m/s** | **0.0000%** |

（⚖ 对照 docs/01 §2：疾跑 5.612 ✅、行走 4.317 ✅，本卡未扰动既有数值。）
持续前进输入即可连续通过，无需手动跳；跨步 tick `velocity.x > 0` 且
`collided_horizontally == false`。

### 4.2 负例（验收 #2）

2 格墙（生物 1.0 亦不跨）、整方块 1.0（玩家 0.6）、0.75 台：**均不抬升**，
水平速度归零，贴停在墙面 x = 9.7。

### 4.3 取最低增益高度（验收 #3）★

**关键交代**：我先写的 4 个「最低增益」用例**实际上是空约束**——把候选循环
改成降序（取最高）后**全部 18 例仍然通过**。原因是常规走/跑速度下子步扫掠
只覆盖 1 个候选面，排序根本不起作用。

补了判别用例后修正：以 2.0 b/tick 的发射速度使**一次扫掠同时覆盖 0.25
近台与 0.50 远台**（两者都 ≤0.6 ⇒ 都是合法候选），断言取 **0.25**：

| 实现 | `step_height_used` | 终点 y | 终点 x |
|---|---|---|---|
| 正确（升序，取第一个增益） | **0.25** | 64.25 | 10.7 |
| 变异（降序，取最高） | 0.50 | 64.50 | 11.5 |

变异体被该用例 **FAIL 抓住**（`CHECK(0.25 < 1e-9)` 不成立）。这条是 ★ 性质
目前**唯一真实的守门测试**，报告如实说明其余同类用例不具备该约束力，避免
后续误以为已被覆盖。

另配楼梯用例：0.25 / 0.50 / 0.75 三级抬升，断言 3 次跨步**全部为 0.25**
（每次只升一级，非一次跳到最高级）。

### 4.4 落地不悬空（验收 #4）

跨上 0.5 台：`on_ground == true` 且 y 精确 = 64.5；走过 3 格台后落回 64.0。
`on_ground` 全程 200 tick 无抖动。

### 4.5 只在地面跨步（验收 #5）

空中撞墙用例先用 `REQUIRE(ever_hit_horizontally)` **断言接触确实发生**
（否则用例会因"根本没撞上"而假绿——第一版就是这个假绿），再断言
`stepped == false` 且 y 未被抬升。跳跃 tick 不报跨步。

### 4.6 按实体可配（验收 #6）

`for_entity(Player).step_height = 0.6` ≠ `for_entity(Mob).step_height = 1.0`；
行为学对照：**同一整方块世界**，玩家被挡（x=9.7、不抬升）、生物走上去并通过。

### 4.7 无回归（验收 #7）

**203/203 全绿**；184 存量全绿；黄金文件 `tests/golden/` **未修改**（`git diff`
为空）⇒ 无重生成、无覆盖率交代事项。黄金世界全是整方块 + 玩家 0.6 ⇒ 候选
恒为空集，跨步机制在黄金世界上**逐位不触发**，这是零 diff 的机理保证。

### 4.8 格式与命名（验收 #8）

`clang-format --dry-run --Werror` 对 6 个改动文件**无 diff**；19 个用例名不含 `[`。

---

## 5. 新增测试清单（`tests/test_step_assist.cpp`，19 例）

| # | 用例（省略公共前缀 `step assist:`） | 对应验收 |
|---|---|---|
| 1 | player walks up a 0.5 ledge without losing speed | #1 |
| 2 | player mounts a 0.5 plateau and stands on it | #1/#4 |
| 3 | reported through MoveResult and speed is retained | #1 |
| 4 | a full block is NOT walkable at step height 0.6 | #2 |
| 5 | a 2-block wall is not climbable even by a mob | #2 |
| 6 | **with two live candidates the LOWEST gainful one wins** | **#3 ★** |
| 7 | takes the lowest lift that gains ground, not the highest | #3 |
| 8 | climbs a staircase one lowest-gain step at a time | #3 |
| 9 | the step is the riser height, not a higher face | #3 |
| 10 | lands back on the ground instead of hovering | #4 |
| 11 | on_ground holds on every tick of the traverse | #4 |
| 12 | does not fire while airborne | #5 |
| 13 | a jump onto a ledge is not a step | #5 |
| 14 | step height is configured per entity kind | #6 |
| 15 | a mob walks up a full block the player cannot | #6 |
| 16 | crossing a riser at a pit lip never hovers | #4（不变量守卫） |
| 17 | default IBlockSource keeps full-cube semantics | §2 接缝 |
| 18 | sub-block ledges do not block movement in general | #1 |
| 19 | a 0.75 ledge is above the 0.6 step and is not crossed | #2 |

**用例 16 的诚实交代**：它是端到端「不悬空」不变量守卫，但**即使删掉代码中
"settle 找不到面" 的 else 分支它也通过**。原因：走/跑速度下跨步总是把实体落在
真实支撑的台阶上，而 footprint 一旦离开台阶，tick 起始的支撑复检就会清掉
`on_ground`。那个 else 分支是防御性的当前不可达路径。同时该用例暴露了一个
**既有引擎语义**：`on_ground` 是「tick 起始权威」，离开支撑面后有 1 tick 滞后
——这是 T007/T-D7 既有设计（整方块边缘同样如此），**非本卡引入**，故未在
本卡断言消除。

---

## 6. Blip-Up 排查（卡面要求：范围外但异常须报）

**未复现位置异常。** 本卡实现只在「onGround + 已发生水平碰撞 + 非水中」触发，
且解算后强制向下 settle，不存在 research/06 §6.3 描述的「垂直运动首尾也触发
跨步」的入口。实测：

- 平地连续 200 tick：y 恒为 `64.000000000`（无 0.104/0.121 累积抬升）；
- 30 级 0.25 楼梯：精确停在 64.25，无过冲；
- Jump-Cancel 同样未实现（范围外）。

**结论：无需为 Blip-Up 立债。** 若将来要实现它，需改的是「垂直运动首尾也调用
跨步」这一入口，属独立机制卡。

---

## 7. 建议表（交 PM 落盘，本卡未自行改 STATE/规格）

| # | 建议 | 依据 | 影响 |
|---|---|---|---|
| S-1 | **裁决 §1 的 (a)/(b)**：验收 #1「1 格高」与 0.6 互斥 | MCPK Stepping + 体素几何 | 决定用户实机抱怨是否已修 |
| S-2 | `docs/01 §2` 补一行「步高 0.6（生物 1.0）」 | 本卡已实现该常数，规格未记 | 低 |
| S-3 | `docs/03 §6` 补「碰撞形状经 `shape_top_at` 单标量接缝，真实形状系统待 M2」 | §2 | 低 |
| S-4 | 新债：**`on_ground` 的 1-tick 滞后**（离开支撑面当 tick 仍报 true） | §5 用例 16 交代 | 中（影响 M2 生物 AI 落地判定） |
| S-5 | 新债：**天花板夹持未高度感知**（`move_axis_y` 上升分支仍按整方块 `-height` 夹） | 本卡范围外，半砖做天花板时才会暴露 | 低（M2 形状卡） |
| S-6 | 新债：**水中不跨步**是本卡保守决定，未对照 MC | §3 触发条件 `!water` | 低（T-D12 水卡一并核） |
| S-7 | lesson 收录：**「最低增益」类排序性质必须用变异测试验证测试是否真约束** | §4.3（首版 4 例全为空约束） | 方法论 |
| S-8 | lesson 收录：**严格重叠判定下，贴墙盒与挡墙盒不相交 ⇒ 候选/邻域查询必须用扫掠盒** | §3(1) | 方法论 |

---

## 8. 合规自查

- **白名单**：改动仅 `engine/physics/**`（4 文件）+ `tests/**`（3 文件），
  `git status` 核对无越界 ✅
- **未改**：`STATE.md`、`docs/01`–`06`、`docs/research/`、`docs/tasks/` 其他文件、
  `game/**`、`engine/{core,voxel,noise,render,net}`、`cmake/`、根 `CMakeLists.txt`、
  `.github/`、`assets/` ✅（本报告为卡面 §7.10 指定落盘路径，属授权写入）
- **未新建全局单例**、**未改** `step_player` 签名、**未改** `IBlockSource` 已有虚签名 ✅
- **数值未放宽**：`step_height` 默认 0.6（= MC），`momentum_threshold` 维持裁决
  值 0.003，`sprint_jump_boost` 维持 0.2，未新增任何凑数系数 ✅
- **构建命令未接管道**：一律 `> log 2>&1; echo EXIT=$?` + 搜 `error:` ✅
- **未把 `FETCHCONTENT_BASE_DIR` 指向主仓 `build/_deps`**：worktree 自行
  configure，主仓构建树未被触碰 ✅
- **合规红线**：全文只记数值/公式/来源 URL + 访问日期（MCPK Stepping 于
  2026-09-15 可达性实测 HTTP 200），**未粘贴任何反编译源码片段**；所有伪代码
  与算法描述均为自撰 ✅

---

## 9. 交付物

- 分支 `task/T-D8-auto-step` @ `636e9ba`（单提交）
- `engine/physics/include/opencraft/physics/block_source.hpp`：`shape_top_at` 接缝
- `engine/physics/include/opencraft/physics/physics_config.hpp`：`step_height`
  + `EntityKind::Mob` + `for_entity` 分支
- `engine/physics/include/opencraft/physics/player_physics.hpp`：`MoveResult`
  加 `stepped` / `step_height_used`
- `engine/physics/src/player_physics.cpp`：跨步主体 + 三处高度感知
- `tests/test_step_assist.cpp`（新增，19 例）+ `tests/physics_test_world.hpp`
  （`partial()` 通道）+ `tests/CMakeLists.txt`
