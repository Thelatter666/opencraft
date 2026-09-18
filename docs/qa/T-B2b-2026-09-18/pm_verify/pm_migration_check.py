#!/usr/bin/env python3
"""PM 第四份实现：mossback v1→v2 迁移逐字节独立复算。
输入：旧 .vox/.png（main 合入件）+ 新 .vox/.png（分支件）。不 import 任何一方脚本。"""
import struct, sys, zlib, hashlib

OLD_VOX, NEW_VOX, OLD_PNG, NEW_PNG = sys.argv[1:5]
fails = []
def check(name, ok, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{': ' + detail if detail else ''}")
    if not ok: fails.append(name)

def parse_vox(path):
    b = open(path, "rb").read()
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
    return size, vox, rgba

def parse_png(path):
    p = open(path, "rb").read(); pos, idat = 8, b""
    while pos < len(p):
        ln, typ = struct.unpack(">I", p[pos:pos+4])[0], p[pos+4:pos+8]
        if typ == b"IHDR": w, h = struct.unpack(">II", p[pos+8:pos+16])
        if typ == b"IDAT": idat += p[pos+8:pos+8+ln]
        pos += 12 + ln
    raw, bpp, stride = zlib.decompress(idat), 4, w*4
    def paeth(a, b, c):
        pa, pb, pc = abs(b-c), abs(a-c), abs(a+b-2*c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)
    px, prev = [], bytearray(stride)
    for r in range(h):
        ft = raw[r*(stride+1)]; line = bytearray(raw[r*(stride+1)+1:(r+1)*(stride+1)+1])
        for i in range(stride):
            a = line[i-bpp] if i >= bpp else 0; b0 = prev[i]; c = prev[i-bpp] if i >= bpp else 0
            if ft == 1: line[i] = (line[i]+a) & 255
            elif ft == 2: line[i] = (line[i]+b0) & 255
            elif ft == 3: line[i] = (line[i]+(a+b0)//2) & 255
            elif ft == 4: line[i] = (line[i]+paeth(a, b0, c)) & 255
        px.append([tuple(line[x*4:x*4+4]) for x in range(w)]); prev = line
    return px  # px[row][col]

for path, want in ((OLD_VOX, "f8072830088c024aa4ad724465a1b99b"),
                   (NEW_VOX, "9215d2bef3333821c07c83b20aa1c360"),
                   (NEW_PNG, "74dcc2a82723523d9f1d0b40b92ba7db")):
    got = hashlib.md5(open(path, "rb").read()).hexdigest()
    check(f"md5 {path.split('/')[-1]}", got == want, got)

so, vo, ro = parse_vox(OLD_VOX)
sn, vn, rn = parse_vox(NEW_VOX)
check("SIZE unchanged", so == sn, f"{so} vs {sn}")
check("voxel count unchanged", len(vo) == len(vn), f"{len(vo)} vs {len(vn)}")
check("coordinate sequence byte-identical", all(a[:3] == b[:3] for a, b in zip(vo, vn)))
viol = [(i, a, b) for i, (a, b) in enumerate(zip(vo, vn))
        if b[3] != (a[3]+8 if 9 <= a[3] <= 16 else a[3])]
check("colour rule exactly {9..16 -> +8, else same}", not viol, f"violations={len(viol)} {viol[:3]}")

# RGBA：entry e(0-based) ↔ colorIndex e+1
U0 = (0x1E, 0x1E, 0x22, 255)
bad = []
for e in range(256):
    o, n = ro[e], rn[e]
    idx = e + 1
    want = U0 if 9 <= idx <= 16 else (ro[idx-9] if 17 <= idx <= 24 else o)
    if n != want: bad.append((idx, o, n, want))
check("RGBA migration (9..16=U0, 17..24=old 9..16, else same)", not bad, f"bad={len(bad)} {bad[:3]}")

po, pn = parse_png(OLD_PNG), parse_png(NEW_PNG)
def cell(px, idx):
    r, c = divmod(idx, 16)
    return px[r][c]
badp = []
for idx in range(16*16):
    o, n = cell(po, idx), cell(pn, idx)
    want = U0 if 9 <= idx <= 16 else (o if not (17 <= idx <= 24) else cell(po, idx-8))
    if n != want: badp.append((idx, o, n, want))
check("PNG migration (cells 9..16=U0, 17..24=old 9..16, else same)", not badp, f"bad={len(badp)} {badp[:3]}")
print("RESULT:", "ALL PASS" if not fails else f"{len(fails)} FAIL {fails}")
sys.exit(1 if fails else 0)
