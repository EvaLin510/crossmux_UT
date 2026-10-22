#!/usr/bin/env python3
"""
Convert a 1-bit bitmap .cpfont (v4, regular style) into a built-in font header.

Used for the UT37 bitmap UI faces: the WenQuanYi Zen Hei (文泉驛正黑) bitmap
strikes were packed into .cpfont files whose pixels are only 0 or 3. This
script keeps those exact pixels, repacks them as a 1-bit bitstream (half the
2-bit size) and keeps only the glyphs in a charset. Every size keeps every
non-CJK glyph in the font (Latin, punctuation, symbols, kana, ...) plus every
CJK character in the Chinese UI translations (chinese.yaml, traditional.yaml),
so built-in strings never depend on the SD card. CJK coverage beyond that:

  * full charset (reader face, any size not in --ui-sizes):
      Big5 level-1 common characters (0xA440-0xC67E, 5401 Traditional chars)
      + lib/EpdFont/scripts/cn_common_chars.txt (3500 Simplified + i18n glyphs)
  * UI charset (--ui-sizes): the characters in --ui-charset, the most frequent
    Traditional + Simplified characters of the full charset

Rare characters outside the subset come from the complete SD-card family
(see docs/engineering/sd-card-font-cache.md).

Usage (from the repository root):
  python3 lib/EpdFont/scripts/cpfont_to_builtin.py lib/EpdFont/builtinFonts wqy_zhenghei \
      lib/EpdFont/builtinFonts/source/WQYZhengHei/WQYZhengHei_{10,12,14,16}.cpfont \
      --ui-sizes 10,12,14

Writes <prefix>_<size>.h per input, <prefix>_intervals.h shared by the
full-charset sizes and <prefix>_ui_intervals.h shared by the --ui-sizes.

Re-rank the UI charset (needs `pip install wordfreq jieba`; the result is
committed so font generation stays reproducible without it):
  python3 lib/EpdFont/scripts/cpfont_to_builtin.py --write-ui-charset 2000
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cpfont_advance_fix import fixed_advance  # noqa: E402

SCRIPT_DIR = Path(__file__).resolve().parent
CN_COMMON_CHARS = SCRIPT_DIR / "cn_common_chars.txt"
UI_CHARSET = SCRIPT_DIR / "wqy_ui_chars.txt"
UI_TRANSLATIONS = [SCRIPT_DIR.parents[2] / "lib/I18n/translations" / name
                   for name in ("chinese.yaml", "traditional.yaml")]

HEADER_SIZE = 32
TOC_ENTRY_SIZE = 32
GLYPH_FORMAT = "<BBHhhH2xI"  # matches EpdGlyph / fontconvert_sdcard.py
GLYPH_SIZE = struct.calcsize(GLYPH_FORMAT)


def is_cjk_ideograph(cp: int) -> bool:
    return 0x3400 <= cp <= 0x4DBF or 0x4E00 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF or 0x20000 <= cp <= 0x2FFFF


def big5_level1() -> set[int]:
    chars = set()
    for hi in range(0xA4, 0xC7):
        for lo in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
            raw = bytes([hi, lo])
            if raw > b"\xc6\x7e":
                continue
            try:
                chars.add(ord(raw.decode("big5")))
            except UnicodeDecodeError:
                pass
    return chars


def load_cpfont(path: Path):
    data = path.read_bytes()
    if data[:8] != b"CPFONT\x00\x00":
        sys.exit(f"{path}: not a .cpfont file")
    version, flags, style_count = struct.unpack_from("<HHB", data, 8)
    if version != 4 or style_count < 1:
        sys.exit(f"{path}: unsupported version {version} / styles {style_count}")
    toc = data[HEADER_SIZE:HEADER_SIZE + TOC_ENTRY_SIZE]
    if toc[0] != 0:
        sys.exit(f"{path}: first style is not regular")
    interval_count, glyph_count = struct.unpack_from("<II", toc, 4)
    advance_y = toc[12]
    ascender, descender = struct.unpack_from("<hh", toc, 13)
    kern_left, kern_right = struct.unpack_from("<HH", toc, 17)
    kern_left_classes, kern_right_classes, ligatures = toc[21], toc[22], toc[23]
    base = struct.unpack_from("<I", toc, 24)[0]

    intervals = [struct.unpack_from("<III", data, base + 12 * i) for i in range(interval_count)]
    glyphs_offset = base + 12 * interval_count
    bitmap_offset = (glyphs_offset + GLYPH_SIZE * glyph_count + 3 * kern_left + 3 * kern_right +
                     kern_left_classes * kern_right_classes + 8 * ligatures)
    is_2bit = (flags & 1) != 0

    glyphs = {}
    grey_glyphs = []
    for first, last, offset in intervals:
        for cp in range(first, last + 1):
            w, h, adv, left, top, length, data_off = struct.unpack_from(
                GLYPH_FORMAT, data, glyphs_offset + GLYPH_SIZE * (offset + cp - first))
            packed = data[bitmap_offset + data_off:bitmap_offset + data_off + length]
            pixels = []
            if is_2bit:
                for byte in packed:
                    for shift in (6, 4, 2, 0):
                        pixels.append((byte >> shift) & 3)
                pixels = pixels[:w * h]
                # Glyphs without a bitmap strike were rasterised from outlines
                # and carry grey edges; threshold them like fontconvert.py does.
                if any(p in (1, 2) for p in pixels):
                    grey_glyphs.append(cp)
                ink = [p >= 2 for p in pixels]
            else:
                for byte in packed:
                    for bit in range(7, -1, -1):
                        pixels.append((byte >> bit) & 1)
                ink = [p == 1 for p in pixels[:w * h]]
            glyphs[cp] = (w, h, adv, left, top, ink)
    return glyphs, advance_y, ascender, descender, grey_glyphs


def pack_1bit(ink: list[bool]) -> bytes:
    out = bytearray((len(ink) + 7) // 8)
    for i, on in enumerate(ink):
        if on:
            out[i >> 3] |= 0x80 >> (i & 7)
    return bytes(out)


def build_intervals(codepoints: list[int]) -> list[tuple[int, int]]:
    intervals = []
    for cp in codepoints:
        if intervals and cp == intervals[-1][1] + 1:
            intervals[-1] = (intervals[-1][0], cp)
        else:
            intervals.append((cp, cp))
    return intervals


def cp_label(cp: int) -> str:
    if cp == 0x5C:
        return "<backslash>"
    return chr(cp) if 0x20 < cp < 0x7F else f"U+{cp:04X}"


def convert(name: str, src: Path, wanted: set[int]):
    glyphs, advance_y, ascender, descender, grey_glyphs = load_cpfont(src)
    if grey_glyphs:
        cjk_grey = sum(1 for cp in grey_glyphs if is_cjk_ideograph(cp))
        print(f"// {name}: {len(grey_glyphs)} glyphs had grey pixels ({cjk_grey} CJK), thresholded at >= 2",
              file=sys.stderr)
    selected = sorted(cp for cp in glyphs if not is_cjk_ideograph(cp) or cp in wanted)
    missing = sum(1 for cp in wanted if cp not in glyphs)
    if missing:
        print(f"// {name}: {missing} charset glyphs absent from {src.name}", file=sys.stderr)

    bitmaps = bytearray()
    glyph_rows = []
    cjk_cell_fp = glyphs[0x4E00][2] if 0x4E00 in glyphs else 0
    fixed = 0
    for cp in selected:
        w, h, adv, left, top, ink = glyphs[cp]
        # Strike ink can overrun outline-derived advances (':' and curly quotes).
        new_adv = fixed_advance(cp, w, left, adv, cjk_cell_fp)
        fixed += new_adv != adv
        adv = new_adv
        packed = pack_1bit(ink)
        glyph_rows.append((w, h, adv, left, top, len(packed), len(bitmaps), cp))
        bitmaps += packed
    intervals = build_intervals(selected)
    print(f"// {name}: {len(selected)} glyphs, {len(intervals)} intervals, {len(bitmaps)} bitmap bytes, "
          f"{fixed} advances fixed", file=sys.stderr)
    return glyph_rows, bitmaps, intervals, (advance_y, ascender, descender)


def write_lines(path: Path, lines: list[str]) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def write_font(path: Path, name: str, src: Path, charset: str, intervals_header: str, intervals_name: str,
               interval_count: int, glyph_rows, bitmaps, metrics) -> None:
    advance_y, ascender, descender = metrics
    lines = [f"""/**
 * generated by cpfont_to_builtin.py; do not edit.
 * name: {name}
 * mode: 1-bit
 * source: {src.name} (WenQuanYi Zen Hei bitmap strikes, GPLv2 with font embedding exception)
 * charset: {charset}
 */
