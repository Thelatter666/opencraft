# T-M2 报告：生物 AI（目标栈）与刷怪 + 三种首发生物

分支 `taskT-M2`　worktree `/Users/happy/Desktop/opencraft_worktree/T-M2-mob-ai`
基线 `8f2b065`（359/359）　交付 `556ec5c0fb5d0a3f21822b93f84896e5`（`build/opencraft`）
测试 `build/tests/opencraft_tests` md5 `d1be1a25018673df947557a00ab1a9f3`
日期 2026-09-18

---

## 1. 交付摘要（一段话）

在 T-E1 实体层上用 **`EntityDef` 填表 + 目标栈调度**建立了生物框架，并以三种**原创名/外观**
的样本验证：**苔背兽 Mossback**（被动，可引诱/可繁育/掉落食物）、**空塚巡行者 Hollow Wretch**
（敌对近战追踪）、**爆芽 Blastbud**（敌对自爆，3 格 + 视线 → 30 tick 引信 → 爆炸，拉开 7 格
取消/引退信）。刷怪按 `docs/01 §6` 落地（光照 0 / 24–128 格环带 / >128 消除 /
mob cap = 70×可刷区块÷289 / 被动按区块一次性生成 / 包 4 只）。新增 **51 个测试**（总 410/410），
**既有 359 个测试的断言一条未改**（`test_entity_store.cpp` 里 `types.size() == 2` 之所以仍然成立，
是因为生物类型由 `MobRegistry` 在 `EntityTypeRegistry::create_default()` **之上**注册，
见 §4）。实机证据见 `docs/qa/T-M2-2026-09-18/`。

---

## 2. 变更文件清单

**新增（生产代码）**

| 文件 | 行数量级 | 作用 |
|---|---|---|
| `game/common/include/opencraft/game/mob_goal.hpp` | 160 | 目标类型 + **纯函数调度器** `scan_goals` |
| `game/common/src/mob_goal.cpp` | 25 | 目标表排序 / 优先级查询 |
| `game/common/include/opencraft/game/mob_type.hpp` | 250 | `MobDef` / `MobRegistry`（**填表点**） |
| `game/common/src/mob_type.cpp` | 380 | **三种生物的内容表**（每生物一个函数） |
| `game/common/include/opencraft/game/entity_pick.hpp` | 60 | 射线-包围盒（客户端选目标 / 权威侧同一判据） |
| `game/server/sim/include/opencraft/sim/mob_sim.hpp` | 1100 | 感知 / 目标实现 / 导航 / 运动 / 战斗 / 繁育 / 引信 |
| `game/server/sim/include/opencraft/sim/mob_spawn.hpp` | 430 | 刷怪规则（地表探针 + 洞穴探针 / cap / 包 / 台账） |
| `tests/mob_test_world.hpp` | 190 | 三个测试文件共用的手搭世界夹具 |
| `tests/test_mob_goal.cpp`（234）/`test_mob_ai.cpp`（695）/`test_mob_spawn.cpp`（263）/`test_mob_authority.cpp`（321） | — | 51 个新测试 |

**修改（生产代码）**

| 文件 | 改动 |
|---|---|
| `game/common/include/opencraft/game/entity_type.hpp` | `EntityClass`（Item/Mob）+ `EntityDef::entity_class`（**末尾追加**，既有位置初始化不受影响） |
| `game/common/include/opencraft/game/protocol.hpp` | `ActorPose::held_item`；`ActionKind::Attack/Feed`；6 个新 `ActionReject`；`ActorEventKind`/`ActorEvent`；`WorldChanges::actor_events`；`IAuthority::observe_actor`（**带默认空实现**）；`kAttackReach`/`kPunchDamage` |
| `game/common/src/item_registry.cpp` | 新增 `raw_haunch`/`sturdy_hide`（苔背兽掉落） |
| `game/server/sim/include/opencraft/sim/entity_store.hpp` | `kActorId`/`kNoEntityId`；`MobAi`（每实例 AI 状态）；`Entity::ai`；`Entity::health` **int → double**（生物伤害是 2.5 这样的分数） |
| `game/server/sim/include/opencraft/sim/item_sim.hpp` | `step_items` 跳过非 Item 类；抽出 `spawn_item_stack_at`（掉落物构造一处） |
| `game/server/sim/include/opencraft/sim/world_sim.hpp/.cpp` | 生物层接线（`mobs_`/`MobWorld`/`ActorEvent` 缓冲）、`observe_actor`、`summon_mob`、`apply_attack`/`apply_feed`、拾取类守卫、`in_attack_reach` |
| `game/client/src/{tick,main,interaction,world}.hpp/.cpp` | 每 tick 上报 `observe_actor`；生物事件落到客户端血量；左键攻击 / 右键喂食；生物渲染（体 + 头两盒） |
| `game/client/src/inventory_wiring.hpp` | `client::mob_skin`（**临时外观**：复用既有图集方块 tile） |

