#!/usr/bin/env python3
"""PM 第四份实现之外的第三份独立读回：mossback.vox + mossback.png。
不 import 开发方的任何脚本；解析器与 PNG 解码全部现写。
判据输出：每条 PASS/FAIL + 数值。"""
import re, struct, sys, zlib
from collections import Counter

VOX = sys.argv[1] if len(sys.argv) > 1 else "assets/mobs/mossback.vox"
PNG = sys.argv[2] if len(sys.argv) > 2 else "assets/palettes/mossback.png"
GUIDE = "/Users/happy/Desktop/opencraft/docs/art/01-style-guide.md"

fails = []
def check(name, ok, detail):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")
    if not ok: fails.append(name)

# ---- .vox ----
b = open(VOX, "rb").read()
check("signature", b[:4] == b"VOX ", b[:4])
ver = struct.unpack("<i", b[4:8])[0]
off, size, n, rgba = 8, None, [], None  # 8 = "VOX " + version
while off + 12 <= len(b):
    cid = b[off:off+4]; csz = struct.unpack("<i", b[off+4:off+8])[0]
    if cid == b"MAIN" and off == 8:
        off += 12; continue
    body = b[off+12:off+12+csz]
    if cid == b"SIZE" and len(body) >= 12: size = struct.unpack("<3i", body[:12])
    elif cid == b"XYZI":
        cnt = struct.unpack("<I", body[:4])[0]; n = []
        for i in range(cnt):
            x, y, z, m = body[4+4*i:8+4*i]; n.append((x, y, z, m))
        check("XYZI count honest", cnt == (len(body)-4)//4, f"claims {cnt}, bytes hold {(len(body)-4)//4}")
    elif cid == b"RGBA": rgba = [tuple(body[i:i+4]) for i in range(0, len(body), 4)]
    off += 12 + csz

check("SIZE", size == (9, 14, 14), str(size))
check("voxel count", len(n) == 672, str(len(n)))
check("budget <=1000", len(n) <= 1000, str(len(n)))
idx = Counter(m for _, _, _, m in n)
check("colorIndex 0 unused", idx.get(0, 0) == 0, str(idx.get(0, 0)))
joint_labels = {t for t in idx if t <= 8}
check("joint labels used ⊆ {1..7} (8 spare)", joint_labels <= set(range(1, 8)) and max(idx) >= 9,
      f"labels={sorted(joint_labels)}, body-group idx={sorted(t for t in idx if t >= 9)}")
def bbox(cond):
    vs = [v for v in n if cond(v)]
    if not vs: return None
    ax = [p for p in vs if True]
    return (min(v[0] for v in vs), max(v[0] for v in vs),
            min(v[1] for v in vs), max(v[1] for v in vs),
            min(v[2] for v in vs), max(v[2] for v in vs), len(vs))
for tag, name, want in [(1,"body",None),(2,"head",(2,6,10,13,7,11)),(3,"arm_l",(1,2,6,8,0,4)),
                        (4,"arm_r",(6,7,6,8,0,4)),(5,"leg_l",(1,2,2,4,0,4)),(6,"leg_r",(6,7,2,4,0,4)),
                        (7,"tail",(4,4,0,1,8,11))]:
    got = bbox(lambda v, t=tag: v[3] == t)
    if want is not None:
        check(f"joint {tag} {name} bbox", got[:6] == want, f"got {got[:6]} count {got[6]}")
ground = set(v[3] for v in n if v[2] == 0)
check("z=0 layer is legs only", ground <= {3, 4, 5, 6}, str(sorted(ground)))
head_z = max(v[2] for v in n if v[3] == 2)
body1_z = max(v[2] for v in n if v[3] == 1)
check("head above body-main(1)", head_z > body1_z, f"head {head_z} vs idx1 {body1_z}")
check("RGBA entries", rgba is not None and len(rgba) == 256, str(len(rgba) if rgba else None))
exp = {1:(0x6E,0x56,0x3C),2:(0xA8,0x84,0x5C),3:(0x42,0x34,0x26),4:(0x42,0x34,0x26),
       5:(0x42,0x34,0x26),6:(0x42,0x34,0x26),7:(0x46,0x70,0x3C),8:(0x2E,0x2A,0x3A)}
for i, c in exp.items():
    check(f"RGBA slot {i} == expected (entry e = idx e+1)", tuple(rgba[i-1][:3]) == c, f"{rgba[i-1]} vs {c}")
check("all voxels inside SIZE", all(v[0]<size[0] and v[1]<size[1] and v[2]<size[2] for v in n), "z-span max "+str(max(v[2] for v in n)))

# ---- PNG (own decoder) ----
p = open(PNG, "rb").read()
assert p[:8] == b"\x89PNG\r\n\x1a\n"
pos, idat, w = 8, b"", None
while pos < len(p):
    ln = struct.unpack(">I", p[pos:pos+4])[0]; typ = p[pos+4:pos+8]
    if typ == b"IHDR": wd, hg, bd, ct = struct.unpack(">IIBB", p[pos+8:pos+18])
    if typ == b"IDAT": idat += p[pos+8:pos+8+ln]
    pos += 12 + ln
check("PNG IHDR", (wd, hg, bd, ct) == (16, 16, 8, 6), f"{wd}x{hg} depth {bd} colortype {ct}")
raw = zlib.decompress(idat)
bpp, stride = 4, 16*4
def paeth(a, bb, c):
    pa, pb, pc = abs(bb-c), abs(a-c), abs(a+b-2*c)
    return a if pa <= pb and pa <= pc else (bb if pb <= pc else c)
px = [[(0,0,0,0)]*16 for _ in range(16)]
prev = bytearray(stride)
for r in range(16):
    ft = raw[r*(stride+1)]; line = bytearray(raw[r*(stride+1)+1:(r+1)*(stride+1)+1])
    for i in range(stride):
        a = line[i-bpp] if i >= bpp else 0; bcv = prev[i]; c = prev[i-bpp] if i >= bpp else 0
        if ft == 0: pass
        elif ft == 1: line[i] = (line[i]+a) & 255
        elif ft == 2: line[i] = (line[i]+bcv) & 255
        elif ft == 3: line[i] = (line[i]+(a+bcv)//2) & 255
        elif ft == 4: line[i] = (line[i]+paeth(a,bcv,c)) & 255
        else: raise SystemExit(f"bad filter {ft}")
    for cx in range(16): px[r][cx] = tuple(line[cx*4:cx*4+4])
    prev = line
# 格号 = 索引：从左上数 (列,行)，索引 = 行*16+列
mismatch = []
for color in range(1, 17):
    r, c = divmod(color, 16)
    png_px = px[r][c][:3]
    if png_px != rgba[color-1][:3]: mismatch.append((color, png_px, rgba[color-1][:3]))
check("PNG cell 1..16 == RGBA entries (PNG wins at runtime)", not mismatch, str(mismatch) if mismatch else "all match")
alphas = set(px[r][c][3] for r in range(16) for c in range(16))
check("PNG alpha all 255", alphas == {255}, str(alphas))
# 色板合规：图上 12 色 + 全 256 格都在 §2.1 表内
g = open(GUIDE, encoding="utf-8").read()
sec = g.split("### 2.1", 1)[1].split("###", 1)[0]
palette_hex = set(h[1:].upper() for h in re.findall(r"#[0-9A-Fa-f]{6}", sec))
def sat(rgb):
    mx, mn = max(rgb)/255, min(rgb)/255
    return 0 if mx == 0 else (mx-mn)/mx
colors = set(px[r][c][:3] for r in range(16) for c in range(16))
bad = [c for c in colors if "%02X%02X%02X" % c not in palette_hex]
check("all PNG colors in style-guide 2.1 table", not bad, f"{len(colors)} distinct, off-table={bad}")
check("max saturation <= 0.50", max(sat(c) for c in colors) <= 0.50, f"{max(sat(c) for c in colors):.3f}")
print("\nRESULT:", "ALL PASS" if not fails else f"{len(fails)} FAIL: {fails}")
sys.exit(1 if fails else 0)
