# OpenCraft 法律与知识产权合规调研笔记

> 项目背景：OpenCraft 是一个原创代码与原创/合规资产的体素沙盒游戏，玩法机制对齐 Minecraft Java Edition，
> 但不复制 Minecraft 的代码、纹理、音效、名称与商标。
>
> **免责声明：本文仅为开发团队内部的调研笔记，由非法律专业人员整理，不构成法律意见。**
> 在做出重大商业决策（公开发布、商业化、命名定稿）之前，请咨询执业律师（尤其是熟悉
> 知识产权法与游戏行业的律师）。本文中的结论均为基于公开资料的初步理解，可能随司法
> 实践、平台政策、微软/Mojang 政策变化而失效。调研日期：2026-09。

---

## 目录

1. 玩法机制 / 游戏规则的版权保护边界
2. Mojang / Microsoft 的公开立场（EULA 与 Usage Guidelines）
3. 商标层面的高风险行为（名称含 "craft"、宣传用语等）
4. 资产层面合规：什么构成"复制"、CC0 来源、字体与音效
5. 文档与数据来源合规：Minecraft Wiki、MCP 映射数据
6. 开源许可选择建议
7. 红线清单（绝对不要做的事）
8. 安全做法清单（推荐做法）
9. 来源汇总

---

## 1. 玩法机制 / 游戏规则的版权保护边界

### 1.1 基本原则：思想/表达二分法（Idea–Expression Dichotomy）

美国《版权法》§102(b) 与中国《著作权法实施条例》第 2 条、以及 TRIPS 第 9(2) 条
（"版权保护应延及表达，但不延及思想、程序、操作方法或数理概念本身"）都确立了同一条
原则：

- **玩法规则、机制、算法、数值系统属于"思想"（idea）层面，不受版权保护。**
- **思想的具体"表达"（expression）——美术、音效、文字、代码、UI 布局、角色形象、
  具体的视听呈现——受版权保护。**

这意味着：像 Minecraft 一样"用方块搭建世界、合成工具、生存挖掘"这件事本身，
任何人都可以做；但 Minecraft 的具体纹理、音效、代码、文案、Logo 不可以复制。

### 1.2 经典案例一：Tetris Holding, LLC v. Xio Interactive, Inc.（D.N.J. 2012）

- 判例号：863 F. Supp. 2d 394（美国新泽西联邦地区法院，2012）。
- 案情：Xio 开发了手机游戏 "Mino"，刻意借鉴俄罗斯方块的玩法，自称"只复制了
  不受保护的玩法元素"。Tetris Holding 起诉。
- 法院的核心结论：
  1. **俄罗斯方块的"玩法规则"（下落的多联骨牌、消除整行等）确实是不受保护的
     思想/功能**——法院承认这一点；
  2. 但是，**在实现这些玩法时有大量可选的设计空间，因此合并原则（merger
     doctrine）和场景原则（scène à faire）抗辩不成立**：当一个思想有很多种
     表达方式时，选择其中一种具体表达仍然受保护；
  3. Xio 复制的具体表达——**方块形状与尺寸、游戏区尺寸、颜色方案、整体
     "look and feel"（外观与感受）——构成实质性相似，侵权成立**。
