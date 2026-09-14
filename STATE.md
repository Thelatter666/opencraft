# OpenCraft 状态（权威状态，PM 每轮更新；与 agentmemory 冲突时以本文件为准）

更新：2026-09-14

## 当前阶段
**M1 完成**（T002–T009 全部验收合入 main，HEAD `5638ffd`，**145/145 测试绿**）。
M1 判据达成：新档可走/挖/建闭环 + 退出重进不丢档（含 `kill -9` 后文件完好）+ 物理回放黄金测试绿。
待办：M1 手感清单人工验收（`docs/01-gameplay-spec.md` §8，需真人）→ 用户补充 T-D1 细节后开 M2。

## 已交付
- 阶段0：调研笔记 4 份 + 规格文档 00–06
- M0：T000（文档）/ T001（仓库骨架+开窗）
- M1（全部 done）：T002 core / T003 区块存储 / T004 地形 / T005 网格化渲染 / T006 光照 / T007 物理 / T008 交互集成 / T009 存读档+HUD

## 关键决策
| 决策 | 内容 | 依据 |
|---|---|---|
| 语言/构建 | C++20 + CMake，三平台 CI | 用户指定 + research/03 |
| 对齐基准 | MC JE 1.21.x/26.x，对齐可观察行为与数值，表达全原创 | docs/00 |
| 进程形态 | 客户端/服务端同仓异进程，服务端权威从第一天做起 | research/03 |
| 核心架构 | 20 TPS 固定步长；16×H×16 区块状态机；双 4-bit 光照 BFS；区域文件+zstd | research/03 |
| 依赖 | GLFW/glm/FastNoiseLite/zstd(T009 已接)/spdlog；未接：EnTT/Tracy/OpenAL Soft | docs/03 §9 |
| 许可 | 代码与资产分层许可（M6 定稿），品牌独立+免责声明 | docs/04 |
| 开发模式 | PM-开发者多对话模式，用户转发；STATE.md 唯一权威状态；记忆层只放指针与方法论（P-001 协议） | docs/05 + 2026-09-14 审计 |

## 任务表
| ID | 任务 | 状态 | agentmemory ID | 验收 |
|---|---|---|---|---|
| T000 | 阶段0：调研+规格文档 | done | act_mtzroq3x_1f6706be3270 | 本文件 |
| T001 | M0 仓库初始化（CMake+CI+开窗） | done（458be6b） | act_mtzsn6tf_9e98ebfcc785 | 三平台构建绿；1280×720 开窗 |
| T002 | M1 engine/core 基础库 | done（070257e，合并 5abcb87） | act_mtzsofnh_3a7cdf45a463 | 23/23 |
| T003 | M1 区块存储+调色板+注册表 | done（7d8be75，合并 57a6712） | act_mtzsp34d_81fd4fd8b974 | 44/44 |
| T004 | M1 噪声地形生成 | done（01da8a5，合并 272437b） | act_mtzspn83_c1216d9d31bf | 56/56 黄金文件 |
| T005 | M1 culled meshing+图集+GL 渲染 | done（2f0c158，合并 62b3838） | act_mtzspn9e_dcc3964163b4 | 82/82；mesh 0.74ms/区块 |
| T006 | M1 双通道光照引擎初版 | done（eddc014，合并 552dfd7） | act_mtzspn9p_40311fd8cf03 | 73/73 跨区块顺序无关 |
| T007 | M1 玩家物理（无头库） | done（d183dd4，合并 74be451） | act_mtzspna1_dfae45044fdf | 108/108 跳高 1.2522 精确 |
| T008 | M1 DDA+挖放+集成 | done（tip ba626e5，合并 ffc37f1） | act_mtzspupw_7ae441cf7872 | 133/133；P2 缺陷移交 T009 |
| T009 | M1 存读档+HUD 打磨+P2 修复 | done（tip 216a6e1，合并 5638ffd；PM 复核 145/145+压缩率实测复现） | act_mtzsq3hn_0b919c433c62 | 端到端存读档+kill -9 恢复；P2 判为非缺陷 |

## 债务/backlog
| ID | 项 | 来源 | 优先级 |
|---|---|---|---|
| ~~T-D1~~ | 已升级为任务卡 → `docs/tasks/T-D1.md`（移动手感专项：网络调研 + 对齐） | 用户反馈 | 已派发 |
| T-D2 | 区域文件不回收空洞（同一次运行内反复改同一区块会增长；全量快照重写故不会无限膨胀）→ research/03 §5.3 碎片整理 | T009 报告 | 低（流式大世界前） |
| T-D3 | `WorldSave` 单把互斥锁：快照写盘期间主线程 `load_chunk` 会短暂等待；M1 规模可接受，流式大世界前改为按区域加锁 | T009 报告 | 中 |
| T-D4 | 区块卸载策略未实现（`LightEngine::forget_chunk` 钩子已实现+单测，但客户端无"出视野即卸载"逻辑，故在游戏内未被驱动） | T009 报告 | 中（M3 流式加载） |
| T-D5 | 输入用每帧轮询（`glfwGetKey`），短于一帧的脉冲会丢——对真人无影响（按键 50–150ms ≫ 帧间隔 11ms），属 P2 观感根因；可选加固：边沿类动作改 `glfwSetKeyCallback` | T009 报告 | 低 |
| T-D6 | QUIT 按钮未经真实鼠标点击验证（开发者用 AX 关闭窗口走同一 `glfwSetWindowShouldClose` 路径验证了退出 flush；合成鼠标在本机仅启动瞬间可达） | T009 报告 | 低（待真人一次点击确认） |

