# T-A2 报告：美术资产管线（把"贴图"从代码里解耦出来）

分支 `task/T-A2-asset-pipeline`　worktree `/Users/happy/Desktop/opencraft_worktree/opencraft-ta2`
日期 2026-09-18　前置：T005 / T-I2 / T-D45

---

## 1. 一句话结论

`assets/` 现在是贴图的真实来源：新增的 PNG 通道逐个瓦片覆盖图集，**文件缺失、尺寸不符、
非 PNG、整个 `assets/` 目录被删掉时一律回退到原程序化图案，且回退结果与改动前逐字节
相同**（全像素 md5 双方都是 `7d537e85c3f04f4a92cc2a01d532e433`）。实机已证明：放一张
品红的 `dirt_side.png`，画面里只有泥土侧面变品红（16 125 个差异像素，全部是品红系）；
草方块侧面上品红下白时，屏幕上就是上品红下白。

验收 9 条全部达成，其中 3 条我用了比卡面更严的装置（见 §5）。

## 2. 变更摘要

### 2.1 新增能力

| 文件 | 内容 |
|---|---|
| `game/client/src/asset_atlas.hpp/.cpp` | **新增**。外部资产通道：`load_tile_png()` / `load_block_tile()` / `load_item_icon()`（物品图标通道，本卡只建不产出）/ `resolve_assets_root()`；PNG 校验（文件头 + 正好 16×16）、上下翻转、逐条 WARN |
| `game/client/src/atlas.cpp` | `generate_atlas()` 每个瓦片先试文件、失败再 `paint_tile()`；新增 `blit_tile()`；`generate_atlas(registry)` 保留为解析资产根的重载。**程序化图案一字未改** |
| `game/client/src/atlas.hpp` | 新增 `generate_atlas(registry, assets_root)` 重载 + 说明 |
| `game/client/CMakeLists.txt` | 新增静态库 `opencraft_client_assets`（`atlas.cpp` + `asset_atlas.cpp`），客户端与测试**链接同一份代码**；`opencraft` 改为链接该库 |
| `cmake/deps.cmake` | 新增 FetchContent：`stb_image`（public domain），pin 到 commit `2c980bb`；`SYSTEM PRIVATE` 抑制第三方告警 |
| `tests/test_asset_atlas.cpp` | **新增** 16 个 TEST_CASE：布局冻结、空目录等价（对**改动前**摘要表）、加载生效、逐 slot、透明、四类失败降级、资产根解析、物品图标通道 |
| `tests/CMakeLists.txt` | 注册新测试文件 + 链接 `opencraft_client_assets` |
| `assets/README.md` | 美术总监的落笔规范（目录/命名/16×16/朝向/回退语义/三条红线**逐字引述**） |
| `assets/CREDITS.md` | 证据链表头 + 字段说明（条目由后续内容卡填） |
| `assets/blocks/.gitkeep`、`assets/items/.gitkeep` | 目录占位 |
| `docs/03-architecture.md` §9 | 依赖表新增 1 行（stb_image / public domain） |
| `docs/qa/T-A2-2026-09-18/` | 证据目录（5 张截图 + 日志 + 复跑脚本 + 等价性产物） |

### 2.2 接口（新增，未破坏既有）

```cpp
// atlas.hpp —— 既有签名保持不变，客户端 main.cpp 一行未改
AtlasImage generate_atlas(const voxel::BlockRegistry &registry);                       // 新增：解析资产根
AtlasImage generate_atlas(const voxel::BlockRegistry &registry,
                          const std::filesystem::path &assets_root);                    // 新增

// asset_atlas.hpp —— 全新
std::optional<TilePixels> load_tile_png(const std::filesystem::path &path);
std::optional<TilePixels> load_block_tile(const std::filesystem::path &assets_root,
                                          std::string_view block_id, int slot);
std::optional<TilePixels> load_item_icon(const std::filesystem::path &assets_root,
                                         std::string_view item_id);
std::filesystem::path block_tile_path(const std::filesystem::path &assets_root,
                                      std::string_view block_id, int slot);
std::filesystem::path item_icon_path(const std::filesystem::path &assets_root, std::string_view item_id);
std::filesystem::path resolve_assets_root();                                            // 候选顺序搜索
std::filesystem::path resolve_assets_root(const std::vector<std::filesystem::path> &);   // 可测版本
const char *tile_slot_name(int slot);                                                   // top/side/bottom
constexpr int kTileSize = 16;  using TilePixels = std::array<std::uint32_t, 256>;
```

