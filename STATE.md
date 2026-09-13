# OpenCraft 状态（权威状态，PM 每轮更新；与 agentmemory 冲突时以本文件为准）

更新：2026-09-14

## 当前阶段
M1 收口段：T009（M1 最后一张卡）开发中；其余全部验收合入 main（HEAD `82153ea`，133/133 测试绿）。

## 已交付
- 阶段0：调研笔记 4 份 + 规格文档 00–06
- M0：T000（文档）/ T001（仓库骨架+开窗）
- M1：T002 core / T003 区块存储 / T004 地形 / T005 网格化渲染 / T006 光照 / T007 物理 / T008 交互集成

## 关键决策
| 决策 | 内容 | 依据 |
|---|---|---|
| 语言/构建 | C++20 + CMake，三平台 CI | 用户指定 + research/03 |
| 对齐基准 | MC JE 1.21.x/26.x，对齐可观察行为与数值，表达全原创 | docs/00 |
| 进程形态 | 客户端/服务端同仓异进程，服务端权威从第一天做起 | research/03 |
| 核心架构 | 20 TPS 固定步长；16×H×16 区块状态机；双 4-bit 光照 BFS；区域文件+zstd | research/03 |
| 依赖 | GLFW/glm/EnTT(未接)/FastNoiseLite/zstd(T009 接入中)/spdlog/Tracy(未接)/OpenAL Soft(未接) | docs/03 §9 |
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
| T009 | M1 存读档+HUD 打磨+P2 修复 | **in progress**（worktree `/Users/happy/Desktop/opencraft-t009`，分支 task/T009-persistence 尚无提交；在改：cmake/deps.cmake、core ByteBuffer u16/u64、新增 game/server/storage/） | act_mtzsq3hn_0b919c433c62 | 端到端存读档+kill -9 恢复+P2 双向 ESC |

## 债务/backlog
| ID | 项 | 来源 | 优先级 |
|---|---|---|---|
| T-D1 | **人物移动手感专项打磨**（用户 2026-09-14 实机反馈"很大瑕疵"，要求后期重点开发；已知短板：游泳出水笨拙/缺半砖碰撞形状/疾跑跳无位移增益/无梯子攀爬/InputState 缺 backward/水中物理简化版；具体指向待用户补充） | 用户反馈 | 高（M1 收口后立即拆卡） |

## 下一步（只放当前有效动作）
1. 等待 T009 开发者回贴报告（勿重复派发）；验收重点：端到端存读档、kill -9 崩溃恢复、P2 双向 ESC 实机验证
2. T009 收口后：M1 里程碑整体验收（01 §8 手感清单人工过 + memory_snapshot_create + 归档）→ 拆 T-D1 移动专项卡
3. 派卡硬规则：开工先 `git worktree add ../opencraft-<task>`；开发者报告整体放单个 markdown 代码块；发卡前自查验收标准与白名单不相交

## 备忘（有效临时项；收口时删除过期条目）
- **P2 待证（T009 视真人结果处理，勿盲目改码）**：ESC 暂停态下再次 ESC 未恢复——**仅在 System Events 合成键盘事件路径下观察到**（暂停方向同路径成功，证明事件确实到达 GLFW）；**物理键盘从未验证**（早前报告"真键盘不响应"系 PM 措辞错误，已更正）。**若真人按两次 ESC 能正常恢复 → 不是玩家可感知缺陷，T009 不应动代码**；若真人同样卡住 → 按缺陷修，怀疑点是暂停分支 `glfwSetInputMode(CURSOR_NORMAL)` 后按键边沿状态未正确轮询/清除。依据：证据包 `docs/qa/p2-esc-2026-09-14/`（含"先真人验证再决定改码"的前置要求，真人结果须追加到该目录）+ 卡面 `docs/tasks/T009.md` 变更记录
- T006 遗留：区块卸载钩子未做（T009 范围：卸载时脏数据先落盘）
- T004 设计取舍（改动洞穴/水填充逻辑时必须知晓）：近地表 6 格内 3D 噪声空腔封成石头（防底层水悬空在空腔上）；代价是陆地表面无天然坑洞，洞口全部来自 carve。**未写入 T009 卡**，M3 世界生成扩容前保持有效
- **卡面归档机制已落地（2026-09-14）**：T003–T009 原文入库 `docs/tasks/`，T001/T002 标注"原文缺失"另作事后摘录，T-D1 未派发——索引与来源等级见 `docs/tasks/README.md`；规则（先落盘再转发/卡面不写状态/正文不可改只追加变更记录/报告仅争议时归档）写入 `docs/05-development-process.md` §2，自 T010 起生效。T009 卡面中"spawn 5×5 安全扫描"一句的原文依据现为该归档文件（此前仅有 PM 自述）
- T005 报备有效项：`is_translucent_block(u16)` 硬编码默认注册表（leaves/glass/water），注册表转数据驱动后换调用方注入的 style provider
- 已解决备忘的处置记录：ChunkManager 坐标语义缺陷 → T008 `c1f3403` 修复（int,int=区块坐标、*_world=世界坐标）；T008 集成清单（渲染接光照/控制器替换相机/破坏触发光照更新/BlockDef.liquid）→ 全部完成；TickClock"300ms 补 6 tick"卡面表述 → 以"执行上限 5+丢弃计数"为准

## agentmemory 备注
- 工具限制与对账方法（slots 500 / requires 只认 act_ ID / 无 hooks / REST 对账）见 lesson `lsn_43fe338b4dd1b48f`
- 记忆分工权威协议（P-001）见 lesson `lsn_4c603462963ca635` 与 default 工作区记忆 `zcode-memory-vs-agentmemory.md`
- 不使用 signals/sentinel/mesh；任务板现有 T001–T009 + T-D1 + 阶段0 共 11 张有效卡（另 2 张 cancelled 为 T003/T004 重建残留）
