# T-R1 报告：MC 流体系统调研白皮书

任务：T-R1（调研卡，不改代码）　基线：`b98d504`
完成：2026-09-16　执行：开发者（研究员角色）

---

## 1. 交付物

| 文件 | 状态 |
|---|---|
| `docs/research/10-mc-fluid-dynamics.md` | ✅ 新建（43 KB） |
| `docs/tasks/T-R1.report.md` | ✅ 本文件 |

**未触碰白名单外任何文件**（`git status` 应只显示上述两个新增文件 + 无其他改动）。

---

## 2. 文档结构（覆盖卡面要求的 8 个方面）

| § | 内容 | 对应卡面要求 |
|---|---|---|
| 0 | 读法说明 + 结论速览参数表 + 能级记号约定 | — |
| 1 | 数据表示：1.13 扁平化的设计动机、流体状态三字段、渲染高度折算 | ① |
| 2 | 调度模型：计划刻 vs 随机刻、执行序与优先级、时间参数表、队列上限与背压 | ② |
| 3 | 元胞自动机转移规则：邻域、转移方程、ΔL、下落柱、垂直优先 | ③ |
| 4 | **坡度寻路（核心）**：动机、搜索半径、落差口定义、可直写的 BFS 伪代码、并列取舍、回退、算例、复杂度与缓存 | ④ |
| 5 | 源生成判定：水无限源三条件、岩浆差异、两流体相变定值 | ⑤ |
| 6 | 实体交互：流场构造、浸没阻尼/重力、Depth Strider、气泡柱定值 | ⑥ |
| 7 | 边界情形：更新抑制、浮空静止流体、其他需自定项 | ⑦ |
| 8 | **对 OpenCraft 的落地建议**：`liquid` 布尔够不够、三条表示层路径对比、调度层新增、4 项需 PM 裁决的自定项、分阶段建议 | ⑧ |
| 9 | 来源核验台账（17 条）+ **建议表 R-1..R-12** | 验收 2/6 |

---

## 3. 验收标准逐条对照

| # | 验收标准 | 结论 |
|---|---|---|
| 1 | 文档存在、结构完整、**通篇无反编译源码片段**、无逐字 wiki 段落 | ✅ 全文 grep `yarn\|mcp\|forge\|net.minecraft\|appendProperties\|getCacheKey\|Vec3d\|syntaxhighlight` 仅命中第 7 行的**合规声明本身**（声明不含这些）。全部英文引用经扫描仅 8 处，均为 ≤2 句的**事实性短语摘录 + 来源 URL**，无逐字段落。公式与数值为原创重写。 |
| 2 | 每个关键数值旁标注来源 URL（规范 URL，无 google 跳转）+ 访问日期 | ✅ §9.1 台账 17 条逐条带 URL；正文各节数值旁也直接标注。**全部 URL 为 `https://minecraft.wiki/w/...` 规范形式，无 google 跳转前缀**。access date 统一 2026-09-16。 |
| 3 | PM 核验的 5 条事实须出现且数值一致；岩浆 3/7 vs 4/8 口径差异须显式注明 | ✅ 5 条全部出现在 §9.1 台账第 1–5 行并逐条标注「与 PM 核验一致」。**3/7 vs 4/8 差异在 §2.3 与 §9.1 第 6 行两处显式注明，未掩盖**。 |
| 4 | 坡度寻路讲透，读者能据此直写 BFS | ✅ §4 独占 140 行：含搜索半径（§4.2）、落差口定义（§4.3）、**逐行可执行伪代码 + 三个易错点**（§4.4）、并列取舍与无落差回退（§4.5）、**带坐标的具体算例**（§4.6）、复杂度与缓存（§4.7）。 |
| 5 | 明确回答「`liquid` 布尔够不够」+ 针对现有结构的建议 | ✅ §8.1 直接答「不够」并列出缺的三件事；§8.2 三条路径对比表（含对 u16 注册表/`Chunk::PaletteSection`/T005 网格化的具体影响）推荐方案 B；§8.3 指出仓库**无任何计划刻设施**并给出照 `LightEngine` 模式的实现路径。 |
| 6 | P-001 合规，建议以「建议表」形式写入文档内 §建议章节 | ✅ §9.2 建议表 R-1..R-12，未改 `STATE.md`、未改记忆层、未改其他 `docs/**`。 |
| 7 | 报告落 `docs/tasks/T-R1.report.md` | ✅ 本文件 |
| 8 | 置 agentmemory action `act_mu2wexpf_a1e8fb003b68` 为 done | ⏳ 见 §7 |

---

## 4. ★ 需要 PM 注意的纠正与新发现

### 4.1 纠正桌面白皮书（R-1 / R-10）

白皮书 §4.1 的坡度搜索半径 **$R_{BFS}=4$（水/下界岩浆）/ $2$（主世界岩浆）是错的**。
wiki 正文口径为 **5 / 3**（`Fluid` 页 "up to 5 blocks away for water or lava in the Nether and
up to 3 blocks away for lava elsewhere"），另 `Water` 页有 "four or fewer blocks" 的第二套表述。

**白皮书的数不是 wiki 口径，不应作为实现依据。**

### 4.2 wiki 自身有两套坡度半径口径（R-2 / R-6）

- `Fluid` 页正文：up to **5 / 3** blocks away
- `Water` 页正文：**four or fewer** blocks

两者相差 1（含不含起点计数），**我无法从公开 wiki 判定哪个是 1.21.x 当前行为**。
已按卡面要求在 §4.2 显式注明、未擅自选一个掩盖。建议实现取 5/3 并标"待实机校准"，
校准实验设计已写入 R-6。

### 4.3 两项白皮书数值**无法从 wiki 核验**（R-7 / R-8）— 请勿写入规格