**证据**：`docs/qa/T-M2-2026-09-18/`（README + 两个 session log + 25 张 PNG + 6 个工具脚本 +
两个临时探针补丁）。

---

## 3. 验收标准逐条对账

| # | 卡面要求 | 结果 | 判据（可复跑） |
|---|---|---|---|
| 1 | ★ 目标栈并发 | ✅ | `test_mob_goal.cpp` 9 例：`scan_goals` 纯函数断言"高优先级在跑 + 低优先级可跑 ⇒ **两者都在跑**"；另断言**同优先级两个目标**（僵尸表 2/2）都启动、**canUse 全表求值**（不是只跑第一个）；实机形态见 §5.2.2（`goals=288` = 引诱 + 注视同时运行） |
| 2 | ★ 三种距离字段区分 | ✅ | **数据层**：`test_mob_goal.cpp` 断言察觉/`follow_range`/`tempt_*` 是独立字段且取值不同。**行为层**：`test_mob_ai.cpp` 用**四个受控场景**证明——(a) 察觉 35 vs 16（30 格处一方锁定一方不锁）(b) `follow_range` 20 vs 35（24 格处一方放弃）(c) 引诱**感知 4 / 起点 6 / 终点 8** 三闸门分别可动（5 格处"在起点半径内但在感知半径外"⇒ 不跟随）(d) 出厂苔背兽"感知 10 但起点 6"（9 格不跟随、5 格跟随） |
| 3 | 三种生物可玩 | ✅ | 被动：实机 **两头不同 id 喂食 → 2.5 s 后产仔**（`scene_session.log`）。敌对近战：实机每秒 3.0 伤害、HUD 红心 20→0。敌对自爆：实机 3 格 → 30 tick → 爆炸（32.7 伤害 = 49×(1−2.5/6)），**拉远到 5 格引信回退、7 格外/失去视线取消**（单测覆盖三种取消路径） |
| 4 | 刷怪条件 | ✅ | `test_mob_spawn.cpp` 10 例：光照 **0** 才刷（等级 1 即不刷）、24–128 环带（`in_ring` 边界 + 生成物距离实assert）、>128 立即消除、cap = 70×可刷区块/289（42 区块 ⇒ cap 10，填满则不再生成）、被动按区块一次性（台账）、和平难度无敌对、未加载区块不刷、位置需地板 + 净空 |
| 5 | 复用公共碰撞 | ✅ | 全仓 `sweep_axis_*` 使用者只有 4 个文件：`sweep.hpp`（定义）、`player_physics.cpp`（玩家）、`item_sim.hpp`（掉落物）、`mob_sim.hpp`（生物）。生物移动另复用 `physics::box_collides` / `highest_surface_below`；**未新增第三份碰撞**（`grep` 结果见 §7） |
| 6 | 存量测试全绿 | ✅ | **410/410，10626 断言**；既有 359 例的**断言一条未改未放宽**（详见 §4 的机制说明） |
| 7 | 待校准项已标注 | ✅ | 代码注释逐项 ⚠：逃跑速度倍率（**故意取 1.0，不发明**）、`follow_range` wiki 口径冲突、体形来源、攻击间隔、爆心伤害的"简单"档、爆炸半径/衰减、游荡半径、恐慌时长、求偶接触距离、眼高比、卡住阈值、`approach_distance` |
| 8 | clang-format 无 diff | ✅ | `/Library/Developer/CommandLineTools/usr/bin/clang-format`（17）对 25 个改动文件 `--dry-run` 无输出；测试名不含 `[` |
| 9 | 独立 worktree + 提交前缀 | ✅ | `/Users/happy/Desktop/opencraft_worktree/T-M2-mob-ai`（分支 `taskT-M2`）；提交 `taskT-M2: …` |
| 10 | 实机证据 | ✅（含装置声明） | `docs/qa/T-M2-2026-09-18/README.md` §1 逐项写明**观测装置**：真客户端 + 真权威侧 + HID 注入；**敌对生物用临时召唤键**（因为光照 0 在本世界=洞穴，玩家不可达），被动为**自然刷怪**；判据以**日志**为主（卡面允许"截图或日志"），截图 25 张（其中 `shots/02b`、`shots/03a` 我逐张核看，可见生物本体） |
| 11 | 报告含寻路简化 + 扩展成本 | ✅ | §5（寻路）/ §6（新增一种生物要改什么） |
| 12 | agentmemory action 置 done | ✅ | `act_mu5q683d_51989f293284` |

