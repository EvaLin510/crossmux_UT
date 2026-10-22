#!/usr/bin/env python3
"""
Rasterize Feather-style stroke SVGs or RGBA PNGs into 1-bit icon arrays.

Pure Python (no Pillow / librsvg) so it runs anywhere the firmware builds.
SVG support covers what Feather icons use: <path> with M/L/H/V/A/Z (absolute
and relative), <rect> (rx), <circle>, <line> and <polyline>, all stroked with
round caps and joins. A pixel is ink when at least half of its 4x4 subsamples
lie within stroke/2 of the outline, after vertices are snapped to the pixel
grid so straight runs come out exactly STROKE_PX wide. PNGs (8-bit RGBA or RGB) are box-filtered
to the target size and thresholded on darkness x alpha.

Output rows go top-to-bottom, MSB first, bit 0 = ink (the freeink::Icon and
Inx tab-icon layout). Each array is preceded by its ASCII art for review.

Usage:
  python3 scripts/icon_to_bitmap.py NAME SIZE STROKE_PX FILE [NAME SIZE STROKE_PX FILE ...] > out.h
SIZE may be DRAW/CANVAS (e.g. 36/38): draw at DRAW px, centred on a CANVAS px
square. Pick DRAW so the scale from the 24 px viewBox is a clean ratio (1.5x
-> 36) and integer coordinates land on half pixels; strokes then stay crisp.
STROKE_PX is the stroke width in output pixels; for a PNG a value in (0, 1)
is the ink threshold instead (default 0.5; lower keeps thin lines).
"""

import math
import re
import struct
import sys
import zlib
from pathlib import Path

SUB = 4  # subsamples per axis


# --- SVG ---------------------------------------------------------------------

