#!/usr/bin/env python3
"""T-B3 · 独立复查 + 出图。**不读分层稿**，只读落盘的 .vox。

用法：
    python3 vox_inspect.py <worktree_root> <out_dir>

本副本 = docs/qa/T-B2b-2026-09-18/tools/vox_inspect.py（契约 v2 版）的**内容替换版**：
解析器、栅格化、光栅化一个字节没改（仍是与 C++ parse_vox 无关的**第二份独立实现**），
换的是——**资产路径**（hollow_wretch）、**关节显示名**（人形用 arm/leg）、
**躯干组的索引口径**（本模型 17..19，不是 mossback 的 17..24），
外加本卡特有的一条断言：**眼睛（索引 10）的包围盒必须落在头（关节 1）的包围盒内**。

做五件事：
  1. 用**自己的**解析器重读 assets/mobs/hollow_wretch.vox，把 SIZE / XYZI / RGBA 解出来，
     逐条打印；
  2. 复算契约自查表的数字：体素数、包围盒、每个 colorIndex 的包围盒
     （用来核对"索引 5 = 左腿 = 低 x 后 y"这类方位断言）、
     按 mob_mesh.cpp 的邻居剔除规则数暴露面与三角形、colorIndex 0 出现次数；
  3. 渲染 4 张视图 + 1 张关节分色图，拼成 contact sheet PNG；
  4. 打印调色板 PNG 的 16×16 格号回读（验证"格号=索引号"）；
  5. ★ 契约 v2 证据：索引 10（头第二色 = 眼窝）与索引 9/11/12 的关节归属与包围盒归属。

出图是用来看形体的（美术自检），不是交付物；交付物只有 .vox 与 .png 两个资产。
"""

import os
import struct
import sys
import zlib

FACE_NAMES = ["+x", "-x", "+y", "-y", "+z", "-z"]
FACES = {
    "+x": ((1, 0, 0), [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]),
    "-x": ((-1, 0, 0), [(0, 0, 1), (0, 1, 1), (0, 1, 0), (0, 0, 0)]),
    "+y": ((0, 1, 0), [(0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0)]),
    "-y": ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
    "+z": ((0, 0, 1), [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]),
    "-z": ((0, 0, -1), [(1, 0, 0), (0, 0, 0), (0, 1, 0), (1, 1, 0)]),
}

# 与引擎的面明暗同构：顶亮 / 侧中 / 底暗
FACE_SHADE = {"+z": 1.00, "+y": 0.72, "-y": 0.60, "+x": 0.84, "-x": 0.84, "-z": 0.42}

# 契约 v2：1..16 = 关节标签（9..16 是关节 (idx-1) mod 8 的第二色），17+ 无标签归躯干
JOINT_OF_INDEX = {i: (i - 1) % 8 for i in range(1, 17)}
JOINT_NAMES = ["body 躯干", "head 头", "arm_l 左臂", "arm_r 右臂",
               "leg_l 左腿", "leg_r 右腿", "tail 尾", "spare 备用"]
JOINT_COLORS = [(150, 150, 150), (255, 210, 120), (110, 170, 255), (60, 110, 200),
                (255, 120, 120), (190, 60, 60), (170, 110, 220), (240, 240, 100)]

# 本模型体检用：躯干组的颜色槽（契约 v2 的 17+）与"头的两种索引"
BODY_GROUP_MIN = 17
HEAD_INDICES = (2, 10)
EYE_INDEX = 10
EXPECT_Z_SPAN = 18
EXPECT_JOINTS = 6

ASSET = "hollow_wretch"


