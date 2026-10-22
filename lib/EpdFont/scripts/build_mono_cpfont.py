#!/usr/bin/env python3
"""
Build crisp 1-bit .cpfont files (v4) from WenQuanYi Zen Hei Sharp.

Two modes, chosen per size:
  * strike sizes (the TTF embeds bitmaps at 12-16 px): glyphs and advances
    come straight from the strike. Exporters that take advances from the
    scaled outline (linearHoriAdvance) misplace Latin letters and symbols
    ('M' too wide so the next letter drifts right, '-' wider than its advance).
  * larger sizes: the unhinted outline is emboldened or thinned with
    FT_Outline_Embolden so its stems are exactly 1.0 px, then rendered mono.
    A band exactly 1 px wide always covers one pixel centre, so every stroke
    comes out 1 px, like the strikes. Strokes that still drop out are
    restored from an anti-aliased coverage mask: lost components get their
    strongest pixel back, and pixels that bridge a break are added.

Output is a 2-bit container with pixels 0 or 3 (like the strike exports), the
character set of a reference .cpfont, and advances checked by
cpfont_advance_fix.py (header byte 13 marked).

Usage:
  python3 build_mono_cpfont.py FONT.ttf REFERENCE.cpfont OUT_PREFIX NAME=PX [NAME=PX ...]
  e.g. ... wqy.ttf WQYZhengHei_16.cpfont out/WQYZhengHei 8=12 10=13 12=14 14=15 16=16 18=18 20=20 22=22
writes out/WQYZhengHei_<NAME>.cpfont rendered at PX pixels.
Requires: pip install freetype-py
"""

import ctypes
import struct
import sys
from pathlib import Path

import freetype
from freetype.raw import FT_Outline_Embolden

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cpfont_advance_fix import ADVANCE_FIX_MARK, fixed_advance  # noqa: E402

MAGIC = b"CPFONT\x00\x00"
VERSION = 4
GLYPH_FORMAT = "<BBHhhH2xI"
NOHINT = freetype.FT_LOAD_NO_BITMAP | freetype.FT_LOAD_NO_HINTING
COVER_THRESHOLD = 64  # AA coverage (0-255) that counts as "part of a stroke"


def reference_codepoints(path: Path) -> list[int]:
    data = path.read_bytes()
    toc = data[32:64]
    interval_count = struct.unpack_from("<I", toc, 4)[0]
    base = struct.unpack_from("<I", toc, 24)[0]
    cps = []
    for i in range(interval_count):
        first, last, _ = struct.unpack_from("<III", data, base + 12 * i)
        cps.extend(range(first, last + 1))
    return cps