**`main.cpp` 零改动**：`generate_atlas(world.registry())` 仍能编译，改为内部解析资产根。

## 3. 实现要点（含我做的判断，PM 请复核）

1. **瓦片像素格式**：图集是 `a<<24|b<<16|g<<8|r`、行序**自下而上**（`paint_tile()` 注释：
   面的上沿采样瓦片最高行）。PNG 行序自上而下 ⇒ 解码时**上下翻转**。
   实机 02 段（草侧面上品红下白）确认屏幕上是上品红下白，与推导一致。
2. **资产根解析**（卡面未规定，我按实际运行约定定）：`$OPENCRAFT_ASSETS_DIR` →
   `<cwd>/assets` → `<cwd>/../assets`，取第一个存在的目录。加第二条是因为**实际启动是
   `cd build && ./opencraft`**（`saves/` 就落在 `build/`，见 main.cpp 注释），此时
   `../assets` 才是仓库的 `assets/`。客户端的 `atlas:` 日志会写明用的是哪一个。
3. **只认 PNG**：自己校验 8 字节 PNG 签名后再交给 stb，所以"扩展名是 .png 的 JPEG/TGA"
   也会被拒（卡面 §3.5 的"非 PNG → 拒绝"我按字面实现）。色彩类型不做要求：灰度/RGB/
   调色板/16 位都由 stb 归一成 RGBA8；**尺寸严格**（不符即拒，不缩放）。
4. **air 不做文件查询**：air 没有面，它的 3 个瓦片永不被采样，查 `air_top.png` 只是白费
   一次 stat。程序化图案照旧画（空目录等价需要它保持原样）。
5. **单测与客户端同源**：新增 `opencraft_client_assets` 静态库，测试链接的是客户端实际
   编译的那份 `atlas.cpp`，不是复制品。
6. **`assets/items/` 目录**：卡面 §3.1 的冻结布局只列了 `blocks/`，但 §1 要求"为物品图标
   留出同类通道"。我按"通道 = 路径约定 + 解码器 + 测试"实现，并**额外建了 `items/`
   目录**（含 `.gitkeep`）。如果 PM 认为这超出了 §3.1 的冻结范围，删目录 + README 里那
   一节即可，代码不用动。
7. **`stb_image` 只在 `asset_atlas.cpp` 定义实现宏**（`STBI_ONLY_PNG` + `STBI_NO_STDIO`；
   自读字节，才能让"文件不存在"静默而"文件坏了"报警）。stb 目录以 `SYSTEM` 引入，
   第三方告警不进我们的零告警构建。

## 4. 验收对照

| # | 卡面要求 | 结果 | 证据 |
|---|---|---|---|
| 1 | 存量 422 TEST_CASE 全绿、存量测试文件零改动 | ✅ **438 全绿**（422 + 新增 16）；`tests/` 下除 `CMakeLists.txt` 无文件被改 | `ctest`；§7 文件清单 |
| 2 | ★ 空目录等价（逐字节） | ✅ 全像素 20736 逐字节相同，md5 双方 `7d537e85…`；81 个瓦片摘要表 diff 为空；实机侧"删掉整个 `assets/`"与"目录为空"**逐像素 0 差异** | `docs/qa/…/atlas_equivalence/`；截图 00 vs 03 |
| 3 | 加载生效，其余瓦片不变 | ✅ `1/63 block tiles loaded`；16 125 个差异像素**全是品红系** | 截图 01 + `pixel_diff.py` |
| 4 | 32×32 / 非 PNG / 空文件 ⇒ WARN + 回退 | ✅ 单测各 1 条 WARN 且断言回到**改动前摘要**；实机 32×32 段：日志 1 条 warning、加载 `0/63`、画面与基线 0 差异 | 单测；截图 04 + 日志 |
| 5 | 缺目录不崩 | ✅ 正常进游戏，日志 `atlas: no assets/ tree found; every block tile is procedural`，画面与基线逐像素相同 | 截图 03 + 日志 |
| 6 | 索引/尺寸公式不变 | ✅ `tiles_per_row == 9`、`144×144`、`tile_index == id*3+slot`、`crack_tile_base(21) == 63` 全部单测断言 | 单测 + §2 的摘要表 |
| 7 | 实机放品红 PNG ⇒ 侧面变品红 + 截图 | ✅ 截图为交付二进制的构建（md5 `77ad679e…`，构建时间晚于最后一次改码） | 截图 01 |
| 8 | `CREDITS.md` + `README.md` 就位 | ✅ CREDITS 含固定字段表头；README 含三条红线**逐字引述** + 命名规范 + 尺寸/朝向/回退语义 | 两个文件 |
| 9 | 依赖留痕 | ✅ `docs/03 §9` 新增一行：`贴图/图标 PNG 解码（美术资产，T-A2） | stb_image | public domain（源码头部自述）`（该文档唯一改动） | `git diff docs/` |