---

## 4. 关键设计决定（含"为什么没动既有断言"）

1. **生物类型注册在 `EntityTypeRegistry::create_default()` 之上**，由
   `MobRegistry::create_default(types, items)` 完成（`WorldSim` 构造时调用一次）。
   这样 T-E1 冻结的 `create_default()` 契约（`types.size() == 2`）逐字不变，
   而**新增一种生物仍然只是"加一行注册 + 一个配置函数"**（见 §6）。代价：任何**绕过
   `MobRegistry`** 自建 `EntityTypeRegistry` 的代码看不到生物类型——本仓只有 `WorldSim`
   一处构造（客户端读 `authority.entity_types()`），已在 `mob_type.hpp` 注明。
2. **一个实体存储，两种类**：`EntityDef::entity_class`（Item/Mob）让
   `step_items` 与 `step_mobs` **按构造互斥**。反方案（两个 `EntityStore`）会让
   "爆炸销毁掉落物""生物不是拾取目标"这类跨类规则变成两处记账。`Entity` 新增 `MobAi`
   （纯 POD、可拷贝、无指针），掉落物侧永不读写它。
3. **运动不走 `physics::step_player`**，而是直接驱动 T-D40 抽出的
   `physics::sweep_axis_x|y|z` / `box_collides` / `highest_surface_below`。
   原因：`step_player` 用 `PlayerState` 固定的 0.6×1.8 盒，而 `docs/03 §6` 冻结
   "物理参数按实体类型实例化"（苔背兽 0.9×1.4）。**已作为接口缺口上报**（§9 建议 R-1）：
   若 `PhysicsConfig` 带上 per-entity 体形，下一张生物卡就能直接复用 `step_player`。
   必须与玩家一致的常量（动量截断 0.003、子步 0.5、生物 step height 1.0）**从
   `PhysicsConfig` 读取而非抄写**，与 T-D40"一份规则一处来源"一致。
4. **速度口径已写明并断言**：`research/01 §10.2` 给的是 attr（僵尸 0.23），
   `research/11 §0.1` 说单位是 block/tick 量级，但**两处都没有给出换算**。
   本卡把加速度定义为让摩擦管线的稳态速度**等于**该 attr（`a = move_speed × (1 − 0.91×0.6)`），
   并用本仓唯一实测过的速度做交叉校验（玩家 4.317 m/s = 0.216 block/tick ⇒ 僵尸 0.23 比走路玩家快 6%、
   明显慢于疾跑 5.612 m/s）。`test_mob_ai.cpp` 用 20 tick 位移断言稳态 ≒ 0.23（±5%）。
   **这是解释而非引文**，已在报告与代码注释中标注。
5. **"掉落食物"可达**：为此加了最小伤害通道（`ActionKind::Attack`，徒手 1 点 +
   ⚖ 10 tick 无敌帧），因为"实现了但不可达"是本项目反复付代价的失败模式。完整攻击系统
   （攻速/84.8% 伤害曲线/暴击/击退/护甲）留给玩家战斗卡，`ItemDef` 也没有攻击字段。
6. **`Entity::health` 由 int 改 double**：生物伤害在来源里是分数（简单 2.5），
   截断会静默改掉 ⚖ 数值。既有用法（`== 5`、`<= 0`、`= max_health`）全部语义不变。

---

## 5. ★ 寻路的简化程度与后续补全点（卡面第 11 条要求）

**实现了**（`mob_sim.hpp` 文件头逐条写明）：
- **目的地模型**：目标给出"要到达的点"，导航每 tick 朝它走（`research/11 §1.4.1` 的
  "当前路径、速度、目的地"三件里，路径退化为"直线 + 遇障绕行"）。