def read_vox(path):
    """独立解析器。只认 SIZE / XYZI / RGBA，其它块跳过。"""
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"VOX ", "missing VOX signature"
    version = struct.unpack_from("<i", data, 4)[0]
    size = None
    voxels = None
    palette = [None] * 256
    offset = 8
    chunks = []
    while offset + 12 <= len(data):
        tag = data[offset:offset + 4]
        content_len, children_len = struct.unpack_from("<ii", data, offset + 4)
        assert content_len >= 0, f"{tag!r} declares a negative content length"
        assert offset + 12 + content_len <= len(data), f"{tag!r} runs past the end"
        body = data[offset + 12:offset + 12 + content_len]
        chunks.append((tag.decode("latin1"), content_len, children_len))
        if tag == b"SIZE":
            size = struct.unpack_from("<iii", body, 0)
        elif tag == b"XYZI":
            n = struct.unpack_from("<I", body, 0)[0]
            assert len(body) >= 4 + 4 * n, "XYZI claims more voxels than it holds"
            voxels = [tuple(body[4 + 4 * i:8 + 4 * i]) for i in range(n)]
        elif tag == b"RGBA":
            for i in range(min(255, len(body) // 4)):
                r, g, b, a = body[i * 4:i * 4 + 4]
                palette[i + 1] = (r, g, b, a)
        offset += 12 + content_len
    assert size is not None and voxels is not None, "SIZE or XYZI missing"
    return version, size, voxels, palette, chunks


def exposed_faces(size, voxels):
    """与 mob_mesh.cpp build_mob_mesh 同一条剔除规则：邻居被占用则该面不生成。"""
    sx, sy, sz = size
    occupied = {(v[0], v[1], v[2]) for v in voxels}
    faces = []
    for x, y, z, c in voxels:
        for name, (n, corners) in FACES.items():
            nb = (x + n[0], y + n[1], z + n[2])
            if nb in occupied:
                continue
            faces.append((x, y, z, c, name, corners))
    return faces


def raster_tri(buf, w, h, zbuf, pts, color, shade):
    """屏幕坐标 + 深度 的三点光栅化，带 z-buffer。"""
    (ax, ay, da), (bx, by, db), (cx, cy, dc) = pts
    minx = max(0, int(min(ax, bx, cx)))
    maxx = min(w - 1, int(max(ax, bx, cx)) + 1)
    miny = max(0, int(min(ay, by, cy)))
    maxy = min(h - 1, int(max(ay, by, cy)) + 1)
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(den) < 1e-9:
        return
    shade_color = tuple(min(255, int(v * shade)) for v in color)
    for py in range(miny, maxy + 1):
        for px in range(minx, maxx + 1):
            w0 = ((by - cy) * (px + 0.5 - cx) + (cx - bx) * (py + 0.5 - cy)) / den
            w1 = ((cy - ay) * (px + 0.5 - cx) + (ax - cx) * (py + 0.5 - cy)) / den
            w2 = 1.0 - w0 - w1
            if w0 < -1e-9 or w1 < -1e-9 or w2 < -1e-9:
                continue
            d = w0 * da + w1 * db + w2 * dc
            i = py * w + px
            if d >= zbuf[i]:
                continue
            zbuf[i] = d
            buf[i] = shade_color


def render_view(size, voxels, palette, color_of, basis, scale, margin):
    """正交投影渲染。basis = (screen_right, screen_up, toward_camera) 三个 3 元组。"""
    sx, sy, sz = size
    right, up, toward = basis
    corners = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0), (1, 0, 1), (0, 1, 1), (1, 1, 1)]
    us, vs = [], []
    for v in voxels:
        for c in corners:
            p = (v[0] + c[0], v[1] + c[1], v[2] + c[2])
            us.append(sum(p[i] * right[i] for i in range(3)))
            vs.append(sum(p[i] * up[i] for i in range(3)))
    umin, umax = min(us), max(us)
    vmin, vmax = min(vs), max(vs)
    w = int((umax - umin) * scale) + 2 * margin
    h = int((vmax - vmin) * scale) + 2 * margin
    buf = [(28, 30, 36)] * (w * h)
    zbuf = [1e18] * (w * h)

    def project(p):
        u = (sum(p[i] * right[i] for i in range(3)) - umin) * scale + margin
        v = (vmax - sum(p[i] * up[i] for i in range(3))) * scale + margin
        depth = -sum(p[i] * toward[i] for i in range(3))
        return (u, v, depth)

    occupied = {(v[0], v[1], v[2]) for v in voxels}
    for x, y, z, c in voxels:
        color = color_of(x, y, z, c, palette)
        for name, (n, fcorners) in FACES.items():
            if sum(n[i] * toward[i] for i in range(3)) <= 0:
                continue  # 背面
            if (x + n[0], y + n[1], z + n[2]) in occupied:
                continue
            quad = [project((x + cc[0], y + cc[1], z + cc[2])) for cc in fcorners]
            shade = FACE_SHADE[name]
            raster_tri(buf, w, h, zbuf, [quad[0], quad[1], quad[2]], color, shade)
            raster_tri(buf, w, h, zbuf, [quad[0], quad[2], quad[3]], color, shade)
    return w, h, buf


