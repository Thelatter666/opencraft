# T-D7 开发者报告：水平移动管线迁移 MC 摩擦模型（地基卡）

- 任务卡：`docs/tasks/T-D7.md`　裁决：`docs/tasks/T-D7.ruling.md`
- 分支：`task/T-D7-friction-pipeline`（worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-td7`）
- 基线：`53b750d`，基线 ctest **163/163 绿**（已在独立 worktree 复核）
- 收口：**184/184 ctest 绿**（163 存量 + 21 新增），clang-format 无 diff

---

## 1. 结论摘要

| 验收项 | 目标 | 实测 | 结果 |
|---|---|---|---|
| 松手滑行距离 | 0.26 格（上限 0.35） | **0.255846** | ✅ |
| 起步达 95% 稳态 | ≤0.3 s（白皮书 0.25 s） | **5 tick = 0.25 s**（旧 29 tick = 1.45 s） | ✅ |
| ⚖ 步行稳态 | 4.317 m/s ±0.5% | **4.3172** | ✅ |
| ⚖ 疾跑稳态 | 5.612 m/s ±0.5% | **5.6123** | ✅ |
| ⚖ 潜行稳态 | 1.295 m/s ±0.5% | **1.2952** | ✅ |
| ⚖ 跳跃顶点 | 1.2522 | **1.252203353** | ✅ |
| 疾跑跳 +0.2 自然命中 7.127 | ±1% | **7.1268 m/s（+0.003%）** | ✅ |
| 冰面 S=0.98 稳态 | 4.157 m/s | **4.1572**（低于普通 4.317） | ✅ |
| 冰面滑行距离 | 1.69 格（≈6.6×） | **1.688468（6.60×）** | ✅ |
| 参数按实体类型实例化 | 玩家/下落方块共存 | 玩家 0.08 / 方块 0.04，互不干扰 | ✅ |
| 无回归 | 163+ 全绿 | **184/184** | ✅ |
| 黄金回放 | 授权重生成，逐 leg 交代 | 已重生成，覆盖率逐项一致 | ✅ |

**一句话**：迁移后松手滑行从 **1.94 格 → 0.256 格**（降 87%），起步从 **1.45 s → 0.25 s**，溜冰感根因消除；
`0.1842` 校准值退役，MC 原始 `+0.2` 自然命中 ⚖（无需任何校准系数）。

---

## 2. 根因与为什么存量测试测不出来（实测确认）

旧管线写 `v = v·drag + dir·target·(1−drag)`，稳态恒为 `target`，**drag 被代数约掉**。
我在基线 worktree 用真实代码实测（非解析推导）：

| 指标 | 旧管线实测 | 卡面/白皮书目标 |
|---|---|---|
| 松手滑行 | **1.942650 格**（500 tick 仍未归零） | 0.26 |
| 起步达 95% | **29 tick = 1.45 s** | 0.25 s |
| 跳跃顶点 | 65.252203353 | 1.2522 ✅ |

滑行距离实测 **1.94 格**，与用户"再漂移一段"的实机反馈一致。旧管线**无动量阈值**，
`velocity.x` 永不归零（500 tick 后仍在衰减），拖尾无限。

---

## 3. 实现要点

### 3.1 每 tick 执行序（严格按 research/06 §1.1）

```
1. 采样 tick 起始 on_ground / S（脚下方块滑度）
2. 摩擦因子 k = horizontal_drag × S   （空中 S 恒 1.0）
3. 跳跃：vy = 0.42；疾跑则沿朝向 +0.2
4. 加速度累加：地面 0.1×M×E×(0.6/S)³；空中 0.02×M；输入×0.98 后归一化、模长<1 钳为 1
5. 位移（Y→X→Z 逐轴裁剪 + 子步）
6. 阻尼（在位移之后）：vy=(vy−0.08)×0.98；vx,vz ×= k
7. 单轴动量阈值 |v|<0.003 归零
```

**关键次序**：第 6 步在位移之后 —— 这是跳跃顶点与水平距离的全部来源。存储速度是"阻尼后"值，
本 tick 位移是"存储值 + 本 tick 加速度"，故 `MoveResult::friction` 与 `state.velocity` 相差一个 k。

### 3.2 `(0.6/S)³` 是三次方（不可降为线性）

冰面稳态 **4.1572 < 普通 4.317** 的反直觉行为，正是三次方（加速度衰减）与线性（阻尼改善）不对称的产物：

| S | (0.6/S)³ | 1/(1−0.91S) | 乘积 | 稳态 m/s |
|---|---|---|---|---|
| 0.6 | 1.000 | 2.202 | 2.202 | 4.3172 |
| 0.8（黏液） | 0.422 | 3.676 | 1.551 | 3.0400 |
| 0.98（冰） | 0.229 | 9.207 | 2.112 | 4.1572 |
| 0.989（蓝冰） | 0.223 | 9.999 | 2.233 | 4.3760 |

### 3.3 动量阈值 0.003（按裁决 A-5）

复算确认裁决结论：

| 阈值 | 跳跃顶点 |
|---|---|
| 无 | 1.252203353 |
| **0.003** | **1.252203353（与无阈值逐位相同）** |
| 0.005 | 1.249187094（破坏 ⚖） |

即 **0.003 对跳跃零影响，只切断滑行拖尾**，纯收益。测试 `a five thousandth threshold would break the spec`
已把这条固化为回归保护。

### 3.4 两条地基约束

**(1) 参数按实体类型实例化** — `PhysicsConfig` 为可复制值 + `for_entity(EntityKind)`：

| 参数 | Player | FallingBlock |
|---|---|---|
| gravity | 0.08 | **0.04** |
| horizontal_drag | 0.91 | **0.98** |

测试断言两者并行模拟时 `player_vy ≈ 2 × block_vy`（同保留率、半重力），且 `PhysicsConfig{}` 未被污染。

**(2) 一次立好接口**
- `IBlockSource` 增 `slipperiness_at()` / `shape_at()`，**均有默认实现** ⇒ 现有适配器零改动可编译
- `step_player` 签名保持四参数，新增**可选出参** `MoveResult*`（`hit_x/hit_y/hit_z/hit_ceiling/landed/fall_distance/friction/slipperiness`）
- 摩擦**乘性解耦** `k = horizontal_drag × S`，天然支持"同实体不同方块"与"不同实体同方块"两维

**未照搬 Minestom `EPSILON=1e-6`**（会抹平 1.8/1.9 跳高差）。

### 3.5 顺带修出的真实缺陷：潜行未取消疾跑

旧管线 `sneak` 与 `sprint` 乘子可叠加，导致**潜行比走路还快**（实测 1.6837 m/s，超 ⚖ 1.295 达 30%）。
存量测试 `sneak overrides sprint` 在旧管线"恰好"通过（因旧管线的 `target` 取潜行值），迁移后被暴露。
MC 依据：疾跑输入门限 `moveForward ≥ 0.8`，潜行把输入 ×0.3 → 0.294 < 0.8 ⇒ 潜行禁止疾跑。
**已修**：`in.sneak` 加入疾跑停止条件与激活条件。属本卡范围内（摩擦/输入管线）。

---

## 4. ★ 重大口径发现：7.127 的采样口径（需 PM 裁决）

### 4.1 发现

卡面验收 4 要求"MC 原始 +0.2 自然命中 7.127"。我在新管线上实测两种口径：

| 口径 | 弧长 | 均速 | 净跨距 |
|---|---|---|---|
| **连续跳跃链**（落地即起跳，按住 W+疾跑） | 12 move | **7.1268 m/s（+0.003%）** | **3.6761** |
| 冷起跳单弧（T-D1 口径：间隔 50 tick，跳跃前 38 tick 地面巡航） | 12 move | 6.0486 m/s（−15.1%） | 3.0292 |

**只有"连续跳跃链"口径能让 MC 原始 +0.2 命中 7.127**，且净跨距 3.6761 与 PM 在
`T-D1.ruling2.md` 独立算出的仿射锁定值 **3.6762 逐位吻合**（这一吻合是强证据）。

### 4.2 依据（wiki 原文，`?action=raw` 抓取，访问 2026-09-15）

```
|rowspan="3" |{{EffectSprite|speed}} [[Sprinting|Sprint-jumping]]
|Flat terrain
|{{tc|No}}                      ← "Running start effective?" = No
|7.127 m/s
...
|Elevation momentum (+1 block){{Note|Assuming the jump key is released
 then held mid-air every jump|name=manual-press}}