## 任务表（补充：M1 后新卡）
| ID | 任务 | 状态 | agentmemory ID | 验收 |
|---|---|---|---|---|
| T-D1 | 移动手感专项（网络调研 + 数值对齐） | queued（卡面已建 `docs/tasks/T-D1.md`，待派发） | act_mu06lrou_d7083120b228 | 疾跑跳 7.127±1%、双击 W、FOV 对账、gap 清单 |

## 下一步（只放当前有效动作）
1. **派发 T-D1**：卡面 `docs/tasks/T-D1.md` 已落盘。这是**首张跨 agent 卡**（可在 opencode 等
   其他 agent 上执行）——把卡面路径给开发者即可，无需口述提示词；报告落
   `docs/tasks/T-D1.report.md`。规则见 `docs/05-development-process.md` §5 §7。
2. **M1 手感清单人工验收**仍未做（`docs/01-gameplay-spec.md` §8，需真人实机；游戏已可运行）
3. T-D1 回帖后：PM 验收 → 登记其 gap 清单（半砖碰撞/游泳/梯子等）为后续卡
4. 进入 M2（生存规则：库存/合成/饥饿/战斗/生物/刷怪），按 docs/05 逐卡拆分
5. 派卡硬规则：worktree 根固定 `/Users/happy/Desktop/opencraft_worktree/`（§6）；卡面先落
   `docs/tasks/T<ID>.md` 再派发；报告落 `docs/tasks/T<ID>.report.md`；发卡前自查验收标准与
   白名单不相交；路径写完整绝对路径

## 备忘（有效临时项；收口时删除过期条目）
- ~~P2 待证~~ → **已结案（2026-09-14）**：非代码缺陷，是合成输入脉冲（`osascript` 2–5ms）短于每帧轮询周期（≈11ms）的采样盲区；HID 层（86ms）在两个构建上双向正常，ESC 逻辑未变。结论与证据见 `docs/qa/p2-esc-2026-09-14/` + `docs/qa/t009-2026-09-14/`；可选加固见债务 T-D5。**GUI 键盘脚本化验收此后一律用 HID 层或按住 ≥50ms，禁止用 `osascript key code`**
- T009 遗留（已登记债务）：T-D2 区域文件空洞 / T-D3 单锁 / T-D4 卸载策略未驱动 / T-D6 QUIT 未真人点击
- M1 决策记录：光照**不落盘**（重进按 T006 init 重算，1.3–8.8ms/区块）；未修改区块不落盘（脏标记驱动）；证据目录统一 `docs/qa/<task>-<date>/`
- T006 遗留（已补）：区块卸载钩子 `LightEngine::forget_chunk` 已由 T009 实现+单测，但客户端尚无卸载策略驱动（见债务 T-D4）
- T004 设计取舍（改动洞穴/水填充逻辑时必须知晓）：近地表 6 格内 3D 噪声空腔封成石头（防底层水悬空在空腔上）；代价是陆地表面无天然坑洞，洞口全部来自 carve。**未写入 T009 卡**，M3 世界生成扩容前保持有效
- **卡面归档机制已落地（2026-09-14）**：T003–T009 原文入库 `docs/tasks/`，T001/T002 标注"原文缺失"另作事后摘录——索引与来源等级见 `docs/tasks/README.md`；规则（先落盘再派发/卡面不写状态/正文不可改只追加变更记录/报告落 `T<ID>.report.md`）写入 `docs/05-development-process.md` §2，自 T010 起生效。T009 卡面中"spawn 5×5 安全扫描"一句的原文依据现为该归档文件（此前仅有 PM 自述）
- **跨 agent 派发已开通（2026-09-14）**：agentmemory 后端监听 `127.0.0.1:3111`、无鉴权、单一数据文件，任何本机 agent 挂 `npx -y @agentmemory/mcp` 即共享任务板与 lessons。规则进 `docs/05` §5（权威位/单写者）§7（跨 agent）。**约束**：开发者不得写 STATE.md/docs/规格/记忆；接入新 agent 前须确认其不会把会话自动抽取进记忆层（否则 P-001 漂移复发）
- T005 报备有效项：`is_translucent_block(u16)` 硬编码默认注册表（leaves/glass/water），注册表转数据驱动后换调用方注入的 style provider
- 已解决备忘的处置记录：ChunkManager 坐标语义缺陷 → T008 `c1f3403` 修复（int,int=区块坐标、*_world=世界坐标）；T008 集成清单（渲染接光照/控制器替换相机/破坏触发光照更新/BlockDef.liquid）→ 全部完成；TickClock"300ms 补 6 tick"卡面表述 → 以"执行上限 5+丢弃计数"为准

## agentmemory 备注
- 工具限制与对账方法（slots 500 / requires 只认 act_ ID / 无 hooks / REST 对账）见 lesson `lsn_43fe338b4dd1b48f`
- 记忆分工权威协议（P-001）见 lesson `lsn_4c603462963ca635` 与 default 工作区记忆 `zcode-memory-vs-agentmemory.md`
- 不使用 signals/sentinel/mesh；任务板现有 T001–T009 + T-D1 + 阶段0 共 11 张有效卡（另 2 张 cancelled 为 T003/T004 重建残留）
