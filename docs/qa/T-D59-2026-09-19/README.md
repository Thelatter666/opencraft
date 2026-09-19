# T-D59 on-machine evidence · 攻击充能 / 工具伤害 / 暴击 / 疾跑击退

工作根：`/Users/happy/Desktop/opencraft_worktree/opencraft-td59`
交付提交：`6057091`（分支 `task/T-D59-attack-charge`）
交付二进制：`build/opencraft` md5 `b448e45f6dc1ecdfb18a0f79e10f6e23`（干净重建，无打点）

---

## 0. 这一轮在测什么

`docs/tasks/T-D59.md` §5.2 要四件实机证据：攻击条、快挥/满充能的伤害差、暴击、
疾跑击退。判据一律取**机器可读的行**，不取目测：

| 要证的事 | 判据 | 落在哪 |
|---|---|---|
| 斜坡公式生效 | 客户端 `attacked mob … charge X`（UI 镜像）＋ 权威侧 `EVIDENCE swing … charge …`（结算值） | `attack_session.log` |
| 快挥低伤 | 两组 charge 序列（0.355/0.416 vs 1.000）＋ 对应的 `damage 1.419520 / 1.665280 / 4.000000` | 同上 |
| 暴击 | `crit true falling true damage 6.000000`（= 4 × 1.5） | 同上 |
| 疾跑击退 | `sprinting true` 的那一击 ⇒ `knockback 0.9 (sprint gate true)` | 同上 |
| 攻击条 | 逐像素读完的热栏上方 4 px 条：填充宽度 = 充能比例、门槛刻度在 x=720–721 | `0[234]_bar_*.png` |

## 1. 装置与信任边界

### 1.1 临时打点（`tools/td59_evidence_hook.patch`，205 行，**未随交付提交**）

两处插入点，都是"**只报告、不决定**"：

- `game/client/src/main.cpp`（帧循环 `if (!paused)` 内、定步长循环之后）：
  - 启动时把 `timber_edge` 放进快捷栏 8 并选中（**出货客户端没有任何办法把物品
    放进快捷栏 / 装备槽**，与 T-D46 的 F9 同类）；
  - `F7`/`F8` 在视线前方 2 格召唤一只 Hollow Wretch（**敌对地表永不刷出**：光照 0
    才刷，本世界无昼夜循环——沿用 T-M2/T-D46 的同一钩子形状）；
  - `F6` 场景复位（调死亡界面自己那条 `client::respawn_player`）；
  - `F10` 回血，外加 `health < 6` 时自动回血；
  - 逐帧记录**被观察生物**的血量与位置/速度；记录每一次鼠标左键沿、按键沿、
    以及一个独立的**瞄准探针**（自己按 tick.cpp 同一套 `ray_box_entry` 重算"视线
    最近的可击生物"）。
- `game/server/sim/src/world_sim.cpp`（`apply_attack` 内两行日志）：记录权威侧
  结算出的 `charge / charged / crit / falling / sprinting / damage` 与
  `knockback` 取值。

### 1.2 打点做了什么**不**被信任

- 场景稳定化（回血、场景复位、发一把剑）**不参与任何被测数值**：伤害、充能、
  击退全部由出货代码算出，打点只把它们打印出来。
- 打点**不写** `ActorPose::sprinting`/`falling`：这两个标志由 `tick.cpp` 的
  `actor_pose()` 从 `PlayerState` 抄，与交付版逐字节同。
- 复核者可用 `tools/attack_scene.py` + `td59input.m` 重跑（见 §4）。

### 1.3 交付版无打点（本项目「打点→还原」规则）

```
$ grep -c EVIDENCE game/client/src/main.cpp game/server/sim/src/world_sim.cpp
game/client/src/main.cpp:0
game/server/sim/src/world_sim.cpp:0
$ strings build/opencraft | grep -c EVIDENCE
0
$ git diff --name-only HEAD -- game/      # 与交付提交相比
（空）
$ md5 -q build/opencraft
b448e45f6dc1ecdfb18a0f79e10f6e23
```

---

## 2. 攻击条：逐像素读数（PM 可复核）

