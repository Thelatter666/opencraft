# OpenCraft 技术调研 03：体素引擎核心技术

> 项目：OpenCraft —— C++ 原创体素沙盒引擎（玩法对标 Minecraft Java Edition）
> 本文档为技术调研笔记，仅调研公开资料并归纳设计决策依据，不涉及任何第三方代码复制。
> 调研日期：2026-09-13

---

## 目录

1. [分块与网格化（Chunk Meshing）](#1-分块与网格化chunk-meshing)
2. [光照引擎（Minecraft 式 15 级光照）](#2-光照引擎minecraft-式-15-级光照)
3. [地形生成（噪声与生物群系）](#3-地形生成噪声与生物群系)
4. [大世界流式加载与线程模型](#4-大世界流式加载与线程模型)
5. [世界存储与区块压缩](#5-世界存储与区块压缩)
6. [碰撞与物理](#6-碰撞与物理)
7. [渲染 API 选型](#7-渲染-api-选型)
8. [开源同类项目经验教训](#8-开源同类项目经验教训)
9. [Minecraft Java 公开性能特性](#9-minecraft-java-公开性能特性)
10. [C++ 生态选型清单](#10-c-生态选型清单)
11. [ECS 在 C++ 中的适用性](#11-ecs-在-c-中的适用性)
12. [网络架构](#12-网络架构)
13. [结论：推荐架构决策汇总](#13-结论推荐架构决策汇总)

---

## 1. 分块与网格化（Chunk Meshing）

### 1.1 分块（Chunking）

- 体素世界必须划分为固定尺寸的 Chunk（Minecraft 为 16×384×16 的纵向柱，Minetest 为 16×16×16 的 MapBlock）。分块的意义：
  - **网格化/生成/持久化/网络传输的天然单位**，只重做脏数据，不做全局重算。
  - 配合哈希表或分页数组做稀疏管理，实现"按需加载、按需卸载"的无限世界。
- 区块尺寸权衡：太小 → 区块管理开销与边界面剔除查询次数上升；太大 → 单次网格化卡顿变大、脏区块重网格化代价高。16³ 是社区长期验证的甜点值；纵向柱式（16×H×16）更贴合 Minecraft 式玩法（便于按列做天光计算与高度图）。

### 1.2 面剔除（Culled Meshing）

- 最基本的优化：只生成与空气/透明方块相邻的实心面（相邻方块不透明则该面被剔除）。
- 跨区块边界查询：网格化时必须能查询相邻区块（需要"邻区就绪"的依赖机制，见 §4）。经典参考是 0fps 的《Meshing in a Minecraft Game》，其中把网格化分为 Stupid / Culled / Greedy 三档算法：

  - [Meshing in a Minecraft Game — 0fps](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/)
  - [Meshing in a Minecraft Game (Part 2) — 0fps](https://0fps.net/2012/07/07/meshing-minecraft-part-2/)

### 1.3 贪心网格化（Greedy Meshing）

- 核心思想：在同一朝向的平面切片上，把**材质/光照等属性完全相同**的相邻面合并成大四边形（quad），显著降低顶点数（0fps 实测可减少一个数量级以上）。
- 实现：逐维度、逐切片扫描，用 2D mask 记录每个 cell 的面属性，然后在 mask 上做最大矩形扩展。
- **与逐顶点光照的冲突**：贪心合并要求面内所有顶点属性一致。若使用平滑光照/AO，则 AO 不同的面无法合并。常见取舍：
  - 将 AO/光照值并入 mask 的比较键，属性不同即中断合并（合并率下降但仍是净收益）；
  - 或对不透明地形使用贪心 + 顶点拉取（vertex pulling），对带 AO 的近景网格使用 culled meshing，两套网格并行。
- 0fps 的 AO 文章还给出四边形翻转规则：当 `a00 + a11 > a01 + a10` 时翻转对角线三角化方向，避免 AO 插值出现"对角伪影"：
  - [Ambient occlusion for Minecraft-like worlds — 0fps](https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/)
  - 贪心与 AO 冲突的社区讨论：[Stack Overflow: Face Merging vs Per-Vertex AO](https://stackoverflow.com/questions/39350250/opengl-voxel-engine-face-merging-vs-per-vertex-ambient-occlusion)、[Defold 论坛：greedy meshing with vertex lighting](https://forum.defold.com/t/greedy-meshing-with-vertex-lighting-how-to-store-per-block-lighting-data/82478)

### 1.4 生产级渲染管线参考

- [Voxel Meshing in Exile — thenumb.at](https://thenumb.at/Voxel-Meshing-in-Exile/)：工业级体素渲染管线长文，覆盖方块数据 → 可渲染网格的完整流程，值得通读。
- [High Performance Voxel Engine: Vertex Pooling — nickmcd.me](https://nickmcd.me/2021/04/04/high-performance-voxel-engine/)：把每个区块网格按 6 个面朝向拆分为顶点池桶（bucket），配合顶点池复用减少分配与提交开销；LOD 降级时各桶 quad 数的分布也可预测。
- 工程要点归纳：
  - 网格化输出使用**索引 + 顶点缓冲**，顶点格式压紧（位置可用 chunk 内局部坐标 + uint8/uint16，纹理层、AO、光照可打包进几个字节）。
  - 用**顶点/索引池 + 双缓冲**避免每帧分配；上传 GPU 用持久映射或 fence。
  - 网格化是纯函数式的"区块快照 → 网格"过程，非常适合放到工作线程（见 §4）。

### 1.5 透明/半透明渲染与排序

- 透明体（水、玻璃、树叶/交叉面片）不能写入深度或需要特殊处理，经典做法：
  - 分成多个渲染层/独立网格（不透明、透明、半透明、cross-plant、cutout），一个区块在网格化时输出多份 mesh，按层分别绘制。
  - 不透明层：从前到后排序区块（基于视锥 + 距离），充分利用 early-Z。
  - 半透明层（水/玻璃）：**从后到前**排序；逐三角形排序代价过高，实践上按区块中心距离排序 + 混合即可接受，再辅以"同区块内按面朝向"的粗排序。水只渲染顶面与侧面即可大幅减面。
  - 带不等式的双重面剔除（如 `glEnable(GL_DEPTH_TEST)` + 关闭背面剔除的水面，或用 stencil/`discard` 处理交叉植物）。
  - 更高阶方案（后续可选）：OIT（per-pixel linked list、weighted blended OIT），但 Minecraft 式玩法通常不需要，按区块排序已足够。
- 社区共识：把"排序粒度"控制在区块级，避免逐面排序；粒子等动态半透明物体单独一层。

---

## 2. 光照引擎（Minecraft 式 15 级光照）

### 2.1 Minecraft 官方语义（公开 wiki）

- 两个独立通道：**天光（skylight）**与**方块光（blocklight）**，各 0–15 共 16 级；实际显示亮度取 `max(sky * daylight_factor, block)`。
- 天光：从上向下无遮挡的列直接为 15；有遮挡后通过**泛洪填充（flood fill）**向周围扩散，每步衰减 1。
- 方块光：由光源方块（火把、萤石等）发出，同样 BFS 扩散衰减。
- 权威描述：[Light — Minecraft Wiki](https://minecraft.wiki/w/Light)、[Grey's Minecraft Coder: Lighting](http://greyminecraftcoder.blogspot.com/2013/08/lighting.html)

### 2.2 BFS 传播与删除（关键难点）

- **传播（加光）**：标准队列 BFS。种子（光源 / 天光列顶）入队，弹出后向 6 邻域扩散，邻居光值 = 当前 − 1（透明方块）或不传播（不透明方块）；天光**垂直向下不衰减**是特例。
- **删除（去光）**：这是最容易出错的部分。放置不透明方块或移除光源时，需要一次"暗度 BFS"：把受影响光照按 −1 级联式扣减入队，扣到 0 为止；凡是被扣减的格子若原本还有其他光源可达，需重新作为种子做一次加光 BFS。参考实现讨论：
  - [Fast Flood Fill Lighting in a Blocky Voxel Game — r/gamedev](https://www.reddit.com/r/gamedev/comments/2iru8i/fast_flood_fill_lighting_in_a_blocky_voxel_game/)
  - [非递归（队列式）光照传播 — GameDev StackExchange](https://gamedev.stackexchange.com/questions/91926/how-can-i-build-minecraft-style-light-propagation-without-recursive-functions)
  - [暗度（light removal）跨 6 通道 BFS 优化 — Ziggit](https://ziggit.dev/t/pointers-to-optimize-darkness-propagation-in-a-minecraft-style-lighting-grid/14439)
- 一定要用**显式队列**而非递归：15 级 × 6 邻域的深度递归在极端场景会爆栈，且队列便于去重与限速。

### 2.3 跨区块传播与缓存

- 光照是全局量，但存储按区块切分。实践模式：
  - 每区块存两个 nibble 数组（每方块 4 bit sky + 4 bit block，即 1 字节/方块，或与方块数据打包）。
  - 区块光照初始化（生成时）在区块内完成，但**边界列/边界面的光照由跨区块 BFS 队列补齐**：当邻区未就绪时把边界光任务挂起，邻区到达后重投递。
  - 光照变化 → 标记受影响区块（可能波及多达 3×3×3 邻域）为"需要重新网格化"，交由网格化调度器合并去重，避免一盏火把触发几十次网格化。
  - 高度图（heightmap）缓存天光列计算：每列记录最高不透明方块，放置/破坏方块时增量更新，天光重算只涉及受影响列。
- 工程建议：把"光照更新"做成与"网格化"平级的**异步任务类型**，在同一个任务图里表达依赖（光照 → 网格化），并设置每帧预算防止光照雪崩卡帧。

---

## 3. 地形生成（噪声与生物群系）

### 3.1 噪声函数选型

| 噪声 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| Perlin | 经典、实现多 | 有方向性网格伪影；Simplex 3D 有专利风险（原始 Simplex 专利 2022 年已到期，但生态仍偏好回避） | 仅作备选 |
| Simplex | 快、各向同性较好 | 3D/4D 曾有专利阴影 | 备选 |
| **OpenSimplex2 / OpenSimplex2S** | 无专利负担、伪影更少、公共领域（Unlicense）；2S 变体更平滑、特别适合域扭曲输入 | 略慢于 Simplex | **推荐主力** |

- OpenSimplex2 官方仓库（2D/3D/4D，Unlicense）：[KdotJPG/OpenSimplex2](https://github.com/KdotJPG/OpenSimplex2)；背景介绍：[Wikipedia: OpenSimplex noise](https://en.wikipedia.org/wiki/OpenSimplex_noise)
- 建议准备两个变体：`OpenSimplex2`（地形主体，略"噪"）与 `OpenSimplex2S`（域扭曲、云层、温度场，更平滑）。

### 3.2 分形布朗运动（fBm）与域扭曲

- **fBm**：多个 octave 叠加（`amp *= 0.5; freq *= 2.0`）得到自然的多尺度地形；配合 Billow/Ridged 变体做山脉脊线。参数化：octave 数 4–8、lacunarity ≈ 2.0、gain ≈ 0.5，并按用途挑选（大陆形状用低 octave 低频，细节用高频）。
- **域扭曲（Domain Warping）**：用 1–2 层噪声去偏移主噪声的输入坐标，消除直线感与"噪声感"，产生蜿蜒海岸、扭曲山脊。经典图文：[Inigo Quilez — Domain Warping](https://iquilezles.org/articles/warp/)。代价是采样次数 ×3，因此只对低频大形态使用。
- 工程化建议：把噪声图组织为**分层的"噪声管线"**（大陆度 → 侵蚀 → 峰值 → 河流 → 洞穴），每层输出可缓存、可单独调试；这正是 Minecraft 1.18 之后公开密度函数（density function）体系的思路。

### 3.3 生物群系参数化

- 主流做法（Minecraft 公开资料与社区共识）：
  - 以**低频气候噪声场**（温度、湿度，可加大陆度/奇异度/深度）连续采样，将 (T, H) 映射到 **Whittaker 生物群系图**（二维查表或加权混合），得到连续过渡而非硬边界；
  - 地表方块、植被、天空/雾颜色由生物群系参数驱动；为避免网格化时生物群系跨界不一致，生物群系 ID 需要按列存进区块数据。
- 多尺度策略：先定"大陆/海洋"（超低频），再定气候带，最后用局部噪声做扰动；洞穴用 3D 噪声（奶酪洞cheese / 意面洞spaghetti 双系统思路）。
- 可参考 FastNoiseLite（OpenSimplex2 的作者参与维护、MIT、C++ 可用）：[Auburn/FastNoiseLite](https://github.com/Auburn/FastNoiseLite)——一个库同时提供 OpenSimplex2/S、fBm/域扭曲封装，适合直接引入。

---

## 4. 大世界流式加载与线程模型

### 4.1 流式加载

- 以玩家为中心的**螺旋/环状加载队列**：按距离排序请求区块，近处优先；带加载半径与卸载半径（滞后带 hysteresis，避免在边界来回抖动）。
- 区块状态机是核心设计：`Empty → Generating（地形）→ Lighting（光照就绪）→ Meshed（网格就绪）→ Active`，以及 `Dirty`（需重网格化/重光照）。状态转移由任务系统驱动，邻区依赖用"至少 4 邻区（8 邻域）就绪才允许网格化/光照边界传播"的门槛解决。
- 优先级随玩家移动动态调整；玩家交互触发的区块（如射线命中）提权。

### 4.2 LOD（可选、后置）

- Minecraft 式玩法（服务器权威、玩家密集交互）通常**不需要视距内多级 LOD**，把视距控制在 16–32 区块即可；LOD 的价值在超远视距/大地图回看。若做，参考：
  - [GPU-Driven Voxel Rendering Framework（arXiv 2025）](https://arxiv.org/html/2505.02017v1)：GPU 驱动的 LOD 与流式系统；
  - [Voxel Tools 性能文档](https://voxel-tools.readthedocs.io/en/latest/performance/)：LOD 切换导致的重建风暴是首要坑；
  - Minetest 的经验：远区块降频更新（见 §8）。
- **建议**：MVP 不做 LOD，但网格化接口预留"细节级别"参数与按朝向分桶（§1.4），未来可平滑加 LOD。

### 4.3 Worker 线程与主线程职责划分（共识模式）

来源：[voxel-engine design doc（线程池 + FinishedChunk 模式）](https://github.com/DanielWLiu07/voxel-engine/blob/main/docs/design.md)、[r/VoxelGameDev: 线程化讨论](https://www.reddit.com/r/VoxelGameDev/comments/376vmv/how_do_you_implement_threading_in_your_game/)、[Unity Job System 体素实践](https://medium.com/@adamy1558/building-a-high-performance-voxel-engine-in-unity-a-step-by-step-guide-part-5-advanced-chunk-7060b4b6275c)。

- **主线程**：游戏 tick（逻辑）、输入、渲染提交、任务调度与结果应用；**不**做重 CPU 工作。
- **Worker 线程池**（`核心数 − 1~2`）：地形生成、光照传播、网格化、区块序列化。每个任务输入是**不可变快照**（生成任务的输入只有种子与坐标，天然无共享；网格化/光照任务的输入需要区块数据 + 邻区数据的只读快照）。
- **同步策略**：
  - 首选"拷贝输入快照 + 单向消息队列回传结果"，主线程在帧末统一采纳结果（swap 而非锁）；这是实践中最不易出 bug 的模式。
  - 读写锁方案（读者多写者少）也可行但容易死锁/优先级反转，且区块数据内部需细粒度分锁。
  - 采纳结果时做版本校验：任务执行期间区块被修改（如玩家编辑）则任务作废重排。
- **每帧预算**：主线程每帧最多采纳/上传 N 个网格（时间预算制，如 2–4 ms），保证交互流畅；队列积压时主动降低视距优先级。
- 序列化（存盘）走独立低优先级队列，避免与渲染抢 IO/带宽。

---

## 5. 世界存储与区块压缩

### 5.1 区域文件（Region File）模式

- Minecraft 的公开格式是最佳参照：一个 `.mca`（Anvil）区域文件存 32×32 个区块，8 KB 头部（4 KB 位置偏移表 + 4 KB 时间戳表），数据按 4 KB 扇区存放，每个区块前有长度 + 压缩类型字节（GZip/Zlib），负载为压缩后的 NBT：
  - [Region file format — Minecraft Wiki](https://minecraft.wiki/w/Region_file_format)
  - 实现讨论：[GameDev SE: Minecraft chunk decompression](https://gamedev.stackexchange.com/questions/129164/minecraft-chunk-file-decompresssion)、[SO: region 数据未存未压缩长度的问题](https://stackoverflow.com/questions/65280078/is-minecraft-missing-zlib-uncompressed-size-in-its-chunk-region-data)
- 教训：
  - **一定要在块头存未压缩长度**（Minecraft 没存，读端要按 1 MB 上限盲读）；
  - 区域文件需要**扇区分配表**，删除/改写区块会产生空洞，需要碎片整理或就地重写策略；
  - 文件内偏移用 3 字节 + 1 字节扇区数即可（Minecraft 方案），单区域上限 256 MB，足够。

### 5.2 压缩算法：zstd 优先

- [facebook/zstd](https://github.com/facebook/zstd)：压缩/解压速度远优于 zlib， ratio 相近或更好，自带 CMake 工程；流式 API、字典训练（对同类区块可用预训练字典提升小负载压缩率）。
- 方案建议：区块序列化后先做**简单变换**（如按 Y 列存储 + RLE 空气段跳过，或调色板化 palette，即 Minecraft 1.13+ 的做法思路）再 zstd；磁盘与网络可用同一序列化格式，一次实现两处受益。网络传输可以考虑更低压缩级别（zstd level 1–3）换取延迟。
- lz4 也可作为"更快但略大"的备选，用于热数据（未保存的编辑日志）。

### 5.3 脏区块回写策略

- 脏标记 + **定时批量回写**（Minecraft 是每 45 秒左右自动保存一批；Paper 等服务端实现异步存盘）：
  - 维护脏区块集合，每隔固定 tick 取一批（数量/字节预算限制）交给 IO 线程写盘；退出/世界卸载时强制 flush。
  - 写入用"临时文件 + 原子替换"或区域文件内追加新副本 + 更新偏移表，避免崩溃时写坏数据；定期做区域文件 GC（碎片整理）。
  - 编辑日志（write-ahead log）是更高级的选项：先追加日志保证持久性，后台再合并进区域文件。

---

## 6. 碰撞与物理

### 6.1 AABB 体素碰撞

- 实体用轴对齐包围盒（AABB，玩家约 0.6×1.8×0.6），地形是隐式网格：碰撞查询 = 遍历 AABB 覆盖的方块坐标，查方块是否实体。
- **逐轴分离（per-axis resolution）**是最常用的稳定方案：先动 Y 解决站立/落地，再动 X、再动 Z，每轴分别夹紧到方块边界并清零该轴速度——自动获得"贴墙滑动"行为。参考：
  - [GameDev SE: AABB swept collision response with voxel world](https://gamedev.stackexchange.com/questions/88298/aabb-swept-collision-response-with-voxel-world)（直接讨论贴墙滑动与边角卡住问题）
  - [Let's Make a Voxel Engine — CCD (Swept AABB)](https://sites.google.com/site/letsmakeavoxelengine/home/collision-detection)
  - [GameDev.net: Swept AABB Detection and Response](https://gamedev.net/tutorials/programming/general-and-gameplay-programming/swept-aabb-collision-detection-and-response-r3084)
  - [Minkowski 和视角的 swept AABB 讲解](https://emanueleferonato.com/2021/10/21/understanding-physics-continuous-collision-detection-using-swept-aabb-method-and-minkowski-sum/)

### 6.2 Swept / 连续碰撞（防穿透）

- 高速实体（箭、掉落物、疾跑玩家在低帧率下）一帧位移可能超过一个方块，离散检测会穿墙。两种方案：
  1. **子步（substep）**：把一步位移切成多个 ≤0.5 方块的小步，简单、稳定、够用（Minecraft 实体逻辑即多次小步推进的思路）；
  2. **swept AABB**：把运动扫掠体与方块求交，取最早撞击时间 t，解析求出撞击点与法线。更精确但要小心边角（edge snagging）问题——GameDev SE 帖中的"卡在方块边缘"即典型。
- 推荐：玩家用子步 + 逐轴；弹道类投射物用 swept AABB 或射线（raycast/voxel DDA，如 Amanatides & Woo 算法）。

### 6.3 实体-方块交互

- 方块碰撞体积应数据驱动（完整方块 / 半砖 / 楼梯 / 交叉植物无碰撞），用查询接口而非硬编码；交互（踩踏压力板、掉入仙人掌伤害）在物理步后做重叠查询。
- 物理步必须与渲染解耦：固定物理步长（如 20 Hz 或 60 Hz）+ 渲染插值（见 §9、§12）。

---

## 7. 渲染 API 选型

调研来源：[r/gamedev: Vulkan on macOS 讨论](https://www.reddit.com/r/gamedev/comments/1auq3k7/vulkan_game_development_in_macos_targeting/)、[GameFromScratch: 跨平台 OpenGL 替代](https://gamefromscratch.com/cross-platform-opengl-alternatives/)、[MoltenVK 官网](https://moltengl.com/moltenvk/)、[LunarG: The State of Vulkan on Apple (2026-01)](https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/)、[HN: MoltenVK 成为默认跨平台 GPU API 的讨论](https://news.ycombinator.com/item?id=29311243)。

| 选项 | 跨平台 | macOS 支持 | 开发效率 | 性能上限 |
|---|---|---|---|---|
| OpenGL 4.x | Win/Linux/macOS | **冻结在 4.1**（Apple 已弃用），新特性缺失 | 高 | 中 |
| Vulkan (+MoltenVK) | Win/Linux/macOS/iOS | 经 MoltenVK 映射到 Metal，成熟但非原生 | 低（样板代码量大） | 高 |
| DirectX 12 | 仅 Windows | 无 | 低 | 高 |
| Metal | Apple 专属 | 原生 | 中 | 高 |

- 结论与建议：
  - **推荐路径：OpenGL 4.3 核心（核心 profile）起步 + 渲染抽象层（RHI）**。理由：体素游戏瓶颈通常在 CPU 侧网格化/光照而非 GPU API；OpenGL 让单人/小团队最快达到可玩；macOS 4.1 冻结意味着抽象层从一开始就要隔离 SSBO（4.3 特性，macOS 不可用）等用法，用 UBO + 纹理即可满足需求。
  - 抽象层保留 Vulkan/D3D12 后端的可能性（社区主流建议也是"先 OpenGL，后 Vulkan"）；若未来确定要 Vulkan，macOS 走 MoltenVK 是标准答案。
  - 不建议一开始就 Vulkan：对体素游戏而言收益滞后，而开发成本立刻兑现。

---

## 8. 开源同类项目经验教训

> 原则：只研究公开文档/开发日志/社区讨论，学习架构经验，不复制代码。

### 8.1 Minetest / Luanti（C++）

- 架构文档：[Engine Structure — Luanti docs](https://docs.luanti.org/for-engine-devs/structure/)、[Basic Data Structures](https://docs.luanti.org/for-engine-devs/structure/)（实际路径见 docs.luanti.org）、[术语表](https://docs.luanti.org/about/glossary/)、[Lua Modding API（lua_api.md）](https://github.com/luanti-org/luanti/blob/master/doc/lua_api.md)、[API 门户](https://api.luanti.org/)、[社区模组书](https://rubenwardy.com/minetest_modding_book/)。
- 关键架构事实：
  - 三大组件：Server（逻辑权威）/ Client（渲染与输入）/ Lua API（模组层）；地图是 **MapBlock（16³ 节点）** 的容器，含静态对象与元数据；**网格更新在客户端做**（服务端只发方块数据）。
  - 服务端为权威：方块修改在服务端验证后广播；模组主要在服务端跑，客户端脚本 API 长期标注"不稳定"——**模组 API 的稳定性承诺要分层**（服务端稳定、客户端实验性），这是 Luanti 路线图的公开教训（[论坛讨论](https://forum.luanti.org/viewtopic.php?t=29907)：MapBlock 传输并非瓶颈，16 KB/块原始数据）。
- 值得学习的经验：
  - **MapBlock 是一切的原语**（生成、光照、网络、存盘、模组挂接都以它为单位）——API 设计上"区块是一等公民"极大简化了心智模型；
  - 模组 API 走**数据驱动 + 回调注册**（`on_construct`/`on_step` 等生命周期钩子），引擎不认识任何具体游戏内容；
  - 教训：Lua 单线程、每节点回调开销大，热点逻辑应留在 C++ 引擎侧并给模组提供批处理 API。

### 8.2 Veloren（Rust）

- 架构文档：[Project Architecture — Veloren Owner's Manual](https://book.veloren.net/contributors/developers/codebase-structure.html)、[ECS 篇](https://book.veloren.net/contributors/developers/ecs.html)、[CHANGELOG](https://github.com/veloren/veloren/blob/master/CHANGELOG.md)、[devblog 127（TCP_NODELAY）](https://veloren.net/blog/devblog-127)、[devblog 178（区块生成耗时）](https://veloren.net/blog/devblog-178)、[区块加载社区讨论](https://www.reddit.com/r/Veloren/comments/graed1/chunk_loading/)。
- 关键经验：
  - **服务端权威 + 多线程 ECS**（specs 衍生的并行系统，rayon 并行）；历史上客户端自算物理（响应快），后来逐步转向可选的**服务端物理**防作弊——启示：早期"信任客户端"的技术债后期偿还成本高，OpenCraft 应从第一天就做服务端权威 + 客户端预测。
  - **区块网络传输压缩**：地形 chunk 传输先做有损量化（lossy quantization）再压缩，带宽显著下降；
  - 关闭 Nagle 算法（TCP_NODELAY）对输入/移动平滑度影响巨大；Veloren 后期也在探索 UDP 方向——启示：长连接游戏协议不要裸用默认 TCP 行为。
  - 河流/道路等"区块间结构"由每个 chunk 的中心高度与河网先算再落图——启示：跨区块结构需要独立于区块边界的全局参数化。

### 8.3 Terasology（Java）

- 资料：[GitHub 主仓库](https://github.com/movingblocks/terasology)、[官网](https://terasology.org/)、[拆分引擎为子系统 issue #4304](https://github.com/MovingBlocks/Terasology/issues/4304)、[实体系统教程 #2153](https://github.com/MovingBlocks/Terasology/issues/2153)。
- 关键经验：
  - **强模块化**：引擎（core）与内容（module）彻底分离，模块以 JAR 形式声明依赖（module.txt），并设 API 分层白名单（Core/Engine/World 等层级，低层禁止依赖高层）——启示：OpenCraft 的模组/内容边界应从代码结构层面强制（CMake target 边界 + 依赖方向检查），而不是靠自觉。
  - 教训：Terasology 以"引擎平台"为目标导致范围蔓延、内容完成度长期不高——**OpenCraft 应以"先做出好玩完整的游戏"为主轴**，引擎通用性让位于游戏需求。

---

## 9. Minecraft Java 公开性能特性

来源：[SpigotMC: Why is chunk sending so costly](https://www.spigotmc.org/threads/why-is-chunk-sending-so-costly.481735/)、[TPS/MSPT 诊断](https://www.sparkanalyzer.io/blog/fix-minecraft-server-lag)、[GameServerKings tick 讲解](https://www.gameserverkings.com/knowledge-base/minecraft/diagnosing-lag-and-low-tps/)、[r/feedthebeast 加载区块掉 TPS 案例](https://www.reddit.com/r/feedthebeast/comments/b7cr9c/is_it_normal_to_have_insane_tick_lag_when_someone/)。

- **客户端/服务端分离**： Dedicated server 与客户端共享世界模型但职责不同；服务端跑逻辑，客户端只做表现。
- **tick 与渲染解耦**：服务端固定 20 TPS（每 tick 50 ms 预算，MSPT 是核心指标）；客户端按自己帧率渲染，用**部分 tick（partial tick）插值**在两个逻辑状态间平滑。这是必须照抄（概念上）的架构：**逻辑固定步长 + 渲染可变帧率 + 插值**。
- **区块发送是著名瓶颈**：区块序列化（调色板压缩）+ 网络发送历史上占用主 tick 线程时间，多玩家同时加载区块会明显掉 TPS；现代服务端（Paper 等）的解法是**异步区块发送/异步序列化**——OpenCraft 从第一天就把"区块网络发送"设计为异步任务。
- 光照更新、方块实体（block entity）tick、实体 AI 都在 tick 循环内，任何 O(区块数) 的每 tick 全量扫描都是雷；一切热路径都要做成脏标记 + 增量。

---

## 10. C++ 生态选型清单

来源：[skypjack/entt](https://github.com/skypjack/entt)、[EnTT in Action 列表](https://github.com/skypjack/entt/wiki/EnTT-in-Action/08e9b948d294c768a4e0d80cd9c1bd10ab85fb1f)、[GLFW vs SDL3 讨论](https://www.reddit.com/r/gameenginedevs/comments/1j75qo0/sdl3_or_glfw_a_bunch_of_other_stuff/)、[facebook/zstd](https://github.com/facebook/zstd)、[体素引擎从零搭建系列（vcpkg + C++）](https://fschoenberger.dev/voxel-game/01-project-setup/)、[Noel Berry: Making games in 2025（SDL3 实战）](https://noelberry.ca/posts/making_games_in_2025/)、[FastNoiseLite](https://github.com/Auburn/FastNoiseLite)。

| 领域 | 推荐库 | 理由 | 备选 |
|---|---|---|---|
| 窗口/输入/GL 上下文 | **GLFW** | 轻量、与 OpenGL/Vulkan 搭配是事实标准 | SDL3（若想要手柄/音频一站式） |
| 数学 | **glm** | GLSL 风格、OpenGL 生态标配 | eigen（更重） |
| ECS | **EnTT** | 头文件即用、MIT、社区第一推荐、大规模实战（含 Minecraft 衍生项目） | Flecs（C，带查询语言） |
| 脚本嵌入 | **Lua (sol2/LuaBridge 绑定)** | Luanti 验证过的模组生态路径，VM 简单、沙箱成熟 | AngelScript（强类型、编译型，引擎界也常用，但模组生态小） |
| 压缩 | **zstd** | 速度/压缩比/字典支持全面胜出 | lz4（更快更大） |
| 噪声 | **FastNoiseLite** | OpenSimplex2 系 + fBm/域扭曲一站式，MIT | 自实现 OpenSimplex2 |
| 纹理/图集 | 简单自研 texture atlas 起步；KTX2/basisu 供未来 | 体素纹理小而规则，运行时拼图集足够 | stb_image 加载源图 |
| 音频 | **OpenAL Soft**（3D 定位）+ miniaudio（备选） | 原生 3D 音效 API，跨平台 | 定制混音器（后期若需 MIDI/复杂总线再上） |
| 网络 | 起步 TCP（自管理协议帧）或 **ENet**（可靠+不可靠通道的 UDP） | Minecraft 协议本身基于 TCP；ENet 给 UDP 可靠性分层 | 自定义 UDP + KCP 思路（后期） |
| 图像加载 | **stb_image** | 单头文件 | — |
| 日志/断言 | spdlog | 异步、格式化 | 自研 |
| 序列化（存档/协议） | 自研二进制（手写，带版本号）；配置用 JSON/toml | 协议要精确控制字节 | cbor/flatbuffers（复杂后用） |
| 测试 | **GoogleTest/Catch2 + 黄金文件快照测试** | 网格化/光照/世界生成输出做快照对比（golden file），防回归 | doctest |
| 构建 | **CMake + FetchContent/vcpkg** | 生态事实标准，zstd/EnTT/glm 均有一级支持 | meson |
| 性能分析 | Tracy（帧级 trace）、perf/Instruments | 体素引擎必须尽早接入 profiler | — |

- 快照（golden file）测试特别适用于：世界生成（固定种子 → 区块哈希）、光照传播结果、网格化顶点数统计——三者都是纯函数，极易做确定性回归测试。

---

## 11. ECS 在 C++ 中的适用性

来源：[EnTT 仓库](https://github.com/skypjack/entt)、[ECS in C++ with EnTT 入门](https://david-delassus.medium.com/a-short-introduction-to-entity-component-system-in-c-with-entt-330b7def345b)、[Veloren ECS 文档](https://book.veloren.net/contributors/developers/ecs.html)、[Object-Oriented ECS Design（voxely.net）](https://voxely.net/blog/object-oriented-entity-component-system-design/)。

- **实体（玩家、怪物、掉落物、箭、TNT）非常适合 ECS**：实体类型多、行为组合正交（可拾取 × 有碰撞 × 有光照发射……），EnTT 的 archetype 存储对这种组合爆炸友好；Minecraft 衍生工程使用 EnTT 的先例降低了风险。
- **方块世界本身不要塞进 ECS**：区块是巨大的连续数据（16³–16×384×16），应以专门的数据结构管理（稀疏哈希 + 连续数组），ECS 只管理"活动实体"；区块与实体的关联（如"实体位于哪个区块"）作为组件字段。Veloren 的做法一致：世界数据与 ECS 并行，系统按需读写。
- 模组 API 面向 ECS 暴露组件与事件，可以让模组获得接近引擎的扩展力，同时引擎保持数据驱动。
- 注意：EnTT view/group 的迭代顺序与删除实体的时机（deferred destroy）要在 tick 框架里明确，避免遍历中失效。

---

## 12. 网络架构

### 12.1 基本模型选择

来源：[Gambetta: Fast-Paced Multiplayer / Client-Side Prediction](https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html)、[Gaffer On Games: Snapshot Interpolation](https://gafferongames.com/post/snapshot_interpolation/) 与 [Snapshot Compression](https://gafferongames.com/post/snapshot_compression/)、[Unity Netcode: Interpolation（抖动缓冲）](https://docs.unity3d.com/Packages/com.unity.netcode@1.3/manual/interpolation.html)、[SnapNet: Netcode Architectures](https://snapnet.dev/blog/)、[Bedrock server-authoritative-movement 佐证](https://board.aternos.org/thread/63444-request-for-server-authoritative-block-breaking-settings-in-server-properties-us/)。

- **服务端权威 + 客户端预测 + 服务端和解（reconciliation）** 是现代默认（Veloren 的教训：后期补服务端权威代价大）：
  - 客户端本地立即模拟自己输入，同时把带**序号**的输入发给服务端；
  - 服务端按序应用输入模拟，广播快照；
  - 客户端收到权威状态后，若有偏差（设置**误差阈值**，避免 1ms 也硬拉导致 rubber-banding），回退到权威状态并**重放未确认输入**。
- **远程实体：快照插值**而非外推——渲染时间轴落后约 2 个快照间隔，用抖动缓冲吸收丢包/抖动（经验：缓冲足以连续丢 2 包）。注意已知坑：预测的本地玩家与插值的远程实体处于不同时间轴，二者间的碰撞判定要特别处理。

### 12.2 确定性与 tick

- 逻辑用**固定 tick**（如 20 TPS），禁止在逻辑里直接使用 deltaTime 与未播种 RNG；浮点在客户端/服务端间的非确定性（编译器、FMA、求和顺序）是重放偏差主源——预测重放只在**客户端本地**做（同一进程同一二进制），即可绕开跨机器浮点一致性问题；不要试图做跨机器确定性 lockstep（除非用定点数），那是另一个量级的工程。
- 区块状态（方块修改）不参与"重放"——方块变更是服务端权威的事件流，客户端收到 set-block 消息直接应用并对本地预测的放置/破坏做和解（预测成功则无感，被拒则回滚该格）。

### 12.3 区块同步与协议设计常见坑

- 区块发送必须**异步化**（§9 教训），并按玩家视距/优先级排序；区块数据带版本/序号，晚到的旧区块数据不得覆盖新区块。
- 方块修改用**增量消息**（坐标 + 新方块 ID）而非整块重发；但服务器要保证最终一致：客户端缺块/乱序时以服务器快照为准拉平。
- 压缩：区块负载走 zstd（低级别）；不要压缩小消息（头开销反而变大）。
- 常见坑清单：
  1. delta 压缩基线错误（基于未确认快照做 delta → 客户端解码错位，Gaffer 的 "compression walk-off"）；
  2. 忘记关 Nagle / 未做应用层分帧 → 黏包与 200ms 级延迟；
  3. 预测无阈值导致画面抖动；重放成本过高（物理复杂时限制重放窗口长度）；
  4. 区块边界消息与光照更新顺序不当 → 客户端短暂黑块（应保证光照数据随区块或紧随其后）；
  5. 无心跳/超时重连策略；无协议版本号导致新旧客户端混连崩溃。
- 传输层：起步可用 TCP（Minecraft Java 即 TCP + 分帧），ENet（可靠 UDP，双通道）是升级路径；无论哪种，协议都要**自带长度前缀分帧 + 版本字段 + 心跳**。

---

## 13. 结论：推荐架构决策汇总

1. **逻辑 20 TPS 固定步长 + 渲染可变帧率 + partial-tick 插值**，客户端/服务端同仓异进程，第一天就是服务端权威。
2. **区块 = 16×H×16 柱式唯一原语**，带显式状态机（生成→光照→网格化），邻区就绪门槛解决跨块依赖。
3. **网格化**：culled meshing 起步，AO 融入贪心合并键做贪心网格化；按面朝向分桶 + 顶点池；透明/半透明独立分层，区块级排序。
4. **光照**：sky/block 双 nibble 通道，显式队列 BFS（含暗度删除 BFS），高度图缓存，跨区块传播走延迟队列，光照变化统一汇入网格化调度去重。
5. **地形生成**：FastNoiseLite（OpenSimplex2/S）+ fBm + 域扭曲 + 连续气候场（温度/湿度 → Whittaker）参数化生物群系，生成器分层可缓存、可快照测试。
6. **线程模型**：主线程只做调度/采纳/GPU 上传（每帧预算制），worker 池跑生成/光照/网格化/序列化，任务输入用不可变快照、结果版本校验后采纳。
7. **存储**：区域文件（32×32 区块/文件，8KB 头 + 扇区分配）+ RLE/调色板预处理 + zstd，块头存未压缩长度；脏区块定时批量异步回写 + 原子替换防损坏。
8. **渲染 API**：OpenGL 4.3 核心起步，包一层 RHI 抽象为 Vulkan/MoltenVK 留后门；生态用 GLFW + glm + EnTT + sol2/Lua + zstd + spdlog + Tracy，CMake + FetchContent/vcpkg。
9. **网络**：TCP/ENet + 长度分帧 + 版本号，客户端预测 + 序号化输入重放和解（带误差阈值），远程实体快照插值（抖动缓冲），区块异步发送 + 增量方块消息。
10. **质量保障**：固定种子世界生成、光照、网格化做黄金文件快照测试；模组 API 分层（服务端稳定/客户端实验），引擎与内容以 CMake target 强制隔离。

---

### 主要来源索引

- 0fps 系列：[Meshing](https://0fps.net/2012/06/30/meshing-in-a-minecraft-game/) / [Meshing Part 2](https://0fps.net/2012/07/07/meshing-minecraft-part-2/) / [Voxel AO](https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/)
- Minecraft Wiki：[Light](https://minecraft.wiki/w/Light) / [Region file format](https://minecraft.wiki/w/Region_file_format)
- Luanti：[docs.luanti.org](https://docs.luanti.org/) / [lua_api.md](https://github.com/luanti-org/luanti/blob/master/doc/lua_api.md)
- Veloren：[book.veloren.net](https://book.veloren.net/contributors/developers/codebase-structure.html) / [devblog 127](https://veloren.net/blog/devblog-127)
- Terasology：[terasology.org](https://terasology.org/) / [GitHub](https://github.com/movingblocks/terasology)
- 网络：[Gambetta](https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html) / [Gaffer On Games](https://gafferongames.com/post/snapshot_compression/) / [Unity Netcode](https://docs.unity3d.com/Packages/com.unity.netcode@1.3/manual/interpolation.html)
- 噪声：[OpenSimplex2](https://github.com/KdotJPG/OpenSimplex2) / [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) / [IQ Domain Warping](https://iquilezles.org/articles/warp/)
- 渲染选型：[MoltenVK](https://moltengl.com/moltenvk/) / [LunarG Vulkan-on-Apple](https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/)
- C++ 生态：[EnTT](https://github.com/skypjack/entt) / [zstd](https://github.com/facebook/zstd) / [GLFW vs SDL3](https://www.reddit.com/r/gameenginedevs/comments/1j75qo0/sdl3_or_glfw_a_bunch_of_other_stuff/)
- 碰撞：[GameDev SE swept AABB voxel](https://gamedev.stackexchange.com/questions/88298/aabb-swept-collision-response-with-voxel-world) / [Let's Make a Voxel Engine](https://sites.google.com/site/letsmakeavoxelengine/home/collision-detection)
