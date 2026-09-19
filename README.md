# OpenCraft

一个用 **C++20** 从零实现的体素沙盒游戏。目标：在**公开可观察的玩法、机制、手感、内容组织**上尽可能对齐 Minecraft Java Edition；**全部代码原创、资产原创或使用合规许可资产**，不复制 Minecraft 的代码、纹理、音效、名称与商标。

> 对齐声明口径：凡声明"已对齐"的机制均以**可复核的原版实测基准**为准并标注版本
> （如流体 = MC **1.19.2** 实机实测）；尚无原版基准的机制（如击退/无敌帧的部分语义）
> 在规格与代码中标注**待校准**，不声称已对齐。详见各规格文档。

## 当前状态（2026-09-19）

**M1（可玩闭环）+ M2a（地基）+ M2b（权威侧）已完成，M2c（生存内容）进行中**；
`529` 项自动测试全绿（ctest）。开发状态与决策记录见 [`STATE.md`](STATE.md)。

已实现：

- **世界**：噪声地形（区块化 16×H×16）、双通道光照、区块流式加载/卸载与存读档
  （含 `kill -9` 后存档完好）、区域文件 + zstd 压缩；
- **流体**：元胞自动机水体（能级扩散），行为已对齐 **MC 1.19.2 实机实测**（含硬门控、半径 5、垂直速率）；
- **交互**：挖掘（硬度/裂纹十阶段/6 tick 连挖延迟）、放置、水桶、41 格库存与快捷栏；
- **生存**：玩家生命/死亡/重生/掉落、受击无敌帧、护甲减伤（JE 公式）、近战击退、
  攻击充能（斜坡 + 84.8% 门槛 + 暴击 + 疾跑击退）；
- **生物**：目标栈 AI（察觉/复仇/逃跑/引诱）、光照与距离驱动的自然刷怪、
  三只体素模型生物（`.vox` 管线，全部原创手绘）；
- **合成**：E 背包（物品移动 + 随身 2×2）、工作台方块 + 3×3 合成、木/石两档工具配方、
  采集等级（`canHarvest`）与工具挖掘倍率；
- **美术管线**：方块贴图 60 张（20/20 方块）与生物体素模型的资产热加载通道，
  限定 32 色色板的原创手绘规格（[docs/art/01-style-guide.md](docs/art/01-style-guide.md)）。

## 文档地图

| 文档 | 内容 |
|---|---|
| [docs/00-vision-and-scope.md](docs/00-vision-and-scope.md) | 愿景、范围、对齐原则、非目标 |
| [docs/01-gameplay-spec.md](docs/01-gameplay-spec.md) | 玩法机制规格：物理数值、挖掘/战斗/饥饿/合成/生物/进程 |
| [docs/02-worldgen-spec.md](docs/02-worldgen-spec.md) | 世界生成与内容组织规格：区块、噪声管线、群系、维度、存档 |
| [docs/03-architecture.md](docs/03-architecture.md) | C++ 技术架构：线程模型、网格化、光照、存储、网络、依赖选型 |
| [docs/04-legal-compliance.md](docs/04-legal-compliance.md) | IP 合规：红线清单、安全做法、许可策略 |
| [docs/05-development-process.md](docs/05-development-process.md) | 开发流程：项目经理-开发者模式 + agentmemory 协作协议 + 实机取证手法 |
| [docs/06-roadmap.md](docs/06-roadmap.md) | 里程碑路线图 M0–M6（含 2026-09-16 结构优先修订） |
| [docs/art/01-style-guide.md](docs/art/01-style-guide.md) | 美术规格：限定色板、贴图与体素模型的制作判据 |
| [docs/tasks/](docs/tasks/README.md) | 任务卡/报告/裁决归档（每张卡的依据与验收细账） |

### 调研笔记（docs/research/，事实与数值的来源层）

| 笔记 | 内容 |
|---|---|
| [01-mc-core-mechanics.md](docs/research/01-mc-core-mechanics.md) | MC JE 核心玩法机制与具体数值（护甲公式/攻击冷却/合成/工具 tiers） |
| [02-mc-worldgen-content.md](docs/research/02-mc-worldgen-content.md) | MC JE 世界生成、区块系统、内容组织 |
| [03-voxel-engine-tech.md](docs/research/03-voxel-engine-tech.md) | 体素引擎公开技术 + 同类开源项目经验 |
| [04-legal-ip.md](docs/research/04-legal-ip.md) | 玩法克隆的版权边界、Mojang 立场、资产与许可 |
| [05–07](docs/research/05-mc-movement-feel.md) | 移动手感、移动/载具物理白皮书、物理知识库（调度/防穿透/对照系） |
| [08–09](docs/research/08-mc-auto-jump-mechanics.md) | Auto-Jump 机制与动画 |
| [10-mc-fluid-dynamics.md](docs/research/10-mc-fluid-dynamics.md) | MC 流体动力学（1.19.2 实机校准的依据） |
| [11-mc-survival-systems.md](docs/research/11-mc-survival-systems.md) | 生存系统缺口：AI 寻路代价/配方清单/敌对属性/刷怪规则 |
| [12-mob-model-formats.md](docs/research/12-mob-model-formats.md) | 体素模型格式与生物模型产线调研 |

## 核心原则（一句话版）

1. **对齐的是"可观察行为"，不是实现**：对齐数值与手感（跳跃弧线、挖掘反馈、攻击冷却），用自己的代码与表达实现。
2. **数值即规格**：规格文档中出现的数值以 `docs/research/` 调研笔记为来源，实现偏差必须记录；无来源的数值**不得入规格**。
3. **原创资产是硬约束**：任何纹理、音效、字体、名称进入仓库前过一遍 [04-legal-compliance.md](docs/04-legal-compliance.md) 的红线清单（贴图手绘、体素模型逐格手稿、命名体系自定）。
4. **可编译可运行优先**：每个任务交付时仓库必须能构建、能启动、验收标准可通过。

## 构建与运行

依赖 CMake ≥ 3.24、C++20 编译器（MSVC / Clang / GCC）。第三方库（GLFW、glm、spdlog、doctest、FastNoiseLite、stb、zstd）由 CMake FetchContent 在首次配置时自动拉取，需要网络。

```bash
# 配置 + 构建 + 测试
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# 运行客户端（1280×720 窗口；ESC 暂停菜单，E 打开背包）
./build/opencraft
```

Linux 上构建 GLFW 需要先安装 X11/OpenGL 开发头文件（Ubuntu 示例）：
`sudo apt-get install libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev libgl1-mesa-dev`。

**离线构建**（网络不可用时）：用 `-DFETCHCONTENT_SOURCE_DIR_<大写库名>=<本地源码目录>`
逐库指向已拉取的源码拷贝即可完成配置（本项目实践见 `docs/05-development-process.md` §6）。
游戏资产（贴图/模型）从仓库根的 `assets/` 解析，无需额外下载。
