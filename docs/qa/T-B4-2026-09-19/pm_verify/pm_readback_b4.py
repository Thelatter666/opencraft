#!/usr/bin/env python3
"""T-B4 · PM 第四份独立实现读回（第五任 PM 现写，不 import 开发者/美术任何脚本）。

用法：
    python3 pm_readback_b4.py <repo_root>

读**落盘的两件资产**（assets/mobs/blastbud.vox、assets/palettes/blastbud.png），
用 PM 自己的 .vox 解析器与自己的 PNG 解码器（zlib + 手写反滤波，不依赖 Pillow）
重算卡面契约 ①②③④⑤ 与 T-B5 起的常设要求（明显面部特征）。

只打印事实与 PASS/FAIL，不改任何文件。
"""

import hashlib
import os
import re
import struct
import sys
import zlib

REPO = sys.argv[1] if len(sys.argv) > 1 else "."
VOX = os.path.join(REPO, "assets/mobs/blastbud.vox")
PNG = os.path.join(REPO, "assets/palettes/blastbud.png")
GUIDE = os.path.join(REPO, "docs/art/01-style-guide.md")

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  -- {detail}" if detail else ""))


# ---------------------------------------------------------------- .vox parser
def parse_vox(path):
    with open(path, "rb") as f:
        d = f.read()
    assert d[:4] == b"VOX ", "no VOX signature"
    version = struct.unpack_from("<i", d, 4)[0]
    size = None
    voxels = None
    rgba = [None] * 256          # rgba[i] = colorIndex i+1  (vox RGBA 整体错一位)
    chunks = []
    off = 8
    while off + 12 <= len(d):
        tag = d[off:off + 4]
        clen, klen = struct.unpack_from("<ii", d, off + 4)
        body = d[off + 12:off + 12 + clen]
        chunks.append((tag.decode("latin1"), clen, klen))
        if tag == b"SIZE":
            size = struct.unpack_from("<iii", body, 0)
        elif tag == b"XYZI":
            n = struct.unpack_from("<I", body, 0)[0]
            assert len(body) >= 4 + 4 * n, "XYZI truncated"
            voxels = [tuple(body[4 + 4 * i:8 + 4 * i]) for i in range(n)]
        elif tag == b"RGBA":
            for i in range(min(255, len(body) // 4)):
                rgba[i + 1] = tuple(body[i * 4:i * 4 + 4])
        # 注意：.vox 的 children chunk 紧跟在父 chunk 内容之后，按线性走即可；
        # 若在顶层再额外跳过 children_len 会把后续块全部错过（PM 首版自踩）。
        off += 12 + clen
    return version, size, voxels, rgba, chunks


# ------------------------------------------------------------ PNG decoder
def decode_png(path):
    """PM 自己的 PNG 解码：只支持 8-bit RGBA/RGB 非隔行。返回 (w,h,rows)。"""
    with open(path, "rb") as f:
        d = f.read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    off = 8
    idat = b""
    w = h = bitdepth = colortype = None
    while off + 8 <= len(d):
        ln = struct.unpack_from(">I", d, off)[0]
        typ = d[off + 4:off + 8]
        body = d[off + 8:off + 8 + ln]
        if typ == b"IHDR":
            w, h, bitdepth, colortype, comp, filt, inter = struct.unpack_from(">IIBBBBB", body, 0)
            assert bitdepth == 8 and inter == 0, f"unsupported PNG ({bitdepth}bit interlace={inter})"
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        off += 12 + ln
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype]
    raw = zlib.decompress(idat)
    stride = w * nch
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise AssertionError(f"bad filter {ft}")
        rows.append(bytes(line))
        prev = line
    return w, h, nch, rows


# ------------------------------------------------------------------ helpers
def md5(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def saturation(rgb):
    mx, mn = max(rgb), min(rgb)
    return 0.0 if mx == 0 else (mx - mn) / mx


JOINT_OF_INDEX = {i: (i - 1) % 8 for i in range(1, 17)}
JOINT_NAMES = ["body 躯干", "head 头", "arm_l 左臂", "arm_r 右臂",
               "leg_l 左腿", "leg_r 右腿", "tail 尾", "spare 备用"]


def parse_guide_colors(path):
    txt = open(path, encoding="utf-8").read()
    sec = txt.split("### 2.1")[1].split("### 2.2")[0]
    return {m.group(1).upper() for m in re.finditer(r"`#([0-9A-Fa-f]{6})`", sec)}


print("=" * 78)
print("T-B4 · PM 第四份独立实现读回（第五任 PM 现写脚本）")
print("=" * 78)

# ---------------------------------------------------------------- 0. md5
print("\n== 0. 交付件与 md5 ==")
mv, mp = md5(VOX), md5(PNG)
print(f"blastbud.vox  {os.path.getsize(VOX)} bytes  md5 {mv}")
print(f"blastbud.png  {os.path.getsize(PNG)} bytes  md5 {mp}")
check("md5 与报告一致", mv == "4ebea03c4373ae04ab35daf3a68ab9be" and
      mp == "3f3cdbb3e9db1e592d32193b66af85b0", f"{mv[:8]}… / {mp[:8]}…")
for other in ("assets/mobs/mossback.vox", "assets/mobs/hollow_wretch.vox",
              "assets/palettes/mossback.png", "assets/palettes/hollow_wretch.png"):
    p = os.path.join(REPO, other)
    print(f"  {other}: md5 {md5(p)}")

# ---------------------------------------------------------------- 1. parse
print("\n== 1. PM 自己的 .vox 解析 ==")
version, size, voxels, rgba, chunks = parse_vox(VOX)
sx, sy, sz = size
print(f"version {version}  chunks {chunks}")
print(f"SIZE {sx} x {sy} x {sz}   voxels {len(voxels)}")
check("体素数 = 510", len(voxels) == 510, f"{len(voxels)}")

occ = {(x, y, z): c for x, y, z, c in voxels}
zero = sum(1 for v in voxels if v[3] == 0)
check("colorIndex 0 出现 0 次", zero == 0, f"{zero}")

xs = [v[0] for v in voxels]; ys = [v[1] for v in voxels]; zs = [v[2] for v in voxels]
bbox = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
print(f"bbox x {bbox[0]}..{bbox[1]}  y {bbox[2]}..{bbox[3]}  z {bbox[4]}..{bbox[5]}")
zspan = bbox[5] - bbox[4] + 1
check("契约② z 跨度恰 = 17", zspan == 17, f"z {bbox[4]}..{bbox[5]} span {zspan}")
check("契约② 体素 z 跨度 → 碰撞高 1.7（17 × 0.1）", abs(zspan * 0.1 - 1.7) < 1e-9, f"{zspan*0.1}")
for side, val, cap in (("x", bbox[1] - bbox[0] + 1, 32), ("y", bbox[3] - bbox[2] + 1, 32),
                       ("z", zspan, 32)):
    check(f"预算 每边 ≤32（{side}）", val <= cap, f"{val}")

# ------------------------------------------------------- 2. 每索引包围盒
print("\n== 2. 每 colorIndex 计数 / 包围盒 / 关节归属（契约 v2）==")
per_idx = {}
for x, y, z, c in voxels:
    per_idx.setdefault(c, []).append((x, y, z))
joint_cells = {}
for c in sorted(per_idx):
    cells = per_idx[c]
    cx = [p[0] for p in cells]; cy = [p[1] for p in cells]; cz = [p[2] for p in cells]
    j = JOINT_OF_INDEX.get(c, 0)
    bx = (min(cx), max(cx), min(cy), max(cy), min(cz), max(cz))
    rgb = rgba[c]
    print(f"  idx {c:3d}  {len(cells):4d} vox  x {bx[0]}..{bx[1]} y {bx[2]}..{bx[3]} "
          f"z {bx[4]}..{bx[5]}  rgb {rgb[:3]}  -> joint {j} {JOINT_NAMES[j]}")
    joint_cells.setdefault(j, []).extend(cells)

# 每关节包围盒
joint_bbox = {}
for j, cells in joint_cells.items():
    cx = [p[0] for p in cells]; cy = [p[1] for p in cells]; cz = [p[2] for p in cells]
    joint_bbox[j] = (min(cx), max(cx), min(cy), max(cy), min(cz), max(cz))
    print(f"  joint {j} {JOINT_NAMES[j]:14s} {len(cells):4d} vox  "
          f"bbox x {joint_bbox[j][0]}..{joint_bbox[j][1]} y {joint_bbox[j][2]}..{joint_bbox[j][3]} "
          f"z {joint_bbox[j][4]}..{joint_bbox[j][5]}")
n_joints = len(joint_cells)
check("预算 关节数 ≤ 8", n_joints <= 8, f"{n_joints}")
check("关节数 = 6（与日志 6 joints 一致）", n_joints == 6, f"{n_joints}")

# --------------------------------------------- 3. 契约③ 第二色落其关节包围盒
print("\n== 3. 契约 v2：第二色（9..16）是否落在同一关节的包围盒内 ==")
second = [c for c in sorted(per_idx) if 9 <= c <= 16]
print(f"  用到的第二色索引: {second}")
allin = True
for c in second:
    j = JOINT_OF_INDEX[c]
    bx = joint_bbox[j]
    out = [p for p in per_idx[c]
           if not (bx[0] <= p[0] <= bx[1] and bx[2] <= p[1] <= bx[3] and bx[4] <= p[2] <= bx[5])]
    print(f"  idx {c} -> joint {j} {JOINT_NAMES[j]}: {len(per_idx[c])} 格，越界 {len(out)} 格")
    allin &= not out
check("契约③ 每个第二色的全部格都在其关节包围盒内", allin, f"第二色 {second}")

# ------------------------------------------------- 4. 抗 y-up + z=0 层
print("\n== 4. 抗 y-up 判据 ==")
head_cells = joint_cells.get(1, [])
body_cells = joint_cells.get(0, [])
head_maxz = max(p[2] for p in head_cells)
body_maxz = max(p[2] for p in body_cells)
print(f"  头 max z {head_maxz} / 躯干 max z {body_maxz} / 模型 max z {bbox[5]}")
check("头 z 高于躯干（z-up 而非 y-up）", head_maxz > body_maxz, f"{head_maxz} > {body_maxz}")
check("最高层 z=16 就是头本身（头顶无附件）", head_maxz == bbox[5] == 16, f"head {head_maxz}, top {bbox[5]}")
low = sorted({occ[(x, y, bbox[4])] for (x, y, z) in occ if z == bbox[4]})
lowj = sorted({JOINT_OF_INDEX.get(c, 0) for c in low})
print(f"  z=最低层({bbox[4]}) 的索引 {low} -> 关节 {lowj}")
check("抗 y-up 锐判据：z=0 层只含腿/足关节", set(lowj) <= {2, 3, 4, 5}, f"joints {lowj}")

# ------------------------------------- 5. 面部特征（T-B5 起的常设要求）
print("\n== 5. 面部特征：占用表内 + 朝正面 +y 邻居为空（表面着色）且落头关节 ==")
face_idx = [c for c in sorted(per_idx) if JOINT_OF_INDEX.get(c, 0) == 1 and 9 <= c <= 16]
print(f"  头关节的第二色索引（候选五官色）: {face_idx}")
face_cells = []
for c in face_idx:
    for p in per_idx[c]:
        nbr = (p[0], p[1] + 1, p[2])
        face_cells.append((c, p, nbr in occ))
bad = [f for f in face_cells if f[2]]
check(f"五官 {len(face_cells)} 格：全部在占用表内且 +y 邻居为空",
      not bad and len(face_cells) > 0, f"{len(face_cells)} 格，非表面格 {len(bad)}")
hb = joint_bbox[1]
outface = [f for f in face_cells
           if not (hb[0] <= f[1][0] <= hb[1] and hb[2] <= f[1][1] <= hb[3] and hb[4] <= f[1][2] <= hb[5])]
check("五官全部落在头关节包围盒内", not outface, f"越界 {len(outface)}")

print("\n  -- PM 自己的正面投影（从 +y 看的第一格；行 z 高→低，列 x 小→大）--")
print("     " + "".join(str(x % 10) for x in range(bbox[0], bbox[1] + 1)))
for z in range(bbox[5], bbox[4] - 1, -1):
    row = ""
    for x in range(bbox[0], bbox[1] + 1):
        best = None
        for y in range(bbox[3], bbox[2] - 1, -1):
            if (x, y, z) in occ:
                best = occ[(x, y, z)]
                break
        row += "." if best is None else (str(best) if best < 10 else chr(ord('a') + best - 10))
    print(f"  z{z:2d}|{row}")

# ------------------------------------------------------- 6. 暴露面/三角
print("\n== 6. 暴露面与三角形（与 mob_mesh 同剔除规则：邻居占用则不生成）==")
NB = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
faces = 0
for (x, y, z) in occ:
    for dx, dy, dz in NB:
        if (x + dx, y + dy, z + dz) not in occ:
            faces += 1
print(f"  暴露面 {faces} ⇒ 三角形 {faces * 2}")
check("预算 暴露面 ≤ 2000", faces <= 2000, f"{faces}")
check("三角形 = 1084（与启动日志一致）", faces * 2 == 1084, f"{faces*2}")

# ------------------------------------------------- 7. 调色板 PNG 读回
print("\n== 7. 调色板 PNG（PM 自己的解码器）==")
w, h, nch, rows = decode_png(PNG)
print(f"  size {w}x{h}  channels {nch}")
check("契约④ PNG 尺寸 16×16", (w, h) == (16, 16), f"{w}x{h}")
check("契约④ 通道 = 4（RGBA）", nch == 4, f"{nch}")
alphas = set()
mismatch = []
used_rgb = {}
for k in range(1, 256):            # PNG 格号 k == colorIndex k
    cx, cy = k % 16, k // 16
    px = rows[cy][cx * 4:cx * 4 + 4]
    alphas.add(px[3])
    if k != 0:                      # 第 0 格按契约不画（不参与色值/饱和判定）
        used_rgb[k] = tuple(px[:3])
        if rgba[k] is not None and tuple(rgba[k][:3]) != tuple(px[:3]):
            mismatch.append((k, tuple(rgba[k][:3]), tuple(px[:3])))
cell0 = tuple(rows[0][0:4])
print(f"  第 0 格 = {cell0}（契约④「第 0 格不画」；三只模型一致填 §2.1 的 U0 #1E1E22 作占位）")
print(f"  参与色值判定的索引（1..255）: 共 {len(used_rgb)} 个；其中模型实际用到 "
      f"{sorted(set(used_rgb) & set(per_idx))}")
check("契约④ 第 0 格 = U0 #1E1E22（与前两只同口径占位，非模型用色）",
      cell0 == (30, 30, 34, 255), f"{cell0}")
print(f"  alpha 集合: {sorted(alphas)}")
check("契约④ PNG 格号 k == .vox RGBA 第 k 条（逐格一致）", not mismatch, f"不一致 {len(mismatch)} 格")
check("契约④ 全部格 alpha = 255（含第 0 格占位）",
      {rows[y][x * 4 + 3] for y in range(16) for x in range(16)} == {255},
      f"{sorted({rows[y][x*4+3] for y in range(16) for x in range(16)})}")

# -------------------------------------------- 8. 色值在 §2.1 内 + 饱和
print("\n== 8. 色值全部取自 §2.1 32 色表 + 饱和 ≤ 0.50（契约④ / 规格 §3）==")
guide = parse_guide_colors(GUIDE)
print(f"  §2.1 表内不同 RGB 数: {len(guide)}")
painted = sorted(set(used_rgb) & set(per_idx))
print(f"  模型实际用到的 {len(painted)} 个索引的色值明细（其余槽位为占位 U0，未参与）:")
offtable = []
maxsat = 0.0
for k in painted:
    rgb = used_rgb[k]
    hexs = "%02X%02X%02X" % rgb
    s = saturation(rgb)
    maxsat = max(maxsat, s)
    intable = hexs in guide
    if not intable:
        offtable.append((k, hexs))
    print(f"  idx {k:3d} #{hexs}  S={s:.3f}  {'in §2.1' if intable else 'NOT IN §2.1'}")
check("契约④ 全部色值在 §2.1 表内", not offtable, f"表外 {offtable}")
check("规格 §3 最大饱和 ≤ 0.50", maxsat <= 0.50, f"max S = {maxsat:.3f}")

# ---------------------------------------------------------------- 汇总
print("\n" + "=" * 78)
nfail = sum(1 for _, ok, _ in results if not ok)
print(f"总计 {len(results)} 项，FAIL {nfail}  ⇒  {'ALL PASS' if nfail == 0 else '有失败项'}")
print("=" * 78)
