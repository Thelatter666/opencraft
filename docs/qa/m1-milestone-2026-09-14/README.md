# M1 里程碑验收截图（2026-09-14，PM 主工作区构建）

- 构建：主工作区 `/Users/happy/Desktop/opencraft`，main 分支 HEAD（merge T009 后），
  命令 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel`
  → configure/build exit 0，**零编译器警告**（唯一 1 行为既有的
  `ld: ignoring duplicate dependencies` 环境噪声）；`ctest` → **145/145 通过**。
- 运行：`./build/opencraft`，日志确认 `new world with seed 0x4f50454e43524146`、
  `spawn scan: surface at (0.5, 132.0, 0.5)`、`startup gen: 24 chunks, avg 9.02 ms/chunk`、
  `atlas: 144x144 px, 9 tiles/row, crack tiles at 63`。

## 截图内容（单张同时覆盖 M1 全部子系统）

| 可见元素 | 对应任务 |
|---|---|
| 真实生成地形（起伏草坡、水塘、洞穴壁、树） | T004 地形生成 |
| 面朝向明暗 + 区块网格化 | T005 culled meshing |
| 光照梯度（坡面明暗过渡、水下半透明） | T006 双通道光照 |
| 准星 + 9 格快捷栏（选中格白框）+ 方块名 "STONE" + 10 颗满心 | T009 HUD |
| 右下角手持方块模型（挥手动画静止位） | T009 挖掘反馈 |
| 第一人称视角 | T007 物理 + T008 控制器 |

- 文件：`m1_main_playtest.png`（窗口截图 `screencapture -x -l<windowID>`）
- 用途：M1 里程碑"可运行"侧的客观记录；**手感清单（`docs/01-gameplay-spec.md` §8）
  仍需真人主观验收**，非本截图可替代。
