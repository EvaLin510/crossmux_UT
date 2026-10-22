#pragma once

#include <cstdint>

class HalFile;

// UTF-8 reader for the WeRead text caches (book intros and highlight/review
// records), shared by WeReadActivity and WeReadBrowseActivity.
namespace weread {

struct Utf8Glyph {
  char text[5] = {};      // NUL-terminated glyph, or "?" for a malformed sequence
  uint8_t fileBytes = 0;  // bytes consumed from the file
  uint8_t textBytes = 0;  // bytes in text, excluding the NUL
};

// Reads one code point, never more than `remaining` bytes. Returns false at the
// end of the budget or on a read error.
bool readUtf8Glyph(HalFile& file, uint32_t remaining, Utf8Glyph& glyph);

}  // namespace weread
