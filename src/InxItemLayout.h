#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class InxItemLayout : uint8_t { Icons, List, Count };

// Low-PPI 240x416 panel (UT37): rows and margins sized for its 13-15 px bitmap
// UI fonts instead of the 800x480 Inx defaults. Drawing and touch hit-testing
// both read these values.
namespace InxCompactLayout {
#if FREEINK_DEVICE_UT37
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif
// UT37: every selectable row (and so its selection block) is 45 px tall, in
// GUI-drawn and FreeInkUI lists alike (uiThemeTokens copies it).
inline constexpr int rowHeight = enabled ? 45 : 66;
inline constexpr int sidePadding = enabled ? 8 : 20;
// Top strip: clock left, battery right, 3 px clear above and 6 px below the
// 13 px battery (status_strip::kHeight).
inline constexpr int statusStripHeight = enabled ? 22 : 0;
// Clear rows between the header's bottom rule and the first content (list
// rows, grid cells, the Recent carousel).
inline constexpr int contentTopGap = enabled ? 5 : 0;
}  // namespace InxCompactLayout

// Selected rows and cells fill black and invert their text and icons (UT37
// review: LightGray dither selection read poorly). false switches every Inx
// list, grid and popup to a LightGray dither with black content.
namespace InxSelection {
inline constexpr bool inverts = true;
}  // namespace InxSelection

namespace InxCoverGeometry {
inline constexpr int sourceWidth = 170;
inline constexpr int sourceHeight = 250;
inline constexpr int thumbnailSourceWidth = 3;
inline constexpr int thumbnailSourceHeight = 5;

struct Size {
  int width = 0;
  int height = 0;
};

constexpr Size fit(const int maxWidth, const int maxHeight) {
  if (maxWidth <= 0 || maxHeight <= 0) return {};

  int width = (maxHeight * sourceWidth + sourceHeight / 2) / sourceHeight;
  if (width > maxWidth) width = maxWidth;
  if (width < 1) width = 1;

  int height = (width * sourceHeight + sourceWidth / 2) / sourceWidth;
  if (height > maxHeight) {
    height = maxHeight;
    width = (height * sourceWidth + sourceHeight / 2) / sourceHeight;
    if (width > maxWidth) width = maxWidth;
    if (width < 1) width = 1;
  }
  return {width, height};
}

constexpr int thumbnailHeightForCropFill(const int displayHeight) {
  if (displayHeight <= 0) return 0;
  constexpr int denominator = sourceHeight * thumbnailSourceWidth;
  return (displayHeight * sourceWidth * thumbnailSourceHeight + denominator - 1) / denominator;
}
}  // namespace InxCoverGeometry

namespace InxGridGeometry {
inline constexpr int columns = 3;
inline constexpr int rows = 4;
inline constexpr int itemsPerPage = columns * rows;

constexpr InxItemLayout layoutFrom(const uint8_t value) {
  return value < static_cast<uint8_t>(InxItemLayout::Count) ? static_cast<InxItemLayout>(value) : InxItemLayout::Icons;
}

constexpr int pageStart(const int selected, const int itemCount) {
  if (selected < 0 || itemCount <= 0) return 0;
  const int clamped = selected < itemCount ? selected : itemCount - 1;
  return clamped / itemsPerPage * itemsPerPage;
}

constexpr int indexFromPoint(const int x, const int y, const int width, const int height, const int start,
                             const int itemCount) {
  if (x < 0 || y < 0 || x >= width || y >= height || width <= 0 || height <= 0) return -1;
  const int column = x * columns / width;
  const int row = y * rows / height;
  const int index = start + row * columns + column;
  return index < itemCount ? index : -1;
}
}  // namespace InxGridGeometry

namespace InxMenuGeometry {
inline constexpr int rowHeight = InxCompactLayout::rowHeight;

constexpr int pageItems(const int contentHeight) { return contentHeight < rowHeight ? 1 : contentHeight / rowHeight; }

constexpr int pageStart(const int selected, const int itemCount, const int contentHeight) {
  if (selected < 0 || itemCount <= 0) return 0;
  const int clamped = selected < itemCount ? selected : itemCount - 1;
  const int count = pageItems(contentHeight);
  return clamped / count * count;
}
}  // namespace InxMenuGeometry

namespace InxOptionGeometry {
inline constexpr int visibleRowLimit = 5;
// UT37 option rows share the 45 px selection block of every other list.
inline constexpr int rowHeight = InxCompactLayout::enabled ? InxCompactLayout::rowHeight : 62;
inline constexpr int headerHeight = InxCompactLayout::enabled ? 40 : 62;

constexpr int visibleRows(const int optionCount) {
  return optionCount < visibleRowLimit ? (optionCount > 0 ? optionCount : 0) : visibleRowLimit;
}

constexpr int start(const int selected, const int optionCount) {
  const int visible = visibleRows(optionCount);
  if (visible == 0) return 0;
  const int clamped = selected < 0 ? 0 : (selected < optionCount ? selected : optionCount - 1);
  const int wanted = clamped - visible / 2;
  const int maxStart = optionCount - visible;
  return wanted < 0 ? 0 : (wanted > maxStart ? maxStart : wanted);
}
}  // namespace InxOptionGeometry

namespace InxAccordionGeometry {
struct Row {
  int category = -1;
  int setting = -1;

  constexpr bool isCategory() const { return setting < 0; }
};

template <size_t N>
constexpr int visibleCount(const std::array<int, N>& settingCounts, const uint8_t expandedMask) {
  int count = static_cast<int>(N);
  for (size_t category = 0; category < N; ++category) {
    if ((expandedMask & (uint8_t{1} << category)) != 0) count += settingCounts[category];
  }
  return count;
}

template <size_t N>
constexpr Row rowAt(const std::array<int, N>& settingCounts, const uint8_t expandedMask, int row) {
  if (row < 0) return {};
  for (size_t category = 0; category < N; ++category) {
    if (row-- == 0) return {static_cast<int>(category), -1};
    if ((expandedMask & (uint8_t{1} << category)) == 0) continue;
    if (row < settingCounts[category]) return {static_cast<int>(category), row};
    row -= settingCounts[category];
  }
  return {};
}

template <size_t N>
constexpr int categoryRow(const std::array<int, N>& settingCounts, const uint8_t expandedMask,
                          const int wantedCategory) {
  int row = 0;
  for (int category = 0; category < wantedCategory && category < static_cast<int>(N); ++category) {
    ++row;
    if ((expandedMask & (uint8_t{1} << category)) != 0) row += settingCounts[category];
  }
  return row;
}
}  // namespace InxAccordionGeometry

namespace InxStatisticsGeometry {
constexpr int viewCount(const int bookCount) { return (bookCount > 0 ? bookCount : 0) + 1; }

constexpr int clampView(const int selected, const int bookCount) {
  const int last = viewCount(bookCount) - 1;
  return selected < 0 ? 0 : (selected > last ? last : selected);
}

constexpr int adjacentView(const int selected, const int bookCount, const int delta) {
  const int count = viewCount(bookCount);
  const int current = clampView(selected, bookCount);
  return ((current + delta) % count + count) % count;
}

constexpr uint64_t averageSessionMs(const uint64_t totalReadingMs, const uint32_t sessions) {
  return sessions == 0 ? 0 : totalReadingMs / sessions;
}
}  // namespace InxStatisticsGeometry