- 判决原文可参考：[Justia 判决全文](https://law.justia.com/cases/federal/district-courts/new-jersey/njdce/3:2009cv06115/235418/61/)、
  [Wikipedia 条目](https://en.wikipedia.org/wiki/Tetris_Holding,_LLC_v._Xio_Interactive,_Inc.)、
  [vLex 全文](https://case-law.vlex.com/vid/tetris-holding-llc-v-890864419)。

**对 OpenCraft 的启示：** 玩法可以借鉴，但不要在"具体表达"层面逐项对齐。
Tetris 案的警示在于：即使是"功能性的"视觉元素（方块尺寸、棋盘尺寸、配色），
只要存在多种设计可能而你选择了与原作几乎一致的那种，法院可能认定你复制了
"表达"。对体素游戏而言：方块世界的概念没问题，但方块默认配色的具体色值、
HUD 布局、GUI 视觉风格、方块/物品的图标设计都应当是独立设计，而不是"刻意
做成一模一样"。

### 1.3 相关美国案例（补充背景）

- **Da Vinci Editrice v. Ziko Games（W.D. Tex. 2020）**：party 游戏基于"文字卡牌 +
  表达"的组合，法院区分了玩法规则与具体的卡牌表达。
- **Spry Fox v. Booyah（N.D. Cal. 2012）**："Triple Town" 的游戏规则与其视觉呈现
  的**组合**受保护；被告的"丛林跳跃"版本被判实质相似侵权——警示"换个皮"
  并不自动安全。
- **Arknights / 数值与关卡**：美国判例倾向于认为关卡设计（level design）的具体
  编排可以构成受保护的表达。

### 1.4 中国司法实践：换皮案

中国法院同样以思想/表达二分法为主线，但对"玩法规则的呈现"有更细的处理：

- **《炉石传说》诉《卧龙传说》案（上海一中院，2014）**：法院明确认为，原告主张的
  卡牌与套牌组合"实质是游戏的规则和玩法"，属于思想范畴，**不受著作权法保护**；
  但对具体卡牌的**文字说明（表达）**给予了保护。参见
  [对电子游戏"换皮"问题的法律思考（中国知识产权律师网）](https://www.ciplawyer.cn/articles/140198.html)。
- **《太极熊猫》诉《花千骨》案（苏州中院，2016）**：法院首次以"换皮"方式认定
  侵权——游戏玩法规则的具体设计（数值、界面布局、玩法衔接的**具体呈现**）被认定
  为可以构成表达，整体比对后认定侵权。这是"玩法规则的具体编排与呈现可能构成
  表达"的代表性判决。
- **司法演变的三个阶段**（参见
  [天同律师事务所：换皮抄袭规制视角下网络游戏规则可版权性探析](https://www.tiantonglaw.com/Content/2022/09-15/1959215747.html)：
  1. 早期："著作权拆分保护"——只保护代码、美术、音乐等单独元素；
  2. 中期：整体视听作品保护 + 换皮认定（《太极熊猫》案为代表）；
  3. 当前：著作权保护不足时，辅以**《反不正当竞争法》**第二条（诚信原则/商业道德）
     规制明显搭便车、混淆行为。
- 学界警示（参见
  [知产财经：游戏规则可版权性再审视](https://www.ipeconomy.cn/yuanchuang/8449.html)）：
  如果把玩法规则本身当成作品保护，会导致"对思想的过度垄断"，这是法院反复
  克制的方向。

**对 OpenCraft 的启示（中国视角）：**
- 纯粹的玩法机制（挖方块、合成、红石逻辑等概念）= 思想，可以自由实现；
- 但**数值体系 + 界面呈现 + 玩法衔接**如果整套照搬、只换美术，在中国司法实践中
  有被认定为"换皮侵权"的风险（《太极熊猫》案路径），即便著作权不成立，
  也可能落入反不正当竞争法的规制；
- 结论：玩法机制对齐可以，**整体呈现应当有明显的独立设计**，避免"逐帧复刻"的观感。

### 1.5 小结：玩法可借鉴、表达不可复制的边界

| 层面 | 是否受版权保护 | OpenCraft 对策 |
| --- | --- | --- |
| 核心玩法概念（体素世界、挖掘、合成、生存） | 否（思想） | 可以对齐 |
| 机制的具体数值（如挖掘时长公式） | 一般否，但整套编排可能被视为表达/反法风险 | 独立推导数值，不逐项照抄 |
| 视觉表达（纹理、模型、GUI、Logo、配色） | 是 | 全部原创，风格可"像素/低多边形"但细节独立 |
| 代码 | 是 | 全部原创，不反编译 MC |
| 音效/音乐 | 是 | 自制或 CC0 |
| 名称、Logo、宣传语 | 商标/反不正当竞争 | 避免混淆（见 §3） |

---

## 2. Mojang / Microsoft 的公开立场

### 2.1 官方文档

- **Minecraft EULA**：<https://www.minecraft.net/en-us/eula>
- **Usage Guidelines for Fans and Creators**：<https://www.minecraft.net/en-us/usage-guidelines>
- **EULA 与商用指南更新公告**：<https://www.minecraft.net/en-us/article/minecraft-eula-and-commercial-usage-guidelines-updates>

### 2.2 EULA 的适用范围：管的是"我们的玩家"，不是"你的游戏"

关键理解：EULA 是 Mojang 与**其游戏的玩家**之间的合同。它约束的是"你作为
Minecraft 玩家/服务器运营者/内容创作者"能对 Minecraft 做什么。它**管不到一个
从零独立开发、不使用 Minecraft 资产的第三方游戏**——OpenCraft 根本不是 Minecraft
的衍生作品，也不是 EULA 的当事方。

因此，Mojang 官方文档中没有任何条款可以禁止"市场上存在一个玩法类似的独立游戏"。
美国 GameDev StackExchange 上对
["Am I allowed to make my Minecraft clone open source?"](https://gamedev.stackexchange.com/questions/22247/am-i-allowed-to-make-my-minecraft-clone-open-source)
的共识回答也是如此：**玩法克隆合法，复制资产/名称/品牌不合法。**

### 2.3 Usage Guidelines 中与 OpenCraft 相关的要点

从官方 Usage Guidelines（2023 年起生效的版本）提炼：

- **"Assets" 的定义**："the code, software, graphics, textures, images, models,
  sounds and other audio from any of our games and any videos or screenshots
  taken of our games"。即：代码、软件、图形、纹理、图像、模型、音效及其他音频、
  以及游戏视频/截图，统统算"资产"。
- **对资产的限制**：不得从游戏中提取（extract）资产并在其他项目中使用；不得
  用 Minecraft 资产制作并分发独立作品；仅限在 Minecraft 生态内（模组、资源包等）
  的特定例外。
- **对名称的限制**：不得把 "Minecraft" 用作你产品的主要/主导名称或标题；官方
  给的正确格式是"你的名字在前，Minecraft 作为描述性后缀"，例如
  *"Kotoba Miners: A Minecraft Server"*。
- **不得暗示官方性**：不得使用 Minecraft 商标、Logo 以暗示官方背景或合作；
  需要明确标注"Not an official Minecraft product / not affiliated with Mojang"。
- **不得在营销中把 Minecraft 品牌/资产当成自己的卖点**：营销素材不得以官方
  资产作为主要视觉元素。
- **付费限制**（主要针对服务器/Marketplace，与 OpenCraft 关系不大，但了解）：
  禁止 pay-to-win、play-to-earn、NFT 等。

### 2.4 Mojang "管不了什么、禁什么" 的对照表

| 行为 | Mojang 能否禁止 | 依据 |
| --- | --- | --- |
| 独立实现体素沙盒玩法 | 不能（玩法不受版权保护，且不在 EULA 管辖内） | 版权法思想/表达二分 |
| 复制 MC 纹理/音效/代码 | 能（且这是版权侵权，不只违约） | Usage Guidelines "Assets" + 版权法 |
| 用 "Minecraft" 做游戏主标题 | 能（商标侵权/不正当竞争） | 商标法 |
| 用 "Minecraft" 做二级描述（"inspired by"） | 通常被官方允许（非主标题、有免责声明） | Usage Guidelines 命名规则 |
| 声称"官方"或暗示 Mojang 背书 | 能（商标/虚假宣传） | Usage Guidelines |
| 用 MC 截图/视频做宣传素材 | 被禁止（且对独立游戏毫无必要） | Usage Guidelines |

---

## 3. 商标层面的高风险行为

### 3.1 "craft" 后缀问题

- Mojang（及微软）**并不拥有 "craft" 这个通用后缀**。前有暴雪的 WarCraft /
  StarCraft（"craft" 用在游戏名里早于 Minecraft 十余年），后有大量含 "craft"
  的游戏与工具（GameDev、KanbanCraft、Craft Docs 等），从未见 Mojang 对后缀
  主张权利。搜索也未发现 Mojang 对 Minetest（现名 Luanti）、Terasology 等
  开源类 MC 项目发出过基于 "-craft" 的律师函。
- 但商标侵权的判断标准是**消费者混淆可能性**：名称的整体读音、外观、含义、
  商品关联度。一个叫 "Minecraftia"、"MineCraft 2"、"MCraft" 的名字显然危险；
  而一个读音、含义、视觉都与 Minecraft 相去甚远的名字风险低得多。
- **实操建议**：
  - "OpenCraft" 一词中 "craft" 是通用词素，"Open" 与 "Mine" 差异明显，整体上
    与 "Minecraft" 混淆可能性较低，但仍建议在正式发布前做一次商标检索
    （美国 USPTO TESS、中国商标网、欧盟 EUIPO）确认无在先冲突；
  - Logo、字体、配色必须与 Minecraft 的 Logo（ MinerCraft 风格 3D 字）明显不同；
  - 避免任何"Mine-"前缀组合（MineXXX）以及 MC 系标志性视觉元素出现在自己的品牌里。

### 3.2 宣传语中的高风险暗示

以下表述属于高危，**一律不要用**：

- "Minecraft 克隆 / Minecraft 开源替代品（官方级）"——描述性提及尚可
  （"an open-source voxel game inspired by Minecraft"），但把"Minecraft"放
  在标题/首屏主位、或暗示等同/官方替代，就有混淆与不正当竞争风险；
- 任何使用 "official"（官方）、"from Mojang"、"Microsoft" 字样的表述；
- 用 MC 的截图、Logo、字体、纹理作为宣传图；
- 在应用商店副标题/关键词里堆砌 "Minecraft" 以截流（各国应用商店都可能
  因此下架，且构成商标性使用）。

相对安全的表述（参考官方允许的格式与常见第三方实践）：

- "OpenCraft —— an independent, open-source voxel sandbox game"
- "inspired by the voxel sandbox genre"
- 在官网/仓库放一行免责声明："OpenCraft is not affiliated with, endorsed by,
  or connected to Mojang Studios or Microsoft. Minecraft is a trademark of
  Mojang Synergies AB."

### 3.3 商标自我保护策略（见 §6.3）

---

## 4. 资产层面合规

### 4.1 什么构成"复制"：不只看逐字节复制

版权侵权采用"接触 + 实质性相似"（access + substantial similarity）标准。
对纹理而言：

- **逐字节复制**：明确侵权，红线。
- **用 MC 纹理"改一改"（调色、翻转、加噪点）**：属于演绎作品，仍侵权，红线。
- **"风格相似"**：像素风、低分辨率方块纹理这一**美术风格本身不受版权保护**
  （风格是思想层），任何人都可以画 16x16 像素风草地方块。但需要警惕
  "整体 look and feel"论（Tetris 案）：如果你的整套纹理在配色体系、噪点分布、
  明暗处理上与 MC 纹理达到"整体观感几乎一致"的程度，仍有被诉风险。
- **实操标准**：画师应从空白画布开始，不参考原纹理绘制；同一物体（如"圆石"）
  在两个游戏里的视觉应能达到"一眼不是同一个东西"的程度——不同主色调、
  不同图案语言、不同边缘处理。

### 4.2 CC0 / 自由许可资产来源

| 来源 | 许可 | 说明 |
| --- | --- | --- |
| [Kenney.nl](https://kenney.nl/) | CC0（公共领域） | 数千套 2D/3D/UI/音效/字体，商用免费，无需署名；官方确认见 [Support 页](https://kenney.nl/support) |
| [OpenGameArt.org](https://opengameart.org/) | 逐件标注（CC0/CC-BY/CC-BY-SA/GPL 等） | 使用前必须逐件核对许可；CC0 筛选入口如 [Kenney 的 CC0 合集](https://opengameart.org/content/all-cc0-uploader-kenney) |
| [Freesound.org](https://freesound.org/) | 逐件标注（CC0/CC-BY 等） | 音效；注意 CC-BY 需署名 |
| [Pixabay](https://pixabay.com/) / [Mixkit](https://mixkit.co/) | 自有自由许可 | 音效/图片，核对站点条款 |
| 自制（录音、合成器、素材绘制） | 自有 | 最干净，成本最高 |

注意事项：

- **CC0 ≠ 零风险**：极个别 CC0 上传者并非真正权利人。优先选择 Kenney 这类
  有长期声誉的作者；保留下载记录与许可截图作为证据链。
- **CC-BY / CC-BY-SA 可以商用但有义务**：BY 要求署名（写入 CREDITS 文件）；
  SA（相同方式共享）要求资产以同许可再许可——混合进项目时要明确资产清单
  的逐项许可（见 §6.2）。
- **绝不使用**：从 MC 资源包提取的音效、C418 音乐、"Minecraft sound effect"
  的 YouTube 转载。

### 4.3 字体许可

- **Minecraft 官方字体不可用**：MC 内置字体（包括社区仿制的 "Minecraftia"、
  "Monocraft" 等）是为复刻 MC 观感而存在，直接使用既违反 Mojang 资产政策，
  又有 look-and-feel 风险。
- **替代方案**：
  - [SIL Open Font License (OFL)](https://openfontlicense.org/) 字体，如
    Google Fonts 中标注 OFL 的像素字体（例如 "Press Start 2P"、"VT323"、
    "Silkscreen"——均为 OFL，可嵌入游戏、可商用）；
  - 自制像素字体（自有着作权，最干净）；
  - 使用任何字体前核对 LICENSE 文件；OFL 允许游戏内嵌入（作为程序使用）
    但不得单独出售字体文件本身。

### 4.4 音效

- 挖掘/脚步/合成等音效：优先 **Freesound CC0**、Kenney 音效包，或用
  sfxr/jsfx 类工具程序化生成（天然原创）；
- 背景音乐：自制、委托创作（合同明确著作财产权归属）、或 CC0/CC-BY 素材；
  切勿使用 C418 的任何曲目或仿写其标志性旋律（曲调本身是表达）。

---

## 5. 文档与数据来源合规

### 5.1 Minecraft Wiki（CC BY-NC-SA 3.0）

- minecraft.wiki 与 Fandom 版 Minecraft Wiki 的文字内容均以
  **CC BY-NC-SA 3.0** 授权（见
  [Minecraft Wiki:About](https://minecraft.wiki/w/Minecraft_Wiki:About)、
  [Fandom 版权页](https://minecraft.fandom.com/wiki/Minecraft_Wiki:Copyrights)）。
- 这带来三个约束（若要复制其文本）：
  1. **署名（BY）**：必须注明出处与作者；
  2. **非商业（NC）**：**不得用于商业目的**——对一个可能商业化的游戏来说，
     逐字复制 wiki 文本实质上不可行；
  3. **相同方式共享（SA）**：衍生文本必须以同一许可发布——会把你的文档
     "传染"成 CC BY-NC-SA。
- **关键区分：事实/数值 vs 文本表达**。版权只保护表达，不保护事实。
  - "一块石头需要 0.75 秒徒手挖掘"是事实/数据，可以自由使用；
  - wiki 上解释这句话的那段文字是表达，逐字复制 = 复制受版权保护的文本。
- **实操做法**：阅读 wiki（或官方/社区资料）→ 提炼事实与机制逻辑 → 用自己的
  语言重新撰写文档 → 不保留任何 wiki 句式。即"独立表达同一事实"。
- 注意 wiki 上部分文件（C418 音乐相关、Noxcrew 素材等）有独立许可标签，
  逐页核对（见
  [MediaWiki:Licenses](https://minecraft.wiki/w/MediaWiki:Licenses)）。

### 5.2 社区逆向数据（MCP 映射等）：不可用于原创实现

- MCP（Mod Coder Pack）/ MCP-Reborn 等社区项目提供 Minecraft 混淆类的
  反编译映射。其许可明确限定"仅用于制作 Minecraft 模组"，**反编译产物仍是
  Mojang 版权代码**，不得再发布、不得用于其他项目（见
  [MCP-LICENSE](https://github.com/Techcable/MinecraftMappings/blob/master/MCP-LICENSE)、
  [社区讨论：发布 MCP 生成的源码违反 Mojang 版权](https://www.reddit.com/r/feedthebeast/comments/1gsi4ig/why_the_hell_does_everyone_hate_mcp_like_honest/)）。
- 对 OpenCraft 的结论：
  - **绝不能**参考 MCP 反编译源码来写游戏逻辑（那会使代码"污染"并涉嫌侵权）；
  - **绝不能**把 MC 客户端 jar 反编译后参考其实现；
  - 机制参数（如"挖掘速度受工具等级影响"）属于思想/事实，可以通过**游玩、
    观察公开 wiki 数值**的方式了解后再独立实现——这是干净的路径；
  - 团队规范：不下载反编译产物进工作区；AI 辅助编码时不得喂入 MC 反编译代码。

### 5.3 其他文档来源

- Mojang 官方博客/文档（usage-guidelines、EULA）：受版权保护，笔记中引用
  属于合理引用，但不要整页复制进游戏或官网；
- 学术/律师文章（本文引用的）：引用观点时注明来源即可。

---

## 6. 开源许可选择建议

### 6.1 代码许可：GPL / MIT / Apache 的取舍

| 许可 | 协作者/闭源分叉 | 对游戏项目的典型影响 | 适合 OpenCraft 吗 |
| --- | --- | --- | --- |
| **GPL-3.0**（或 AGPL） | 分叉必须开源、同许可 | 防止别人拿代码做闭源换皮；但会吓跑部分商业合作者；资源文件/数据打包需注意边界 | 候选：若目标是"永远开源、抗闭源换皮" |
| **MIT** | 可闭源商用 | 最大传播度，但任何人可闭源分叉（包括做收费换皮） | 候选：若目标是最大化社区与传播 |
| **Apache-2.0** | 可闭源商用，含专利授权 | 比 MIT 多专利条款与商标条款（§6.3 会用到） | 候选：企业友好，且内置商标不授权条款 |

要点：

- **许可证不能保护"玩法"，只能约束"我的代码的使用方式"**。选择 GPL 意味着
  任何基于 OpenCraft 代码的分叉也必须开源——这是对"用我们的代码做闭源换皮"
  的制度性防御；它防不了"别人从零独立写一个换皮游戏"（那只能靠著作权法
  与反不正当竞争法）。
- 若担心代码被闭源利用，选 **GPL-3.0**；若担心资产被闭源利用，见 §6.2。
- MIT/Apache 项目里"换皮"是合法的（只要遵守许可），要有此预期。

### 6.2 代码与内容资产分别许可（常见做法）

开源游戏项目的通行做法是"双许可结构"：

- **代码**：单一 OSI 许可（如 GPL-3.0 或 Apache-2.0）；
- **美术/音效/音乐/字体/文本**：仓库内单独的 `ASSETS-LICENSE`（或
  `assets/CREDITS.md` 逐文件注明），常用
  - **CC BY-SA 4.0 / CC BY 4.0**：允许他人复用资产但要求署名（BY），
    SA 强制衍生资产同许可（可防止资产被直接搬去闭源项目）；
  - **CC0**：若想最大化复用友好；
- **第三方资产**：逐项登记来源、作者、许可、下载日期，形成
  `assets/THIRD_PARTY_NOTICES`，发布时随游戏分发署名文件。
- 文档（本笔记等）可单独标 CC BY 4.0。
- 注意 CC 许可与代码许可的"边界"问题：嵌入打包后如何界定"程序"与"数据"，
  建议在 README 中写明分层许可声明。

### 6.3 自身商标策略

- OpenCraft 名称本身可考虑在主要市场（至少中国 + 美国）注册文字商标
  （第 9 类：游戏软件；第 41 类：在线游戏服务）。成本不高，早注册早占位。
- 使用 Apache-2.0 时其条款已默认"不授予商标权"，配合一份
  `TRADEMARKS.md`（说明哪些使用需要许可、如何做非官方声明）即可。
- 保留 Logo 设计的设计底稿与源文件（作为自主著作权的证据）。

---

## 7. 红线清单（绝对不要做）

1. **反编译/参考 MC 代码写代码**：包括使用 MCP/MCP-Reborn/Yarn 映射产出的
   反编译源码；不把任何 MC jar 反编译结果放进工作区或喂给 AI 工具。
2. **复制或改编 MC 资产**：纹理、模型、音效、音乐（含 C418 曲目）、字体
   （含 Minecraftia/Monocraft）、Logo、加载界面，直接或"改色/翻转/加噪点"
   式衍生都算。
3. **把 "Minecraft" 用于主标题/首屏宣传位**，或任何暗示官方、暗示"开源版
   Minecraft"等同地位的表述与视觉（用 MC Logo 风格的 3D 字做自己的 Logo 等）。
4. **逐字复制 Minecraft Wiki 文本**进游戏文档/教程/官网（NC + SA 限制，且
   属于复制表达而非使用事实）。
5. **整体 look-and-feel 复刻**：默认配色体系、GUI 布局、方块/物品图标逐项
   与 MC 对齐到"几乎一致"的程度（Tetris 案风险 + 中国换皮案风险 + 反不正当
   竞争法风险）。

## 8. 安全做法清单（推荐）

1. **玩法机制自由对齐**：体素世界、挖掘/建造/合成/生存等机制概念不受版权
   保护，可以放心实现；数值自己独立推导，避免整套数值+呈现逐项照抄。
2. **资产 100% 原创 + 可溯源**：纹理/模型从零绘制、音效自制或 CC0
   （Kenney、Freesound CC0）、字体用 OFL 或自制；建立
   `assets/CREDITS.md` 逐项登记来源与许可，保留下载证据。
3. **品牌独立**：名称、Logo、宣传语与 Minecraft 保持明显区分；官网与仓库
   附固定免责声明（非官方、无关联；Minecraft 是 Mojang 的商标）；发布前做
   目标市场商标检索，并尽早注册自己的商标。
4. **文档独立撰写**：只从 wiki/公开资料提炼**事实与数值**，用自己的语言
   重写；不保留 wiki 句式与段落结构。
5. **分层开源许可**：代码（GPL-3.0/Apache-2.0/MIT 择一）与内容资产
   （CC BY-SA 4.0 或 CC0）分别许可；仓库根目录写清 `LICENSE` +
   `ASSETS-LICENSE` + `TRADEMARKS.md`；对外发布随附第三方声明文件。
6. （附加）**留痕**：保留原创性证据（设计源文件、Git 历史、素材工程文件），
   万一发生争议时用于证明独立创作。

---

## 9. 来源汇总

### 判例与法理

- Tetris Holding, LLC v. Xio Interactive, Inc., 863 F. Supp. 2d 394 (D.N.J. 2012)
  - 判决全文（Justia）：<https://law.justia.com/cases/federal/district-courts/new-jersey/njdce/3:2009cv06115/235418/61/>
  - Wikipedia：<https://en.wikipedia.org/wiki/Tetris_Holding,_LLC_v._Xio_Interactive,_Inc.>
  - vLex：<https://case-law.vlex.com/vid/tetris-holding-llc-v-890864419>
  - 律所评析：<https://www.loeb.com/en/insights/publications/2012/06/tetris-holding-llc-v-xio-interactive-inc> 、
    <https://www.coleschotz.com/tetris-defeats-the-clones-in-copyright-infringement-battle/>
- 中国换皮案与玩法可版权性讨论：
  - 《炉石传说》案分析（中国知识产权律师网）：<https://www.ciplawyer.cn/articles/140198.html>
  - 天同律师事务所：<https://www.tiantonglaw.com/Content/2022/09-15/1959215747.html>
  - 知产财经：<https://www.ipeconomy.cn/yuanchuang/8449.html> 、
    <https://www.ipeconomy.cn/huodong/8464.html>
  - 九则换皮裁判规则汇总（知乎专栏）：<https://zhuanlan.zhihu.com/p/559340180>
  - 卓建律师事务所（游戏知产保护路径演变）：<https://www.lawzj.cn/news_view.aspx?TypeId=5&Id=1782&Fid=t2:5:2>
  - 集佳（广州知产法院典型案例）：<https://www.unitalenlaw.com/html/report/25041129-1.htm>

### Mojang / Microsoft 官方

- Minecraft EULA：<https://www.minecraft.net/en-us/eula>
- Usage Guidelines for Fans and Creators：<https://www.minecraft.net/en-us/usage-guidelines>
- EULA 与商用指南更新公告：<https://www.minecraft.net/en-us/article/minecraft-eula-and-commercial-usage-guidelines-updates>
- 关于独立开源克隆的社区共识讨论：
  <https://gamedev.stackexchange.com/questions/22247/am-i-allowed-to-make-my-minecraft-clone-open-source>

### 资产来源与许可

- Kenney（CC0）：<https://kenney.nl/> 、<https://kenney.nl/support>
- OpenGameArt（逐件许可）：<https://opengameart.org/> 、
  Kenney CC0 合集：<https://opengameart.org/content/all-cc0-uploader-kenney>
- Freesound：<https://freesound.org/>
- SIL Open Font License：<https://openfontlicense.org/>
- CC 许可证文本：<https://creativecommons.org/licenses/>

### Wiki 与逆向数据

- Minecraft Wiki 版权说明：<https://minecraft.wiki/w/Minecraft_Wiki:About> 、
  <https://minecraft.wiki/w/MediaWiki:Licenses>
- Fandom Minecraft Wiki 版权页：<https://minecraft.fandom.com/wiki/Minecraft_Wiki:Copyrights>
- MCP 许可：<https://github.com/Techcable/MinecraftMappings/blob/master/MCP-LICENSE>
- MCP 版权风险讨论：<https://www.reddit.com/r/feedthebeast/comments/1gsi4ig/why_the_hell_does_everyone_hate_mcp_like_honest/> 、
  <https://news.ycombinator.com/item?id=18159287>
