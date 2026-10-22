#!/usr/bin/env python3
"""
Repair advances in bitmap-strike .cpfont files whose ink overruns the advance.

Bitmap strikes (e.g. WenQuanYi Zen Hei) were packed with advances taken from
the outline font, so some glyphs draw past their advance and collide with the
next character: ':' touches the following digit, and the curly quotes
U+2018/2019/201C/201D, drawn for a full-width CJK cell, get a ~5 px advance and
land on the next ideograph.

Rule (advances are 12.4 fixed point):
  * full-width punctuation drawn for a CJK cell (FULL_CELL) gets the advance of
    U+4E00 when its ink overruns the advance;
  * other non-CJK glyphs get at least 1 px of right bearing:
    advance >= left + width + 1;
  * CJK ideographs and full-width forms keep their advance; their ink may
    touch the cell edge by design.

Run on SD fonts (rewrites in place, keeping the original as *.orig):
  python3 lib/EpdFont/scripts/cpfont_advance_fix.py /path/to/WQYZhengHei_*.cpfont

The fixer also sets header byte 13 (reserved, 0 in unfixed files) to
ADVANCE_FIX_MARK. The byte is part of the content hash, so the flash font cache
and the reader page caches built with the old advances are invalidated
instead of reusing stale metrics.
"""

import shutil
import struct
import sys
from pathlib import Path

GLYPH_FORMAT = "<BBHhhH2xI"
GLYPH_SIZE = struct.calcsize(GLYPH_FORMAT)
HEADER_SIZE = 32
TOC_ENTRY_SIZE = 32
ADVANCE_FIX_MARK = 1

# Punctuation the WenQuanYi strikes position inside a full CJK cell.
FULL_CELL = {0x2014, 0x2015, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026}


def is_cjk_cell(cp: int) -> bool:
    return (0x2E80 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF or 0xFF00 <= cp <= 0xFFEF or
            0x3000 <= cp <= 0x303F or 0x20000 <= cp <= 0x2FFFF)


def fixed_advance(cp: int, width: int, left: int, advance_fp: int, cjk_cell_fp: int) -> int:
    """Return the repaired 12.4 fixed-point advance for one glyph."""
    if width == 0 or is_cjk_cell(cp):
        return advance_fp
    ink_right_fp = (left + width) * 16
    if cp in FULL_CELL:
        return max(advance_fp, cjk_cell_fp) if ink_right_fp > advance_fp else advance_fp
    return max(advance_fp, ink_right_fp + 16)


def fix_file(path: Path) -> int:
    data = bytearray(path.read_bytes())
    if data[:8] != b"CPFONT\x00\x00":
        raise SystemExit(f"{path}: not a .cpfont file")
    if data[13] == ADVANCE_FIX_MARK:
        print(f"{path.name}: already fixed")
        return 0
    style_count = data[12]
    changed = 0
    for s in range(style_count):
        toc = data[HEADER_SIZE + s * TOC_ENTRY_SIZE:HEADER_SIZE + (s + 1) * TOC_ENTRY_SIZE]
        interval_count, glyph_count = struct.unpack_from("<II", toc, 4)
        base = struct.unpack_from("<I", toc, 24)[0]
        intervals = [struct.unpack_from("<III", data, base + 12 * i) for i in range(interval_count)]
        glyphs_offset = base + 12 * interval_count

        def glyph_pos(cp):
            for first, last, offset in intervals:
                if first <= cp <= last:
                    return glyphs_offset + GLYPH_SIZE * (offset + cp - first)
            return None

        ref = glyph_pos(0x4E00)
        cjk_cell_fp = struct.unpack_from(GLYPH_FORMAT, data, ref)[2] if ref is not None else 0
        for first, last, offset in intervals:
            for cp in range(first, last + 1):
                pos = glyphs_offset + GLYPH_SIZE * (offset + cp - first)
                w, h, adv, left, top, length, data_off = struct.unpack_from(GLYPH_FORMAT, data, pos)
                new_adv = fixed_advance(cp, w, left, adv, cjk_cell_fp)
                if new_adv != adv:
                    struct.pack_into("<H", data, pos + 2, new_adv)
                    changed += 1
    data[13] = ADVANCE_FIX_MARK
    backup = path.with_suffix(path.suffix + ".orig")
    if not backup.exists():
        shutil.copyfile(path, backup)
    path.write_bytes(bytes(data))
    print(f"{path.name}: {changed} advances fixed")
    return changed


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for arg in sys.argv[1:]:
        fix_file(Path(arg))