布局（`hud.cpp` 的常量）：热栏宽 232 px、起点 `bar_x0 = fb_width/2 − 116 = 524`，
条高 4 px，位于 `bar_y0 − 6 … bar_y0 − 2`。窗口 1280×748 = 720 客户区 + 28 标题栏，
所以条落在**窗口图像第 708–711 行**，门槛刻度在 `bar_x0 + 232 × 0.848 = 720.7`
⇒ **x = 720–721**，上下各露 1 px（707…712）。

实测（三张图同一行 y=709 的颜色分段）：

| 图 | 填充 | 含义 |
|---|---|---|
| `03_bar_cold_just_after_a_swing.png` | x 524..620 = **97 px**，rgb (87,56,2) 琥珀 | 97/232 = **0.418**，与同一时刻日志里的 `charge 0.416320` 一致 |
| `04_bar_charged_past_the_gate.png` | x 524..755 全宽，rgb (99,97,24) 亮黄 | 越过门槛 ⇒ 换色 |
| `02_bar_full_never_attacked.png` | 全宽，rgb (99,97,24) | "从未攻击 = 满充能"（C-2）在 UI 上的样子 |

门槛刻度 x=720..721 在**三张图里都可见**：03 里它是暗刻度落在空轨道上
(rgb 9,9,10 vs 轨道 27,23,26)；04 里它压在亮填充上仍是暗刻
(rgb 14,14,11 vs 填充 99,97,24)——这就是"分界在满充能时也读得出"的像素证据。

> 截图为**暂停帧**（ESC）：暂停不跑 tick 但照常渲染，所以定格的是点击把它留在的
> 那个值。暂停菜单在中部，条在底部 40 px，无遮挡。

---

## 3. 四段场景的读数

`attack_session.log`，一次会话一个世界。

### 3.1 斜坡：同一把剑，快挥 vs 满充能

客户端 `charge`（UI 镜像，`attack_age` 推出来的）：

```
快挥（150 ms 间隔）: 0.355  0.416  0.416  0.355  0.416 …
按满（≥1 s 间隔）  : 1.000  1.000  1.000  1.000 …
```

权威侧结算（`EVIDENCE swing`）：

```
weapon 33 charge 0.354880 charged false damage 1.419520   ×3  （t=5）
weapon 33 charge 0.416320 charged false damage 1.665280   ×9  （t=6）
weapon 33 charge 0.569920 charged false damage 2.279680   ×1  （t=8）
weapon 33 charge 0.877120 charged true  damage 3.508480   ×2  （t=11，越过门槛后的最小 tick）
weapon 33 charge 1.000000 charged true  damage 4.000000   ×10 （t≥12，全额）
```

（27 击的全量去重表；每次挥击都等于 4.0 × 该 tick 的斜坡值，无例外。）

（weapon 33 = `timber_edge`，4.0 伤害。0.354880 = 剑 T=12.5 的 t=5；0.416320 = t=6；
0.877120 = t=11，**越过 84.8% 门槛的最小整数 tick**，此时 `charged` 才变 true，
而伤害仍不是全额——全额要到 t=12。这正是卡面 §3 说的"C-1 让斜坡不再是死代码"。）

`06_after_paced_clicking.png` 与 `05_after_fast_clicking.png` 是这两段结束时的画面。

### 3.2 暴击

```
weapon 33 charge 1.000000 charged true crit true falling true sprinting false damage 6.000000
```

木剑 4.0 × 1.5 = **6.0**，与卡面 §5.2 要求逐字一致。截图 `07_crit_jump_attack.png`。

实机上的对照只有一半：**`charged true` 而 `falling false` 的 12 击全部 `crit false`**
（0.877120 那两击与 1.000000 那十击），说明"下落"这个条件真的在起作用。另一半
（`falling true` 而未达门槛）本轮 27 击里没撞上——8 次尝试中只有一次下落段被拍到，
而那次的充能恰好是满的。这一格由单测钉死（`tests/test_attack_charge.cpp` 的
"a crit needs BOTH the fall and the charge" 四格）。

### 3.3 疾跑击退

```
weapon 33 charge 1.000000 charged true crit false falling false sprinting true damage 4.000000
EVIDENCE knockback 0.9 (sprint gate true)
```

