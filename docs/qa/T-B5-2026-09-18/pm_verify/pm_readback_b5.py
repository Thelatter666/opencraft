#!/usr/bin/env python3
"""PM 第四份实现：T-B5 两只返工模型独立读回（不 import 双方脚本）。
通用判据 + 本卡专项（腿高/外缘齐平/苔不越头/五官=表面着色）。"""
import struct, sys, zlib, hashlib, re
from collections import Counter

MB_VOX, MB_PNG, WR_VOX, WR_PNG = sys.argv[1:5]
GUIDE = "/Users/happy/Desktop/opencraft/docs/art/01-style-guide.md"
fails = []
def check(name, ok, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{': ' + detail if detail else ''}")
    if not ok: fails.append(name)

def parse_vox(path):
    b = open(path, "rb").read(); assert b[:4] == b"VOX "
    off, size, vox, rgba = 8, None, [], None
    while off + 12 <= len(b):
        cid, csz = b[off:off+4], struct.unpack("<i", b[off+4:off+8])[0]
        if cid == b"MAIN" and off == 8: off += 12; continue
        body = b[off+12:off+12+csz]
        if cid == b"SIZE": size = struct.unpack("<3i", body[:12])
        elif cid == b"XYZI":
            n = struct.unpack("<I", body[:4])[0]
            vox = [tuple(body[4+4*i:8+4*i]) for i in range(n)]
        elif cid == b"RGBA": rgba = [tuple(body[i:i+4]) for i in range(0, len(body), 4)]
        off += 12 + csz
    return size, vox, rgba

def parse_png(path):
    p = open(path, "rb").read(); pos, idat = 8, b""
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
    return px

def joint(i): return (i - 1) % 8 if 1 <= i <= 16 else 0
def bbox(vs):
    return (min(v[0] for v in vs), max(v[0] for v in vs), min(v[1] for v in vs), max(v[1] for v in vs),
            min(v[2] for v in vs), max(v[2] for v in vs), len(vs)) if vs else None
def inside(inner, outer):
    return outer and inner and inner[0] >= outer[0] and inner[1] <= outer[1] and inner[2] >= outer[2] and \
           inner[3] <= outer[3] and inner[4] >= outer[4] and inner[5] <= outer[5]

def common(tag, vox_path, png_path, want_size, want_count, want_zspan, ground_joints):
    print(f"\n===== {tag} =====")
    size, vox, rgba = parse_vox(vox_path)
    idx = Counter(v[3] for v in vox)
    check(f"{tag} SIZE", size == want_size, str(size))
    check(f"{tag} voxel count", len(vox) == want_count, str(len(vox)))
    check(f"{tag} colorIndex 0 unused", idx.get(0, 0) == 0)
    zs = (min(v[2] for v in vox), max(v[2] for v in vox))
    check(f"{tag} z-span exactly {want_zspan}", zs[1]-zs[0]+1 == want_zspan, str(zs))
    occ = {(v[0], v[1], v[2]) for v in vox}
    ground = sorted({joint(v[3]) for v in vox if v[2] == 0})
    check(f"{tag} z=0 layer is legs/feet joints only", set(ground) <= set(ground_joints), str(ground))
    # 第二色落在其关节包围盒内（v2）
    second = sorted(i for i in idx if 9 <= i <= 16)
    for i2 in second:
        sb = bbox([v for v in vox if v[3] == i2]); jb = bbox([v for v in vox if joint(v[3]) == joint(i2)])
        check(f"{tag} idx {i2} inside joint {joint(i2)} bbox", inside(sb, jb), f"{sb[:6]} ⊆ {jb[:6]}")
    # 五官=表面着色：idx10 每格在占用表内且 +y 邻居为空
    face = sorted((v[0], v[1], v[2]) for v in vox if v[3] == 10)
    check(f"{tag} face cells all painted on surfaces (+y empty)",
          bool(face) and all(c in occ and (c[0], c[1]+1, c[2]) not in occ for c in face), f"{len(face)} cells {face}")
    hb = bbox([v for v in vox if joint(v[3]) == 1])
    check(f"{tag} face cells inside head joint bbox",
          all(inside((c[0],c[0],c[1],c[1],c[2],c[2]), hb) for c in face), f"head bbox {hb[:6]}")
    # PNG 与 RGBA 一致 + 色板合规
    px = parse_png(png_path)
    def cell(i): r, c = divmod(i, 16); return px[r][c]
    bad = [i for i in range(1, 256) if cell(i)[:3] != rgba[i-1][:3]]
    check(f"{tag} PNG cell k == RGBA entry k", not bad, f"bad={bad[:5]}")
    g = open(GUIDE, encoding="utf-8").read(); sec = g.split("### 2.1", 1)[1].split("###", 1)[0]
    pal = set(h[1:].upper() for h in re.findall(r"#[0-9A-Fa-f]{6}", sec))
    used = {tuple(rgba[i-1][:3]) for i in sorted(idx)}
    check(f"{tag} used colours in §2.1", not [c for c in used if "%02X%02X%02X" % c not in pal], f"{len(used)} distinct")
    def sat(c):
        mx, mn = max(c)/255, min(c)/255
        return 0 if mx == 0 else (mx-mn)/mx
    check(f"{tag} max saturation <= 0.50", max(sat(c) for c in used) <= 0.50, f"{max(sat(c) for c in used):.3f}")
    print(f"  md5 {tag} vox: {hashlib.md5(open(vox_path,'rb').read()).hexdigest()}")
    print(f"  md5 {tag} png: {hashlib.md5(open(png_path,'rb').read()).hexdigest()}")
    for i in sorted(idx):
        sb = bbox([v for v in vox if v[3] == i])
        print(f"    idx {i:3d} n={sb[6]:4d} x {sb[0]}..{sb[1]} y {sb[2]}..{sb[3]} z {sb[4]}..{sb[5]} -> joint {joint(i)}")
    return vox, idx

# ---- Mossback v5 专项 ----
vox, idx = common("MOSS", MB_VOX, MB_PNG, (7,17,14), 705, 14, {2,3,4,5})
legs = [v for v in vox if v[3] in (3,4,5,6)]
lb = bbox(legs)
check("MOSS leg z height == 7 (0..6)", (lb[4], lb[5]) == (0, 6), f"z {lb[4]}..{lb[5]}")
tor = bbox([v for v in vox if v[3] == 1])
legx = {v[0] for v in legs}
check("MOSS legs flush with torso x range", legx == {tor[0], tor[0]+1, tor[1]-1, tor[1]}, f"legs x {sorted(legx)} torso x {tor[0]}..{tor[1]}")
joint0 = [v for v in vox if joint(v[3]) == 0]
head = [v for v in vox if joint(v[3]) == 1]
check("MOSS head max z >= body/moss max z (no moss above head)", bbox(head)[5] >= bbox(joint0)[5], f"head {bbox(head)[5]} vs joint0 {bbox(joint0)[5]}")

# ---- Wretch v5 专项 ----
vox, idx = common("WRETCH", WR_VOX, WR_PNG, (12,9,18), 496, 18, {4,5})
face_n = idx.get(10, 0)
check("WRETCH face cells >= 14 (2x2 eyes + nose + mouth)", face_n >= 14, str(face_n))
for j, name, want_w in [(2,"arm_l",2),(3,"arm_r",2),(4,"leg_l",2),(5,"leg_r",2)]:
    jb = bbox([v for v in vox if joint(v[3]) == j])
    check(f"WRETCH joint {j} {name} limb width == {want_w}", jb[1]-jb[0]+1 == want_w, f"x {jb[0]}..{jb[1]}")
print("\nRESULT:", "ALL PASS" if not fails else f"{len(fails)} FAIL {fails}")
sys.exit(1 if fails else 0)
