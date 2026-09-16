# T-E1 开发者报告：实体层 + 掉落物（M2c 地基第一卡）

> 分支 `task/T-E1-entity-layer`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-T-E1`
> 提交 `cf98ff4`（`taskT-E1:` 前缀）+ 一轮实机取证后的渲染修复提交（见 §7.6）
> 基线：卡面写 **324**，实测 **324 / 9863 断言**（干净检出 configure+build 后跑出）；本卡交付 **359 / 10198 断言**
> 实机证据：`docs/qa/T-E1-2026-09-17/`（README.md 是索引，**已完成**）
> 二进制：`build/opencraft`（Release，worktree 内）md5 `2deca4791afd8d1436bc7b4e6773c995`

---

## 1. 变更摘要

一句话：**仓库第一次有了实体层**（此前 `find engine game -iname "*entit*"` 为空），
并在其上交付**掉落物**（挖方块 → 掉落物 → 走近拾取进库存），
物理/拾取/合并/超时/环境销毁全部按 `research/11 §4.1–4.4` 落地并有单测。

- **实体层**：类型注册表（`game::EntityTypeRegistry`，仿 Block/ItemRegistry 约定）+ 稠密槽池存储
  （`server::EntityStore`，遍历即升序 id ⇒ 确定顺序由容器保证）+ 每类型物理参数
  （`game::EntityDef`，即 `docs/03 §6` 的「物理参数按实体类型实例化」）+
  AABB 与逐轴扫掠（复用 `physics::IBlockSource` 契约）+ **权威侧持有实体状态**
  （`WorldSim` 内第二个系统面，不与区块数据同存）。
- **掉落物**：A → P → D 执行序、重力 −0.04、阻尼 0.98/0.98、终端速度 39.2 m/s（推导非硬编码）、
  拾取盒几何、10 tick 拾取延迟（投掷 40 只留接口）、40 tick 合并（跨方块 2 tick）、
  6000 tick 超时且**区块卸载暂停**、5 HP 环境销毁、虚空即灭、不弹跳。
- **客户端**：掉落物渲染（复用 crack 着色器 + 图集，0.25 立方 + 自转/浮动）、
  每 tick 拾取循环（本地几何预筛 + 库存副本干跑 + `PickUp` 请求）。
- **接口**：`game::ActionKind` 新增 `PickUp`、`game::ActionReject` 追加 5 个码；
  **物品注册表上收至权威侧**（`WorldSim::items()`，客户端改用 `world.items()`）；
  `IAuthority` **五个既有动词零改动**、`engine/physics/**` **零 diff**、既有 324 例断言逐字未动。

### 1.1 文件清单（新增 2 222 行 + 修改 360 行 / 删 5 行）

| 文件 | 性质 | 内容 |
|---|---|---|
| `game/common/include/opencraft/game/entity_type.hpp`（113） | 新增 | 实体类型注册表：`EntityDef`（box/gravity/drag/health/是否携带物品栈）+ 双向查询 |
| `game/common/src/entity_type.cpp`（112） | 新增 | 注册表实现 + launch 类型表（只有 `item`，卡面不做生物） |
| `game/common/include/opencraft/game/pickup.hpp`（52） | 新增 | 拾取盒几何，**两端共用**（客户端预筛 + 权威侧复核） |
| `game/server/sim/include/opencraft/sim/entity_store.hpp`（211） | 新增 | `EntityId` / `Entity` / 稠密槽池 + 确定顺序遍历 + 槽位复用 |
| `game/server/sim/include/opencraft/sim/item_sim.hpp`（602） | 新增 | `IItemWorld` 接缝、`ItemRules`、`ItemBlockHazard`、A→P→D 运动、合并、销毁、生成、爆炸 API |
| `game/common/include/opencraft/game/protocol.hpp` / `src/protocol.cpp` | 修改 | `PickUp` 动词 + 5 个拒绝码（追加，旧值不变）+ 文案 |
| `game/common/include/opencraft/game/item_registry.hpp` / `src/item_registry.cpp` | 修改 | `item_for_block()`（方块 → 掉落物，升序扫描确定性） |
| `game/common/CMakeLists.txt` | 修改 | 挂 `entity_type.cpp` |
| `game/server/sim/include/opencraft/sim/world_sim.hpp` / `src/world_sim.cpp` | 修改 | 实体状态成员 + `EntityWorld` 私有嵌套适配器 + `apply_pickup` + `tick()` 推进 + `apply_dig` 生成掉落物 |
| `game/client/src/world.hpp` | 修改 | 只读视图新增 `entities()` / `entity_types()` / `items()` 三个 const 转发 |
| `game/client/src/tick.cpp` | 修改 | 每 tick 拾取循环 |
| `game/client/src/main.cpp` | 修改 | 掉落物渲染 + 改用 `world.items()`（不再自建物品注册表） |
| `tests/CMakeLists.txt` | 修改 | 挂三个新测试文件 |
| `tests/test_entity_store.cpp`（145） | 新增 | 9 例：类型注册表 + 存储/遍历/复用 |
| `tests/test_item_drop.cpp`（715） | 新增 | 25 例：运动参数/执行序/终端速度/着地/墙/拾取几何/合并/超时/环境销毁 |
| `tests/test_drop_authority.cpp`（272） | 新增 | 6 例：权威侧挖→掉落物、拾取动词的 4 种拒绝、冻结与恢复、物品注册表归属 |

---

## 2. 实体层的存储结构选择（卡面 §13 要求说明）

**选了：稠密槽池 + u16/u32 稳定句柄；不用 ECS，不用 `unordered_map`。**

```
slots_  : std::vector<Entity>      // 一个实体 = 一个连续结构体，alive 标志在结构体内
free_ids_: std::vector<EntityId>   // 空闲槽 LIFO
EntityId = 槽下标 + 1              // 0 = 无实体（与 BlockRegistry::kAirId / ItemRegistry::kEmptyId 同构）
```

理由，按重要性排序：

1. **遍历确定顺序是规格要求**（`docs/03 §6`：实体遍历为确定顺序），而且它必须是**容器的性质**，
   不能靠每个调用点自觉。槽下标升序 == id 升序，所以 `for_each_entity()` **由构造保证确定**；
   `unordered_map`（按 id 查找是热操作时的自然选择）会让遍历顺序跟着哈希走，
   `std::map` 则每个实体多一次节点分配。单测直接断言遍历序列 == 升序 id，并断言它**在擦除与复用之后仍成立**。
2. **状态要过网**（M3 权威侧发实体快照）：一个实体 = 一个结构体，中间没有间接层，
   今天这个结构体就是将来的载荷。ECS 会把「必须过网的字段」藏到组件 id 后面，与 T-A1 划定的边界冲突。
3. **槽位复用是可观测的，所以做了防护**：`erase()` 把槽还给 LIFO 空闲表，后来的 `spawn` 会拿到同一个 id。
   进程内不可观测（客户端在同一 tick 内先读后发，且只有 `tick()`/`submit()` 会改存储），
   但网络一上就能看见 —— 因此 `PickUp` 请求**同时携带客户端读到的 item id**，权威侧双向复核（见 §4）。
4. **擦除不动内存**：`erase()` 只翻标志 + 压空闲表，`find()` 拿到的指针在下次 `spawn` 前一直有效。
   模拟内部（合并/销毁）依赖这一点，已在头文件写明。

**为什么不是 EnTT**：卡面明确否决，理由也站得住 —— 可达内容只有一种实体，
而组件式存储会让「哪些字段要序列化」变成每个组件各自的问题；类型注册表已经把扩展口留好了
（`EntityDef` 加字段 + `register_type`，存储与遍历都不用动）。**M3 定完线上格式再回头看 ECS。**

**实体是独立系统面**：`EntityStore` 与 `ChunkManager` 完全分离，
既不随区块卸载释放，也不参与区块序列化（`docs/03 §6` 的「实体存 entities/，方块存区块」）。
代价：实体**不落盘** —— 见 §8 缺口 3。

---

## 3. 掉落物规则逐条对照（research/11 §4.1–4.4）

| 规则（来源） | 实现位置 | 值/做法 | 单测 |
|---|---|---|---|
| 重力 −0.04（§4.1） | `EntityDef::gravity`（`entity_type.cpp`） | 0.04，**不是**玩家的 0.08 | `item drop: the parameters are…` |
| 阻尼 0.98/0.98（§4.1） | `EntityDef::vertical_drag` / `horizontal_drag` | 0.98/0.98，**不是** 0.91 | 同上 + `…damping is 0.98…` |
| **执行序 A → P → D**（§4.1） | `step_item_motion()` | 先加速、后位移、最后阻尼 | 三条：首 tick 已下落 0.04；与 P→A→D 参考积分器差 `g×tick`；玩家参数落点相差 >1 格 |
| 终端速度 39.2 m/s（§4.1） | **推导**：`g·drag/(1−drag)` | 1.96 格/tick | `terminal velocity is 1.96 blocks/tick` |
| 碰撞盒 0.25³（§4.2） | `EntityDef::half_width/height` | 0.125 / 0.25 | `entity box: the corners…` |
| 拾取盒水平 +1 含边界、垂直 +0.5 不含（§4.2） | `game/pickup.hpp` | 四条比较，两含两不含 | `pickup box: horizontal faces are inclusive…`（6 个边界断言） |
| 拾取盒随姿态变化（§4.2） | `pickup_box_contains()` 用 `actor.height` | 潜行 1.5 时上界 2.0 | `pickup box: the sneaking pose shrinks it` |
| 拾取延迟 10 / 投掷 40（§4.2） | `ItemRules::pickup_delay_natural/thrown` | 10 生效；40 **只留字段** | `spawn_item_drop…` + `pickup refuses…a live delay` |
| 只装进主 27 + 快捷栏 9（§4.2） | 客户端 `add_item()`（既有 `storage_range()`） | 复用 T-I1 已有规则，未另写 | 既有 `test_inventory` 覆盖；实机 HUD 计数见 §6 |
| 装不下就留在地上（§4.2） | `tick.cpp` 拾取循环 | **先在库存副本上插入**，占不下就不发请求 | 实机 §6 |
| 合并盒 0.5×0.25×0.5（§4.3） | `ItemRules::merge_box_*` | 见 §7.3 读法申报 | `outside the 0.5 x 0.25 x 0.5 box nothing merges` |
| 同类型 + 可堆叠 + 不超上限（§4.3） | `item_detail::mergeable()` | `limit ≤ 1` 不合并；溢出**不做**部分合并（卡面口径） | `different items, unstackable…never merge` |
| 数量多者保留（§4.3） | `item_detail::merge_pass()` | 计数相等时**小 id 保留**（确定性） | `the larger stack keeps…` |
| 计时器取剩余更长（§4.3） | 同上 | `age = min`、`pickup_delay = max` | `the survivor takes the longer remaining timers` |
| 每 40 tick / 跨方块 2 tick（§4.3） | `merge_timer` + `last_block` | 见实现 | `crossing a block boundary re-arms…`（断言跨方块后计时器 ≤2 且第 4 tick 已合并） |
| 6000 tick 超时（§4.4） | `ItemRules::despawn_ticks` | `age >= 6000` | `despawns at 6000 ticks…` |
| **区块卸载暂停计时**（§4.4） | `step_items()` 按 `chunk_loaded` 整体跳过 | **一个计数器**，卸载即不推进 | 单测（冻结 100 tick 后 age 不变）+ 权威侧集成测试（`stream()` 释放 9 区块后同样不变） |
| 落入虚空即灭（§4.4） | `ItemRules::void_y = −128` | 位置低于 −128 即销毁 | `a drop below the void line…` |
| 5 点生命 + 火/岩浆/仙人掌/爆炸（§4.4） | `ItemBlockHazard` + `touches_hazard()` + `destroy_items_in_radius()` | 三类方块接触即毁；爆炸 API | 三条环境测试（含爆炸半径内外） |
| **伤害性方块不伤害掉落物**（§4.4 警告） | 规则表里**没有**那五个名字 | 负规则由构造保证（同一张表） | `the rule table names fire, lava and cactus and nothing else` + `a damage block…leaves the drop alone`（岩浆块上站 20 tick 5 HP 不变） |
| 不可被玩家/生物攻击（§4.4） | **不需要代码**（没有伤害通道） | — | `nothing attacks a drop…`（1000 tick 后 5 HP、count 不变） |
| 不与其它实体碰撞（§4.5） | 只查方块 | — | 结构上成立（实体层不参与彼此的碰撞） |
| 生成位置/弹出（§4.5） | `spawn_item_drop()` | 格子中心、按半高上移；向上初速 0.2（⚠ 无来源） | `spawn_item_drop puts the drop in the cell…` |

**新增常驻日志（卡面允许的唯一一条）**：`WorldSim::apply_dig()` 成功后一行
`item drop <item> spawned at (x, y, z) [entity N]`。除此之外本卡**没有**新增任何常驻日志
（拾取路径刻意无日志，见 §6 取证手法）。

---

## 4. 接口契约（冻结项逐条核对）

| 冻结项 | 结果 |
|---|---|
| `engine/physics/**` 零 diff | ✅ `git status engine/physics` 空；实体层只**复用** `physics::IBlockSource`（`solid_at`/`liquid_at`/`slipperiness_at`/`shape_top_at`） |
| `IAuthority` 五个既有动词语义不变 | ✅ 未改签名与语义；`PickUp` 是 `submit()` 的**新增 kind**，不是第六个动词 |
| 既有 324 个测试行为不变 | ✅ 324 → 359，**无一条既有断言被改**（`git diff` 里 `tests/` 只新增文件 + CMakeLists 追加） |

### 4.1 新增/变更的 API

```cpp
// game/protocol.hpp
enum class ActionKind  { …, PickUp };                  // target.x = 实体 id；item_or_block = 客户端读到的 item id
enum class ActionReject{ …, UnknownEntity, EntityItemMismatch, PickupDelayActive,
                            OutOfPickupRange, EntityNotLoaded };   // 追加，旧值不变

// game/pickup.hpp（两端共用，与 kReachDistance / check_placement 同一范式）
bool pickup_box_contains(const ActorPose &, const glm::dvec3 &box_min, const glm::dvec3 &box_max);

// game/entity_type.hpp
class EntityTypeRegistry { register_type / find_id / id_of / string_of / def_of / kEmptyId };
struct EntityDef { half_width, height, gravity, vertical_drag, horizontal_drag, max_health, carries_item_stack };

// server/entity_store.hpp
using EntityId = std::uint32_t;
class EntityStore { spawn / erase / find / for_each_entity / live_ids / alive_count / clear };
struct Entity { id, type, position, velocity, on_ground, age, pickup_delay, merge_timer,
                last_block, health, stack, alive };

// server/item_sim.hpp
struct IItemWorld : physics::IBlockSource { block_at / chunk_loaded };
void step_items(EntityStore&, const IItemWorld&, const ItemBlockHazard&, const ItemRules&,
                const EntityTypeRegistry&, const ItemRegistry&);
EntityId spawn_item_drop(…);  std::size_t destroy_items_in_radius(…);

// server/world_sim.hpp（只读访问器，与 registry() 同形）
const game::ItemRegistry       &items()        const;   // ★ 物品注册表归属变更
const game::EntityTypeRegistry &entity_types() const;
const EntityStore              &entities()     const;
const ItemRules                &item_rules()   const;

// game/item_registry.hpp
std::optional<std::uint16_t> item_for_block(std::uint16_t block) const;
```

### 4.2 ★ 一处需要 PM 认可的归属变更：物品注册表搬到权威侧

**动了什么**：`main.cpp` 里客户端不再 `ItemRegistry::create_default()`，
改用 `world.items()`（权威侧构造的那一个）。这是本卡对既有结构的唯一实质改动。

**为什么**：掉落物的物品 id 由**世界规则**决定（挖掉什么方块掉什么物品），
权威侧必须持有该映射；如果客户端再自建一份，两者只是**碰巧**因为 `create_default()` 的表顺序一致而相等
（T-D27 已记过 `ItemRegistry` 运行时依赖 `BlockRegistry` 的耦合）。一份注册表 = 一个 id 空间。

**影响面**：`main.cpp` 4 行；`WorldSim` 构造多一次 `create_default()`；
测试里 `sim.items()` 与客户端看到的是同一个对象。客户端所有既有用法（HUD、起始包、容器）不变。

### 4.3 边界怎么划的（沿用 T-A1 范式）

```
        客户端（进程内）                                    权威侧 server::WorldSim
   ┌───────────────────────────────┐        ┌──────────────────────────────────────────┐
   │ 渲染：for_each_entity(const)  │◀──只读──│  EntityStore entities_（第二系统面）      │
   │ 拾取循环：pickup_box_contains │  视图   │  step_items()：运动/合并/销毁/超时        │
   │ 库存（仍是客户端的）           │        │  apply_dig() → spawn_item_drop()          │
   │  ── 干跑库存副本 ──            │        │  ── private ── EntityWorld（IItemWorld）  │
   │ submit(PickUp{id, item}) ─────┼──请求─▶│  apply_pickup()：实体/物品/区块/延迟/几何  │
   └───────────────────────────────┘        └──────────────────────────────────────────┘
```

**为什么 `PickUp` 是请求而不是回推**：回推（`WorldChanges`）只能表达「权威侧改变了什么」，
而「装不下」这件事只有库存拥有者知道。做成请求后，客户端**先干跑库存副本**、
权威侧**再复核世界侧条件**，两边各管一半，且「装不下就留在地上」不需要回滚
（库存副本在 accepted 之后才提交，沿用 T-A1「扣物品改在 accepted 之后」的规则）。

---

## 5. 测试

```
基线（干净检出，2026-09-17）：324 例 / 9 863 断言
本卡：                        359 例 / 10 198 断言   （+35 例 / +335 断言）
```

新增 35 例分布：实体层 9（类型注册表 2 + 存储遍历 2 + 运动参数 5… 见 §1.1 表）、
掉落物规则 25、权威侧集成 6。**关键断言清单**（卡面验收 2/4/5/6/7 要求的都在）：

- 落地/执行序：`A→P→D` 首 tick 位移 = 0.04（P→A→D 为 0）、
  与参考积分器「每 tick 多一个重力步」逐条一致、玩家参数下相差 >1 格；
- 终端速度：500…1000 tick 后 1.96 格/tick（由参数推导，不存第三个常量）；
- 着地/墙：restitution 0 停在面上、200 tick 不沉不漂、5 格/tick 撞墙被 substep 拦住；
- 拾取几何：水平边界含（≤1）、垂直边界不含（<0.5）、潜行盒变小；
- 合并：大堆保留、计时器取剩余更长、超距/异类/不可堆叠/超上限都不合并、跨方块 2 tick 提速；
- 超时：5999 仍在、卸载 100 tick 计时不动、重载下一 tick 消失；
- 环境：火/岩浆/仙人掌销毁、岩浆块（伤害性方块）不销毁、爆炸只销毁半径内。

构建/运行：

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-T-E1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # 不设 FETCHCONTENT_BASE_DIR（约 55 s，需联网）
cmake --build build -j8                              # 无管道；判成败看 exit code / error:
./build/tests/opencraft_tests                        # 359/359
cd build && ./opencraft                              # 实机；saves/ 落在 build/ 下
```

clang-format（CLT 17，`/Library/Developer/CommandLineTools/usr/bin/clang-format`）：
本卡触碰的全部文件 `--dry-run --Werror` **零 diff**。
仓库内**剩余的违规**只在 `docs/qa/**` 的**历史证据工具**里（`td13-2026-09-15/client_sim.cpp` 等 4 个文件，
前几张卡的归档探针，不是本卡白名单内文件，也未改动）。

---

## 6. 实机验收（验收 12）—— 已完成

**结论**：`挖方块 → 掉落物出现（画面可见的 0.25 立方）→ 掉落物消失 → 快捷栏计数 32 → 33`，
四步在实机上逐帧可见 + 日志可证。窗口 `2466 @ (320,89) 1280×748`（按 PID 反查 + 尺寸过滤），
单实例，取证后已 kill 自己的实例。全部证据与复现工具见 `docs/qa/T-E1-2026-09-17/`。

### 6.1 证据 A：画面（正式二进制 md5 `2deca4791afd8d1436bc7b4e6773c995`）

| 文件 | 内容 |
|---|---|
| `A1_pristine_hotbar32.png` / `A1b_hotbar_zoom_32.png` | 挖之前：快捷栏第 2 格 `sod_loam` 计数 **32** |
| `A2_aim_straight_down.png` | 视角压到客户端自己的 −89° 钳位（准星正对脚下的方块，选中线框可见） |
| `A3_digging_crack.png` | 挖掘中（裂纹叠加层 + 破块粒子） |
| `A4/A5/A6_drop_visible_*.png` + `A5b_drop_magnified.png` | **掉落物可见**：坑里一个 0.25 立方、草皮纹理、姿态倾斜的小方块 |
| `A7_after_walk.png` | 按 W 之后：掉落物已消失 |
| `A8_settled_hotbar33.png` / `A8b_hotbar_zoom_33.png` | **快捷栏第 2 格 `sod_loam` 计数 33** |

判据的决定性：起始快捷栏里 `sod_loam` 只有 32 个，除了"拾取刚挖出的那个掉落物"之外
没有任何来源能让它变成 33；而掉落物在 A4–A6 可见、在 A7 消失，期间没有其它世界改动。
诚实边界：A4/A5/A6 之间整帧差异很大（**主因是玩家挖掉脚下那块后落进坑里、相机位移**），
所以这三帧**不用于**证明自转/浮动的动画速率；动画按 `research/11 §4.5` 实现，速率未独立实测。

### 6.2 证据 B/C：两条日志

- **常驻日志**（本卡新增的唯一一条，正式二进制）：`session_final_PRISTINE_binary.log`
  `item drop sod_loam spawned at (0.50, 131.38, 0.50) [entity 1]`
  （该文件 `grep -c PROBE` = 0，`strings build/opencraft | grep -c PROBE` = 0）
- **拾取决策的临时打点**（打点版二进制，已还原 ⇒ 同一 md5）：`session_transient_probe.log`
  `PROBE pickup: 1 candidate(s) in box, first=(0.50, 131.13, 0.50)` →
  `PROBE pickup: took item sod_loam x1 (entity 1)` → `PROBE render: drops=0`
  三行分别证明：客户端的拾取几何把掉落物判为**在拾取盒内** → 请求被接受、库存副本提交成功 →
  权威侧存储里该实体已消失。打点位置与还原双证见 `transient_probe.patch.txt`。

### 6.3 取证过程本身的三条发现（都写进证据目录，供后续卡复用）

1. **macOS 14+ 的 `NSApplicationActivateIgnoringOtherApps` 是空操作**：非 bundle 可执行文件的窗口
   无法用旧 `activate` 切到前台，注入事件会落到别人窗口上。本卡给注入器加了
   `front`（AX `kAXFrontmostAttribute` + `kAXRaiseAction`）与 `trust`（`AXIsProcessTrusted()`）两个子命令。
   ⇒ 建议入 `docs/05` 的取证手法节。
2. **视角注入必须分小步下发**：单次 180 px 与「40+50+30×n px」结果不同（前者有时一次打满钳位）。
   本卡最终做法是**一路压到客户端自己的 −89° 钳位**，把"瞄准脚下那块"变成确定性动作。
3. **出生点前方 2–4 格的地表之下是空洞（竖井）**：掉落物会掉进竖井（实测落到 y=127、y=126），
   既看不见也拾不到 —— 这是场景选择问题，不是缺陷；但它解释了为什么"挖前方地面"这条
   直觉上更好的取证路线反而不可用（唯一保证下方实心的列是玩家自己站的那一列）。
## 7. 主动纠正与待校准申报

### 7.1 ★ 卡面与 `STATE.md` T-D36 ③ 数值冲突（**卡面正确，债务行写错**）

- `STATE.md` 债务表 T-D36 ③ 写：「掉落物着地摩擦（先按 **0.91 × friction**）」；
- 卡面 §待校准 2 与验收 8 写：「先按 **0.98 × friction** 实现并标注待校准」。

0.91 是**玩家**的 `horizontal_drag`（`PhysicsConfig` 默认值），掉落物的是 0.98
（`research/11 §4.1` 表格）。**我按卡面实现 0.98 × S**（`def.horizontal_drag × slipperiness`），
并在代码注释里写明「待校准，来源见 T-D36」。建议 PM 把 `STATE.md` T-D36 ③ 的 0.91 改成 0.98。

### 7.2 ★ 着地摩擦修正会**吃掉**「掉落物滑得远」的手感（新发现，值得连同 §7.1 一起校准）

`research/11 §4.1` 说「水平阻尼 0.98 而非 0.91 —— 这就是掉落物能在地上滑很远的原因」。
但在**普通方块上（S=0.6）**，加上 T-D36 项 2 的着地修正后：

| | 地面保留率 k = drag × S | 相同初速的滑行距离 ∝ 1/(1−k) |
|---|---|---|
| 玩家 | 0.91 × 0.6 = 0.546 | 2.20 |
| 掉落物（本卡） | **0.98 × 0.6 = 0.588** | **2.43** |
| 掉落物（不做着地修正） | 0.98 | 50 |

也就是说：**着地修正一开，掉落物在普通方块上只比玩家多滑约 10%**，
`§4.1` 描述的「滑很远」只在**空中/冰面**成立。若实机手感要求「物品在地上明显滑出去」，
那么要么（a）掉落物**不做**着地摩擦修正（S≡1，k=0.98），要么（b）确认修正存在但普通方块上表现如此。
两条都是原始来源（wiki）没写的，属 T-D36 项 2 的校准范围。单测把 0.588 这个值钉住了
（`ground retention is horizontal_drag x slipperiness`），改口径时会立刻失败。

### 7.3 合并盒 0.5×0.25×0.5 的读法（申报）

`research/11 §4.3` / `research/01 §7` 说的是「另一个在 **0.5×0.25×0.5 的包围盒**内」。
我按**字面读法**实现：以自己碰撞盒中心为心的一个 0.5×0.25×0.5 体积
⇒ 两个 0.25 立方体的**中心水平距离 < 0.375 格**才合并（垂直 < 0.25）。
另一种读法是「把碰撞盒向外扩 0.5 格」（有效体积 1.25×0.25×1.25，中心距离 < 0.75）。
来源只给了盒子尺寸、没给「相对谁」，故按字面实现并在此申报。若要放宽到原版手感，
改 `ItemRules::merge_box_*` 一个数即可（单测断言的是盒子语义，不是魔数）。

### 7.4 T-D36 三项的落地位置（卡面要求「写进代码注释」）

| 项 | 代码位置 | 取值 |
|---|---|---|
| ① 弹性系数无来源 | `ItemRules::restitution` 注释 + `move_axis_*` 的碰撞响应 | **0**（接触即停，不弹跳） |
| ② 着地摩擦修正 | `ItemRules` 的 `★ T-D36 待校准项 2` 段 + `step_item_motion` 的 D 段 | **0.98 × S** |
| ③（本卡不涉及逃跑速度） | — | — |

**我另外申报两项来源同样缺失、我自己选的数值**（不在卡面 T-D36 清单里，故在此标明"待校准"）：

- **弹出初速 0.2（`ItemRules::spawn_velocity_y`）**：`research/11` 没有这个数。
  取的是「看得见的一跳」的量级；水平方向的随机抖动**故意不做**（会在权威侧生成路径里引入 RNG，
  且本卡没有任何需要）。标"待校准"。
- **动量阈值 0.003（`ItemRules::momentum_threshold`）**：沿用玩家的截断阈值
  （`research/06 §1.1`）。**掉落物是否同样截断，来源未写**；加它是因为不加的话掉落物会以
  无限小的速度永远滑下去。标"待校准"。

### 7.5 两处 repo 文档互相矛盾（已在实现里选边并记录）

- **合并后计时器**：`research/01 §7` 写「大堆保留原剩余时间」，`research/11 §4.3` 写
  「计时器取剩余时间更长的那个」。**卡面表格采 §4.3**，我实现 `age = min(...)`（剩余更长者胜）。
- **实体架构**：`docs/03 §6` 写 EnTT ECS，卡面明确否决（先简单结构，M3 再说）。已按卡面做，未改规格。

---

### 7.6 ★ 实机取证抓到的一个真缺陷（渲染，单测覆盖不到）——已修

**症状**：掉落物在权威侧存在、物理正确、能拾取（计数 32→33 都对了），但**画面里根本看不见**。
判据是逐帧像素比对：相机静止时相邻两帧**逐像素完全相同**（`diff bbox = None`）——
说明没有任何东西在动，即掉落物没被画出来。

**根因**：掉落物的模型矩阵 `T(centre)·R·T(-0.5)`。着色器先做 `a_pos * u_scale`
（`u_scale` = 边长 0.25），所以需要居中的是**缩放之后**的立方体，平移量应为 `-0.5 × 边长 = -0.125`；
`-0.5` 把立方体压到地面以下 0.375 格 ⇒ 看不见。

**为什么只有实机能发现**：渲染路径没有任何单测（`tests/` 里没有 GL 上下文），
而单测覆盖的"存在/物理/拾取"全都正常 —— 这正是验收 12 要求实机证据的价值所在。

**修复**：`game/client/src/main.cpp` 改为 `T(-0.5f * side)`，并在注释里写明"缩放发生在 u_mvp 之前"，
防止后来者再按未缩放的立方体去算。修复后同一场景可见 0.25 立方（证据 A4–A6 + A5b 放大图）；
单测不变（359/359）、`clang-format` 零 diff、二进制 md5 见 §6.1。

**建议入 `docs/05` 的教训**：凡新增"世界里多了一个可见物体"的卡，验收必须包含
**相机静止时的两帧逐像素比对**（有动画就该不同；没有动画也应在画面里找得到它）——
"日志说它存在"不等于"玩家看得见"。

---

## 8. 已知问题与缺口（诚实清单）

1. **掉落物不落盘（本卡最大缺口）**：实体只活在内存里，区块卸载时**冻结但保留**，
   重启即全部消失。`research/11 §4.4` 的超时规则因此在本卡是「运行期语义」。
   ⇒ 后续卡（存档/实体持久化）要补：实体的存储格式 + 随区块（或独立表）写盘。
   ⚠ 现有语义下会出现「挖掉的方块丢了、它的掉落物还在」的短暂不一致（区块卸载不写盘时最明显）。
2. **水中行为未实现**：`research/01 §7` 提到掉落物「水中浮起、被水流/活塞推动」。
   本卡只做 §4.1–4.4（卡面数值清单的范围），所以掉落物在水里按空气处理（**会沉底**，无浮力、无水流推力）。
   `block_id_destroys_items` 不含水（水不销毁掉落物）✓ 但浮力是缺口。
3. **卡在方块内不会弹出**：`research/11 §4.5` 的「从无遮挡侧/顶部飞出」未实现
   （挖方块生成的掉落物不卡方块，只有往掉落物上放方块才会遇到）。
4. **爆炸只有 API 没有调用方**：`destroy_items_in_radius()` 已实现 + 单测，
   但没有任何爆炸内容（无 TNT/苦力怕），所以实机不可达。
5. **免疫规则无对象**：下界合金类免疫火、下界之星免疫爆炸 —— 本项目的物品集里没有对应物品（原创命名体系），故未建模。
6. **投掷 40 tick 只留字段**（卡面明确不做）：`ItemRules::pickup_delay_thrown` 无调用方。
7. **合并的「部分填充」按卡面简化**：MC 会把能装的部分装进去、剩下的留在地上；
   卡面写的是「装不下就留在地上」，我实现为**整堆装不下就这一 tick 不拾取**（不拆堆）。
8. **`IAuthority` 的五个动词没动**，但 `PickUp` 的语义（target.x 借位放实体 id）是"借字段"，
   已在 `protocol.hpp` 写清。M3 上网络时建议给 `ActionRequest` 加一个显式的 `entity` 字段
   （现在借 `target.x` 是为了不动既有字段的语义）。
9. **物理层碰撞代码重复**：`engine/physics` 本卡冻结，其 `box_collides` / `highest_surface_below` /
   `move_axis_*` 都是 .cpp 内 file-private，无法复用 → 在 `item_sim.hpp` 里**重写了一份同形实现**
   （同样的 `shape_top_at` 契约、同样的 Y→X→Z + substep 顺序，约 120 行）。
   这是本卡唯一一处「双重真相源」风险：两份实现靠**契约**（`IBlockSource`）和**注释交叉引用**保持一致，
   但语义漂移是可能的。⇒ 建议后续开物理层的卡把它抽成公开的 `sweep_move(box, world, …)`，
   让玩家/掉落物/生物/载具共用一份（已列入 §9 建议）。
10. **两个 `chunk_ready` 语义面**：`IItemWorld::chunk_loaded` 由 `WorldSim::chunk_ready` 实现，
    而 `solid_at` 对未加载区块返回 true（玩家的"看不到虚空"规则）。掉落物因为「未加载就整体跳过」
    而永远问不到这一条，但读代码时容易误判 ⇒ 已在 `EntityWorld` 注释写明。

---

## 9. 给 PM 的建议表（不动任何状态/规格文件，仅建议）

| # | 建议 | 依据 | 优先级 |
|---|---|---|---|
| 1 | `STATE.md` 债务表 T-D36 ③ 的 `0.91 × friction` 改为 `0.98 × friction`（0.91 是玩家的数） | §7.1（卡面正确、债务行与卡面冲突） | 中（实现已按 0.98） |
| 2 | T-D36 补一条：**掉落物着地摩擦修正与"滑得远"的观感冲突**，需要在实机上定口径（做/不做修正） | §7.2（新发现：开修正后普通方块上只比玩家多滑 10%） | 中（手感，M2c） |
| 3 | T-D36 补一条：**合并盒 0.5×0.25×0.5 的相对基准未定**（字面体积 vs 外扩 0.5） | §7.3 | 低 |
| 4 | 若希望拾取也有常驻日志（本卡的拾取证据只能靠 HUD 像素对比），请授权**第二条**常驻日志；我未擅自加 | §6、卡面"唯一允许的日志新增" | 低（请裁决） |
| 5 | 登记债务：**实体不落盘**（区块卸载不释放实体、重启全失）——需要在存档卡里做实体持久化 | §8 缺口 1 | 中（M2c/M3） |
| 6 | 登记债务：**掉落物无水中浮力/水流推力** | §8 缺口 2 | 低（M2c 流体卡） |
| 7 | 登记债务：**碰撞代码重复**（`item_sim.hpp` 与 `engine/physics` 各一份）——建议下次开物理层时抽公共 `sweep_move` | §8 缺口 9 | 中（M2c 生物卡前，否则生物会再造第三份） |
| 8 | 建议 M2c 生物卡**直接复用本卡实体层**：`EntityStore` 加字段（AI 目标栈/感知）+ `register_type("mob")`，不要再造一套存储 | §2（存储结构选择） | — |
| 9 | `ActionRequest` 建议加显式 `entity` 字段（本卡用 `target.x` 借位） | §8 缺口 8 | 低（M3 网络化时） |

---

## 10. 结论

- 实体层从无到有，**是后续生物/战斗/投射物/载具可以直接复用的最小正确地基**（存储/遍历/类型参数/碰撞接缝/权威侧归属）；
- 掉落物的物理与规则逐条对齐 `research/11 §4.1–4.4`，含执行序 A→P→D 这种"错了落点就偏"的细节；
- 三处 T-D36 待校准项写在代码注释里，另申报两处我自己选的数值（§7.4）；
- **实机验收 12 已完成**：挖 → 掉落物可见 → 消失 → 计数 32→33，证据逐帧归档（§6）；
- 实机取证**抓到一个单测覆盖不到的渲染缺陷**并修复（§7.6）——这一条是本卡最有价值的副产品；
- 待 PM 裁决/登记：§9 九条建议（其中 T-D36 ③ 的 0.91/0.98 数值冲突、着地摩擦与"滑得远"的观感冲突请优先处理）。
