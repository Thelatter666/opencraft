#!/usr/bin/env python3
"""T-B5 · 独立复查 + 出图（读**落盘的** .vox，不读分层稿）。

用法：
    python3 vox_inspect.py model <vox_path> <out_dir> <tag>
    python3 vox_inspect.py compare <out_png> <png_left> <png_right>

与 T-B2b/T-B3 的原件同构（docs/qa/T-B2b-2026-09-18/tools/vox_inspect.py）：解析与
光栅化照抄，两处新增都是本卡带来的：
  1. 接受**任意** .vox 路径与标签 ⇒ 同一份脚本既能查 v5 交付件，也能查从 git 取出的
     v4 旧件（卡面 §4.5 要求"与旧版同角度对照"）；
  2. 新增 `render_perspective`：**针孔透视**（非正交）渲染，即卡面 §2.5 的"游戏机位"，
     相机放在 4.5 格外、略微俯视 —— 这是本卡新增的可读性门槛的取证工具。

做四件事：
  1. 用**自己的**解析器重读 .vox（与 C++ parse_vox 独立实现），把 SIZE / XYZI / RGBA
     三个块解出来，逐条打印；
  2. 复算契约自查表的数字：体素数、包围盒、每个 colorIndex 的包围盒（用来核对
     "索引 3 = 左前肢 = 低 x 前 y"这类方位断言）、按 mob_mesh.cpp 的邻居剔除规则
     数暴露面与三角形、colorIndex 0 出现次数；
  3. 渲染 4 张正交视图 + 1 张关节分色图 + 1 张剪影 + 1 张**透视游戏机位**图；
  4. 打印调色板 PNG 的 16×16 格号回读（验证"格号=索引号"）。

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
JOINT_NAMES = ["body 躯干", "head 头", "arm_l 左前肢/左臂", "arm_r 右前肢/右臂",
               "leg_l 左后肢/左腿", "leg_r 右后肢/右腿", "tail 尾", "spare 备用"]
JOINT_COLORS = [(150, 150, 150), (255, 210, 120), (110, 170, 255), (60, 110, 200),
                (255, 120, 120), (190, 60, 60), (170, 110, 220), (240, 240, 100)]


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
    """屏幕坐标 + 深度 的三点光栅化，带 z-buffer。深度**越小越近**。"""
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


def render_perspective(size, voxels, palette, color_of, eye, target, px_per_unit,
                       viewport):
    """★ 针孔透视（**非正交**）渲染 —— 卡面 §2.5 的"游戏机位"。

    与 render_view 的差别只有一处：屏幕上的一点 = (x_cam / z_cam, y_cam / z_cam)，
    即"近大远小"；正交投影里这一步是常数缩放，远近一样大。
    相机用 (eye, target) 给出；px_per_unit = **在 target 那个距离上** 1 体素占几个像素。

    ★ 出图**不按模型自适应缩放**（那会把 v4 与 v5 各缩到一样大、比例对照就没意义了）：
    固定 px_per_unit + 固定 viewport，模型按它在世界里的真实大小落在画面正中 ——
    于是 v4/v5 两张对照图可以直接并排比"谁腿长、谁身子厚、谁更长"。
    """
    def sub(a, b):
        return tuple(a[i] - b[i] for i in range(3))

    fwd = norm(sub(target, eye))
    right = norm(cross(fwd, (0.0, 0.0, 1.0)))
    up = cross(right, fwd)
    dist = sum(sub(target, eye)[i] * fwd[i] for i in range(3))
    f = px_per_unit * dist  # 焦距（像素）：target 距离上 1 体素 = px_per_unit 像素

    corners = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0), (1, 0, 1), (0, 1, 1), (1, 1, 1)]
    us, vs = [], []
    for v in voxels:
        for c in corners:
            p = (v[0] + c[0], v[1] + c[1], v[2] + c[2])
            rel = sub(p, eye)
            zc = sum(rel[i] * fwd[i] for i in range(3))
            assert zc > 1e-6, "geometry behind the camera - move the eye further out"
            us.append(sum(rel[i] * right[i] for i in range(3)) / zc * f)
            vs.append(sum(rel[i] * up[i] for i in range(3)) / zc * f)
    w, h = viewport
    ox = (min(us) + max(us)) * 0.5   # 把投影包围盒的中心放到画面中心
    oy = (min(vs) + max(vs)) * 0.5
    buf = [(28, 30, 36)] * (w * h)
    zbuf = [1e18] * (w * h)

    def project(p):
        rel = sub(p, eye)
        zc = sum(rel[i] * fwd[i] for i in range(3))
        u = sum(rel[i] * right[i] for i in range(3)) / zc * f
        v = sum(rel[i] * up[i] for i in range(3)) / zc * f
        return (u - ox + w * 0.5, h * 0.5 - (v - oy), zc)

    occupied = {(v[0], v[1], v[2]) for v in voxels}
    for x, y, z, c in voxels:
        color = color_of(x, y, z, c, palette)
        for name, (n, fcorners) in FACES.items():
            if sum(n[i] * fwd[i] for i in range(3)) <= 0:
                continue  # 背面（相机朝向 fwd ⇒ 法线背对者不可见）
            if (x + n[0], y + n[1], z + n[2]) in occupied:
                continue
            quad = [project((x + cc[0], y + cc[1], z + cc[2])) for cc in fcorners]
            # 面明暗改用"面法线 vs 视线"的夹角：透视下同一朝向的面明暗才一致
            shade = FACE_SHADE[name]
            raster_tri(buf, w, h, zbuf, [quad[0], quad[1], quad[2]], color, shade)
            raster_tri(buf, w, h, zbuf, [quad[0], quad[2], quad[3]], color, shade)
    return w, h, buf, (min(us), max(us), min(vs), max(vs)), f


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


# ★ 游戏机位参数（两只模型、v4/v5 都共用同一组 ⇒ 对照图是同角度、同世界尺度的）
GAMECAM_AZIMUTH_DEG = 38.0     # 从正前方（+y）往右偏 38°
GAMECAM_ELEV_DEG = 16.0        # 略俯视（4.5 格外一只 1.4~1.8 格高的生物就是这个俯角感）
GAMECAM_DISTANCE_BLOCKS = 4.5  # 卡面 §2.5：近 4–5 格距离感 ⇒ 45 体素
GAMECAM_PX_PER_UNIT = 16.0     # 1 体素在 target 距离上占 16 px（两版共用 ⇒ 可直接比大小）
GAMECAM_VIEWPORT = (760, 620)  # 固定画布（不按模型自适应）


def analyze(vox_path, out_dir, tag, root=None, gamecam_distance_blocks=GAMECAM_DISTANCE_BLOCKS):
    version, size, voxels, palette, chunks = read_vox(vox_path)
    print(f"file        : {vox_path} ({os.path.getsize(vox_path)} bytes)")
    print(f"version     : {version}")
    print("chunks      : " + ", ".join(f"{t}({c}B)" for t, c, _ in chunks))
    print(f"SIZE        : {size[0]} x {size[1]} x {size[2]}  (max side {max(size)})")
    print(f"voxels      : {len(voxels)}")
    assert len(voxels) == len({(v[0], v[1], v[2]) for v in voxels}), "duplicate cells"

    zero = sum(1 for v in voxels if v[3] == 0)
    print(f"colorIndex 0: {zero} voxels  (contract: must be 0)")

    lo = [min(v[i] for v in voxels) for i in range(3)]
    hi = [max(v[i] for v in voxels) for i in range(3)]
    print(f"bbox        : x {lo[0]}..{hi[0]}  y {lo[1]}..{hi[1]}  z {lo[2]}..{hi[2]}")
    span = [hi[i] - lo[i] + 1 for i in range(3)]
    print(f"span        : x {span[0]}  y {span[1]}  z {span[2]}  (z-span must equal collision height / 0.1)")

    print("per-colorIndex bbox and joint mapping:")
    for c in sorted({v[3] for v in voxels}):
        cells = [v for v in voxels if v[3] == c]
        clo = [min(v[i] for v in cells) for i in range(3)]
        chi = [max(v[i] for v in cells) for i in range(3)]
        joint = JOINT_OF_INDEX.get(c, 0)
        rgb = palette[c][:3] if palette[c] else None
        print(f"  idx {c:>2}  {len(cells):>4} voxels  "
              f"x {clo[0]}..{chi[0]}  y {clo[1]}..{chi[1]}  z {clo[2]}..{chi[2]}  "
              f"rgb {rgb}  -> joint {joint} {JOINT_NAMES[joint]}")

    print("joint groups actually present (body = index 1 plus every 17+ colour):")
    groups = {}
    for v in voxels:
        j = JOINT_OF_INDEX.get(v[3], 0)
        groups[j] = groups.get(j, 0) + 1
    for j in sorted(groups):
        print(f"  joint {j} {JOINT_NAMES[j]}: {groups[j]} voxels")

    head = [v for v in voxels if JOINT_OF_INDEX.get(v[3], 0) == 1]
    if head:
        hmax = max(v[2] for v in head)
        print(f"head max z  : {hmax}")
        for label, idxs in (("torso core (idx 1/17/18/19)", (1, 17, 18, 19)),
                            ("all joint 0 (incl. moss/hump)", None)):
            grp = ([v for v in voxels if v[3] in idxs] if idxs
                   else [v for v in voxels if JOINT_OF_INDEX.get(v[3], 0) == 0])
            if grp:
                print(f"  head max z {hmax} vs {label} max z {max(v[2] for v in grp)}  "
                      f"(centroid {sum(v[2] for v in head)/len(head):.2f} vs "
                      f"{sum(v[2] for v in grp)/len(grp):.2f})")

    # ★ 抗 y-up 的最锐判据：接触地面的那一层只许是**腿**（关节 2..5）
    bottom = sorted({v[3] for v in voxels if v[2] == lo[2]})
    bottom_joints = sorted({JOINT_OF_INDEX.get(c, 0) for c in bottom})
    leg_only = all(j in (2, 3, 4, 5) for j in bottom_joints)
    print(f"lowest layer z={lo[2]} colorIndices {bottom} -> joints {bottom_joints} "
          f"({'feet/legs only' if leg_only else 'NOT leg-only - model may be lying down'})")
    # 逐格打印 z=0 层：本卡要求"四腿 x 与躯干外缘对齐（逐格打印）"
    if lo[2] == 0 and tag.endswith("mossback-v5"):
        print("lowest layer, per-cell (x, y, index):")
        for v in sorted((v for v in voxels if v[2] == 0)):
            print(f"  (x={v[0]}, y={v[1]}, z=0) idx {v[3]}")
        torso_lo = min(v[0] for v in voxels if v[3] in (1, 17, 18, 19))
        torso_hi = max(v[0] for v in voxels if v[3] in (1, 17, 18, 19))
        leg_x = sorted({v[0] for v in voxels if JOINT_OF_INDEX.get(v[3], 0) in (2, 3, 4, 5)})
        print(f"  torso x range {torso_lo}..{torso_hi}; legs x {leg_x}; "
              f"flush: {leg_x[0] == torso_lo and leg_x[-1] == torso_hi}")
        for c in (3, 4, 5, 6):
            cells = [v for v in voxels if v[3] == c]
            if cells:
                print(f"  idx {c} leg bbox z {min(v[2] for v in cells)}..{max(v[2] for v in cells)} "
                      f"(height {max(v[2] for v in cells) - min(v[2] for v in cells) + 1})")

    # ★ 契约 v2 专项（卡面 §4.3）：第二色必须落在**它自己那个关节**的包围盒里；
    #   "眼是表面着色不是挖空" —— 眼格必须在占用表内、且朝正面（+y）的邻居为空。
    occupied = {(v[0], v[1], v[2]) for v in voxels}
    second = sorted({v[3] for v in voxels if 9 <= v[3] <= 16})
    print(f"second-colour indices painted: {second}")
    for c in second:
        cells = [v for v in voxels if v[3] == c]
        j = JOINT_OF_INDEX[c]
        jc = [v for v in voxels if JOINT_OF_INDEX.get(v[3], 0) == j]
        jlo = [min(v[i] for v in jc) for i in range(3)]
        jhi = [max(v[i] for v in jc) for i in range(3)]
        clo = [min(v[i] for v in cells) for i in range(3)]
        chi = [max(v[i] for v in cells) for i in range(3)]
        inside = all(jlo[i] <= clo[i] and chi[i] <= jhi[i] for i in range(3))
        print(f"  idx {c:>2} -> joint {j} {JOINT_NAMES[j]}: cells x {clo[0]}..{chi[0]} "
              f"y {clo[1]}..{chi[1]} z {clo[2]}..{chi[2]} within joint bbox "
              f"x {jlo[0]}..{jhi[0]} y {jlo[1]}..{jhi[1]} z {jlo[2]}..{jhi[2]}  "
              f"[{'OK' if inside else 'OUTSIDE'}]")
    eyes = sorted(v for v in voxels if v[3] == 10)
    if eyes:
        print(f"eye voxels (idx 10): {len(eyes)}")
        all_surface = True
        for v in eyes:
            nb = (v[0], v[1] + 1, v[2])
            surface = nb not in occupied
            all_surface = all_surface and surface
            print(f"  ({v[0]}, {v[1]}, {v[2]}) present in occupancy table; "
                  f"+y neighbour ({nb[0]}, {nb[1]}, {nb[2]}) empty: {surface}")
        cols = sorted({v[0] for v in eyes})
        rows = sorted({v[2] for v in eyes})
        print(f"  eye voxels span x {cols} z {rows}; every eye voxel is a surface "
              f"colour (not a carved hole): {all_surface}")

    faces = exposed_faces(size, voxels)
    print(f"exposed faces: {len(faces)}  (budget 2000)")
    print(f"triangles    : {len(faces) * 2}  (what the startup log reports)")
    per_color = {}
    for f in faces:
        per_color[f[3]] = per_color.get(f[3], 0) + 1
    print("faces per colorIndex: " + ", ".join(f"{c}:{n}" for c, n in sorted(per_color.items())))

    # —— 渲染 ——
    os.makedirs(out_dir, exist_ok=True)

    def true_color(x, y, z, c, pal):
        return pal[c][:3] if pal[c] else (255, 0, 255)

    def joint_color(x, y, z, c, pal):
        return JOINT_COLORS[JOINT_OF_INDEX.get(c, 0)]

    def silhouette(x, y, z, c, pal):
        return (235, 235, 240)

    zaxis = norm((1.0, 1.0, 1.0))
    xaxis = norm(cross((0.0, 0.0, 1.0), zaxis))
    yaxis = cross(zaxis, xaxis)
    basis_side_l = norm((0.0, 1.0, 0.0)), norm((0.0, 0.0, 1.0)), norm((-1.0, 0.0, 0.0))
    basis_side_r = norm((0.0, -1.0, 0.0)), norm((0.0, 0.0, 1.0)), norm((1.0, 0.0, 0.0))
    basis_front = norm((1.0, 0.0, 0.0)), norm((0.0, 0.0, 1.0)), norm((0.0, 1.0, 0.0))
    basis_top = norm((1.0, 0.0, 0.0)), norm((0.0, 1.0, 0.0)), norm((0.0, 0.0, 1.0))
    basis_iso = xaxis, yaxis, zaxis

    jobs = [
        ("side-left-nose-right", basis_side_l, true_color),
        ("front-face", basis_front, true_color),
        ("top", basis_top, true_color),
        ("iso", basis_iso, true_color),
        ("iso-joints", basis_iso, joint_color),
    ]
    tiles = []
    for name, basis, colfn in jobs:
        w, h, buf = render_view(size, voxels, palette, colfn, basis, 22, 8)
        tiles.append((name, w, h, buf))
        print(f"rendered {name}: {w}x{h}")

    sil = []
    for name, basis in (("sil-side", basis_side_l), ("sil-iso", basis_iso)):
        w, h, buf = render_view(size, voxels, palette, silhouette, basis, 22, 8)
        sil.append((name, w, h, buf))
        print(f"rendered {name}: {w}x{h}")

    # ★ 透视游戏机位：相机 = 模型几何中心 + 4.5 格球面偏移（方位 38°、仰角 16°）。
    #   视网膜平面用**固定 px_per_unit**（不是自适应缩放），画布也固定 ⇒ v4/v5 可直接比大小。
    cx = (lo[0] + hi[0] + 1.0) * 0.5
    cy = (lo[1] + hi[1] + 1.0) * 0.5
    cz = (lo[2] + hi[2] + 1.0) * 0.5
    import math
    az = math.radians(GAMECAM_AZIMUTH_DEG)
    el = math.radians(GAMECAM_ELEV_DEG)
    d = gamecam_distance_blocks * 10.0
    eye = (cx + d * math.sin(az) * math.cos(el),
           cy + d * math.cos(az) * math.cos(el),
           cz + d * math.sin(el))
    gw, gh, gbuf, _bbox_px, _fpx = render_perspective(
        size, voxels, palette, true_color, eye, (cx, cy, cz),
        GAMECAM_PX_PER_UNIT, GAMECAM_VIEWPORT)
    write_png(os.path.join(out_dir, f"{tag}_gamecam.png"), gw, gh, gbuf)
    print(f"rendered gamecam (perspective): {gw}x{gh} @ {GAMECAM_PX_PER_UNIT} px/voxel")
    print(f"gamecam camera: eye=({eye[0]:.2f}, {eye[1]:.2f}, {eye[2]:.2f}) "
          f"target=({cx:.2f}, {cy:.2f}, {cz:.2f}) dist={d:.1f} voxels "
          f"({gamecam_distance_blocks} blocks) az={GAMECAM_AZIMUTH_DEG} el={GAMECAM_ELEV_DEG} "
          f"focal={GAMECAM_PX_PER_UNIT * d:.1f}px")
    # ★ 透视的机器自证（不是"看着像"）：地面上**等长**的两小段，一段在近端、一段在远端，
    #   投到屏幕上的像素长度必须不等（近端更长）。正交相机下这两个数**必然相等** ——
    #   所以这条断言一旦成立，就证明这张图真的不是正交投影。
    fwd_g = norm((cx - eye[0], cy - eye[1], cz - eye[2]))
    right_g = norm(cross(fwd_g, (0.0, 0.0, 1.0)))
    up_g = cross(right_g, fwd_g)
    half = (hi[1] + 1 - lo[1]) * 0.5

    def px(p):
        rel = (p[0] - eye[0], p[1] - eye[1], p[2] - eye[2])
        zc = sum(rel[i] * fwd_g[i] for i in range(3))
        f = GAMECAM_PX_PER_UNIT * d
        return (sum(rel[i] * right_g[i] for i in range(3)) / zc * f,
                sum(rel[i] * up_g[i] for i in range(3)) / zc * f)

    def seg_len(y0):
        a = px((cx, y0, 0.0))
        b = px((cx, y0 + 1.0, 0.0))
        return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5

    near_px = seg_len(cy + half)
    far_px = seg_len(cy - half)
    print(f"perspective self-check: a 1-voxel ground segment projects to "
          f"{near_px:.2f} px at the near end vs {far_px:.2f} px at the far end "
          f"(ratio {near_px / far_px:.3f}; an orthographic camera would give 1.000)")
    assert near_px > far_px * 1.02, "projection is not visibly perspective"

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

    stack([tiles], f"{tag}_contact_sheet.png")
    stack([sil], f"{tag}_silhouette.png")
    print("contact sheet tiles: " + " | ".join(t[0] for t in tiles))

    # —— 调色板 PNG 回读：格号 = 索引号 ——
    png_path = os.path.join(os.path.dirname(os.path.dirname(vox_path)), "palettes",
                            os.path.basename(vox_path).replace(".vox", ".png"))
    if os.path.exists(png_path):
        with open(png_path, "rb") as f:
            blob = f.read()
        assert blob[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
        ihdr_w, ihdr_h, depth, ctype = struct.unpack_from(">IIBB", blob, 16)
        print(f"palette png : {png_path} {ihdr_w}x{ihdr_h} depth {depth} "
              f"color type {ctype} ({len(blob)} bytes)")
        off = 8
        idat = b""
        while off + 12 <= len(blob):
            ln = struct.unpack_from(">I", blob, off)[0]
            tagb = blob[off + 4:off + 8]
            if tagb == b"IDAT":
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
        worst = 0.0
        used = sorted({v[3] for v in voxels})
        for index in sorted(set(used) | {0}):
            row, col = divmod(index, 16)
            cell = px[row][col][:3]
            model = palette[index][:3] if palette[index] else None
            flag = "match" if (index in (0,) or cell == model) else "MISMATCH"
            mx, mn = max(cell), min(cell)
            sat = 0.0 if mx == 0 else (mx - mn) / mx
            if index in used:
                worst = max(worst, sat)
            print(f"  cell {index:>2} (row {row}, col {col:>2}) png {cell}  vox {model}  "
                  f"sat {sat:.3f}  {flag}")
        print(f"max saturation over painted cells: {worst:.3f} (cap 0.50)")
    return 0


def compare(out_png, left, right):
    """把两张 PNG 并排拼成一张对照图（中间 12 px 分隔），用 Pillow 读回自己写的 PNG。"""
    from PIL import Image, ImageDraw
    a = Image.open(left).convert("RGB")
    b = Image.open(right).convert("RGB")
    h = max(a.height, b.height)
    gap = 12
    out = Image.new("RGB", (a.width + gap + b.width, h), (24, 24, 28))
    out.paste(a, (0, (h - a.height) // 2))
    out.paste(b, (a.width + gap, (h - b.height) // 2))
    d = ImageDraw.Draw(out)
    d.line([(a.width + gap // 2, 0), (a.width + gap // 2, h)], fill=(200, 200, 210), width=1)
    out.save(out_png)
    print(f"wrote {out_png} ({out.width}x{out.height}) from {os.path.basename(left)} "
          f"+ {os.path.basename(right)}")
    return 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "compare":
        if len(sys.argv) != 5:
            print(__doc__)
            return 2
        return compare(sys.argv[2], sys.argv[3], sys.argv[4])
    if len(sys.argv) >= 2 and sys.argv[1] == "model":
        if len(sys.argv) != 5:
            print(__doc__)
            return 2
        return analyze(sys.argv[2], sys.argv[3], sys.argv[4])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