def arc_points(x1, y1, rx, ry, phi_deg, large, sweep, x2, y2, steps=24):
    """SVG elliptical arc (endpoint form) flattened to points, excluding start."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    phi = math.radians(phi_deg)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p = cp * dx + sp * dy
    y1p = -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        coef = -coef
    cxp = coef * rx * y1p / ry
    cyp = -coef * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    pts = []
    n = max(4, int(steps * abs(dt) / (2 * math.pi)) + 2)
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x = rx * math.cos(t)
        y = ry * math.sin(t)
        pts.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return pts


def path_polylines(d):
    tokens = re.findall(r"[MmLlHhVvAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d)
    polys, cur = [], []
    x = y = sx = sy = 0.0
    i, cmd = 0, None

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]):
            cmd = tokens[i]
            i += 1
            if cmd in "Zz":
                if cur:
                    cur.append((sx, sy))
                    polys.append(cur)
                cur = []
                x, y = sx, sy
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            if cur:
                polys.append(cur)
            x, y, sx, sy = nx, ny, nx, ny
            cur = [(x, y)]
            cmd = "l" if rel else "L"  # implicit lineto
        elif c == "L":
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            x, y = nx, ny
            cur.append((x, y))
        elif c == "H":
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
        elif c == "V":
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
        elif c == "A":
            rx, ry, rot, large, sweep, nx, ny = (num() for _ in range(7))
            if rel:
                nx, ny = x + nx, y + ny
            cur.extend(arc_points(x, y, rx, ry, rot, int(large), int(sweep), nx, ny))
            x, y = nx, ny
        else:
            raise ValueError(f"unsupported path command {cmd}")
    if cur:
        polys.append(cur)
    return polys


def attr(tag, name, default=0.0):
    m = re.search(rf'\b{name}="([^"]*)"', tag)
    return float(m.group(1)) if m else default


def svg_polylines(text):
    polys = []
    for tag in re.findall(r"<(?:path|rect|circle|line|polyline)\b[^>]*>", text):
        kind = re.match(r"<(\w+)", tag).group(1)
        if kind == "path":
            polys += path_polylines(re.search(r'\bd="([^"]*)"', tag).group(1))
        elif kind == "line":
            polys.append([(attr(tag, "x1"), attr(tag, "y1")), (attr(tag, "x2"), attr(tag, "y2"))])
        elif kind == "polyline":
            nums = [float(v) for v in re.findall(r"-?\d+\.?\d*", re.search(r'points="([^"]*)"', tag).group(1))]
            polys.append(list(zip(nums[0::2], nums[1::2])))
        elif kind == "circle":
            cx, cy, r = attr(tag, "cx"), attr(tag, "cy"), attr(tag, "r")
            polys.append([(cx + r * math.cos(2 * math.pi * k / 48), cy + r * math.sin(2 * math.pi * k / 48))
                          for k in range(49)])
        elif kind == "rect":
            x, y, w, h = attr(tag, "x"), attr(tag, "y"), attr(tag, "width"), attr(tag, "height")
            r = min(attr(tag, "rx"), w / 2, h / 2)
            if r <= 0:
                polys.append([(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)])
            else:
                d = (f"M{x + r} {y}H{x + w - r}A{r} {r} 0 0 1 {x + w} {y + r}V{y + h - r}"
                     f"A{r} {r} 0 0 1 {x + w - r} {y + h}H{x + r}A{r} {r} 0 0 1 {x} {y + h - r}"
                     f"V{y + r}A{r} {r} 0 0 1 {x + r} {y}Z")
                polys += path_polylines(d)
    view = re.search(r'viewBox="([^"]*)"', text)
    vb = [float(v) for v in view.group(1).split()] if view else [0, 0, 24, 24]
    return polys, vb


def seg_dist2(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    qx, qy = ax + t * dx - px, ay + t * dy - py
    return qx * qx + qy * qy


def rasterize_svg(path, size, stroke_px):
    polys, vb = svg_polylines(Path(path).read_text(encoding="utf-8"))
    scale = size / max(vb[2], vb[3])
    # Snap vertices so stroke edges land on pixel boundaries: odd integer
    # strokes centre on pixel centres (k + 0.5), even ones on pixel edges.
    odd = round(stroke_px) % 2 == 1

    def snap(v):
        return math.floor(v) + 0.5 if odd else float(round(v))

    segs = []
    for poly in polys:
        pts = [(snap((px - vb[0]) * scale), snap((py - vb[1]) * scale)) for px, py in poly]
        if len(pts) == 1:
            pts = pts * 2
        segs += list(zip(pts, pts[1:]))
    half2 = (stroke_px / 2) ** 2
    grid = []
    for row in range(size):
        line = []
        for col in range(size):
            hits = 0
            for sy in range(SUB):
                for sx in range(SUB):
                    px = col + (sx + 0.5) / SUB
                    py = row + (sy + 0.5) / SUB
                    if any(seg_dist2(px, py, a[0], a[1], b[0], b[1]) <= half2 for a, b in segs):
                        hits += 1
            line.append(hits * 2 >= SUB * SUB)
        grid.append(line)
    return grid


# --- PNG ---------------------------------------------------------------------

def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), "only 8-bit RGB/RGBA PNGs"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        rows.append(line)
        prev = line
    ink = [[0.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b = rows[y][x * bpp:x * bpp + 3]
            alpha = rows[y][x * bpp + 3] / 255 if bpp == 4 else 1.0
            ink[y][x] = (1 - (0.299 * r + 0.587 * g + 0.114 * b) / 255) * alpha
    return ink, w, h


def rasterize_png(path, size, threshold=0.5):
    ink, w, h = read_png(path)
    # Fit the image into size x size, centred, preserving aspect ratio.
    scale = size / max(w, h)
    ow, oh = max(1, round(w * scale)), max(1, round(h * scale))
    ox, oy = (size - ow) // 2, (size - oh) // 2
    grid = [[False] * size for _ in range(size)]
    for row in range(oh):
        y0, y1 = row / scale, (row + 1) / scale
        for col in range(ow):
            x0, x1 = col / scale, (col + 1) / scale
            total = n = 0.0
            for yy in range(int(y0), min(h, int(math.ceil(y1)))):
                for xx in range(int(x0), min(w, int(math.ceil(x1)))):
                    total += ink[yy][xx]
                    n += 1
            grid[oy + row][ox + col] = n > 0 and total / n >= threshold
    return grid


# --- output ------------------------------------------------------------------

def emit(name, grid):
    size = len(grid)
    row_bytes = (size + 7) // 8
    out = [f"// {name} ({size}x{size})"]
    out += ["//   " + "".join("#" if v else "." for v in row) for row in grid]
    data = []
    for row in grid:
        bits = [0 if v else 1 for v in row] + [1] * (row_bytes * 8 - size)
        for b in range(row_bytes):
            byte = 0
            for k in range(8):
                byte = (byte << 1) | bits[b * 8 + k]
            data.append(byte)
    out.append(f"static const uint8_t {name}[] = {{")
    for i in range(0, len(data), 16):
        out.append("    " + ", ".join(f"0x{v:02X}" for v in data[i:i + 16]) + ",")
    out.append("};")
    return "\n".join(out)


def main():
    args = sys.argv[1:]
    if not args or len(args) % 4:
        sys.exit(__doc__)
    blocks = []
    for i in range(0, len(args), 4):
        name, size_arg, stroke, src = args[i], args[i + 1], float(args[i + 2]), args[i + 3]
        draw, _, canvas = size_arg.partition("/")
        draw = int(draw)
        canvas = int(canvas) if canvas else draw
        if src.lower().endswith(".png"):
            grid = rasterize_png(src, draw, stroke if 0 < stroke < 1 else 0.5)
        else:
            grid = rasterize_svg(src, draw, stroke)
        pad = (canvas - draw) // 2
        padded = [[False] * canvas for _ in range(canvas)]
        for y, row in enumerate(grid):
            for x, v in enumerate(row):
                padded[y + pad][x + pad] = v
        blocks.append(emit(name, padded))
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    print("\n\n".join(blocks))


if __name__ == "__main__":
    main()