- **遇障绕行 = 贴墙走**：直行被挡时**一次性选定一侧并保持**（`ai.nav_side`），
  沿墙 45° 斜走。**保持侧向是承重设计**——"每 tick 重新评估邻居取最优"会在墙前左右抖动，
  永远过不去（本卡先写成那样，测试里表现为原地不动）。
- **卡住检测**：累计无位移超过 60 tick ⇒ 放弃该目的地（§1.4.1 的"卡住检测并停掉路径"），
  由目标重新选点，而不是永久顶墙。
- 运动本身：重力/阻尼/子步/逐轴 Y→X→Z/步高 1.0 走上整方块（T-D8 的 mob 形态），
  全部经 `sweep_axis_*`。

**未实现（后续补全点，按优先级）**：
1. **无节点路径** ⇒ 绕不过"走不到边缘的凹形障碍"（一堵墙可以，一个 U 形不行）。
   补全方式：把 `navigate_step()` 换成体素 A*/JPS，**只有这一个函数要改**（目标/运动/感知不动）。
2. **无代价表**（−1/0/4/8/16）⇒ 不会为避危险绕远路，也不会因 `animal` 覆盖"绝不走进火里"。
   本世界目前**没有会造成伤害的方块**，所以建表也没有东西可覆盖；伤害方块落盘时按
   `research/11 §1.4.2` 的"默认表 + 逐生物覆盖表"两层结构补，`MobDef` 留扩展位。
3. **无跳跃/游泳/飞行**：生物靠 1.0 步高，遇到 2 格墙就停（测试断言了这一点）；
   水中行为、摔落伤害都没有。
4. **单路径**（§1.4.4 的"生成多条路径取代价最低"未做）。
5. 无"随机点落在方块内则上移到最近空气格"的严格实现：`pick_ground_point` 用
   "向下/上各探几格找地板 + 头顶净空"近似（6 次尝试失败就放弃该轮游荡）。

---

## 6. ★ 再新增一种生物需要改哪些地方（卡面第 11 条要求）

**答：写一个配置函数 + 一行注册。** 以斑纹羊等价物为例：

```cpp
// game/common/src/mob_type.cpp
MobDef stripewool_def(const ItemRegistry &items) {
    MobDef def;
    def.physics = mob_physics("Stripewool", 0.45, 1.3, 8);  // 体形/HP（块体 + 重力/阻尼已由 helper 填好）
    def.mob_class = MobClass::Passive;
    def.move_speed = 0.23;            // ⚖ research/11 §6.1
    def.sight_range = 16.0;
    def.follow_range = 32.0;
    def.tempt_range = 10.0; def.tempt_start_range = 6.0; def.tempt_stop_range = 10.0;
    def.tempt_item = items.id_of("grain_loaf");
    def.drops = {{items.id_of("raw_haunch"), 1, 2}, {items.id_of("sturdy_hide"), 0, 1}};
    def.goals = {{GoalKind::Panic, 1}, {GoalKind::Tempt, 4}, {GoalKind::Breed, 5},
                 {GoalKind::RandomStroll, 7}, {GoalKind::LookAtPlayer, 8}};
    return def;
}

MobRegistry MobRegistry::create_default(EntityTypeRegistry &types, const ItemRegistry &items) {
    // ...
    registry.register_mob(types, "stripewool", stripewool_def(items));   // ← 唯一的新增行
}
```

**不需要改的地方**（这是本卡要证明的设计目标）：
- 调度器（`mob_goal.hpp` 只认 `MobDef::goals`，不认生物）；
- 目标实现（`mob_sim.hpp` 只读 `MobDef` 字段，没有任何 `switch (mob_id)`）；
- 刷怪（`mob_spawn.hpp` 只问 `mob_class` 与注册表；cap 按**类**计）；
- 协议/客户端（渲染按 `EntityDef` 体形 + `client::mob_skin` 表；未知 id 有兜底皮肤 stone）；
- `Entity`/`EntityStore`（`MobAi` 已经是所有生物共用的状态块）。

**仍要改的**：`client::mob_skin` 加一行配色（纯外观，未知 id 也能渲染）。若新生物需要
**新目标类型**（远程/开门/吃草/跟随主人…），才需要动 `GoalKind` + `mob_sim.hpp` 的目标实现。
若需要**新掉落物**，在 `item_registry.cpp` 加一行。

---

## 7. 验收 5 的 `grep` 证据

