# T-B2 证据目录 · Mossback 首个生物体素模型

卡面：`docs/tasks/T-B2.md`　报告：`docs/tasks/T-B2.report.md`
工作根：`/Users/happy/Desktop/opencraft_worktree/opencraft-tb2/`（分支 `task/T-B2-mossback-model`）

## 交付物（仓库里唯一的两个资产文件）

| 文件 | 说明 |
|---|---|
| `assets/mobs/mossback.vox` | 3784 字节。SIZE 9×14×14，672 体素，7 个关节标签，RGBA 满 256 条 |
| `assets/palettes/mossback.png` | 134 字节。16×16 RGBA 全不透明，**格号 = colorIndex** |

## 本目录

| 文件 | 是什么 |
|---|---|
| `tools/mossback_layers.txt` | ★ **创作本体**：14 层逐格手写字符稿（每层 14 行 × 9 格），头 30 行写明坐标系、朝向、字符↔索引对照 |
| `tools/mossback_palette.txt` | ★ 调色板手稿：16×16 格字符稿，注明"格号 = colorIndex、第 0 格永不画" |
| `tools/vox_build.py` | 编码器：把上述两份手稿**逐格翻译**成 `.vox` 与 `.png`。无几何、无对称展开、无填充算法、无随机数 |
| `tools/vox_inspect.py` | 自写的**独立**读回器：只读落盘的 `.vox`，复算预算/每索引包围盒/关节方位/暴露面，并渲染 5 视图 + 剪影 |
| `tools/check_palette_png.py` | 用**第三方**解码器（Pillow）复核调色板 PNG |
| `inspect_selfcheck.txt` | 上面 `vox_inspect.py` 的完整输出（报告 §3 自查表的原始出处） |
| `pillow_palette_check.txt` | 上面 `check_palette_png.py` 的完整输出 |
| `mossback_contact_sheet.png` | 侧视（鼻在右）/ 正视 / 俯视 / 等轴测 / 等轴测关节分色，5 视图拼图 |
| `mossback_silhouette.png` | 侧视 + 等轴测**纯剪影**（判"一眼读出四条腿+一个头+背上有苔"用这张） |
| `run_with_model.log` | 产品码启动日志：有模型 |
| `run_without_model.log` | 产品码启动日志：把 `assets/mobs/` 移走后 |
| `build_configure.log` / `build_compile.log` | 本 worktree 的 configure 与编译日志（证明这次构建是干净的） |
| `goxel_attempt_1..3_*.png` + `goxel_third_party_attempt.md` | 第三方 `.vox` 校验尝试的四条路与结果（**未达成**，如实登记） |
| `ingame_sweep_no_mob.png` | 实机取证尝试：走一段路后 360° 扫视 8 帧的拼图，**没有拍到苔背兽** |
| `pm_goxel_verified.png` | PM 2026-09-18 交的 Goxel 复验图（**不是本案产出**，随目录一并留存） |

## 复现（逐字可执行）

```bash
cd /Users/happy/Desktop/opencraft_worktree/opencraft-tb2

# 1) 从手稿重新生成两个资产
python3 docs/qa/T-B2-2026-09-18/tools/vox_build.py .

# 2) 独立读回 + 复算预算 + 出图
python3 docs/qa/T-B2-2026-09-18/tools/vox_inspect.py . docs/qa/T-B2-2026-09-18/

# 3) 第三方 PNG 复核
python3 docs/qa/T-B2-2026-09-18/tools/check_palette_png.py .

# 4) 产品码启动日志判据（cwd 必须在 build 里）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # 不设 FETCHCONTENT_BASE_DIR
cmake --build build --parallel                       # 不接管道
cd build && rm -rf saves
./opencraft > ../docs/qa/T-B2-2026-09-18/run_with_model.log 2>&1 &
PID=$!
for i in $(seq 1 60); do grep -q "spawn scan" ../docs/qa/T-B2-2026-09-18/run_with_model.log && break; sleep 0.5; done
sleep 1; kill $PID; sleep 1
grep -E "mobs:|mob model" ../docs/qa/T-B2-2026-09-18/run_with_model.log
```

## 两条纪律（本案遵守）

- `tools/` 下的一切**不接进构建**：没有新增源文件、没有 CMakeLists 改动（卡面 §4 白名单自洽检查）。
- 过程垃圾不入仓：`__pycache__/`、`*.pyc` 已清（交付前 `find` 复核为空）。
