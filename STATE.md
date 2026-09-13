# OpenCraft 状态（权威状态，PM 每轮更新；与 agentmemory 冲突时以本文件为准）

更新：2026-09-13

## 当前阶段
阶段0（文档落实）✅ 完成。

## 已交付
- 调研笔记 4 份：docs/research/01（核心机制数值）、02（世界生成）、03（引擎技术）、04（IP合规）
- 规格文档：README、00 愿景与范围、01 玩法机制规格、02 世界生成规格、03 C++架构、04 合规指南、05 开发流程（PM-开发者模式+agentmemory协议）、06 路线图（M0–M6）

## 关键决策
| 决策 | 内容 | 依据 |
|---|---|---|
| 语言/构建 | C++20 + CMake，三平台 CI | 用户指定 + research/03 |
| 对齐基准 | MC JE 1.21.x/26.x，对齐可观察行为与数值，表达全原创 | docs/00 |
| 进程形态 | 客户端/服务端同仓异进程，服务端权威从第一天做起 | research/03 |
| 核心架构 | 20 TPS 固定步长；16×H×16 区块状态机；双 4-bit 光照 BFS；区域文件+zstd | research/03 |
| 依赖 | GLFW/glm/EnTT/FastNoiseLite/zstd/spdlog/Tracy/OpenAL Soft | docs/03 §9 |
| 许可 | 代码与资产分层许可（M6 定稿），品牌独立+免责声明 | docs/04 |
| 开发模式 | PM-开发者多对话模式，用户转发；STATE.md 双写；agentmemory 作外置记忆 | docs/05 |

## 下一步
1. 用户将 T009 任务卡转发给开发者对话（act_mtzsq3hn_0b919c433c62）——M1 最后一张卡
2. **T009 必含 P2 修复**：ESC 暂停态下再次 ESC 无法恢复（PM 实机复现：暂停方向可达、恢复方向真键盘不复现；frontmost 已确认 opencraft；main.cpp:697 切换逻辑看似对称）。修复后须真机验证 ESC↔暂停双向 + RESUME/QUIT 按钮
3. T009 其余要点见下方备忘行
3. T008 集成要点（已并入卡面）：ChunkManager 坐标语义修复（T005 报告）；渲染接光照（T006 备忘）；第一人称控制器替换轨道相机（T005 备忘）；破坏方块触发光照更新（T006 on_block_changed）；BlockDef 建议加 liquid 字段（T007 建议）；疾跑跳位移增益未做（对齐 7.127 m/s 可作后续小任务）
4. T009 备忘：core 补 u16/u64 标量读写；spawn 逻辑做 5×5 安全地面扫描（T004 未做出生点搜索）；近地表 6 格空腔封石是 T004 既定取舍；`opencraft_server` INTERFACE 已链 worldgen；区块卸载钩子补齐（T006 留）
5. **并行硬规则（常设）**：任何任务开工先 `git worktree add ../opencraft-<task>`，主工作区只归 PM 做合并与簿记；PM 验收合并一律先在干净 worktree 以 merge commit 复验
5. T006 集成备忘（写 T008/T009 卡时用）：光照消费入口=ChunkLightWorld 适配器（ChunkManager&+BlockRegistry+EmissionFn→ILightWorld）；LightEngine 非线程安全，须由 worker 池串行调度；init_chunk 1.3–8.8ms/区块（发射体全格扫描是大头，可加"含光源"提示优化）；区块卸载钩子未做，集成任务补
6. **ChunkManager 语义缺陷（T005 报告，T008 必修）**：`get_or_load(int,int)`/`find(int,int)` 形参名为区块坐标、实现却按世界坐标 floor_div 16 解析（T005 已用 int64 key 重载绕开）。T008 卡须统一修正（改实现或改名 world 语义）并更新全部调用方与测试
7. T005 报备：`is_translucent_block(u16)` 暂硬编码默认注册表（leaves/glass/water），有逐 id 对账测试；注册表转数据驱动后换调用方注入的 style provider。主树 T005 残留 WIP 已清理（备份 /tmp/t005-main-tree-wip-backup）

## 债务/backlog（非当前里程碑，防丢失）
| ID | 项 | 来源 | 优先级 |
|---|---|---|---|
| T-D1 | **人物移动手感专项打磨**（用户 2026-09-14 实机反馈"很大瑕疵"，要求后期重点开发；已知短板：游泳出水笨拙/缺半砖碰撞形状/疾跑跳无位移增益/无梯子攀爬/InputState 缺 backward/水中物理简化版；具体指向待用户补充） | 用户反馈 | 高（M1 收口后立即拆卡） |

## 任务表
| ID | 任务 | 状态 | agentmemory ID | 验收 |
|---|---|---|---|---|
| T000 | 阶段0：调研+规格文档 | done | — | 本文件 |
| T001 | M0 仓库初始化（CMake+CI+开窗） | done（458be6b） | act_mtzsn6tf_9e98ebfcc785 | 三平台构建绿；1280×720 开窗 |
| T002 | M1 engine/core 基础库 | done（070257e，合并 5abcb87） | act_mtzsofnh_3a7cdf45a463 | 23/23 |
| T003 | M1 区块存储+调色板+注册表 | done（7d8be75，合并 57a6712） | act_mtzsp34d_81fd4fd8b974 | 44/44 |
| T004 | M1 噪声地形生成 | done（01da8a5，合并 272437b；PM 复核 56/56） | act_mtzspn83_c1216d9d31bf | 黄金文件+语义抽查全过 |
| T005 | M1 culled meshing+图集+GL 渲染 | done（2f0c158，合并 62b3838；PM 复核 82/82+截图视觉验收） | act_mtzspn9e_dcc3964163b4 | mesh 0.74ms/区块；水半透明；ESC 退出码 0 |
| T006 | M1 双通道光照引擎初版 | done（eddc014，合并 552dfd7；PM 复核 73/73） | act_mtzspn9p_40311fd8cf03 | 光照单测+跨区块顺序无关全过 |
| T007 | M1 第一人称控制器+物理 | done（d183dd4，合并 74be451；PM 复核 108/108+常数对账） | act_mtzspna1_dfae45044fdf | 物理黄金回放+跳高 1.2522 精确命中 |
| T008 | M1 DDA 选取+挖掘/放置+集成 | done（5 提交，tip ba626e5，合并 ffc37f1；PM 复核 133/133+实机截图；遗留 P2） | act_mtzspupw_7ae441cf7872 | 挖掘公式单测+第一人称闭环+真实地形 |
| T009 | M1 HUD/暂停/存读档 | queued（已派发，含 P2 修复） | act_mtzsq3hn_0b919c433c62 | 存读档不丢档 |

## agentmemory 备注
- 可用：actions（任务板/依赖）、lease、lessons、memory_save/recall、facet、snapshot、crystallize
- 不可用：memory_slot_*（后端 500，已记录）；signals/sentinel/mesh 不使用（开发者对话靠用户转发）