```
$ grep -rln "sweep_axis_" game engine
engine/physics/include/opencraft/physics/sweep.hpp      ← 定义（T-D40）
engine/physics/src/player_physics.cpp                   ← 玩家
game/server/sim/include/opencraft/sim/item_sim.hpp      ← 掉落物
game/server/sim/include/opencraft/sim/mob_sim.hpp       ← 生物（本卡）
```
生物侧另用 `physics::box_collides`（探测）/ `physics::highest_surface_below`（上台阶）。
唯一的 `min_x/max_x` 手写几何是 `melee_reach`，那是**攻击盒相交判定**（`research/11 §1.5.1`
的"碰撞盒外扩 0.828"），不是移动碰撞。

---

## 8. 构建 / 运行 / 测试

```
cd /Users/happy/Desktop/opencraft_worktree/T-M2-mob-ai
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # 不设 FETCHCONTENT_BASE_DIR
cmake --build build -j8
./build/tests/opencraft_tests                      # 410 test cases / 10626 assertions / 0 failed
./build/opencraft                                   # 需 cwd 在 build：
```
- **基线对账**：改动前 `359/359`（10198 断言）→ 改动后 `410/410`（10626）。
  新增 51 例；既有断言**零改动**。
- 二进制 md5：`build/opencraft` = `556ec5c0fb5d0a3f21822b93f84896e5`，
  `build/tests/opencraft_tests` = `d1be1a25018673df947557a00ab1a9f3`。
- 警告：本卡涉及的 TU 在 `-Wall -Wextra -Wpedantic` 下**零警告**（修掉了 4 个：未用参数 ×2、
  `[[nodiscard]]` 用在变量上、ctor 初始化顺序）。

---

## 9. 卡面纠正与待校准（主动记功项）

### 9.1 卡面/规格与调研文档的冲突

| # | 冲突 | 处理 | 依据 |
|---|---|---|---|
| C-1 | 卡面"难度倍率：简单 ×1 / 普通 ×1.5 / 困难 ×2"（`docs/01 §6` 同）**不能**从普通值 3 推出简单的 2.5 | **按来源行取数**（2.5 / 3 / 4.5），并在 `MobDef` 注释写明卡面梯子推不出简单档 | `research/01 §10.2` 僵尸行 |
| C-2 | 卡面"刷怪：光照 0" vs `research/01 §10.3`"光照 ≤ 随机 0–7（内天空光 ≤7 且方块光 0）" | **按卡面/规格取严**（有效光照 = 0），差异记于此 | `docs/01 §6` 是规格 |
| C-3 | `research/01 §10.3` 还有一条"32–128 格随机消失（1/800 per tick）" | **未实现**（需要逐 tick 随机抽样；卡面验收只要求 >128 消除） | 列为缺口 |
| C-4 | 三种敌对生物的**体形**在任何来源里都没有 | 取项目既有人形体宽 0.6 + 生态位近似高度（巡行者 0.6×1.8、爆芽 0.6×1.7），注释标"实现选择，非引文" | `research/11 §6.1` 只给被动体形 |
| C-5 | `MobDef` 的全部"⚠ 待校准"数值 | 见 §9.2，一律标注，**不发明** | 卡面 §"待校准" |

### 9.2 待校准清单（代码里每项都有 ⚠ 注释）

| 项 | 取值 | 说明 |
|---|---|---|
| 逃跑速度倍率 | **1.0（不加成）** | 卡面明令"不得编造"；wiki 未给，故意取 1.0 作为"校准前的无加成" |
| `follow_range` 口径冲突 | 出厂值按生物分档（巡行者 35 / 爆芽 32） | `research/11 §1.3.1` 记载的 wiki 自身冲突 |
| 生物眼高 | `height × 0.9` | 无来源；取玩家比例（1.62/1.8） |
| 近战攻击间隔 | 20 tick | 无来源；1 次/秒 |
| 简单难度爆炸伤害 | = 普通值 | `research/01 §10.2` 只给普通 49 / 困难 64.5 |
| 爆炸半径/衰减 | 2×威力 = 6 格、线性衰减 | ⚠ 未校准；`docs/01 §6` 只给"威力 3" |
| 游荡半径 | 10 格（被动）/ 20 格（大范围） | 无来源，实现旋钮 |
| 恐慌时长 | 60 tick | 来源只说"数秒" |
| 求偶接触距离 / 停步距离 / 卡住阈值 | 1.5 / 1.5 / 60 tick | 实现旋钮 |

### 9.3 建议表（请 PM 裁决后落盘；本卡不改 `docs/**`）

