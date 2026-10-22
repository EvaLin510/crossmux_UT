#pragma once

// Unified firmware: compact international UI fonts are primary, with the
// built-in Simplified-Chinese subsets registered as missing-glyph fallbacks.
// Broad reader families and larger CJK sizes are supplied as .cpfont files.
//
// Low-PPI panels (UT37: 3.7" 240x416, ~150 PPI) instead replace the UI faces
// with 1-bit WenQuanYi Zen Hei bitmap strikes covering Traditional and
// Simplified common characters; the outline 8/10pt CJK subsets are dropped to
// pay for them. The 14 px strike also serves as the offline reader fallback, so
// notosans_cjk_12 (~575 KB) is not compiled in either.
#if FREEINK_DEVICE_UT37
#define CROSSMUX_BITMAP_UI_FONT 1
#else
#define CROSSMUX_BITMAP_UI_FONT 0
#endif

#include <builtinFonts/notosans_8_regular.h>
#if !CROSSMUX_BITMAP_UI_FONT
#include <builtinFonts/notosans_cjk_12.h>
#endif
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_medium.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_medium.h>
#if CROSSMUX_BITMAP_UI_FONT
#include <builtinFonts/wqy_zhenghei_10.h>
#include <builtinFonts/wqy_zhenghei_12.h>
#include <builtinFonts/wqy_zhenghei_14.h>
#include <builtinFonts/wqy_zhenghei_16.h>
#else
#include <builtinFonts/notosans_cjk_8.h>
#include <builtinFonts/notosans_cjk_10.h>
#endif
