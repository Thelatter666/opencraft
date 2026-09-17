# T-A2 §6.2「空目录等价」的可复跑产物

判据：`assets/blocks/` 为空时，生成的图集与改动前**逐字节相同**。

## 装置

同一份 dumper 源码（`dump_atlas.cpp`），**绕开 CMake** 在 `/tmp` 编译两次：

* `build_dumper_old.sh` → 链接 `bc20aab`（本卡父提交）的 `game/client/src/atlas.cpp`
* `build_dumper_new.sh` → 链接本卡最终源码的 `atlas.cpp` + `asset_atlas.cpp`

两者都在 worktree 根目录运行（客户端就在那里解析 `assets/`），
后者会额外打印一行 `atlas: 0/63 block tiles loaded from assets/blocks`——
那一行不是数据，比对时先滤掉。

## 结果

全像素（`build_pixels_both.sh` 造的两个 dumper，输出 81×256 个 `%08x`）：

```
old md5: 7d537e85c3f04f4a92cc2a01d532e433    (165905 字节)
new md5: 7d537e85c3f04f4a92cc2a01d532e433
cmp: 相同（byte-identical）
```

逐瓦片 FNV-1a 64 摘要表（81 格，含 8 个背景格）：

```
digests_pre_change.txt   md5 6f6fc963272d8b7b39e96aba3494d1da
digests_post_change.txt  md5 6f6fc963272d8b7b39e96aba3494d1da   ← diff 为空
```

头两行（两版一致）：

```
dims 144 144 9 pixels=20736
whole 334e2cce2595568d
```

## 复跑

```bash
WT=/Users/happy/Desktop/opencraft_worktree/opencraft-ta2
bash build_pixels_both.sh dump_pixels_old old
bash build_pixels_both.sh dump_pixels_new new
cd "$WT" && /tmp/ta2_baseline/dump_pixels_old > /tmp/a.txt
/tmp/ta2_baseline/dump_pixels_new 2>/dev/null | grep -v '^\[' > /tmp/b.txt
cmp /tmp/a.txt /tmp/b.txt && echo identical
```