| ID | 类型 | 内容 | 依据 |
|---|---|---|---|
| R-1 | **接口缺口（建议立案）** | `PhysicsConfig` 只有 per-kind 的 step height，**没有 per-entity 体形**；`step_player` 用固定的 0.6×1.8 盒，因此"实体层以 `EntityDef` 实例化物理参数"与"复用 `step_player`"目前不可兼得。建议给 `PhysicsConfig` 补 `half_width`/`height`（纯加字段，不改既有语义），下一张生物/载具卡即可整段复用玩家管线 | 本卡 §4.3 |
| R-2 | **规格澄清** | 建议把"敌对生成光照 0"与 `research/01 §10.3` 的"≤0–7"在规格里对齐口径（本次按 0 实现） | C-2 |
| R-3 | **新增债务建议** | (a) 生物与玩家**无碰撞**（生物可走进玩家体内，实机表现为"生物贴脸时其方盒铺满屏幕"）；(b) 无**摔落伤害**（不走 `step_player`）；(c) 玩家**死亡状态不存在**（血量到 0 仍在玩，HUD 空心）；(d) 敌对 AI 的**玩家无敌帧/护甲/击退**未做。四条都属"mob/玩家战斗卡" | §10 |
| R-4 | **文档补充建议** | `docs/03 §6` 可补一句"生物运动复用 `sweep_*` 但不复用 `step_player`（体形不同）"，避免下一张卡重复踩坑 | §4.3 |
| R-5 | **环境备忘建议** | 记入 `docs/05 §3.1`：① 合成鼠标移动在本机**严重非线性**（单次 30 px 可把 pitch 打到 ±89° 钳位），**瞄准不要用鼠标注入**——改用"潜行（眼高 1.27 < 生物体高）后水平视线即可命中"；② HUD 第 N 格 = 槽 N−1（`GLFW_KEY_1 + slot`），脚本按键常错一格；③ `screencapture -l <win>` 对**非前台**窗口返回旧表面，多张"不同"截图 md5 相同，截图前必须 activate + 连拍 | 本卡取证 |
| R-6 | **内容排期建议** | 首发表名单（`docs/01 §6` 15–25 种）在本卡框架下已是"纯填表"，建议按生态位分批出内容卡；**远程攻击（骷髅/蜘蛛档）会第一次需要新目标类型**（弓/投射物），应单独成卡 | §6 |

---

## 10. 已知问题与缺口

1. **路径简化**（§5）：无节点路径/代价表/跳跃/游泳/多路径。
2. **生物不落盘**：`EntityStore` 内容不随区块存档（T-E1 已如此），重开世界生物全部重新生成（种子确定，故同种子可复现）。
3. **无昼夜** ⇒ 地表天光恒 15 ⇒ **敌对生物只在洞穴自然生成**，玩家几乎遇不到（这也是实机行为取证用召唤键的原因）。
4. **无 XP 系统**：`MobDef::xp_reward` 是接口预留（同 T-E1 的 `pickup_delay_thrown`），击杀不掉经验。
5. **无击杀掉落以外的战斗反馈**：生物死亡无动画/音效/粒子（卡面明令不做）。
6. **自爆不破坏方块**：只伤玩家 + 销毁掉落物（`destroy_items_in_radius`，T-E1 既有入口）；方块破坏属爆炸系统卡。
7. **生物间不互相攻击**：只有玩家能伤害生物（`Revenge` 因此只对玩家生效）。
8. **`ActionKind::Attack` 用徒手 1 点伤害**，物品攻击力字段不存在。
9. **外观是占位**：生物 = 两个复用图集的方盒（`client::mob_skin`），无模型/贴图集；名字与配色原创。
10. **生物与玩家无碰撞**（R-3a），贴脸时生物方盒会充满屏幕。

---

## 11. 结论

- 目标栈框架**可并发、可填表**：新增一种生物 = 一个配置函数 + 一行注册（§6）。
- 三种距离字段**三处独立**，并且有"改一个值行为就变"的行为级测试（验收 2）。
- 刷怪规则按 `docs/01 §6` 落地，含 cap 与"按区块一次性"两处容易漏掉的规则。
- **410/410**；既有断言零改动；`sweep_axis_*` 使用者仍是 4 个文件（无第三份碰撞）。
- 待 PM 裁决的是 §9.3 的 R-1（`PhysicsConfig` 体形缺口）与 R-3（四条战斗/物理债务）。