同一份日志里其余 25 击全部是 `knockback 0.4 (sprint gate false)`——**非疾跑路径没有
任何一击变过**。（27 击里有 1 击没有 knockback 行：那一击把生物打死了，死在物化
之前不需要冲量，这是 `apply_attack` 的既有分支。）截图 `08_after_sprint_hit.png`。

> 疾跑用**按键路径**（`tick.cpp` 的 `in.sprint` = LEFT_CONTROL，与 W 同时按住即
> 直接进入疾跑），不走双击 W 的边沿路径：后者要求 HID 真的发出"抬起沿"，本机
> 实测只有一次成功。判定不靠猜——先等客户端自己的 `sprint start` 行出现，再看
> 权威侧那一击的 `sprinting` 是不是 true。

---

## 4. 复跑方法

```bash
# 1. 注入工具（HID 层；docs/05 §3.1：禁用 osascript）
cd <worktree>/docs/qa/T-D59-2026-09-19/tools
clang -framework Foundation -framework CoreGraphics -framework AppKit -o /tmp/td59input td59input.m

# 2. 把临时打点贴回交付源码（唯一需要手工的一步：patch 的上下文行号按 6057091）
cd <worktree> && git apply docs/qa/T-D59-2026-09-19/tools/td59_evidence_hook.patch
cmake --build build -j8 --target opencraft        # 构建禁接管道

# 3. 跑场景（自带单实例检查与收尾 kill）
bash docs/qa/T-D59-2026-09-19/tools/run_attack_scene.sh <worktree>/build \
     <worktree>/docs/qa/T-D59-2026-09-19

# 4. 还原打点
git checkout game/client/src/main.cpp game/server/sim/src/world_sim.cpp
cmake --build build -j8        # 或 cmake --build . -j8 视工作目录
strings build/opencraft | grep -c EVIDENCE        # 必须 0
```

### 4.1 这台机器上的已知注入坑（本轮新踩到的，供后续卡复用）

1. **F9 / F11 不要用**：F9 在早期一轮里没有送达；F11 是 macOS 系统键（本轮
   场景复位一开始挂在 F11 上，整段"复位"从未发生，表现为玩家在几十格外、点击
   全部 `picked 0`）。可用：F6/F7/F8/F10。
2. **一次进程只发一个键 ⇒ 合成修饰键会被丢**：`hold <code>…` 子命令一次进程按下
   多个键（本轮 Ctrl+W 用两条命令发时 `keys w=0 ctrl=1`，W 从未落地）。
3. **`activate` 与"按住键"互斥**：每次 `key`/`click` 前的 front/activate 会让 GLFW
   清掉按键状态（docs/05 §3.1 规则 2）。需要按住时要 `clickraw`（不带激活前导），
   本轮就是靠它拿到 `sprinting true` 那一击。
4. **生物会走过玩家**：Hollow Wretch 直线走过来并在动量下越过玩家约半格，之后
   就在背后 ⇒ 点击全部落空。**每次挥击前重新召唤**（2 格正前方）比"等它到"可靠。
5. **死亡界面是模态的**：玩家一旦死，后续所有点击都被"你死了"吃掉，表现为
   「点击不生效」而不是「攻击坏了」。本轮第一个完整 run 就是这样丢掉暴击段的。
   夹具必须回血 + 复位。
6. **`~180°` 转身不要照抄**：T-D46 的 `move 1257` 在本轮实测是 **360°**（yaw 从
   0 到 −6.285 rad），转过去反而把生物甩到背后。本场景改用"召唤在正前方"。

## 5. 未取证 / 未做

- **与真实鼠标键盘的交互**（真人验收的手感）不在本轮范围内：点击制下"按住不连击"
  是裁决 C-1 的预期，需真人确认（卡面 §7.1）。
- **斧/镐/锹的实机读数**：本轮实机只用 `timber_edge`（快捷栏里只有它和镐；镐在
  格 7 但场景未切换）。四件工具的伤害走单测（`tests/test_attack_charge.cpp`）。
- **水下/骑乘等暴击禁止条件**：对应系统不存在，见契约 ④ 的边界说明。