| 白皮书声称 | 核验结果 |
|---|---|
| 水中有效重力 $g_{eff}=0.005$ | ❌ wiki `Entity` 页**无 in-water 行**，无法核验 |
| 水阻尼 0.20 / 岩浆阻尼 0.50 | ❌ 同上 |
| 推力常数 $\alpha_{push}\approx0.014$ | ❌ 同上 |
| 流场「高度差加权 + 归一化」公式 | ⚠ 由 wiki 定性描述**反推**；wiki 只确认「四邻流向向量和」与「16 个方向」，未给权重公式 |

已核验可用的替代：`Entity` 页通用系数（重力 0.08、垂直阻尼 0.98、水平阻尼 0.91、
顺序 Position→Acceleration→Drag）+ `Water#Current` 的 1.39 m/s 推力大小。
**建议规格改用这些已核验值，未核验项留待实机校准。**

### 4.4 takeover 缺口（R-9）

任务卡第 7 条提到的 **takeover** 在 wiki 全站检索**无对应条目**：
`Takeover_fluid` 为红链（页面不存在），`takeover` 全文检索只命中 `Tiny Takeover` 等无关页面。

**本缺口无法用公开资料补齐**，已在 §7.2 如实标注。请 PM 裁定：需用户提供定义/来源，或缺口就此关闭。

### 4.5 仓库现状核查（R-4）

- `BlockDef.liquid` 确实只有一位（`block_registry.hpp:31`，`liquid = false`），
  注册处 `block_registry.cpp:37` 的 `water` 为 `{..., false, true, 100.0f, true}`。
- **仓库里没有任何计划刻设施**：`grep -rniE "schedule|scheduled_tick|pending tick" engine/ game/`
  只命中 `game/client/src/main.cpp:1055` 的一条渲染插值注释。现有调度只有
  `core::TickClock`（20 TPS + `kMaxCatchUpTicks=5`）与 `voxel::LightEngine`（显式 deque BFS
  + 跨区块 deferred offer 重放）。
- 流体计划刻队列**需从零新建**，建议照 `LightEngine` 模式写（单线程、deque、跨区块 offer 重放），
  可直接复用 T006 已验证的「顺序无关」验收思路。

### 4.6 两处需 PM 裁决的自定项（R-5）

1. **计划刻队列溢出行为**：wiki 只给 JE 上限 **65,536 / tick**（BE 为每区块 100），
   **未给溢出后语义**（丢弃/延迟/报错三者对红石机器玩家的可观察结果完全不同）。
   建议"丢弃并计数"，对齐 `TickClock::dropped_ticks` 的既有哲学。
2. **未加载区块边界**：建议视为**墙（不可流）**，避免水看见假落差口流向未加载区。
   ⚠ 注意这与 `LightEngine` 对未初始化 chunk 的「按空气作答」**相反**，必须在规格里显式区分。

### 4.7 落地顺序建议（R-11）

**坡度寻路必须在阶段 1（水桶卡）就做，不能后置。** 它是唯一决定「崖边是否收窄成一股」的规则，
对观感影响最大；后置会导致视觉返工。

---

## 5. 取证方法说明（R-12，建议记入环境备忘）

- 用 `curl -sL "<url>?action=raw"` 拉 wiki 原文，本次 9 个页面全部 200 可达。
- ⚠ **本机需显式指定代理**：`curl -x http://127.0.0.1:7890`。
  不加 `-x` 时 DNS 被拦截到 `198.18.0.35`，curl 报 `(35) LibreSSL SSL_connect: SSL_ERROR_SYSCALL`
  （系统 HTTP/HTTPS 代理虽已开启，curl 不会自动读）。
- 本次抓取页面：`Water` / `Lava` / `Tick` / `Fluid` / `Bubble_Column` / `Game_rule` /
  `Waterlogging` / `Chunk_format` / `Entity` / `Depth_Strider` / `Tutorial:Update_suppression`。

---

## 6. 已知问题 / 本文档的局限

1. **坡度搜索半径 4 vs 5 未定论**——wiki 自身两套口径，需实机校准（R-6）。
2. **水中物理数值未核验**——白皮书给的那组无法溯源，已标"勿写规格"（R-8）。
3. **流场权重公式为反推**——wiki 只给定性描述（R-7）。
4. **takeover 缺口未补**——wiki 无此条目（R-9）。
5. **计划刻溢出语义未知**——wiki 只给上限数字（R-5）。
6. 本文档刻意未展开（按卡面「避免发散」）：含水机制细节、气泡柱渲染与交互、玄武岩生成、
   红石与流体的具体机器。均只给定值或一句话，M4+ 再展开。

---

## 7. agentmemory

action `act_mu2wexpf_a1e8fb003b68` → **done** ✅ 已置位（HTTP 200，`success: true`）。

本次会话未挂载 agentmemory MCP 工具，改走 REST 直连（后端 `127.0.0.1:3111`，无鉴权）：

```bash
curl -sS --noproxy '*' -X POST http://localhost:3111/agentmemory/actions/update \
  -H "Content-Type: application/json" \
  -d '{"actionId":"act_mu2wexpf_a1e8fb003b68","status":"done","result":"..."}'
```

已同时写入 `result` 字段（本文 §4 的 8 条要点摘要）。

---

## 8. 变更清单

| 文件 | 操作 |
|---|---|
| `docs/research/10-mc-fluid-dynamics.md` | 新增 |
| `docs/tasks/T-R1.report.md` | 新增 |

**接口变更**：无（调研卡，未改任何代码）。
**构建/测试**：不适用（无代码改动）。建议 PM 在合并前跑一次 `git status` 确认无白名单外改动。