def mono_grid(bm):
    return [[1 if bm.buffer[y * bm.pitch + x // 8] & (0x80 >> (x % 8)) else 0 for x in range(bm.width)]
            for y in range(bm.rows)]


def components(img, h, w):
    lab = [[0] * w for _ in range(h)]
    n = 0
    for y in range(h):
        for x in range(w):
            if img[y][x] and not lab[y][x]:
                n += 1
                stack = [(y, x)]
                lab[y][x] = n
                while stack:
                    cy, cx = stack.pop()
                    for yy in range(max(0, cy - 1), min(h, cy + 2)):
                        for xx in range(max(0, cx - 1), min(w, cx + 2)):
                            if img[yy][xx] and not lab[yy][xx]:
                                lab[yy][xx] = n
                                stack.append((yy, xx))
    return lab, n


def neighbour_labels(lab, y, x, h, w):
    return {lab[yy][xx] for yy in range(max(0, y - 1), min(h, y + 2)) for xx in range(max(0, x - 1), min(w, x + 2))
            if lab[yy][xx]}


def uniform_stroke_glyph(face, cp, strength):
    """Outline at exactly-1px stems, rendered mono, dropouts repaired."""
    face.load_char(chr(cp), NOHINT)
    advance_x = face.glyph.advance.x
    if face.glyph.outline.n_points == 0:
        return 0, 0, advance_x, 0, 0, []
    FT_Outline_Embolden(ctypes.byref(face.glyph.outline._FT_Outline), int(strength))
    face.glyph.render(freetype.FT_RENDER_MODE_MONO)
    g = face.glyph
    thin, tl, tt = mono_grid(g.bitmap), g.bitmap_left, g.bitmap_top
    face.load_char(chr(cp), NOHINT | freetype.FT_LOAD_RENDER)
    g = face.glyph
    bm = g.bitmap
    aa = [[bm.buffer[y * bm.pitch + x] for x in range(bm.width)] for y in range(bm.rows)]
    al, at = g.bitmap_left, g.bitmap_top
    # Common canvas (1 px margin) for the thin and coverage renders.
    ox = min(tl, al) - 1
    oy = max(tt, at) + 1
    right = max(tl + (len(thin[0]) if thin else 0), al + (len(aa[0]) if aa else 0)) + 1
    bottom = min(tt - len(thin), at - len(aa)) - 1
    w, h = right - ox, oy - bottom
    t = [[0] * w for _ in range(h)]
    a = [[0] * w for _ in range(h)]
    for src, left, top, dst in ((thin, tl, tt, t), (aa, al, at, a)):
        for y, row in enumerate(src):
            for x, v in enumerate(row):
                dst[oy - top + y][left - ox + x] = v
    cover = [[1 if v >= COVER_THRESHOLD else 0 for v in row] for row in a]
    # 1) A coverage component with no thin pixel is a stroke that dropped out:
    #    keep its strongest pixel.
    clab, cn = components(cover, h, w)
    has_thin = {clab[y][x] for y in range(h) for x in range(w) if t[y][x] and clab[y][x]}
    for c in range(1, cn + 1):
        if c not in has_thin:
            _, y, x = max((a[y][x], y, x) for y in range(h) for x in range(w) if clab[y][x] == c)
            t[y][x] = 1
    # 2) Bridge breaks: add coverage pixels touching two separate thin parts.
    for _ in range(4):
        tlab, _ = components(t, h, w)
        cands = sorted(((a[y][x], y, x) for y in range(h) for x in range(w)
                        if not t[y][x] and cover[y][x] and len(neighbour_labels(tlab, y, x, h, w)) >= 2),
                       reverse=True)
        added = False
        for _, y, x in cands:
            if len(neighbour_labels(tlab, y, x, h, w)) >= 2:
                t[y][x] = 1
                added = True
                tlab, _ = components(t, h, w)
        if not added:
            break
    # Crop to ink.
    rows = [y for y in range(h) if any(t[y])]
    cols = [x for x in range(w) if any(t[y][x] for y in range(h))]
    if not rows:
        return 0, 0, advance_x, 0, 0, []
    y0, y1, x0, x1 = rows[0], rows[-1], cols[0], cols[-1]
    pixels = [t[y][x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    return x1 - x0 + 1, y1 - y0 + 1, advance_x, ox + x0, oy - y0, pixels


def strike_glyph(face, cp):
    """Glyph from the embedded bitmap strike, with the strike's own advance."""
    face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    g = face.glyph
    grid = mono_grid(g.bitmap)
    pixels = [v for row in grid for v in row]
    return g.bitmap.width, g.bitmap.rows, g.advance.x, g.bitmap_left, g.bitmap_top, pixels


def pack(pixels):
    packed = bytearray((len(pixels) + 3) // 4)
    for i, v in enumerate(pixels):
        if v:
            packed[i >> 2] |= 3 << (6 - 2 * (i & 3))
    return bytes(packed)


def stem_strength(face):
    face.load_char("一", NOHINT)
    b = face.glyph.outline.get_bbox()
    hstem = (b.yMax - b.yMin) / 64
    face.load_char("丨", NOHINT)
    b = face.glyph.outline.get_bbox()
    vstem = (b.xMax - b.xMin) / 64
    return (1.0 - (hstem + vstem) / 2) * 64


def build(font_path: Path, cps, name: str, px: int, prefix: str):
    face = freetype.Face(str(font_path))
    face.set_pixel_sizes(0, px)
    strike = any(s.y_ppem // 64 == px for s in face.available_sizes)
    strength = 0 if strike else stem_strength(face)
    present = [cp for cp in cps if face.get_char_index(cp)]
    rendered = {}
    for i, cp in enumerate(present):
        rendered[cp] = strike_glyph(face, cp) if strike else uniform_stroke_glyph(face, cp, strength)
        if not strike and i % 2000 == 0:
            print(f"  {name}: {i}/{len(present)}", file=sys.stderr, flush=True)
    cjk_cell_fp = round(rendered[0x4E00][2] / 4) if 0x4E00 in rendered else px * 16
    glyph_bytes, bitmaps, fixed = [], bytearray(), 0
    for cp in present:
        w, h, adv26, left, top, pixels = rendered[cp]
        adv = round(adv26 / 4)  # 26.6 -> 12.4
        new_adv = fixed_advance(cp, w, left, adv, cjk_cell_fp)
        fixed += new_adv != adv
        packed = pack(pixels)
        glyph_bytes.append(struct.pack(GLYPH_FORMAT, w, h, new_adv, left, top, len(packed), len(bitmaps)))
        bitmaps += packed
    intervals = []
    for cp in present:
        if intervals and cp == intervals[-1][1] + 1:
            intervals[-1][1] = cp
        else:
            intervals.append([cp, cp])
    offset, interval_bytes = 0, b""
    for first, last in intervals:
        interval_bytes += struct.pack("<III", first, last, offset)
        offset += last - first + 1
    metrics = face.size
    advance_y = (metrics.height + 63) // 64
    ascender = (metrics.ascender + 63) // 64
    descender = metrics.descender // 64
    header = bytearray(struct.pack("<8sHHB19s", MAGIC, VERSION, 1, 1, bytes(19)))
    header[13] = ADVANCE_FIX_MARK
    toc = struct.pack("<B3xIIBhhHHBBBI4x", 0, len(intervals), len(present), advance_y, ascender, descender, 0, 0, 0,
                      0, 0, 64)
    out = Path(f"{prefix}_{name}.cpfont")
    out.write_bytes(bytes(header) + toc + interval_bytes + b"".join(glyph_bytes) + bytes(bitmaps))
    print(f"{out.name}: {px}px {'strike' if strike else 'uniform-stroke'}, {len(present)} glyphs, advY {advance_y}, "
          f"{fixed} advances fixed, {out.stat().st_size} bytes")


def main():
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    font, ref, prefix = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    cps = reference_codepoints(ref)
    for spec in sys.argv[4:]:
        name, _, px = spec.partition("=")
        build(font, cps, name, int(px or name), prefix)


if __name__ == "__main__":
    main()
