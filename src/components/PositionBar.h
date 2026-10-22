#pragma once

#include <GfxRenderer.h>

#include <algorithm>

// UT37 position bar, one style for every scrolling or paging surface: a 1 px
// track line with a 3 px thumb centred on it. Vertical bars mark list pages;
// horizontal bars sit at the bottom of screens that page sideways.
namespace position_bar {

inline constexpr int kTrackWidth = 1;
inline constexpr int kThumbWidth = 3;
inline constexpr int kMinThumbLength = 8;

// Thumb length and offset along a track of `length` px when `visible` of
// `total` items show, starting at item `first` (a paged list passes its page
// start; the last page clamps to the track end).
inline void thumbSpan(const int length, const int total, const int visible, const int first, int& offset,
                      int& thumbLength) {
  thumbLength = std::min(length, std::max(kMinThumbLength, length * visible / std::max(1, total)));
  const int range = std::max(1, total - visible);
  offset = (length - thumbLength) * std::clamp(first, 0, range) / range;
}

// Track on column lineX over [y, y + height).
inline void drawVertical(const GfxRenderer& renderer, const int lineX, const int y, const int height, const int total,
                         const int visible, const int first) {
  if (total <= visible || visible <= 0 || height <= 0) return;
  int offset = 0;
  int thumbLength = 0;
  thumbSpan(height, total, visible, first, offset, thumbLength);
  renderer.fillRect(lineX, y, kTrackWidth, height, true);
  renderer.fillRect(lineX - (kThumbWidth - kTrackWidth) / 2, y + offset, kThumbWidth, thumbLength, true);
}

// Track on row lineY over [x, x + width).
inline void drawHorizontal(const GfxRenderer& renderer, const int x, const int lineY, const int width, const int total,
                           const int visible, const int first) {
  if (total <= visible || visible <= 0 || width <= 0) return;
  int offset = 0;
  int thumbLength = 0;
  thumbSpan(width, total, visible, first, offset, thumbLength);
  renderer.fillRect(x, lineY, width, kTrackWidth, true);
  renderer.fillRect(x + offset, lineY - (kThumbWidth - kTrackWidth) / 2, thumbLength, kThumbWidth, true);
}

}  // namespace position_bar
