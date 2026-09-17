# assets/CREDITS.md —— 美术资产证据链

**每一条资产一行，字段固定，不许省列。** AI 生成不豁免留痕：正因为它更容易产出
接近原版的东西，才更该写清楚来源、工具、提示词和人工后处理。

规则与红线见 `assets/README.md` §5（引述自 `docs/05-development-process.md` §2 与
`docs/04-legal-compliance.md`）。

## 格式

| 文件 | 来源 | 工具 | 提示词摘要 | 日期 | 人工后处理 |
|---|---|---|---|---|---|
| blocks/dirt_side.png | 自制（AI 辅助） | <工具名> | "low-saturation brown 16x16 pixel dirt" | 2026-09-18 | 量化到 16×16 + 限色板 |

- **文件**：相对 `assets/` 的路径，如 `blocks/dirt_side.png`。
- **来源**：`自制` / `自制（AI 辅助）` / `公共领域（链接）` / `CC0（链接）` 之一，
  外部来源必须给可复核的链接。
- **工具**：具体到模型或软件名；手绘写软件名。
- **提示词摘要**：实际使用的提示词（可精简，但不得改写含义）；手绘写"无"。
- **日期**：落盘日期 `YYYY-MM-DD`。
- **人工后处理**：调色板、尺寸量化、手工修补等都写出来；没有就写"无"。

## 条目

<!-- 下方开始逐条登记；示例行在格式表里，不要留在条目区。 -->

| 文件 | 来源 | 工具 | 提示词摘要 | 日期 | 人工后处理 |
|---|---|---|---|---|---|
| blocks/grass_block_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 草面：F3 主色 + F2/F4 两级受光散点簇，1–2 px 簇，无描边 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/grass_block_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 侧面：上部 3–5 px 参差草边（逐列手写深度）+ 下部泥土颗粒 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/grass_block_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 底面：纯泥土颗粒，稿源同 dirt_bottom（§5 可复用约定） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/dirt_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 泥土颗粒：E1 主色 + E0/E2 散点簇，三面各自独立手稿 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/dirt_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 泥土颗粒（侧）：E1 主色 + E0/E2 散点簇 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/dirt_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 泥土颗粒（底）：E1 主色 + E0/E2 散点簇 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/stone_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石面：S2 主色 + S0/S1 暗斑 + S3 高光斑，斑块 1–3 px | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/stone_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石面（侧）：S2 主色 + S0/S1 暗斑 + S3 高光斑 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/stone_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石面（底）：S2 主色 + S0/S1 暗斑 + S3 高光斑 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/sand_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 沙：E4 主色 + E3 阴影带，1–2 px 散点簇 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/sand_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 沙（侧）：E4 主色 + E3 阴影带 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/sand_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 沙（底）：E4 主色 + E3 阴影带 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/log_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 年轮：自外圈 W0 树皮向内 W2→W1 同心环 + 中心 W0 髓心 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/log_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 树皮竖纹：W0/W1/W2 三档竖条 + 一处节疤（第 5–6 行） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/log_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 年轮 + 一道自中心向右贯通的径向裂（第 7–8 行） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/leaves_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 叶丛：F1 主色 + F0 暗 / F2 亮，2–3 px 不规则簇；三面同源不同行序 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/leaves_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 叶丛（侧）：同上母稿，手写行序重排（§5 均质类约定） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/leaves_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 叶丛（底）：同上母稿，手写行序重排 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/planks_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 木板横排四块：W3 主色 + W2 受光纹，第 3/7/11/15 行为 W0 板缝 + 竖向端接头 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/planks_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 木板竖排四块：第 3/7/11/15 列为 W0 板缝，横向端接头错缝（第 5/11 行） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/planks_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 木板横排四块：同 top 母稿，端接头位置错开 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/water_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 水面：A1 主色 + A2 斜向波光带（α 170/190），逐行位移 4 px | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/water_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 水体侧壁：近水平分层色带（A2 亮带 + A1 主体） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/water_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 水体底：A1 主体 + 稀疏 A2 短亮带 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/glass_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 玻璃：G0 四边框 + G1 面（α 44）+ 一条 45° 斜向 2 px 高光 G1h（α 120） | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/glass_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 玻璃（侧）：同上，斜向高光方向相反 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/glass_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 玻璃（底）：同框同面，右上角一段斜向高光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/coal_ore_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_top 手稿）+ 煤矿点 2×2/2×1 簇（逐面手写坐标表）+ 哑光：无高光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/coal_ore_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_side 手稿）+ 煤矿点 2×2/2×1 簇（逐面手写坐标表）+ 哑光：无高光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/coal_ore_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_bottom 手稿）+ 煤矿点 2×2/2×1 簇（逐面手写坐标表）+ 哑光：无高光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/iron_ore_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_top 手稿）+ 铁矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/iron_ore_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_side 手稿）+ 铁矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/iron_ore_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_bottom 手稿）+ 铁矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/gold_ore_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_top 手稿）+ 金矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/gold_ore_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_side 手稿）+ 金矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/gold_ore_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_bottom 手稿）+ 金矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/diamond_ore_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_top 手稿）+ 钻石矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/diamond_ore_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_side 手稿）+ 钻石矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/diamond_ore_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_bottom 手稿）+ 钻石矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/copper_ore_top.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_top 手稿）+ 铜矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/copper_ore_side.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_side 手稿）+ 铜矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
| blocks/copper_ore_bottom.png | 自制 | 手绘（字符网格稿 art_source.py）+ Python 3 标准库 zlib 手写 PNG 编码器；未用 AI、未用图像编辑软件 | 石底（同 stone_bottom 手稿）+ 铜矿点 2×2/2×1 簇（逐面手写坐标表）+ 每面 1 px S3 反光 | 2026-09-18 | 逐格手写 16×16 字符网格稿（docs/qa/T-A3-2026-09-18/tools/art_source.py）；量化到限定色板 32 色；无缩放/无抖动/无抗锯齿 |
