#!/usr/bin/env python3
"""PM 第四份实现：hollow_wretch.vox + PNG 独立读回（不 import 任何一方脚本）。
含契约 v2 专项：第二色落在同关节包围盒、眼格是表面着色（+y 邻居为空）、z=0 层只许腿。"""
import struct, sys, zlib, hashlib, re

VOX, PNG = sys.argv[1], sys.argv[2]
GUIDE = "/Users/happy/Desktop/opencraft/docs/art/01-style-guide.md"
fails = []
def check(name, ok, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{': ' + detail if detail else ''}")
    if not ok: fails.append(name)

for path, want in ((VOX, "6b5dc511340220c4de29f279389fb16d"), (PNG, "0d0db42dc0b4e6b7b1602ea7c5788bac")):
    got = hashlib.md5(open(path, "rb").read()).hexdigest()
    check(f"md5 {path.split('/')[-1]}", got == want, got)

b = open(VOX, "rb").read()
assert b[:4] == b"VOX "
off, size, vox, rgba = 8, None, [], None
while off + 12 <= len(b):
    cid, csz = b[off:off+4], struct.unpack("<i", b[off+4:off+8])[0]
    if cid == b"MAIN" and off == 8:
        off += 12; continue
    body = b[off+12:off+12+csz]
    if cid == b"SIZE": size = struct.unpack("<3i", body[:12])
    elif cid == b"XYZI":
        n = struct.unpack("<I", body[:4])[0]
        vox = [tuple(body[4+4*i:8+4*i]) for i in range(n)]
    elif cid == b"RGBA": rgba = [tuple(body[i:i+4]) for i in range(0, len(body), 4)]
    off += 12 + csz

check("SIZE", size == (6, 6, 18), str(size))
check("voxel count", len(vox) == 169, str(len(vox)))
from collections import Counter
idx = Counter(v[3] for v in vox)
check("colorIndex 0 unused", idx.get(0, 0) == 0)
expect = {1:2, 2:40, 3:17, 4:17, 5:19, 6:19, 9:2, 10:2, 11:2, 12:2, 17:28, 18:17, 19:2}
check("per-index counts match report table", dict(idx) == expect, f"got={dict(sorted(idx.items()))}")

def bbox(cond):
    vs = [v for v in vox if cond(v)]
    return (min(v[0] for v in vs), max(v[0] for v in vs), min(v[1] for v in vs), max(v[1] for v in vs),
            min(v[2] for v in vs), max(v[2] for v in vs), len(vs)) if vs else None

# v2 关节映射
def joint(i): return (i - 1) % 8 if 1 <= i <= 16 else 0
# 每关节联合包围盒（主标签 + 第二色）
for j, name in [(0,"body"),(1,"head"),(2,"arm_l"),(3,"arm_r"),(4,"leg_l"),(5,"leg_r")]:
    jb = bbox(lambda v, j=j: joint(v[3]) == j)
    print(f"    joint {j} {name}: {jb[:6]} n={jb[6]}")
# 契约 v2：第二色落在同关节包围盒内
for i2 in (9, 10, 11, 12):
    sb = bbox(lambda v, i=i2: v[3] == i2)
    jb = bbox(lambda v, j=joint(i2): joint(v[3]) == j)
    inside = sb[0] >= jb[0] and sb[1] <= jb[1] and sb[2] >= jb[2] and sb[3] <= jb[3] and sb[4] >= jb[4] and sb[5] <= jb[5]
    check(f"idx {i2} (joint {joint(i2)} second colour) inside its joint bbox", inside, f"{sb[:6]} ⊆ {jb[:6]}")

occ = {(v[0], v[1], v[2]) for v in vox}
eyes = sorted((v[0], v[1], v[2]) for v in vox if v[3] == 10)
check("eyes are exactly (1,5,15)+(4,5,15)", eyes == [(1,5,15), (4,5,15)], str(eyes))
check("eye cells are occupied voxels (not carved)", all(e in occ for e in eyes))
check("eye +y neighbours are empty (surface paint)", all((e[0], e[1]+1, e[2]) not in occ for e in eyes))

ground = sorted({v[3] for v in vox if v[2] == 0})
check("z=0 layer is legs only (indices 5/6)", set(ground) <= {5, 6}, str(ground))
head_max = bbox(lambda v: joint(v[3]) == 1)
body_max = bbox(lambda v: joint(v[3]) == 0)
check("head max z above body max z (anti y-up)", head_max[5] > body_max[5], f"{head_max[5]} vs {body_max[5]}")
check("z span exactly 18", max(v[2] for v in vox) == 17 and min(v[2] for v in vox) == 0)
check("RGBA present", rgba is not None and len(rgba) == 256, str(len(rgba) if rgba else None))

# PNG（自写解码）
p = open(PNG, "rb").read(); pos, idat = 8, b""
while pos < len(p):
    ln, typ = struct.unpack(">I", p[pos:pos+4])[0], p[pos+4:pos+8]
    if typ == b"IHDR": w, h = struct.unpack(">II", p[pos+8:pos+16])
    if typ == b"IDAT": idat += p[pos+8:pos+8+ln]
    pos += 12 + ln
raw, bpp, stride = zlib.decompress(idat), 4, w*4
def paeth(a, b0, c):
    pa, pb, pc = abs(b0-c), abs(a-c), abs(a+b0-2*c)
    return a if pa <= pb and pa <= pc else (b0 if pb <= pc else c)
px, prev = [], bytearray(stride)
for r in range(h):
    ft = raw[r*(stride+1)]; line = bytearray(raw[r*(stride+1)+1:(r+1)*(stride+1)+1])
    for i in range(stride):
        a = line[i-bpp] if i >= bpp else 0; bb = prev[i]; c = prev[i-bpp] if i >= bpp else 0
        if ft == 1: line[i] = (line[i]+a) & 255
        elif ft == 2: line[i] = (line[i]+bb) & 255
        elif ft == 3: line[i] = (line[i]+(a+bb)//2) & 255
        elif ft == 4: line[i] = (line[i]+paeth(a, bb, c)) & 255
    px.append([tuple(line[x*4:x*4+4]) for x in range(w)]); prev = line
check("PNG 16x16 RGBA", (w, h) == (16, 16))
def cell(i): r, c = divmod(i, 16); return px[r][c]
bad = [i for i in range(1, 256) if cell(i)[:3] != rgba[i-1][:3]] if rgba else []
check("PNG cell k == RGBA entry k (engine runtime consistency)", not bad, f"bad={bad[:6]}")
check("PNG cell 0 is the never-used slot (U0 #1E1E22)", cell(0)[:3] == (0x1E, 0x1E, 0x22), str(cell(0)))
alphas = {px[r][c][3] for r in range(16) for c in range(16)}
check("alpha all 255", alphas == {255}, str(alphas))
g = open(GUIDE, encoding="utf-8").read()
sec = g.split("### 2.1", 1)[1].split("###", 1)[0]
pal = set(h[1:].upper() for h in re.findall(r"#[0-9A-Fa-f]{6}", sec))
used = {tuple(rgba[i-1][:3]) for i in sorted(idx)}
off_table = [c for c in used if "%02X%02X%02X" % c not in pal]
check("all used colours in style-guide 2.1", not off_table, f"{len(used)} distinct, off-table={off_table}")
def sat(rgb):
    mx, mn = max(rgb)/255, min(rgb)/255
    return 0 if mx == 0 else (mx-mn)/mx
check("max saturation <= 0.50", max(sat(c) for c in used) <= 0.50, f"{max(sat(c) for c in used):.3f}")
print("RESULT:", "ALL PASS" if not fails else f"{len(fails)} FAIL {fails}")
sys.exit(1 if fails else 0)
