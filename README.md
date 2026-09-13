# OpenCraft

一个用 **C++** 从零实现的体素沙盒游戏。目标：在**公开可观察的玩法、机制、手感、内容组织**上尽可能对齐 Minecraft Java Edition；**全部代码原创、资产原创或使用合规许可资产**，不复制 Minecraft 的代码、纹理、音效、名称与商标。

## 文档地图

| 文档 | 内容 |
|---|---|
| [docs/00-vision-and-scope.md](docs/00-vision-and-scope.md) | 愿景、范围、对齐原则、非目标 |
| [docs/01-gameplay-spec.md](docs/01-gameplay-spec.md) | 玩法机制规格：物理数值、挖掘/战斗/饥饿/合成/生物/进程 |
| [docs/02-worldgen-spec.md](docs/02-worldgen-spec.md) | 世界生成与内容组织规格：区块、噪声管线、群系、维度、存档 |
| [docs/03-architecture.md](docs/03-architecture.md) | C++ 技术架构：线程模型、网格化、光照、存储、网络、依赖选型 |
| [docs/04-legal-compliance.md](docs/04-legal-compliance.md) | IP 合规：红线清单、安全做法、许可策略 |
| [docs/05-development-process.md](docs/05-development-process.md) | 开发流程：项目经理-开发者模式 + agentmemory 协作协议 |
| [docs/06-roadmap.md](docs/06-roadmap.md) | 里程碑路线图 M0–M6 |

### 调研笔记（docs/research/，事实与数值的来源层）

| 笔记 | 内容 |
|---|---|
| [01-mc-core-mechanics.md](docs/research/01-mc-core-mechanics.md) | MC JE 核心玩法机制与具体数值（含 wiki 来源） |
| [02-mc-worldgen-content.md](docs/research/02-mc-worldgen-content.md) | MC JE 世界生成、区块系统、内容组织 |
| [03-voxel-engine-tech.md](docs/research/03-voxel-engine-tech.md) | 体素引擎公开技术 + 同类开源项目经验 |
| [04-legal-ip.md](docs/research/04-legal-ip.md) | 玩法克隆的版权边界、Mojang 立场、资产与许可 |

## 核心原则（一句话版）

1. **对齐的是"可观察行为"，不是实现**：对齐数值与手感（跳跃弧线、挖掘反馈、攻击冷却），用自己的代码与表达实现。
2. **数值即规格**：规格文档中出现的数值以 `docs/research/` 调研笔记为来源，实现偏差必须记录。
3. **原创资产是硬约束**：任何纹理、音效、字体、名称进入仓库前过一遍 [04-legal-compliance.md](docs/04-legal-compliance.md) 的红线清单。
4. **可编译可运行优先**：每个任务交付时仓库必须能构建、能启动、已验收标准可通过。

## 当前状态

阶段 0（文档落实）进行中。下一步：按 [06-roadmap.md](docs/06-roadmap.md) 启动 M1（可运行引擎骨架），采用 [05-development-process.md](docs/05-development-process.md) 的项目经理-开发者模式推进。

## 构建与运行

依赖 CMake ≥ 3.24、C++20 编译器（MSVC / Clang / GCC）。第三方库（GLFW、glm、spdlog、doctest）由 CMake FetchContent 在首次配置时自动拉取，需要网络。

```bash
# 配置 + 构建 + 测试
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# 运行客户端（出现 1280×720 窗口，按 ESC 或点关闭按钮退出）
./build/opencraft
```

Linux 上构建 GLFW 需要先安装 X11/OpenGL 开发头文件（Ubuntu 示例）：
`sudo apt-get install libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev libgl1-mesa-dev`。