def write_png(path, w, h, buf):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b = buf[y * w + x]
            raw += bytes((r, g, b, 255))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def norm(v):
    m = sum(c * c for c in v) ** 0.5
    return tuple(c / m for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def bbox(cells):
    lo = [min(c[i] for c in cells) for i in range(3)]
    hi = [max(c[i] for c in cells) for i in range(3)]
    return lo, hi


def inside(inner_lo, inner_hi, outer_lo, outer_hi):
    return all(inner_lo[i] >= outer_lo[i] and inner_hi[i] <= outer_hi[i] for i in range(3))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    root, out_dir = sys.argv[1], sys.argv[2]
    vox_path = os.path.join(root, "assets", "mobs", ASSET + ".vox")
    version, size, voxels, palette, chunks = read_vox(vox_path)

    print(f"file        : {vox_path} ({os.path.getsize(vox_path)} bytes)")
    print(f"version     : {version}")
    print("chunks      : " + ", ".join(f"{t}({c}B)" for t, c, _ in chunks))
    print(f"SIZE        : {size[0]} x {size[1]} x {size[2]}  (max side {max(size)})")
    print(f"voxels      : {len(voxels)}")
    assert len(voxels) == len({(v[0], v[1], v[2]) for v in voxels}), "duplicate cells"

    # —— colorIndex 0 ——
    zero = sum(1 for v in voxels if v[3] == 0)
    print(f"colorIndex 0: {zero} voxels  (contract: must be 0)")

    # —— 包围盒（按模型轴；.vox 的 z 就是"上"）——
    lo = [min(v[i] for v in voxels) for i in range(3)]
    hi = [max(v[i] for v in voxels) for i in range(3)]
    print(f"bbox        : x {lo[0]}..{hi[0]}  y {lo[1]}..{hi[1]}  z {lo[2]}..{hi[2]}")
    span = [hi[i] - lo[i] + 1 for i in range(3)]
    print(f"span        : x {span[0]}  y {span[1]}  z {span[2]}  (z-span must be {EXPECT_Z_SPAN})")

    # —— 每个 colorIndex 的包围盒（方位断言的机器判据）——
    print("per-colorIndex bbox and joint mapping:")
    per_index = {}
    for c in sorted({v[3] for v in voxels}):
        cells = [v for v in voxels if v[3] == c]
        per_index[c] = cells
        clo, chi = bbox(cells)
        joint = JOINT_OF_INDEX.get(c, 0)
        rgb = palette[c][:3] if palette[c] else None
        print(f"  idx {c:>2}  {len(cells):>4} voxels  "
              f"x {clo[0]}..{chi[0]}  y {clo[1]}..{chi[1]}  z {clo[2]}..{chi[2]}  "
              f"rgb {rgb}  -> joint {joint} {JOINT_NAMES[joint]}")

    # —— ★ 契约 v2 证据（本卡特有）：第二色的体素**落在同关节的包围盒内** ——
    print("contract-v2 evidence: each second colour must sit inside ITS joint's bbox")
    v2_ok = True
    for idx in sorted(c for c in per_index if 9 <= c <= 16):
        joint = JOINT_OF_INDEX[idx]
        joint_cells = [v for v in voxels if JOINT_OF_INDEX.get(v[3], 0) == joint]
        jlo, jhi = bbox(joint_cells)
        ilo, ihi = bbox(per_index[idx])
        ok = inside(ilo, ihi, jlo, jhi)
        v2_ok &= ok
        print(f"  idx {idx:>2} (joint {joint} {JOINT_NAMES[joint]}): bbox "
              f"x {ilo[0]}..{ihi[0]} y {ilo[1]}..{ihi[1]} z {ilo[2]}..{ihi[2]}  "
              f"inside joint bbox x {jlo[0]}..{jhi[0]} y {jlo[1]}..{jhi[1]} z {jlo[2]}..{jhi[2]}  "
              f"{'CONTAINED' if ok else 'OUTSIDE'}")
    print(f"  -> {'every second colour is inside its joint' if v2_ok else 'CONTRACT VIOLATION'}")
    assert v2_ok, "a joint's second colour leaked outside that joint's bounding box"

    # —— 关节聚集检查：每个有体素的关节，它的所有体素必须同属一个连通块 ——
    print("joint groups actually present (body = index 1/9 plus every 17+ colour):")
    groups = {}
    for v in voxels:
        groups.setdefault(JOINT_OF_INDEX.get(v[3], 0), 0)
        groups[JOINT_OF_INDEX.get(v[3], 0)] += 1
    for j in sorted(groups):
        print(f"  joint {j} {JOINT_NAMES[j]}: {groups[j]} voxels")
    print(f"  joints (parts) = {len(groups)} -> the startup log's 'joints' field")

    # —— 头比躯干高（防 y-up 趴地）——
    # 契约那条是"头（标签 2）的 z 应高于躯干**主体**"。本模型躯干最高的是**驼峰**
    # （背的一部分，属躯干组 17+），所以两个口径都报：对"躯干主体"（索引 1）与
    # 对"整个 joint 0"（含驼峰、空洞、破布），供 PM 判。
    head = [v for v in voxels if JOINT_OF_INDEX.get(v[3], 0) == 1]
    body_core = [v for v in voxels if v[3] == 1]
    body_all = [v for v in voxels if JOINT_OF_INDEX.get(v[3], 0) == 0]
    for label, grp in (("torso core (idx 1)", body_core), ("all joint 0 (incl. hump/rag)", body_all)):
        hz = sum(v[2] for v in head) / len(head)
        bz = sum(v[2] for v in grp) / len(grp)
        hmax = max(v[2] for v in head)
        bmax = max(v[2] for v in grp)
        verdict = "OK" if (hz > bz and hmax > bmax) else "CHECK"
        print(f"head vs {label}: centroid {hz:.2f} vs {bz:.2f}, max z {hmax} vs {bmax}  [{verdict}]")

    # —— ★ 抗 y-up 的最锐判据：接触地面的那一层只许是**腿** ——
    bottom = sorted({v[3] for v in voxels if v[2] == lo[2]})
    bottom_joints = sorted({JOINT_OF_INDEX.get(c, 0) for c in bottom})
    leg_only = all(j in (4, 5) for j in bottom_joints)
    print(f"lowest layer z={lo[2]} colorIndices {bottom} -> joints {bottom_joints} "
          f"({'legs only - model stands on its legs' if leg_only else 'NOT leg-only - model may be lying down'})")

    # —— 眼窝必须在头的包围盒内（本卡的契约判据，人眼一列也印出来）——
    eyes = sorted(per_index.get(EYE_INDEX, []))
    jlo, jhi = bbox(head)
    print(f"eye sockets (idx {EYE_INDEX}) at {[(e[0], e[1], e[2]) for e in eyes]}; "
          f"head joint bbox x {jlo[0]}..{jhi[0]} y {jlo[1]}..{jhi[1]} z {jlo[2]}..{jhi[2]}")

    # —— 暴露面 ——
    faces = exposed_faces(size, voxels)
    print(f"exposed faces: {len(faces)}  (budget 2000)")
    print(f"triangles    : {len(faces) * 2}  (what the startup log reports)")
    per_color = {}
    for f in faces:
        per_color[f[3]] = per_color.get(f[3], 0) + 1
    print("faces per colorIndex: " + ", ".join(f"{c}:{n}" for c, n in sorted(per_color.items())))

    # —— 渲染 ——
    os.makedirs(out_dir, exist_ok=True)
    zaxis = norm((1.0, 1.0, 1.0))
    xaxis = norm(cross((0.0, 0.0, 1.0), zaxis))
    yaxis = cross(zaxis, xaxis)
    # 明确基：屏幕右、屏幕上、朝相机
    basis_side_l = norm((0.0, 1.0, 0.0)), norm((0.0, 0.0, 1.0)), norm((-1.0, 0.0, 0.0))
    basis_side_r = norm((0.0, -1.0, 0.0)), norm((0.0, 0.0, 1.0)), norm((1.0, 0.0, 0.0))
    basis_front = norm((1.0, 0.0, 0.0)), norm((0.0, 0.0, 1.0)), norm((0.0, 1.0, 0.0))
    basis_top = norm((1.0, 0.0, 0.0)), norm((0.0, 1.0, 0.0)), norm((0.0, 0.0, 1.0))
    basis_iso = xaxis, yaxis, zaxis

    def true_color(x, y, z, c, pal):
        rgb = pal[c][:3] if pal[c] else (255, 0, 255)
        return rgb

    def joint_color(x, y, z, c, pal):
        return JOINT_COLORS[JOINT_OF_INDEX.get(c, 0)]

    def silhouette(x, y, z, c, pal):
        return (235, 235, 240)

    jobs = [
        ("side-left-nose-right", basis_side_l, true_color),
        ("front-face", basis_front, true_color),
        ("top", basis_top, true_color),
        ("iso", basis_iso, true_color),
        ("iso-joints", basis_iso, joint_color),
    ]
    tiles = []
    for name, basis, colfn in jobs:
        w, h, buf = render_view(size, voxels, palette, colfn, basis, 16, 8)
        tiles.append((name, w, h, buf))
        print(f"rendered {name}: {w}x{h}")

    # 剪影：只判轮廓（"一眼读出人形 + 前倾 + 细长四肢"靠这张）
    sil = []
    for name, basis in (("sil-side", basis_side_l), ("sil-front", basis_front),
                        ("sil-iso", basis_iso)):
        w, h, buf = render_view(size, voxels, palette, silhouette, basis, 16, 8)
        sil.append((name, w, h, buf))
        print(f"rendered {name}: {w}x{h}")

    def stack(rows_of_tiles, filename):
        gap = 8
        sw = max(sum(t[1] for t in row) + gap * (len(row) + 1) for row in rows_of_tiles)
        sh = sum(max(t[2] for t in row) for row in rows_of_tiles) + gap * (len(rows_of_tiles) + 1)
        sheet = [(24, 24, 28)] * (sw * sh)
        y0 = gap
        for row in rows_of_tiles:
            x0 = gap
            for name, w, h, buf in row:
                for yy in range(h):
                    for xx in range(w):
                        sheet[(y0 + yy) * sw + x0 + xx] = buf[yy * w + xx]
                x0 += w + gap
            y0 += max(t[2] for t in row) + gap
        write_png(os.path.join(out_dir, filename), sw, sh, sheet)
        print(f"wrote {os.path.join(out_dir, filename)}")

    stack([tiles], f"{ASSET}_contact_sheet.png")
    stack([sil], f"{ASSET}_silhouette.png")
    print("contact sheet tiles: " + " | ".join(t[0] for t in tiles))

    # —— 调色板 PNG 回读：格号 = 索引号 ——
    png_path = os.path.join(root, "assets", "palettes", ASSET + ".png")
    if os.path.exists(png_path):
        with open(png_path, "rb") as f:
            blob = f.read()
        assert blob[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
        ihdr_w, ihdr_h, depth, ctype = struct.unpack_from(">IIBB", blob, 16)
        print(f"palette png : {ihdr_w}x{ihdr_h} depth {depth} color type {ctype} "
              f"({len(blob)} bytes)")
        # 解 IDAT。PNG 块布局 = [4 字节长度][4 字节类型][数据][4 字节 CRC]
        off = 8
        idat = b""
        while off + 12 <= len(blob):
            ln = struct.unpack_from(">I", blob, off)[0]
            tag = blob[off + 4:off + 8]
            if tag == b"IDAT":
                idat += blob[off + 8:off + 8 + ln]
            off += 12 + ln
        raw = zlib.decompress(idat)
        px = []
        for row in range(ihdr_h):
            base = row * (1 + ihdr_w * 4)
            assert raw[base] == 0, "unexpected PNG filter type"
            px.append([tuple(raw[base + 1 + 4 * i:base + 5 + 4 * i]) for i in range(ihdr_w)])
        alphas = {p[3] for row in px for p in row}
        print(f"palette png : alpha set {sorted(alphas)} (opaque asset -> {{255}})")
        for index in sorted({v[3] for v in voxels} | {0}):
            row, col = divmod(index, 16)
            cell = px[row][col][:3]
            model = palette[index][:3] if palette[index] else None
            flag = "match" if (index in (0,) or cell == model) else "MISMATCH"
            print(f"  cell {index:>2} (row {row}, col {col:>2}) png {cell}  vox {model}  {flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
