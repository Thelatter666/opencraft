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
1. 用户将 T004/T005/T006/T007 四张任务卡分别转发给四个开发者对话（可并行）
2. 各任务回贴后 PM 逐卡审查；四线齐后派 T008（依赖 T005+T007）
3. T009 备注：届时在 core 补 write_u16/read_u16/write_u64/read_u64 标量接口（T003 开发者建议）；网格化/光照如需"非空 section 快速遍历"用 section_empty() 组合，勿依赖 section_stats() 内部含义

## 任务表
| ID | 任务 | 状态 | agentmemory ID | 验收 |
|---|---|---|---|---|
| T000 | 阶段0：调研+规格文档 | done | — | 本文件 |
| T001 | M0 仓库初始化（CMake+CI+开窗） | done（458be6b） | act_mtzsn6tf_9e98ebfcc785 | 三平台构建绿；1280×720 开窗 |
| T002 | M1 engine/core 基础库 | done（070257e，合并 5abcb87） | act_mtzsofnh_3a7cdf45a463 | 23/23 |
| T003 | M1 区块存储+调色板+注册表 | done（7d8be75，合并 57a6712；PM 复核 44/44） | act_mtzsp34d_81fd4fd8b974 | 调色板/序列化/负坐标单测全绿 |
| T004 | M1 噪声地形生成 | queued（已派发） | act_mtzspn83_c1216d9d31bf | 种子确定性黄金测试 |
| T005 | M1 culled meshing+图集+GL 渲染 | queued（已派发） | act_mtzspn9e_dcc3964163b4 | 多区块渲染；网格化<5ms |
| T006 | M1 双通道光照引擎初版 | queued（已派发） | act_mtzspn9p_40311fd8cf03 | 光照单测+黄金测试 |
| T007 | M1 第一人称控制器+物理 | queued（已派发） | act_mtzspna1_dfae45044fdf | 物理回放黄金测试 |
| T008 | M1 DDA 选取+挖掘/放置 | blocked(T005,T007) | act_mtzspupw_7ae441cf7872 | 挖掘公式单测+手测 |
| T009 | M1 HUD/暂停/存读档 | blocked(T008) | act_mtzsq3hn_0b919c433c62 | 存读档不丢档 |

## agentmemory 备注
- 可用：actions（任务板/依赖）、lease、lessons、memory_save/recall、facet、snapshot、crystallize
- 不可用：memory_slot_*（后端 500，已记录）；signals/sentinel/mesh 不使用（开发者对话靠用户转发）
