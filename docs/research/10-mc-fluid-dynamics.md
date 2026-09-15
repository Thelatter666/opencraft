# 10 · MC 流体动力学调研白皮书（有界局部元胞自动机 + 加权图 BFS）

> 成文日期：2026-09-16　状态：**调研文档（非实现规格）**
> 对齐基准：**MC JE 1.21.x / 26.x**（与 `docs/01` 首行同口径）
> 定位：为 OpenCraft 流体系统提供**唯一规格依据**。直接消费者是「水桶 + 放置后呈现同款流体效果」
> 实现卡；本卡只做调研，不写代码、不改规格。
> 合规声明：本文档**不含任何反编译源码片段**，不含 MCP / Yarn / Forge 反混淆产物，
> 不含 Minecraft Wiki 的逐字段落。所有机制描述均为本项目语言的原创重写；保留的只有
> 数学公式、数值常量、结构性描述与来源 URL。符合 `docs/04-legal-compliance.md` 红线 1 与 4。
> 落地规则：本文档**不直接改 `docs/01` / `docs/03`**。写入规格的改动须经 PM 裁决并落卡；
> 本文档 §9 以**建议表**形式登记，供 PM 落盘。

---

## 目录

1. [读法说明与结论速览](#0-读法说明与结论速览)
2. [数据表示：流体状态与方块状态](#1-数据表示流体状态与方块状态)
3. [调度模型：计划刻](#2-调度模型计划刻)
4. [元胞自动机转移规则](#3-元胞自动机转移规则)
5. [坡度寻路（核心）](#4-坡度寻路核心)
6. [源生成判定](#5-源生成判定)
7. [实体交互：流场与浸没动力学](#6-实体交互流场与浸没动力学)
8. [边界情形](#7-边界情形)
9. [对 OpenCraft 的落地建议](#8-对-opencraft-的落地建议)
10. [来源核验与建议表](#9-来源核验与建议表)

---

## 0. 读法说明与结论速览

### 0.1 一句话模型

流体不是连续介质求解，而是**跑在体素网格上的有界局部元胞自动机**：每个流体格有一个离散
「能级」，每到一个**计划刻**就按局部邻域重算一次自身能级并向外广播更新；水平扩散时额外跑一次
**短半径 BFS** 找「落差口」，把流量优先导向最近的那个落差。

### 0.2 结论速览（后续卡可直接抄的表）

| 量 | 水（全维度） | 岩浆（主世界/末地） | 岩浆（下界） |
|---|---|---|---|
| 计划刻间隔 | 5 tick（250 ms） | 30 tick（1500 ms） | 10 tick（500 ms） |
| 水平扩散半径 | 7 格 | 3 格 | 7 格 |
| 每格能级衰减 ΔL | 1 | 2 | 1 |
| 坡度搜索半径 | 5 格 | 3 格 | 5 格 |
| 源再生闸门 | 默认开 | 默认关 | 默认关 |

> 坡度搜索半径的 5/3 与水平扩散半径的 7/3 是**两个不同参数**，不要混用。见 §4.2。

### 0.3 本文的「能级」记号

wiki 用「emptiness（空度）」记 level：源 = 0，越远越大。另一套（1.13 起的流体状态）用
「满度」记：源 = 8。两者互为补数，容易出错。**本文统一用「能级」= 满度**，记为
$L \in [0, 8]$：$L=8$ 为源，$L$ 越小越浅，$L=0$ 表示无流体。
换算：若某处引用的是空度 $e$，则 $L = 8 - e$。

（依 `Fluid#Level`：源 level 为 0；每流一格 +1（下界岩浆同）；主世界/末地岩浆每格 +2，
最大空度 6；上方有同种流体时 JE 直接记为 8。
来源：<https://minecraft.wiki/w/Fluid> ，访问 2026-09-16）

---

## 1. 数据表示：流体状态与方块状态

### 1.1 1.13「扁平化」做了什么，为什么对我们关键

扁平化之前，流体的水位塞在方块 ID 的 4-bit metadata 里，水位和方块类型是**同一个数域**里的
两个字段：一个方块要么是「水」且带一个 0–15 的水位，要么是别的东西。这带来两个结构性限制：

1. **水位与方块类型共享命名空间**：新增一种流体就要在 metadata 里抢位；
   水位语义随方块 ID 走，「这是水且水位 3」和「这是台阶且朝向 3」在存储层无法区分。
2. **无法表达叠加**：一个格子不能同时是「楼梯」和「水」——因为一个格子只有一个方块 ID。

1.13 之后水位被抽出来成为独立的**流体状态**，与方块状态**并行组合**存储：一个体素位置同时持有
一个方块状态和一个流体状态。含水方块（栅栏、楼梯、台阶）因此可以让几何碰撞体与有效流体
在同一坐标叠加，而不必再挤占方块 ID。

结构示意（原创画法，不对应任何具体类名）：

```
   体素位置 (x, y, z)
   ┌───────────────────────────────────┐
   │  方块状态：几何 / 碰撞 / 渲染      │
   │  流体状态：种类 + 能级 + 是否下落  │   ← 两个通道并行，互不挤占
   └───────────────────────────────────┘

   普通流体块：方块状态 = 流体占位块，流体状态 = 实际流体
   含水方块：  方块状态 = 栅栏/楼梯，    流体状态 = 水（叠加）
```

对 OpenCraft 的意义（§8 展开）：我们现在的 `BlockDef.liquid` 布尔是**扁平化之前的形态**——
它把「是不是流体」编码进方块定义，而水位没有地方放。这不是布尔够不够的问题，是**缺少并行通道**
的问题。

（扁平化依据：`Chunk_format` 的历史段与分节 palette/data 结构，
<https://minecraft.wiki/w/Chunk_format> ，访问 2026-09-16；
流体/方块状态并行与含水：<https://minecraft.wiki/w/Waterlogging> ，访问 2026-09-16）

### 1.2 流体状态的三个字段

一次流体更新需要的状态量只有三个：

| 字段 | 取值域 | 语义 |
|---|---|---|
| 种类（kind） | 枚举（水 / 岩浆 …） | 决定 ΔL、tick 间隔、搜索半径 |
| 能级 $L$ | 整数 $[0, 8]$ | 8 = 源或下落柱；$1..7$ = 水平流动，越小越浅；0 = 无 |
| 下落中（falling） | 布尔 | 上方有同种流体且向下畅通 |

「下落中」不是渲染细节：它决定（a）渲染高度取满高还是按 $L$ 折算；（b）是否抑制自身向外的
水平扩散（§3.3）；（c） buoyancy/实体的下沉流判定（§6.1）。

### 1.3 水位与渲染高度

wiki 只说「渲染高度部分取决于 level」，没有给公式。业界常用且与水深表现自洽的折算为：

$$h(P)=
\begin{cases}
0, & L(P)=0\ \text{（无流体）} \\[4pt]
\dfrac{L(P)}{9}, & \text{普通水平流动} \\[8pt]
1, & L(P)=8\ \text{（源或下落柱）}
\end{cases}$$

即分母取 9 而非 8，使得最外圈 $L=1$ 的薄层高度约 0.111 格（而非 0.125），源为满高。

> ⚠ **此折算式 $L/9$ 是推算值，不是 wiki 明文**。wiki 只确认「渲染基于 level，表现为高度与
> 流向」。采纳前建议在实机对一次「平地单源」的截面高度做一次目视/量测校核。

（来源：<https://minecraft.wiki/w/Fluid> ，访问 2026-09-16）

---

## 2. 调度模型：计划刻

### 2.1 计划刻 vs 随机刻

流体**不走随机刻**。随机刻是每 tick 从每个 subchunk 里随机挑若干位置触发，用于作物生长、
叶子腐烂、冰融化这类「不需要精确时刻」的事；JE 下每 subchunk 每 tick 抽 `randomTickSpeed`
（默认 3）个位置，单个方块两次随机刻的中位间隔约 47.30 秒、平均约 68.27 秒
（几何分布，$\log_{(4096-3)/4096}(0.5)\approx946.03$ tick；$4096/3\approx1365.33$ tick）。
把流体挂在随机刻上，流速会变成随机的，完全不可控。

流体走**计划刻（scheduled tick）**：流体格自己向世界调度器登记「N tick 后叫我」，调度器到点
回调。设计动机是流体扩散必须是**确定性、可预测**的——玩家要能数着拍子造水利机械。

（<https://minecraft.wiki/w/Tick> ，访问 2026-09-16）

### 2.2 执行序与优先级

JE 的 scheduled tick 阶段分两类：**方块刻（block tick）** 与 **流体刻（fluid tick）**。

- 方块刻**先**执行，按 priority 升序，同 priority 按登记顺序。中继器面向另一二极管的背面/侧面时
  priority = −3，正在断电时 −2，其余 −1；比较器面向另一二极管的背面/侧面时 −1；其他方块刻
  priority = 0。
- 随后执行流体刻。**流体刻不区分优先级，纯按登记顺序（FIFO）**。

这条顺序关系直接决定了「红石与流体谁先看到对方」，做红石联动的机器时必须对齐。

（<https://minecraft.wiki/w/Tick> ，访问 2026-09-16）

### 2.3 时间参数表

| 流体 | 维度 | 计划刻间隔 | 实延迟 | 折算速度 |
|---|---|---|---|---|
| 水 | 全维度 | 5 tick | 250 ms | 4 格/秒 |
| 岩浆 | 主世界 / 末地 | 30 tick | 1500 ms | 2 格 / 3 秒 |
| 岩浆 | 下界 | 10 tick | 500 ms | 2 格/秒 |

（Water 正文：「Water spreads at a rate of 1 block every 5 game ticks, or 4 blocks per second」
<https://minecraft.wiki/w/Water> ；Lava 正文：「In the Overworld and the End, lava travels
3 blocks …」，「In the Nether, lava travels 7 blocks horizontally and spreads 1 block every
10 game ticks」；Lava 信息框模板 `flowrate = 30 ticks/block (Overworld, End) / 10 ticks/block
(Nether)`，<https://minecraft.wiki/w/Lava> ；Fluid 页 Spread 节速度表三行一致，
<https://minecraft.wiki/w/Fluid> 。均访问 2026-09-16）

### 2.4 与计划的 20 TPS 的关系

水每 5 tick 走一格 = **每 4 秒走 20 格的理论上限**；但受 §3 的能级上限约束，平地上单源最远只到
7 格就停（能级耗尽），所以「5 tick/格」是**前沿推进速度**，不是射程。

### 2.5 队列上限与背压

- JE：**每游戏刻最多 65,536 个计划刻**；BE 是**每区块每刻** 100 个。
  （<https://minecraft.wiki/w/Tick> ，访问 2026-09-16）
- wiki 对 JE 的**溢出后行为没有明文**（只给了上限数字）。因此**不得假设**溢出是丢弃、延迟还是
  报错——这三种行为对红石机器玩家的可观察结果完全不同。OpenCraft 侧应当**自己选一个并写进规格**
  （建议见 §8.4）。
- 大规模破坏蓄水池 / 高频红石切水源时会造成计划刻**重度背压**，表现为流体等级衰减计算被推迟多个
  游戏刻（俗称流体卡顿）。这是排队延迟，不是计算错误。

---

## 3. 元胞自动机转移规则

### 3.1 邻域

一次更新只看 6 个位置：正上方 $P_{up}$、正下方 $P_{down}$、四个水平正交邻居 $\mathcal{N}_4$。
其中「能否流过去」的判定用 `canFlowThrough`（目标是可替换的空气/流体，且不受阻挡）。

```
                 [ P_up  ]        ← 上：同种流体且下通 → 本格变下落柱
                     |
   [P-1,0,0] — [   P    ] — [P+1,0,0]   ← 水平四邻：能级梯度来源
                     |
                 [ P_down ]      ← 下：可渗透 → 优先向下
```

### 3.2 能级转移方程

定义 $\mathcal{N}_4^* \subseteq \mathcal{N}_4$ 为「能有效流向 $P$ 且未被固体面阻隔」的水平邻居集合。
$P$ 的新能级：

$$L_{\text{new}}(P)=
\begin{cases}
8, & \text{若 } P_{up} \text{ 是同种流体且可向下穿过} \\[4pt]
0, & \text{若 } \displaystyle\max_{Q\in\mathcal{N}_4^*}\bigl(L(Q)-\Delta L\bigr)\le 0 \\[10pt]
\displaystyle\max_{Q\in\mathcal{N}_4^*}\bigl(L(Q)-\Delta L\bigr), & \text{其他}
\end{cases}$$

要点：

1. **取 max 而非求和/平均**。这是元胞自动机的关键：流体不做质量守恒，只做「从最强的那个邻居
   继承并衰减」。所以两条细流汇合不会叠加成更深的水，而多条源并排时会取最强的那一路。
2. **$\Delta L$ 是介质常数**：水 = 1，下界岩浆 = 1，主世界/末地岩浆 = 2。
   这直接导致主世界岩浆在平地上只能走出 3 格（$8 \to 6 \to 4 \to 2 \to 0$，再走一格
   $2-2=0$ 停），下界岩浆能走 7 格（$8\to7\to6\to\cdots\to1\to0$）。
3. **「$\le 0$ 则置 0」是射程的物理来源**——能级耗尽即停止，不需要额外的距离计数器。
   「7 格 / 3 格」是上面这个递推的**副产品**，不是独立规则。

ΔL 依据：Fluid 页「Water and lava in the Nether have a level equal to the level of the fluid
that generated them +1 … Lava in the Overworld or the End increases two levels per flowing block
instead」；Water 页 blockstate 段「Along a line on a flat plane, water drops one level per
meter」；Lava 页「lava drops one level per meter in the Nether and two everywhere else. So in
the End and Overworld, only 2, 4 and 6 are used」。
（<https://minecraft.wiki/w/Fluid> / <https://minecraft.wiki/w/Water> /
<https://minecraft.wiki/w/Lava> ，访问 2026-09-16）

### 3.3 下落柱

当 $P_{up}$ 是同种流体且向下畅通：$L(P) := 8$ 且 `falling := true`。
下落柱的水平外向扩散按 $L_{\text{spread}} = 8 - \Delta L$ 计算——即它对外表现得**像能级 $8-\Delta L$
的源**（水与下界岩浆为 7，主世界/末地岩浆为 6），但它本身**不是源**（不可被桶采集，
也不参与 §5 的源再生作为「源」计数）。

这条解释了「瀑布砸在地上为什么会向四周摊开一大片」：瀑布底部是 $L=8$ 的下落柱，
对水而言向外摊出 $8-1 = 7$，再每格 $-1$ 递减，于是地上一片 7 格半径的水。

### 3.4 垂直优先

一次更新的判定顺序是**先下、后水平**：

1. 若 $P_{down}$ 可渗透 → 把下方置为同种流体的下落柱（新空度 0，即 $L=8$），加入扩散表，
   **本轮不做水平扩散**。
2. 若 $P_{down}$ 是固体或不受流体影响的非固体（如梯子）→ 才做水平扩散（含 §4 的坡度寻路）。
3. 若 $P_{down}$ 是**同种流体的源** → 停止流动。
4. 若 $P_{down}$ 是**另一种流体** → 走混合规则（§5.3）。

「先下后水平」是观感的来源：悬崖边的水会先整条掉下去，再在底部摊开；而不是边掉边摊。

（Fluid 页 Spread 节的 7 条有序规则，<https://minecraft.wiki/w/Fluid> ，访问 2026-09-16）

---

## 4. 坡度寻路（核心）

> 这一节是后续实现卡的直接依据，写得最细。

### 4.1 为什么需要它

如果只按 §3.2 的 max 规则做水平扩散，水在平地上会**对称地摊成一个菱形**。但只要附近有落差
（悬崖边、洞、向下的阶梯），真实表现是：水会**收窄成一股**，径直奔向那个落差，形成瀑布。
这是纯观感需求——wiki 明确说了动机是「giving preference to the creation of water or lava
falls, for aesthetic purposes」。

没有这条，倒一桶水在悬崖边会得到「均匀摊开 + 边缘慢慢滴」，而 MC 的表现是「一条 1 格宽的水流
直奔崖边然后落下」。**这是本调研里对观感影响最大的一条。**

（<https://minecraft.wiki/w/Fluid> ，访问 2026-09-16）

### 4.2 参数：搜索半径

| 流体 | 搜索半径（wiki 措辞） |
|---|---|
| 水 / 下界岩浆 | 至多 5 格远 |
| 主世界 / 末地岩浆 | 至多 3 格远 |

wiki 原文（Fluid 页 Flow direction 节）：「The area checked is up to 5 blocks away for water
or lava in the Nether and up to 3 blocks away for lava elsewhere.」
另有一条独立表述（Water 页）：「tries to find a way down that is reachable in **four or fewer
blocks** from the block it wants to flow to」。

> ⚠ **口径差异（必须显式说明，不掩盖）**：同一个概念在 wiki 有两套数——
> Fluid 页正文的「up to 5 / 3 blocks away」，与 Water 页正文的「four or fewer blocks」。
> 两者不等价：以「从待流格出发的步数」计，5 blocks away 与 four or fewer 相差 1。
> 我**无法从公开 wiki 判定哪个是 1.21.x 的当前行为**（可能一个是含起点计数、一个不含，
> 也可能是版本漂移）。
> **建议：实现卡取 4 或 5 都不足以被判错，但必须把它做成常量并在规格里写明取值与依据；
> 若要精确，需要一次实机对照实验（见 §9 建议表 R-6）。** 本文落地建议（§8）取 **5**（含起点）
> 并注明待实机校准。

> 另注：桌面白皮书里写的是「水与下界岩浆 $R_{BFS}=4$、主世界岩浆 $R_{BFS}=2$」，
> 与 wiki 正文的 5/3 各差 1。这是**白皮书的错**，不是 wiki 的错——已在 §9 建议表登记。

（<https://minecraft.wiki/w/Fluid> 、<https://minecraft.wiki/w/Water> ，访问 2026-09-16）

### 4.3 落差口（drop-off hole）的定义

对候选格 $K$：若 $K$ 的**正下方** $K_{down}$ 满足「流体可以流进去」（是空气 / 可被流体替换的
方块 / 同种流体），则 $K$ 是一个**落差口**。

> 精确一点：wiki 的可达性措辞是「checked for air, fluids … or blocks that can be destroyed by
> fluids **one block below** the fluid block」。也就是说检查的是「候选格下方那一格是否可进流体」。
> 「完整实心方块（阻挡流体穿透）」不构成落差口。

注意：**能流过去 ≠ 能到达落差口**。搜索过程中每一步也要满足 `canFlowThrough`，
被固体挡住的方向不能穿过。

### 4.4 算法（可直接照此写 BFS）

对每个**待流格**（source 或 flowing block），对其四个水平方向分别算一个代价：

```
输入：world, 待流格 P, 方向 d, 搜索半径 R
输出：D_d ∈ {1..R} ∪ { INF }   —— 沿方向 d 走，最近落差口的步数

1. 令 N = P.offset(d)
2. 若 !canFlowThrough(P → N)：返回 INF        // 这个方向根本流不出去
3. 若 isHole(N)：返回 1                        // 一步就踩到落差口，代价 1
4. 从此起做 BFS（无权图，四向，不穿固体）：
     queue ← {(N, cost = 1)};  visited ← {N}
     while queue 非空：
         (cur, c) ← queue.pop_front()
         if c >= R: continue                   // 超出搜索半径，剪枝
         for nd in 四个水平方向：
             nxt ← cur.offset(nd)
             if nxt 已访问 or !canFlowThrough(cur → nxt): continue
             标记 nxt 已访问
             if isHole(nxt): 返回 c + 1        // 命中，第一个命中的即最短
             queue.push_back({nxt, c + 1})
5. 返回 INF                                    // 半径内无落差口
```

三个容易写错的点：

- **是 BFS 不是 Dijkstra**。每步代价恒为 1，无权图；用优先队列是把简单问题做复杂，
  而且会掩盖「第一个命中即最短」这个不变量。
- **代价从 1 开始**（$N$ 本身算第 1 步），不是 0。落差口就在隔壁时 $D=1$。
- **BFS 只在水平面内展开**，不上下走。它是二维搜索，落差是**终止条件**不是移动方向。

### 4.5 方向取舍（并列怎么办）

对四个方向算出代价向量：

$$\vec{D} = [D_N,\ D_S,\ D_W,\ D_E],\qquad D_{\min} = \min \vec{D}$$

- **若 $D_{\min} < \infty$**：激活掩码 $\mathcal{M}_d = \{d \mid D_d = D_{\min}\}$。
  **只有代价最小的方向流，其余方向全部抑制。**
- **若 $D_{\min} = \infty$**（半径内无落差口）：**回退**到四个可通行方向**对称扩散**
  （即退回 §3.2 的纯 max 规则，不再挑方向）。

**并列时全部并列者都流，不是任选一个。** 例如正北和正东都恰好距离 2 格有落差，则水同时向北和
向东流，形成分叉。这一点对「T 型水槽」的观感很关键。

wiki 的等价表述（Water 页）：每个方向的权重初值 1000，找到落差后置为到落差的最短路径距离，
最后「water spreads in the directions with the lowest flow weight」。
（<https://minecraft.wiki/w/Water> ，访问 2026-09-16）

> 权重初值 1000 是「INF 的一个实现取值」。若要严格对齐，用 INF 更干净；
> 但若将来要做「多个落差的加权竞争」，保留整数权重比布尔掩码更好扩展。

### 4.6 一个具体算例（平地单源 + 崖边）

```
俯视（S = 源，. = 可流地面，# = 落差口，即该格下方为空）

        z=0  z=1  z=2  z=3  z=4
  x=0    .    .    .    .    .
  x=1    .    .    .    .    #      ← (1,4) 下方为空
  x=2    .    .    S    .    .
  x=3    .    .    .    .    .

源在 (2,2)。四个方向：
  N (2,1): BFS 半径 5 内无落差口 → INF
  S (2,3): 无 → INF
  W (1,2): 从 (1,2) 出发，(1,3)→(1,4) 是落差口，步数 3 → D=3
  E (3,2): 无 → INF

D_min = 3 < INF → 只激活 W 方向。
结果：水收窄成一股直奔 (1,4)，在那里掉下去；而不是向四周摊开。
```

这就是 wiki 那张「Fluids consider the shortest distance to the edge of a cliff, and prioritize
flowing in that direction」配图的机制。

### 4.7 复杂度与缓存

单次搜索的规模：半径 $R=5$ 的菱形区域约 $2R^2+2R+1 = 61$ 格，四个方向共约 244 次访问。
**每个待流格每次计划刻都要跑一次**——一片正在摊开的水会有上百个待流格，于是每 tick 上万次格子查询。

wiki 没给缓存机制的明文，但工程上必须做的两件事（建议，非 wiki 事实）：

1. **每 tick 内对同一 (待流格, 方向) 的查询结果做 memo**，避免同一格被多个邻居重复触发时
   重算。这正是「ThreadLocal 拓扑状态缓存」类做法的动机。
2. **未加载/未生成区块视为空气还是视为墙**：必须显式选一个并在规格里写死。
   若视为空气，跨区块边界的水会「看见」一个假落差口而流向未加载区；若视为墙，则会正确等待。
   **建议视为墙（不可流）**，与 `LightEngine` 对未初始化 chunk 的处理保持一致
   （`ILightWorld::props_at` 对无光照存储的区块以空气作答——但流体应反过来，见 §9 建议表 R-5）。

---

## 5. 源生成判定

### 5.1 水的无限源

一个流动格晋升为源，须同时满足三条：

$$
\texttt{becomeSource}(P) \iff
\mathcal{G}_{\text{water}} \ \wedge\
\Bigl(\sum_{d\in\mathcal{N}_4} \mathbb{1}\bigl[P+\vec{u}_d \text{ 是同种流体源}\bigr] \ \ge\ 2\Bigr)
\ \wedge\ \mathcal{B}_{\text{floor}}
$$

- $\mathcal{G}_{\text{water}}$：gamerule 闸门 `waterSourceConversion`，**默认 true**。
- 水平四邻中**至少 2 个**是同种流体的源（**注意：只数水平四邻，不含上下、不含对角**）。
- $\mathcal{B}_{\text{floor}}$：$P$ 的**正下方**必须是「流体流不进去的方块」——wiki 措辞是
  「has a block that liquids cannot flow into below itself」，即实心底面（或另一个水/岩浆源）。

三条都满足才晋升。经典 2×2 水池：挖掉一格后，该格四邻有 2 个源且底面实心 → 立刻补满，
这就是「无限水」。

gamerule 依据：`waterSourceConversion` 与 `lavaSourceConversion` 于 **1.19.3（22w44a）** 加入
（<https://minecraft.wiki/w/Game_rule> ，访问 2026-09-16）；默认值与判定条件见
Fluid 页「when a block that liquids can flow into … has at least two water sources next to any
of its horizontal faces and has a block that liquids cannot flow into below itself, a water
source is created」及「controlled by the game rules for waterSourceConversion (set to true by
default) and lavaSourceConversion (set to false by default)」
（<https://minecraft.wiki/w/Fluid>）；Water 页的等价表述见「A water source block is created from
a flowing block that is horizontally adjacent to two or more other source blocks, and sitting on
top of a solid block or another water source block」（<https://minecraft.wiki/w/Water>）。
均访问 2026-09-16。

### 5.2 岩浆的差异

岩浆用**同一套判定**，但闸门 `lavaSourceConversion` **默认 false**（1.19.3+ 引入）。
即：默认状态下岩浆**不会**自我再生，倒一桶岩浆就是一桶，不会像水那样从两个源之间长出新源。
把 gamerule 打开后行为与水一致。

### 5.3 两流体相变（定值，不展开）

| 条件 | 产物 |
|---|---|
| 流动岩浆从**非下方**任意方向接触水块（源或流动） | 圆石 |
| 岩浆**流入**水块（源或流动）——由上往下 | 石头 |
| 水（源或流动）从**顶面或侧面**接触岩浆源 | 黑曜石 |
| 岩浆上方接触灵魂土 **且** 任意方向接触蓝冰 | 玄武岩 |

（<https://minecraft.wiki/w/Fluid#mixing> 与 <https://minecraft.wiki/w/Lava> ，访问 2026-09-16）

---

## 6. 实体交互：流场与浸没动力学

> 本节只记数值与公式。OpenCraft 侧怎么接（是否复用 T007 物理管线、推力在哪一阶段注入）
> 由后续卡定。

### 6.1 流场推力向量的构造

当前定值的 wiki 事实：

- **水平流**：一个流体块的水平流向由其与四个水平邻居的**流入/流出向量和**决定。
  「若某块从北进水、向南和向东出水、西边是固体，则合成流向为南偏东南」，因此
  **平面上共有 16 个可能的流向**。（<https://minecraft.wiki/w/Water#Current> ，访问 2026-09-16）
- **下拽流**：由**下方**方块引起。多数「上表面不实心」的方块会在其上方水体产生向下流；
  冰、以及下落水块（向下扩散生成的块）也会。**下落水块默认带向下流。**
  （同上）
- **推力大小**：有流的水以约 **1.39 m/s**（≈ 25 格 / 18 秒）推动玩家与生物；创造模式飞行不推动。
  （同上）
- 创造飞行之外，流动水对**掉落物**也有推力（会被水流带走直到卷入漩涡或流到尽头）。

由「向量和 + 16 方向」可反推构造（**推算，非 wiki 明文**）：对四个水平方向 $d$，
取邻居与本格的**高度差** $\Delta h_d = h(P+\vec{u}_d) - h(P)$ 作为该方向的权重，合成
$\vec{V}_{\text{flow}} = \sum_d \Delta h_d \,\vec{u}_d$，再**归一化**后按 1.39 m/s 施加。
若邻居无水但其下方有水且该邻居不阻挡运动，则改用下方水体的高度参与差值计算。

> ⚠ 上面这段的「高度差加权 + 归一化」是**由 wiki 的定性描述反推的结构**，wiki 只确认了
> 「vector sum of the flows to and from that block from its four horizontal neighbors」与
> 「16 horizontal directions are possible」，没有给权重公式。采纳前须实机校准（§9 建议表 R-7）。

### 6.2 浸没实体的阻尼与重力

wiki `Entity` 页给了**通用**的实体运动三操作与系数（每 tick）：

1. 加加速度（通常只有重力）
2. 乘阻尼系数
3. 更新位置

不同实体三者的**顺序**不同。玩家/生物为 **Position → Acceleration → Drag**；
重力 $-0.08\ \text{m/tick}^2$（double），垂直阻尼 $0.98$，水平阻尼 $0.91$（float）；
着地时水平阻尼为 $0.91 \times \text{friction}$（默认 friction 0.6）。
（<https://minecraft.wiki/w/Entity> ，访问 2026-09-16）

> ⚠ **wiki 的 `Entity` 页没有给「水中」的专用重力/阻尼数值**。桌面白皮书声称水中有效重力
> $g_{eff}=0.005$、水阻尼 0.20（保留 80%）、岩浆阻尼 0.50（保留 50%）、推力常数
> $\alpha_{push}\approx0.014$。**这些数值我无法从 wiki 独立核验**，wiki `Entity` 页的表格只覆盖
> 空气/下落方块/物品/投射物等，没有「in water」行。
> **因此这些数在本文档中标为「未核验，勿直接写入规格」**，见 §9 建议表 R-8。

### 6.3 深海探索者（Depth Strider）

wiki 明文：每级使「water movement efficiency」提升 **⅓**，包括**降低静止时被流水推动的速度**；
3 级时游泳速度等同陆地步行，再高无效；只影响水平，不影响垂直。
（<https://minecraft.wiki/w/Depth_Strider> ，访问 2026-09-16）

### 6.4 气泡柱（定值，不展开）

- 灵魂沙上方 → **上升**气泡柱；岩浆块上方 → **下拽**（漩涡）气泡柱。
- 由**水源方块**传播，遇到流动水或含水块停止；高度只受水面或水下阻挡物限制。
- 放置后 **20 tick** 生成，破坏基底后 **5 tick** 消失，整柱同时生灭。
- **速度**：JE 中实体在上升柱中约 **11 格/秒**，下降柱中约 **4.9 格/秒**；
  BE 为 14 / 6 格每秒。
- 玩家与需呼吸生物在柱中补气速度等同离开水体。

（<https://minecraft.wiki/w/Bubble_Column> ，访问 2026-09-16）

---

## 7. 边界情形

### 7.1 更新抑制（update suppression）

指**强制中断当前方块更新流程**，跳过所有未执行的更新及其后的代码，通常由「更新抑制器」装置
（利用栈溢出、OOM、类转换异常、声音更新、实体 ID 等触发）实现。后果包括：可以做出空中悬浮的
牌子/火把这类本不可能的放置。

栈溢出式抑制在 **22w11a 被「修复」**（MC-249082 判为 WAI），**22w12a 又回退**，
相关 MC-249181 显示该 bug 已修；类转换异常式抑制在 **23w35a** 被修。
（<https://minecraft.wiki/w/Tutorial:Update_suppression> ，访问 2026-09-16）

### 7.2 浮空静止流体

按上文机制，流体停在「本应继续流动却不流动」的状态（例如能级已置 8、下落标记为真，但不再向
邻格传递调度更新）即为浮空静止流体。其**可观察成因**有两类，性质完全不同：

1. **生成期缺少方块更新**：生成的建筑（如通向峡谷的洞）**不会**主动给相邻流体发更新，
   所以海底破洞要等到某次方块更新才会开始漏水。反之，作为结构**一部分**生成的流体
   若未完全封闭则立即流动。
   （<https://minecraft.wiki/w/Fluid#Block_updates> ，访问 2026-09-16）
2. **更新被抑制**（§7.1），使流体卡在中间状态。

> ⚠ **「takeover」一词我在 wiki 全站检索无对应条目**（搜索 `takeover` 只命中
> `Tiny Takeover` 等无关页面，`Takeover_fluid` 为红链）。任务卡第 7 条提到的 takeover
> 未能定位到公开 wiki 来源。**本节按公开资料只覆盖「更新抑制」与「浮空静止流体」两项**，
> takeover 缺口登记在 §9 建议表 R-9，请 PM 裁定是否需要用户提供定义/来源。

### 7.3 其他需要显式定义但对观感影响较小的边界

- **四邻全为固体或流体源**：扩散停止（这正是「不摊开的水柱/岩浆柱」的成因）。
- **四邻全为流体源**：同样停止。
- **下方是同种流体的源**：停止流动。
- **未加载区块边界**：wiki 无明文，需自定（§4.7、§9 R-5）。

---

## 8. 对 OpenCraft 的落地建议

### 8.1 `BlockDef.liquid` 布尔够不够？——不够

现状（`engine/voxel/include/opencraft/voxel/block_registry.hpp:23-32`）：

```cpp
struct BlockDef {
    std::string display_name;
    bool solid = true;
    bool transparent = false;
    float hardness = 0.0f;
    bool liquid = false;   // ← 只有这一位
};
```

注册处（`engine/voxel/src/block_registry.cpp:37`）：
`registry.register_block("water", {"Water", false, true, 100.0f, true});`

**缺的不是「是不是流体」这一位，而是三件事**：

1. **水位没有地方放**。`liquid` 只能表达「这一格是水」，不能表达「这一格是水且能级 5」。
   要表达水位必须再加一个并行通道。
2. **流体种类无法区分**。水与岩浆的 ΔL、tick 间隔、搜索半径、源再生闸门全部不同，
   靠一个布尔无法参数化。
3. **`solid=false` 与 `liquid=true` 的语义边界没划清**。当前注释说 fluid 是
   "non-solid but also not place-through targets" —— 这已经是三个正交属性挤在两个位里了，
   再加 falling 状态会彻底挤爆。

### 8.2 表示层：三条可选路径

| 方案 | 做法 | 代价 | 评 |
|---|---|---|---|
| **A. 扩注册表 ID** | 为 `water_l0..water_l8` 注册 9 个 ID（岩浆另 9 个） | u16 空间充足；**但调色板每个 section 会被打爆**——一片摊开的水会让 palette 从 2 项涨到 10 项，bits 从 4 跳到 8，与 T005 的网格化/批渲染直接冲突 | ✗ 强烈不推荐 |
| **B. 独立流体层（并行位平面）** | 与 `Chunk` 的 `PaletteSection` 平行，加一个 per-voxel 的流体数组（种类 2 bit + 能级 4 bit + falling 1 bit = 7 bit，对齐到 u8） | 每 section 额外 4096 字节；序列化格式 v1→v2；网格化要读第二通道 | ✓ 与 §1.1 的 1.13 设计同构，**推荐** |
| **C. 稀疏叠加表** | 只有含水格进 `unordered_map<key, FluidCell>` | 省内存；但每次邻域查询都要哈希，§4 的 BFS 每 tick 上万次访问会退化 | △ 仅当内存成为瓶颈时 |

**推荐 B**，理由：

- 与 `Chunk` 现有结构最贴合。`Chunk` 已经把 $16^3$ 分节做成 `PaletteSection`，
  `section_empty()` / 序列化都按节走；加一个平行的 `$16^3$ 流体节` 只需复用同一套
  「空节不落盘」逻辑，不必改 `PaletteSection` 内部。
- 网格化（T005）已经在按节取块，加一个通道是**加读一次数组**，不是改算法。
- 未来含水机制（M4+）天然就是「方块调色板 + 流体层」的叠加，方案 B 直接支持，A 不支持。

方案 B 下的每格布局建议（1 字节）：

```
 bit:  7  6 | 5 4 3 2 | 1 | 0
      kind  |  level  | - | falling
      2 bit   4 bit       1 bit
      kind: 0 = 无流体, 1 = 水, 2 = 岩浆, 3 = 预留
      level: 0..8（4 bit 够，8..15 预留）
```

### 8.3 调度层：需要一个流体计划刻队列

现状：**仓库里没有任何计划刻设施**（`grep -rniE "schedule|scheduled_tick|pending tick" engine/ game/`
只命中一条渲染插值注释）。现有调度只有两个：

- `core::TickClock`：20 TPS 固定步长，只负责「这一帧跑几个逻辑 tick」，
  带 `kMaxCatchUpTicks = 5` 的抗死亡螺旋上限和 `dropped_ticks` 计数
  （`engine/core/include/opencraft/core/tick_clock.hpp:13-42`）。
- `voxel::LightEngine`：显式 `std::deque` BFS 队列 + 跨区块延迟 offer 重放，
  单线程、调用方串行化（`engine/voxel/src/light_engine.cpp:70` 起）。

**建议新增一个「流体计划刻队列」，直接照 `LightEngine` 的模式写**：

- 用 `std::deque`（或按 tick 分桶的优先级结构）存 pending 条目，条目 = (世界坐标, 到期 tick)；
- `TickClock` 每推进一 tick 就调用一次 `FluidSim::step()`，取出到期条目执行；
- 跨区块引用用 **deferred offer 重放**，与 `LightEngine` 一致——
  这样区块加载顺序不影响结果，能直接复用 T006 已经验证过的「顺序无关」验收思路；
- **单线程，调用方串行化**，与 `LightEngine` 的既有约定一致，不引入新并发模型。

为什么不用「每 tick 全量扫描活跃流体集」：
流体的 tick 间隔是 5 / 10 / 30，全量扫描等价于把最慢的代价摊到最快的头上，且无法表达
「这一格在 t+7 到期」。计划刻队列是 wiki 明文机制，也是唯一能对齐「5 tick/格」的做法。

### 8.4 需要 PM 裁决的自定项（wiki 无明文）

| 项 | 建议取值 | 理由 |
|---|---|---|
| 计划刻队列溢出行为 | **拒绝入队 + 记 `dropped_fluid_ticks` 计数**，与 `TickClock::dropped_ticks` 同风格 | wiki 只给上限 65,536/tick，未给溢出语义；对齐项目已有的「丢弃并计数、不排队」哲学 |
| 未加载区块边界 | **视为不可流（墙）** | 避免水看见假落差口流向未加载区；注意与 `LightEngine` 对未初始化 chunk 的「按空气作答」相反，需在规格里显式区分 |
| 单 tick 流体更新预算 | 先不设上限，只设**观测计数器** | 先能看见背压，再决定要不要限流 |
| 坡度搜索半径 | 取 **5 / 3**（Fluid 页口径），标"待实机校准" | Water 页另有 "four or fewer"，两套口径相差 1（§4.2） |

### 8.5 分阶段建议（不写代码）

- **阶段 1（下一张卡）**：水一种流体、无源再生、无限高下落 + 水平扩散 + 坡度寻路。
  产出：倒一桶水在地上，看到与基准一致的摊开形状与崖边收窄。
  —— **坡度寻路必须在这个阶段就做**，否则视觉不对，后面返工成本更高。
- **阶段 2**：源再生（含 gamerule 闸门）、流体计划刻背压观测、序列化 v2 迁移。
- **阶段 3**：岩浆（同参数化框架，只是换 ΔL / tick / 半径）、两流体相变。
- **阶段 4（M4+）**：含水机制、气泡柱、玄武岩生成。

---

## 9. 来源核验与建议表

### 9.1 来源核验台账

全部经 `curl -sL "<url>?action=raw"` 拉取原文核对，访问日期统一 **2026-09-16**。
（注：本机需经 `http://127.0.0.1:7890` 代理出网。）

| # | 事实 | 值 | 来源 URL | 状态 |
|---|---|---|---|---|
| 1 | 水水平扩散 | 7 格（源起，平地） | <https://minecraft.wiki/w/Water> 正文「Water can spread downward infinitely until stopped by a block, and **7 blocks** horizontally from a source block on a flat surface」+ 信息框 `flowdistance = 7 blocks` | ✅ 与 PM 核验一致 |
| 2 | 水流速 | 1 格 / 5 tick = 4 格/秒 | 同上，信息框 `flowrate = 5 ticks/block` | ✅ 一致 |
| 3 | 岩浆扩散（主世界/末地） | **正文 3 格** | <https://minecraft.wiki/w/Lava> 正文「lava travels 3 blocks in any horizontal direction」 | ✅ 一致（正文口径） |
| 4 | 岩浆扩散（下界） | 7 格 + 1 格 / 10 tick | 同上正文「In the Nether, lava travels 7 blocks horizontally and spreads 1 block every 10 game ticks」 | ✅ 一致 |
| 5 | 岩浆流速 | 30 tick/格（主世界/末地）、10 tick/格（下界） | 同上信息框 `flowrate` | ✅ 一致 |
| 6 | **岩浆 flowdistance 口径差异** | 信息框 `4 blocks (Overworld, End)` / `8 blocks (Nether)` **vs** 正文 3 / 7 | 同一页信息框 vs 正文 | ⚠ **已在 §2.3 与本节显式注明，按卡面要求未掩盖** |
| 7 | ΔL | 水 1、下界岩浆 1、主世界岩浆 2 | <https://minecraft.wiki/w/Fluid> + Water/Lava blockstate 段 | ✅ 核验 |
| 8 | 计划刻机制、65,536 上限、流体刻无优先级 | — | <https://minecraft.wiki/w/Tick> | ✅ 核验 |
| 9 | 源再生条件（≥2 水平源 + 底面）+ 闸门默认 | — | <https://minecraft.wiki/w/Fluid> | ✅ 核验 |
| 10 | gamerule 引入版本 | 1.19.3 / 22w44a | <https://minecraft.wiki/w/Game_rule> | ✅ 核验 |
| 11 | 坡度搜索半径 | **5 / 3**（Fluid 页）**vs** "four or fewer"（Water 页） | <https://minecraft.wiki/w/Fluid> 、<https://minecraft.wiki/w/Water> | ⚠ **两套口径，已在 §4.2 注明** |
| 12 | 气泡柱 11 / 4.9 格每秒（JE）；20 tick 生成 / 5 tick 消失 | — | <https://minecraft.wiki/w/Bubble_Column> | ✅ 核验 |
| 13 | 实体通用运动系数（重力 0.08、阻尼 0.98/0.91、顺序 P→A→D） | — | <https://minecraft.wiki/w/Entity> | ✅ 核验 |
| 14 | 水流推力 1.39 m/s；平面 16 流向；下拽流成因 | — | <https://minecraft.wiki/w/Water#Current> | ✅ 核验 |
| 15 | 更新抑制机制与版本史 | — | <https://minecraft.wiki/w/Tutorial:Update_suppression> | ✅ 核验 |
| 16 | Depth Strider 每级 +⅓、3 级等同陆地、仅水平 | — | <https://minecraft.wiki/w/Depth_Strider> | ✅ 核验 |
| 17 | 两流体相变产物表 | — | <https://minecraft.wiki/w/Fluid#mixing> | ✅ 核验 |

### 9.2 对 PM 的建议表（请 PM 裁决后落盘）

| ID | 类型 | 内容 | 依据 |
|---|---|---|---|
| **R-1** | **纠正桌面白皮书** | 白皮书 §4.1 的坡度搜索半径 $R_{BFS}=4$ / $2$ **是错的**，wiki 正文为 5 / 3（Fluid 页）与 "four or fewer"（Water 页）。白皮书的数**不是 wiki 口径**，勿作为实现依据。 | §4.2 |
| **R-2** | **口径差异待裁** | 坡度搜索半径 wiki 自身有两套口径（5/3 vs four-or-fewer），相差 1。建议实现取 5/3 并标"待实机校准"。 | §4.2 |
| **R-3** | **表示层改动** | `BlockDef.liquid` 布尔**不够**。建议加**并行流体层**（每体素 1 字节：kind 2bit + level 4bit + falling 1bit），与 `Chunk::PaletteSection` 平行，序列化升 v2。**不推荐**扩注册表 ID 方案（会打爆调色板，与 T005 冲突）。 | §8.1 / §8.2 |
| **R-4** | **调度层新增** | 仓库**无任何计划刻设施**。建议新增流体计划刻队列，照 `LightEngine` 模式写（`std::deque` + 跨区块 deferred offer 重放 + 单线程串行化），由 `TickClock` 每次推进驱动。 | §8.3 |
| **R-5** | **需自定并写进规格** | ① 计划刻队列溢出行为（建议"丢弃并计数"，对齐 `TickClock::dropped_ticks`）；② 未加载区块边界（建议视为**墙**，与 `LightEngine` 的"按空气作答"**相反**，须在规格里显式区分）。 | §8.4 |
| **R-6** | **待实机实验** | 坡度搜索半径 4 vs 5 的实测校准：在平地上距崖边 5 格处放单源，观察是否收窄成一股。 | §4.2 |
| **R-7** | **未核验，勿写规格** | §6.1 的「高度差加权 + 归一化」流场构造是**由 wiki 定性描述反推**的，wiki 只确认"四邻流向向量和"与"16 个方向"，未给权重公式。采纳前须实机校准。 | §6.1 |
| **R-8** | **未核验，勿写规格** | 桌面白皮书声称的水中重力 0.005、水阻尼 0.20、岩浆阻尼 0.50、推力常数 0.014，**无法从 wiki 独立核验**（`Entity` 页无 in-water 行）。**建议不要写入规格**，改用 §6.2 已核验的通用系数 + 实机校准。 | §6.2 |
| **R-9** | **缺口上报** | 任务卡第 7 条提到的 **takeover** 在 wiki 全站检索无对应条目（`Takeover_fluid` 为红链，其余命中为无关页面）。请 PM 裁定：是否需要用户提供定义/来源，或本缺口就此关闭。 | §7.2 |
| **R-10** | **纠正桌面白皮书** | 白皮书声称主世界岩浆"最大水平扩散 3 格"的同时，其 ΔL 表给的是 2、搜索半径给 2 —— ΔL=2 是对的（wiki 一致），但**搜索半径 2 与 wiki 的 3 不符**，同 R-1。 | §4.2 |
| **R-11** | **落地顺序建议** | 坡度寻路必须在**阶段 1（水桶卡）就做**，不能留到后续。它对观感影响最大（崖边是否收窄成一股），后置返工成本高。 | §4.1 / §8.5 |
| **R-12** | **环境与取证** | 本机 wiki 抓取需经 `http://127.0.0.1:7890` 代理（系统 HTTP/HTTPS 代理已开启，但 `curl` 需显式 `-x`，否则 DNS 被拦截到 198.18.0.35 导致 SSL 失败）。建议记入环境备忘。 | §9.1 |

### 9.3 与 `docs/research/07` 的关系

`research/07` 回答「谁在什么时候驱动世界」（调度与执行序、开源参照架构）。
本文 §2 是它在**流体子系统**上的细化：07 给的是全局主循环，本文给的是流体专用的计划刻队列、
时间参数与背压行为。两处不冲突，本文不改写 07 的任何结论。

---

## 附：本文档刻意未展开的内容

按任务卡「避免发散」的要求，以下只给定值或一句话概述，M4+ 再展开：

- **含水机制（waterlogging）**：只给结构（§1.1）与「方块状态 + 流体状态并行叠加」的结论；
  具体哪些方块可含水、各面的 spreading 限制未列。
- **气泡柱**：只给 §6.4 的定值与生成/消失延迟，不含渲染、不含与活塞/观察者的交互。
- **玄武岩生成**：只在 §5.3 给触发条件一行。
- **红石与流体的交互**：只给 §2.2 的执行序（方块刻先于流体刻、流体刻无优先级），不含具体机器。