构建：`cmake --build build -j` 零错误；**新代码零告警**（全仓 8 条告警 = 6 条
`-Wunused-result`（存量 `test_mob_spawn.cpp` / `test_mob_authority.cpp`）+ 2 条既有 ld
"ignoring duplicate libraries"，**改动前就在**：改动前的 `link.txt` 与改动后的重复库集合
逐项相同）。`clang-format`（CLT 17）对 5 个 C++ 文件 `--dry-run -Werror` 通过。

## 5. 我做得比卡面更严的三处（供 PM 判是否保留）

1. **等价性用"改动前二进制算出的摘要"做基准**，不是"再跑一遍新代码自比"。
   dumper 源码同一份，分别链接 `bc20aab` 的 `atlas.cpp` 与本卡源码，逐像素 `cmp`。
2. **朝向做实机证明**（卡面没要求）：两段色探针 ⇒ 草方块侧面上品红下白、顶面仍绿，
   证明瓦片朝向在"网格化 → 着色器 → 屏幕"整条链上正确，而不只是在单测里正确。
3. **"缺目录"与"目录为空"逐像素对比**（0 / 957440）：把 §3.3 的等价从"看起来一样"
   变成可计数的同一帧。

## 6. 已知问题与建议（不顺手修，登记备查）

| # | 现象 | 影响 | 建议 |
|---|---|---|---|
| 1 | 存量 `test_mob_spawn.cpp` / `test_mob_authority.cpp` 有 **6 条** `-Wunused-result` 告警（2 个文件；PM 在 T-D45 裁决里记为"2 处"应是按文件计） | 与本卡无关，但"零新增警告"这条纪律被存量告警稀释，无法用"构建输出为空"当判据 | 建议单开一张清理卡（T-D46 之类）补 `(void)` 或改断言 |
| 2 | 客户端/manifest 里仍有一处 ld "ignoring duplicate libraries" 提示（physics/render/game/storage/worldgen 各出现两次） | 同样存量（改动前 link.txt 的重复集合完全一样） | 属构建整洁项，可并入上一条 |
| 3 | `inventory_wiring.hpp::item_tint()` 仍按 id 硬编码纯色，**没有**接上 `items/` 通道 | 本卡范围外（§2 明确"不做物品图标的实际绘制"）；通道已建好且有单测 | 内容卡（T-A3+）落地图标时，把 HUD 取色改为"先查 `assets/items/<id>.png`，缺失回退 tint" |
| 4 | 生物模型仍是立方体占位（`main.cpp:711` 自述 STAND-IN） | 本卡不碰 | 归 T-R3 |
| 5 | 无热重载：贴图在启动时读一次 | 卡面 §2 明确不做 | 若美术总监迭代频繁，可另开小卡（文件 mtime 轮询 + 重建 GL 纹理），但要注意 §3.2 的图集尺寸冻结（重载只允许同尺寸替换） |
| 6 | 每瓦片每次启动一次 `is_regular_file`+`filesystem::file_size`（最多 63 次） | 启动期一次性的 63 次 stat，实测无感 | 不建议优化 |

## 7. 文件清单

**改动**（均在卡面白名单内）：