```

表头为 **"Average speed in m/s"**，条件列为 **"Running start effective? No"**，
脚注写明 **"the jump key is released then held mid-air every jump"** —— 描述的是**重复跳跃循环**，
即"疾跑跳"作为一种**移动方式**的持续均速，而非单次孤立跳跃。

### 4.3 与 T-D1 的关系

T-D1 用"间隔 50 tick 的孤立跳"采样，该口径下新管线只有 6.05 m/s；要让**冷弧**达到 7.127
需要约 +0.33 的冲量（比 MC 原始高 65%）—— 这正说明该口径**不是** 7.127 的语义。
T-D1 的 `0.1842` 是在旧管线的冷弧口径下凑出 7.1867 的产物，**已按卡面要求退役**。

在链式口径下旧管线的数值是 7.6384（+0.1842）/ 7.9031（+0.2）—— 即旧管线在**正确口径**下超标 7~11%，
反过来说明旧管线的校准是在错误口径上做的补偿。

### 4.4 请求裁决

我按卡面"应能自然命中 7.127"与"不得回头改校准值敷衍"执行，**采用链式口径**（`+0.2` 命中 7.1268）。
但这同时暴露一个**规格内部冲突**（见下节），故特此报告。

---

## 5. ⚠ 规格内部冲突：7.127 与净跨距 3.7 不可兼得（已有裁决，此处复核）

`docs/01 §2` 同时写「疾跑跳均速 7.127」与「净跨距 3.7–4.3」。但同一行下方已注明仿射锁定
`净跨距 = 0.6 × 均速 − 0.6`，故：

- 均速 7.127 ⇒ 净跨距 **3.6762**（**低于** 3.7 下限）
- 净跨距 3.7 ⇒ 均速 **7.1667**

本卡实测链式净跨距 **3.6761**，**精确落在这条代数关系的预测上**。
即：**命中 wiki 的 7.127 必然带来 3.676 的跨距**，二者数学上不可兼得（此为 T-D1 已知结论的重现与加强）。

**我的处置**：按卡面优先命中 7.127（"不得回头改校准值敷衍"），把跨距断言改为
「锁定关系 + 3.60–3.75 合理带」，并在报告中标出冲突。**请 PM 裁决**是否需要在 `docs/01 §2`
把跨距下限由 3.7 调整为 3.65（或明确"以均速为准"）。

---

## 6. 黄金回放：逐 leg 旧值→新值（授权重生成）

### 6.1 覆盖率核对（**优先于数值 diff**，按 lesson）

发现并修复了一处**真实覆盖丢失**（迁移使角色在同样 tick 数内走得更远，下游 leg 冲过头）：

| 覆盖项 | 旧黄金 | 初版新黄金（未修） | 最终新黄金 |
|---|---|---|---|
| 快照数 | 39 | 39 | **39** |
| 滞空快照 | 9（t=400,420,440,460,620,660,680,700,760） | 8（丢 t=460） | **9（逐列相同）** |
| 潜行快照 | 7（t=480..600） | 6（丢 t=600） | **7（逐列相同）** |
| HP 值集 | {20, 13} | {20, 12} | **{20, 13}** |
| HP 首次 =13 | t=640 | — | **t=640（相同）** |
| x 范围 | [−31.70, 23.70] | [−31.70, 23.70] | **[−31.70, 23.70]** |

**未修版本的两个丢覆盖**：
1. 泳出 leg 冲过条带（60 tick 后 x=−25.63，越过东唇 x=−24），角色落进坑里 → 潜行夹边失效，hp 20→12 而非保持 20
2. 10 格坠落伤害覆盖消失（脚本预期的 20→13 变成 12→12 的无关掉血）

**修复方式（tick 中性）**：冲刺 leg `145 → 137`、释放 leg `5 → 13`。
两者合计仍为 **150 tick**（沿用 T-D1 用过的 tick 中性手法），故**后续每个 leg 的 tick 数与快照列全部不变**，
脚本总长仍为 762 tick。这是扫描多个变体后唯一同时满足"覆盖率逐项一致"与"块边界不变"的解。

> 注：先试过"泳出 leg 60→54 + 别处补 6 tick"，总长也对，但把潜行窗口整体前移 6 tick，
> 丢了 t=600 的潜行快照 —— **只看 tick 总数会漏掉这个回退**。这正是 lesson 强调的先查覆盖率。

### 6.2 逐 leg 旧值 → 新值

| # | leg | ticks | 旧 end_x | 新 end_x | 旧 hp | 新 hp | 变化原因 |
|---|---|---|---|---|---|---|---|
| 0 | walk cruise +X | 100 | 20.1424 | **21.8263** | 20 | 20 | 起步快（1.45 s→0.25 s）+ 不再滑行；同 tick 数位移更大 |
| 1 | idle decelerate | 5 | 20.9379 | **22.0733** | 20 | 20 | 滑行 1.94→0.256 格，idle 段推进大幅缩短 |
| 2 | walk into wall | 55 | 23.7000 | 23.7000 | 20 | 20 | 撞墙钳位，逐位相同 |
| 3 | idle | 5 | 23.7000 | 23.7000 | 20 | 20 | 同上 |
| 4 | −Z excursion | 40 | −6.2201 | **−7.8748** | 20 | 20 | 地面巡航更快（同 tick 更远） |
| 5 | +Z return | 42 | −0.9648 | **0.6721** | 20 | 20 | 同上；z 落点改变 |
| 6 | idle | 5 | −0.1882 | **0.9191** | 20 | 20 | 滑行大幅缩短（不再漂 0.78） |
| 7 | sprint plunge −X | 137/145 | −14.4616 | **−14.4070** | 20 | 20 | leg 缩短 8 tick，但速度快；落点几乎不变（见下） |
| 8 | release fwd | 13/5 | −15.4029 | **−15.1636** | 20 | 20 | 释放段加长 8 tick，滑行缩短，位移净近似 |
| 9 | swim out west lip | 60 | −23.1191 | **−21.8050** | 20 | 20 | 更早出水、更少水下位移（水模型未改，但入水点/速度不同） |
| 10 | settle | 10 | −24.4306 | **−22.5605** | 20 | 20 | 承接 leg 9 落点差异 |
| 11 | sneak to pit rim | 120 | −25.2999 | **−25.2967** | 20 | 20 | **夹边位置几乎相同（差 0.003）**，潜行稳态未变 |
| 12 | idle at rim (sneak) | 10 | −25.2999 | **−25.2999** | 20 | 20 | 夹边静止，逐位相同 |
| 13 | walk off 10b fall | 50 | −31.7000 | −31.7000 | **13** | **13** | **坠落伤害覆盖完整保留** |
| 14 | bob in pit water | 60 | y=54.0410 | y=**54.0505** | 13 | 13 | 水中 bob 相位略差（水模型未动） |
| 15 | toward pit west wall | 30 | −31.7000 | −31.7000 | 13 | 13 | 逐位相同 |
| 16 | idle | 10 | −31.7000 | −31.7000 | 13 | 13 | 逐位相同 |
| 17 | final hop | 10 | y=54.1596 | y=54.1596 | 13 | 13 | 逐位相同 |

**39 个快照中 3 张逐字相同**（t=0/740/760 等静止点），其余全部差异均落在
"地面段位移更大 / 滑行段更短 / 空中段更远"这三类**预期漂移**上，无一是行为异常。
滞空、潜行、坠落伤害、撞墙、水中 bob、最终 hop **六类覆盖逐项保留**。

新 hash：`fnv1a64=5516b08e2707acaf`（旧 `4903419f701d9630`）

---

## 7. 测试清单（新增 21 条）

新增文件 `tests/test_friction_pipeline.cpp`（20 条）+ 改写 2 条：

| 分组 | 测试 | 断言要点 |
|---|---|---|
| **瞬态（本卡重点）** | releasing walk input glides only about a quarter of a block | 0.2558，上限 0.35 |
| | walking reaches ninety five percent within a quarter second | ≤6 tick（实测 5） |
| | the momentum threshold truncates the glide tail | ≤12 tick 归零；潜行滑更短 |
| **摩擦因子（迁移后才可断言）** | ground friction is the product of block friction and entity air resistance | k=0.91×0.6；存储速度=0.1×0.98×k |
| | airborne friction ignores the block below | S=1.0，k=0.91 |
| **⚖ 稳态** | the migrated pipeline preserves all three spec cruise speeds | 4.3172 / 5.6123 / 1.2952 |
| | diagonal input is faster by one over 0.98 | 45° Strafe 比值 |
| **⚖ 跳高** | jump apex is unchanged at 1.2522 | |
| | a five thousandth threshold would break the spec apex | 0.003 维持，0.005 变低 |
| **冰面钩子** | ice is slower to reach but keeps speed six times longer than stone | 4.1572 < 4.317；滑行 1.688（6.60×） |
| | slime blocks are the slowest of the three friction classes | 3.0400 |
| **参数实例化** | a falling block and a player keep independent gravity configurations | 0.08/0.04 共存且 2:1 |
| | a caller-supplied config leaves the default config untouched | 值语义 |
| **MoveResult** | move result reports wall hits without any signature break | 四参调用仍可编译行为不变 |
| | move result reports a jump landing with its fall distance | landed + fall_distance |
| | move result flags a ceiling bump without landing | hit_ceiling ∧ ¬landed |
| **形状查询** | the block shape query defaults to solid means full cube | 形状≠solid 时仍支撑 |
| **⚖ 疾跑跳** | sprint jump chain average is 7.127 m/s with MC raw plus 0.2 | 7.1268（+0.003%） |
| | sprint jump chain clearance follows the affine lock | 锁定式 + 3.60–3.75 带 |
| | a strafe-launched sprint jump is slower than a straight one | MC §5.1 行为 |
| **确定性** | the migrated pipeline stays bit-identical across repeated runs | 逐位一致 |

改写 `test_sprint_feel.cpp`：`arc_distances(50,400)` 冷弧 → `chain_arc_distances()` 链式（见 §4）；
`test_player_physics.cpp` 仅更新过时注释（`ground_drag=0.9` 已不存在）。

---

## 8. ★ 建议表（交 PM 落盘，开发者未改任何状态/规格文件）

| # | 建议 | 目标文件 | 理由 / 证据 |
|---|---|---|---|
| S-1 | `docs/01 §2` 跨距下限由 **3.7 调整为 3.65**，或明确标注"以均速为准" | `docs/01-gameplay-spec.md` | 7.127 ⇒ 3.6762 为代数必然；本卡实测 3.6761 精确落在锁定关系上。二者不可兼得（§5） |
| S-2 | `docs/01 §2` 疾跑跳指标补注**采样口径 = 连续跳跃链**（落地即起跳，12-move 窗口） | `docs/01-gameplay-spec.md` | wiki 表头 "Average speed" + "Running start effective? No" + 脚注 "every jump"；链式口径才使 +0.2 命中（§4） |
| S-3 | `research/06 §5.2` 的闭式解不动点复算得 6.8601 m/s，与本实现链式 7.1268 差 +3.9% | `docs/research/06-...md`（PM 决定是否加注） | 差异源于 §5.2 把 J 折算为 0.3274 的等效值；实测用原始 +0.2/跳。**不影响本卡结论**，仅作口径备忘 |
| S-4 | `docs/03 §6` 补一行"摩擦因子 `k = entity.horizontal_drag × block.slipperiness`；空中 S≡1.0；阻尼在位移之后" | `docs/03-architecture.md` | 本卡冻结的接口契约，M2 生物/载具复用 |
| S-5 | 登记新债：**冰/蓝冰/黏液方块内容**（`BlockDef` 增 friction 字段并接线到运行时适配器） | `STATE.md` 债务表 | 本卡只立钩子，未做内容（范围内）；`BlockRegistry::create_default` 目前无冰，运行时恒返回 0.6 |
| S-6 | 登记新债：**水的 MC 流体模型**（本卡保持了 T007 的简化水模型，`water_cruise_speed` 由旧 `water_speed_mult=0.5` 折算） | `STATE.md` 债务表 | 水物理明文在 T-D7 范围外；迁移后有 2 处水中数值小幅漂移（§6.2 leg 9/14） |
| S-7 | `tests/golden/physics_golden_replay_v1.txt` 头注释写"660 ticks"，实际脚本为 **762 tick** | （PM 决定） | 该注释自 T007 起即不准确（快照网格 t=0..760）；本卡未改注释以免混淆重生成 diff |
| S-8 | 潜行禁止疾跑的依据建议入 `research/05` | `docs/research/05-mc-movement-feel.md` | `moveForward ≥ 0.8` 门限 × 潜行 0.3 = 0.294 < 0.8（research/05 已记门限，未记推论） |

---

## 9. 合规与边界自查

- **白名单**：改动仅 `engine/physics/**`、`tests/**`（含 `tests/CMakeLists.txt`）。
  `engine/voxel/**` **未改动** —— `slipperiness_at`/`shape_at` 的默认实现使运行时适配器零改动，
  无需触碰 `BlockDef`（内容留 M2，见 S-5）。
- **禁碰项**：`STATE.md`、`docs/01`–`docs/06`、`docs/research/`、`docs/tasks/` 其他文件、
  `game/**`、`engine/{core,noise,render,net}`、`cmake/`、`CMakeLists.txt`、`.github/`、`assets/`
  **均未改动**（建议以 §8 建议表形式提出）。
- **合规红线**：报告只记数值/公式/来源 URL/访问日期，**无任何反编译源码片段**；
  所有伪代码/说明均自撰。
- **范围边界**：未做自动上台阶、Blip-Up、Jump-Cancel、1.14+ 轴向序、状态效果、游泳/蜘蛛网、
  灵魂沙、生物 AI、载具内容。
- **未调参凑数**：`0.1842` 退役，未新增任何校准系数；`momentum_threshold` 维持裁决值 0.003。

---

## 10. 给 PM 的两个待裁决点（摘要）

1. **§5 跨距冲突**：7.127 与跨距 ≥3.7 不可兼得，本卡按卡面优先命中 7.127（跨距 3.6761）。
   请裁决是否调整 `docs/01 §2` 下限（建议 S-1）。
2. **§4 采样口径**：7.127 的语义是"连续跳跃链"均速（wiki 原文支持），本卡据此实现并让 +0.2 自然命中。
   请确认该口径（建议 S-2），并知悉 T-D1 的 `0.1842` 是在冷弧口径下凑出的、已退役。