#pragma once
#include "EpdFontData.h"
#include "{intervals_header}"
""", f"static const uint8_t {name}Bitmaps[{len(bitmaps)}] = {{"]
    for i in range(0, len(bitmaps), 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in bitmaps[i:i + 16]))
    lines += ["};", "", f"static const EpdGlyph {name}Glyphs[] = {{"]
    for w, h, adv, left, top, length, offset, cp in glyph_rows:
        lines.append(f"    {{ {w}, {h}, {adv}, {left}, {top}, {length}, {offset} }}, // {cp_label(cp)}")
    lines += ["};", "", f"static const EpdFontData {name} = {{",
              f"    {name}Bitmaps,", f"    {name}Glyphs,", f"    {intervals_name},", f"    {interval_count},",
              f"    {advance_y},", f"    {ascender},", f"    {descender},", "    false,",
              "    nullptr,", "    0,", "    nullptr,"]
    lines += ["    nullptr,"] * 10 + ["    0,"] * 4 + ["    nullptr,", "    0,", "};", ""]
    write_lines(path, lines)


def cjk_codepoints(path: Path) -> set[int]:
    return {ord(c) for c in path.read_text(encoding="utf-8") if is_cjk_ideograph(ord(c))}


def write_ui_charset(top: int, path: Path, full: set[int], ui_strings: set[int]) -> None:
    try:
        import wordfreq
    except ImportError:
        sys.exit("error: wordfreq not installed. Run: pip install wordfreq jieba")
    # wordfreq scores a Traditional form like its Simplified form, so a common
    # character keeps both. Ties break by codepoint to keep the list stable.
    pool = sorted(full - ui_strings)
    ranked = sorted(pool, key=lambda cp: (-wordfreq.word_frequency(chr(cp), "zh"), cp))
    path.write_text("".join(chr(cp) for cp in sorted(ranked[:top])), encoding="utf-8", newline="\n")
    print(f"// {path.name}: top {top} of {len(pool)} full-charset characters (UI strings excluded)", file=sys.stderr)


def write_intervals(path: Path, intervals_name: str, intervals: list[tuple[int, int]]) -> None:
    lines = ["// Generated by cpfont_to_builtin.py; do not edit.", "#pragma once", '#include "EpdFontData.h"', "",
             f"inline constexpr EpdUnicodeInterval {intervals_name}[] = {{"]
    offset = 0
    for first, last in intervals:
        lines.append(f"    {{ 0x{first:X}, 0x{last:X}, 0x{offset:X} }},")
        offset += last - first + 1
    lines += ["};", ""]
    write_lines(path, lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out_dir", nargs="?", type=Path)
    parser.add_argument("prefix", nargs="?")
    parser.add_argument("sources", nargs="*", type=Path)
    parser.add_argument("--ui-sizes", default="", help="comma-separated sizes that use the UI charset, e.g. 10,12,14")
    parser.add_argument("--ui-charset", type=Path, default=UI_CHARSET, help=f"default: {UI_CHARSET.name}")
    parser.add_argument("--write-ui-charset", type=int, metavar="N",
                        help="rank the full charset by frequency, write the top N to --ui-charset and exit")
    args = parser.parse_args()

    ui_strings = set()
    for path in UI_TRANSLATIONS:
        ui_strings |= cjk_codepoints(path)
    full = big5_level1() | cjk_codepoints(CN_COMMON_CHARS) | ui_strings

    if args.write_ui_charset is not None:
        write_ui_charset(args.write_ui_charset, args.ui_charset, full, ui_strings)
        return
    if args.out_dir is None or args.prefix is None or not args.sources:
        sys.exit(__doc__)
    ui_sizes = {s for s in args.ui_sizes.split(",") if s}
    ui = cjk_codepoints(args.ui_charset) | ui_strings if ui_sizes else set()

    # (charset label, wanted codepoints, interval header, interval array name)
    charsets = {
        False: ("non-CJK glyphs + Big5 level-1 + cn_common_chars.txt + Chinese UI strings", full,
                f"{args.prefix}_intervals.h", f"{args.prefix}Intervals"),
        True: (f"non-CJK glyphs + {args.ui_charset.name} + Chinese UI strings", ui,
               f"{args.prefix}_ui_intervals.h", f"{args.prefix}UiIntervals"),
    }
    converted = []
    for src in args.sources:
        size = src.stem.rsplit("_", 1)[-1]
        if not size.isdigit():
            sys.exit(f"{src}: expected <family>_<size>.cpfont")
        name = f"{args.prefix}_{size}"
        is_ui = size in ui_sizes
        converted.append((name, src, is_ui, *convert(name, src, charsets[is_ui][1])))
    unknown = ui_sizes - {c[1].stem.rsplit("_", 1)[-1] for c in converted}
    if unknown:
        sys.exit(f"--ui-sizes {','.join(sorted(unknown))}: no matching source")

    # Sizes with the same charset cover the same codepoints, so one interval
    # table serves each group.
    for is_ui in (False, True):
        group = [c for c in converted if c[2] == is_ui]
        if not group:
            continue
        shared = group[0][5]
        for name, _, _, _, _, intervals, _ in group:
            if intervals != shared:
                sys.exit(f"{name}: interval table differs from {group[0][0]}; cannot share")
        write_intervals(args.out_dir / charsets[is_ui][2], charsets[is_ui][3], shared)

    for name, src, is_ui, glyph_rows, bitmaps, intervals, metrics in converted:
        label, _, intervals_header, intervals_name = charsets[is_ui]
        write_font(args.out_dir / f"{name}.h", name, src, label, intervals_header, intervals_name, len(intervals),
                   glyph_rows, bitmaps, metrics)


if __name__ == "__main__":
    main()