```
cmake/deps.cmake                        +15   stb_image FetchContent
docs/03-architecture.md                 +1    §9 依赖表一行
game/client/CMakeLists.txt              +26/-1 新增 opencraft_client_assets 库并链接
game/client/src/atlas.cpp               +47/-2 资产通道接线（程序化图案未改）
game/client/src/atlas.hpp               +16   新重载 + 说明
tests/CMakeLists.txt                    +7/-2  注册新测试 + 链接资产库
```

**新增**：

```
assets/README.md                        美术总监落笔规范（含三条红线逐字引述）
assets/CREDITS.md                       证据链模板
assets/blocks/.gitkeep  assets/items/.gitkeep
game/client/src/asset_atlas.hpp         PNG/资产根接口
game/client/src/asset_atlas.cpp         stb 实现 + 校验 + 翻转 + 告警
tests/test_asset_atlas.cpp              16 个 TEST_CASE
docs/qa/T-A2-2026-09-18/**              证据（README/5 截图/日志/工具/等价性产物）
docs/tasks/T-A2.report.md               本报告
```

**禁碰项零改动**：`engine/**`、`game/common/**`、`game/server/**`、
`game/client/src/main.cpp`、`STATE.md`、`docs/01`、`docs/04`、`docs/05`、
`docs/tasks/*.ruling*.md`、任何美术产出（探针 PNG 拍完即删，`assets/blocks/` 交付时为空）。

## 8. 构建 / 运行 / 测试方法

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-ta2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # 首次含 FetchContent 取 stb（约 60 s）
cmake --build build -j                            # 不要接管道
cd build && ctest -j 8                            # 期望 438/438
```

运行（与既有约定一致，`saves/` 与 `assets/` 都相对 cwd 解析）：

```bash
cd build && ./opencraft          # 日志里会有 atlas: N/63 block tiles loaded from ...
```

放一张贴图试试（不需要重编译）：

```bash
python3 - <<'EOF'
import struct, zlib
def chunk(k,d): return struct.pack(">I",len(d))+k+d+struct.pack(">I",zlib.crc32(k+d)&0xffffffff)
raw=b"".join(b"\x00"+b"\xff\x00\xff\xff"*16 for _ in range(16))
open("dirt_side.png","wb").write(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",16,16,8,6,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b""))
EOF
mv dirt_side.png ../assets/blocks/ && cd build && ./opencraft   # 泥土侧面变品红
```

## 9. 给项目经理的备注

1. **判据的核心数字**：空目录等价 = 全像素 md5 双方 `7d537e85c3f04f4a92cc2a01d532e433`
   （20736 像素 `cmp` 相同）；加载生效 = 16 125 差异像素**全部品红系**。
2. **卡面 §0 的"21 个方块"实测口径**：`register_block` 调用是 **20** 次（`create_default()`
   里 20 个内容方块），`registry.size()` 是 **21**（`BlockRegistry` 的构造函数把 air 也
   push 进了 `defs_`）；PM 在派发提交里把卡面 20 改成 21 的依据写的是"register_block 实测"，
   数字 21 对、出处差一个：它来自 `size()` 而不是调用次数。图集因此是 `21*3 + 10 = 73` 个
   瓦片 → 9 列（`crack_tile_base = 63`），我的实现与测试都按这个写。后续卡若要按
   "21 个 register_block" 推瓦片数会多算 3 个。
3. **需要 PM 拍板的两处**：
   (a) `assets/items/` 目录是否超出 §3.1 冻结范围（§3 第 6 点，删目录即可，代码不用动）；
   (b) `resolve_assets_root()` 的候选顺序（§3 第 2 点）是否认可——它是"实际启动是
   `cd build`"这个既成事实逼出来的，客户端每次启动都会把选中路径打进日志。
4. **stb 的许可留痕**：`docs/03 §9` 那一行按卡面 §4 的核验结论写（源码头部自述 public
   domain；GitHub API 的 NOASSERTION 是因为 stb 仓库混装多许可）。pin 到 commit
   `2c980bb59875b0d32144a71867fbdebb2f77cd20`（stb 没有 release tag，所以用 commit 而不是 tag）。
5. **首次 configure 变慢约 10 s**（拉 stb，12 MB 全量克隆），属预期不是回归。
6. **我没有顺手修的既有问题**已列入 §6 建议表（存量告警、ld 重复库提示、item_tint 未接
   通道、无热重载）。
