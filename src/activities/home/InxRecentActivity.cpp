#include "InxRecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/PositionBar.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/themes/inx/InxTheme.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr int kGap = 8;
constexpr int kPagePadding = 18;
constexpr int kProgressHeight = 6;
constexpr int kHomeBatteryWidth = 15;
constexpr int kHomeBatteryHeight = 12;
constexpr int kHomeBatteryRightMargin = 12;

// The grid pages vertically through more than 4 books; it then keeps a strip
// on the right for the shared side scrollbar, and drawing and hit-testing
// both size their cells from this width.
constexpr int kGridScrollBarReserve = 6;
int gridCellsWidth(const Rect& content, const int bookCount) {
  return content.width -
         (bookCount > InxRecentGeometry::itemsPerPage(InxRecentLayout::Grid) ? kGridScrollBarReserve : 0);
}

Rect contentRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return Rect{0, top, renderer.getScreenWidth(),
              InxRecentGeometry::contentHeight(renderer.getScreenHeight(), top, metrics.buttonHintsHeight)};
}

const char* titleOf(const RecentBook& book) { return book.title.empty() ? book.path.c_str() : book.title.c_str(); }

void drawMiniProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 2);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) renderer.fillRect(rect.x + 1, rect.y + 1, fillWidth, std::max(0, rect.height - 2));
}

uint8_t progressOf(const ReadingBookStats* stats) { return stats ? stats->lastProgressPercent : 0; }

// Placeholder-cover title wrap. GfxRenderer::wrappedText() breaks only at
// spaces, so a CJK title (no spaces) collapsed to one ellipsised line. This
// breaks between any two characters, backing up to the last space when a Latin
// word would be split, and ellipsises only what the last line cannot hold.
std::vector<std::string> wrapCoverTitle(const GfxRenderer& renderer, const int fontId, const char* text,
                                        const int maxWidth, const int maxLines) {
  std::vector<std::string> lines;
  if (!text || maxWidth <= 0 || maxLines <= 0) return lines;
  lines.reserve(static_cast<size_t>(maxLines));
  const char* p = text;
  while (*p == ' ') ++p;
  while (*p && static_cast<int>(lines.size()) < maxLines) {
    if (static_cast<int>(lines.size()) == maxLines - 1) {
      lines.push_back(renderer.truncatedText(fontId, p, maxWidth));
      break;
    }
    // Longest prefix that fits, advancing one UTF-8 character at a time.
    const char* end = p;
    const char* lastSpace = nullptr;
    std::string candidate;
    while (*end) {
      const char* next = end + 1;
      while (*next && (static_cast<unsigned char>(*next) & 0xC0) == 0x80) ++next;
      candidate.assign(p, next);
      if (renderer.getTextWidth(fontId, candidate.c_str()) > maxWidth) break;
      if (*end == ' ') lastSpace = end;
      end = next;
    }
    if (end == p) {  // not even one character fits
      lines.push_back(renderer.truncatedText(fontId, p, maxWidth));
      break;
    }
    const bool splitsWord = *end && *end != ' ' && static_cast<unsigned char>(*end) < 0x80 &&
                            static_cast<unsigned char>(end[-1]) < 0x80 && end[-1] != ' ';
    if (splitsWord && lastSpace) end = lastSpace;
    std::string line(p, end);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    lines.push_back(std::move(line));
    p = end;
    while (*p == ' ') ++p;
  }
  return lines;
}

void drawSparseInk(const GfxRenderer& renderer, const Rect rect) {
  for (int y = rect.y; y < rect.y + rect.height; y += 2) {
    for (int x = rect.x; x < rect.x + rect.width; x += 2) renderer.drawPixel(x, y, true);
  }
}

void drawDottedSeparator(const GfxRenderer& renderer, const int x, const int y, const int width) {
  for (int px = x; px < x + width; px += 3) renderer.drawPixel(px, y, true);
}

Rect fitCoverRect(const Rect bounds) {
  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  return Rect{bounds.x + (bounds.width - size.width) / 2, bounds.y + (bounds.height - size.height) / 2, size.width,
              size.height};
}

void drawThickFrame(const GfxRenderer& renderer, const Rect rect, const int thickness = 3) {
  for (int inset = 0; inset < thickness; ++inset) {
    renderer.drawRect(rect.x - inset, rect.y - inset, rect.width + inset * 2, rect.height + inset * 2, true);
  }
}

void drawProgressBadge(const GfxRenderer& renderer, const Rect cover, const uint8_t progress) {
  char label[8];
  snprintf(label, sizeof(label), "%u%%", static_cast<unsigned>(progress));
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
  const int textHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int width = std::max(24, textWidth + 8);
  const int height = textHeight + 4;
  const int x = cover.x + cover.width - width - 2;
  const int y = cover.y + 2;
  renderer.fillRect(x, y, width, height, true);
  renderer.drawText(SMALL_FONT_ID, x + (width - textWidth) / 2, y + 2, label, false, EpdFontFamily::BOLD);
}

void drawMetric(const GfxRenderer& renderer, const int x, const int y, const char* value, const char* label,
                const int width) {
  const std::string shownValue = renderer.truncatedText(UI_12_FONT_ID, value, width, EpdFontFamily::BOLD);
  const std::string shownLabel = renderer.truncatedText(SMALL_FONT_ID, label, width);
  renderer.drawText(UI_12_FONT_ID, x, y, shownValue.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, x, y + renderer.getLineHeight(UI_12_FONT_ID) + 4, shownLabel.c_str());
}

// Returns the y just below the drawn text.
int drawBookText(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const bool author) {
  // Compact: title one UI size above the author, regular weight (bitmap faces
  // have no bold); the author line follows the title's line height.
  constexpr auto titleStyle = InxCompactLayout::enabled ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, titleOf(book), width, titleStyle);
  renderer.drawText(UI_10_FONT_ID, x, y, title.c_str(), true, titleStyle);
  int bottom = y + renderer.getLineHeight(UI_10_FONT_ID);
  if (author && !book.author.empty()) {
    const int authorY = InxCompactLayout::enabled ? bottom : y + 27;
    const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x, authorY, subtitle.c_str());
    bottom = authorY + renderer.getLineHeight(SMALL_FONT_ID);
  }
  return bottom;
}
}  // namespace

void InxRecentActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  selected = MainTabs::contentEdgeIndex(edge, books ? static_cast<int>(books->size()) : 0);
}

InxRecentLayout InxRecentActivity::layout() const {
  const auto value = static_cast<InxRecentLayout>(SETTINGS.inxRecentLayout);
  return value < InxRecentLayout::Count ? value : InxRecentLayout::Flow;
}

const ReadingBookStats* InxRecentActivity::statsAt(const int index) const {
  return index >= 0 && index < static_cast<int>(bookStats.size()) ? bookStats[index] : nullptr;
}

void InxRecentActivity::onEnter() {
  Activity::onEnter();
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  books = &RECENT_BOOKS.getBooks();
  bookStats.fill(nullptr);
  for (size_t i = 0; i < std::min(books->size(), bookStats.size()); ++i) {
    const RecentBook& book = (*books)[i];
    bookStats[i] = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  }
  selected = 0;
  thumbnailHeight = 0;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  requestUpdate();
}

void InxRecentActivity::onExit() {
  books = nullptr;
  bookStats.fill(nullptr);
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  thumbnailHeight = 0;
  Activity::onExit();
}

void InxRecentActivity::openSelected() {
  if (!books || selected < 0 || selected >= static_cast<int>(books->size())) return;
  onSelectBook((*books)[selected].path);
}

void InxRecentActivity::setThumbnailHeight(const int height) {
  if (thumbnailHeight == height) return;
  thumbnailHeight = height;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
}

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
void InxRecentActivity::clearCoverCaches() {
  std::for_each(targetCoverCaches.begin(), targetCoverCaches.end(), [](auto& cache) { cache = {}; });
  std::for_each(fallbackCoverCaches.begin(), fallbackCoverCaches.end(), [](auto& cache) { cache = {}; });
  cachedCoverBytes = 0;
}

InxRecentActivity::CoverCacheLoadResult InxRecentActivity::tryLoadCoverCache(HalFile& file, CoverRamCache& cache) {
  if (cache.attempted) return CoverCacheLoadResult::Stream;
  cache.attempted = true;

  const size_t fileSize = file.fileSize();
  if (fileSize == 0 || fileSize > MAX_CACHED_COVER_FILE_BYTES || cachedCoverBytes > MAX_COVER_CACHE_BYTES ||
      fileSize > MAX_COVER_CACHE_BYTES - cachedCoverBytes) {
    return CoverCacheLoadResult::Stream;
  }
  if (!memory::psramHasHeadroom(fileSize, fileSize, COVER_CACHE_MAX_ALLOC_RESERVE)) {
    return CoverCacheLoadResult::Stream;
  }

  // A cover is up to 64 KB, so it cannot live on the task stack. Allocate once
  // on first use and retain it for this Activity.
  auto bytes = memory::makePsramByteBufferNoThrow(fileSize);
  if (!bytes) return CoverCacheLoadResult::Stream;
  if (!file.seek(0) || file.read(bytes.get(), fileSize) != static_cast<int>(fileSize)) {
    LOG_ERR("INX", "Short read loading cover into PSRAM (%u bytes)", static_cast<unsigned>(fileSize));
    file.seek(0);
    return CoverCacheLoadResult::Stream;
  }

  Bitmap bitmap(bytes.get(), fileSize);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    return CoverCacheLoadResult::Invalid;
  }

  cache.bytes = std::move(bytes);
  cache.size = fileSize;
  cachedCoverBytes += fileSize;
  return CoverCacheLoadResult::Loaded;
}
#endif

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state,
                                         CoverRamCache& cache) {
#else
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state) {
#endif
  switch (state) {
    case CoverCacheState::Unchecked:
      if (!Storage.exists(path.c_str())) {
        state = CoverCacheState::Missing;
        return false;
      }
      state = CoverCacheState::Ready;
      break;
    case CoverCacheState::Ready:
      break;
    case CoverCacheState::Missing:
    case CoverCacheState::Unavailable:
      return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  const auto drawCached = [this, &bounds, &state, &cache] {
    Bitmap bitmap(cache.bytes.get(), cache.size);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
      cachedCoverBytes -= cache.size;
      cache = {};
      state = CoverCacheState::Missing;
      return false;
    }
    return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
  };

  if (cache.bytes) return drawCached();
#endif

  HalFile file;
  if (!Storage.openFileForRead("INX", path, file)) {
    state = CoverCacheState::Missing;
    return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  switch (tryLoadCoverCache(file, cache)) {
    case CoverCacheLoadResult::Loaded:
      return drawCached();
    case CoverCacheLoadResult::Invalid:
      state = CoverCacheState::Missing;
      return false;
    case CoverCacheLoadResult::Stream:
      break;
  }
#endif

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    state = CoverCacheState::Missing;
    return false;
  }
  return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
}

bool InxRecentActivity::drawBookCover(const int bookIndex, const Rect& bounds) {
  renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, false);
  if (books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size()) &&
      bookIndex < static_cast<int>(RecentBooksStore::MAX_RECENT_BOOKS) && thumbnailHeight > 0) {
    const RecentBook& book = (*books)[bookIndex];
    if (!book.coverBmpPath.empty()) {
      std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbnailHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex], targetCoverCaches[bookIndex])) return true;
#else
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex])) return true;
#endif
      if (book.coverBmpPath.find("[HEIGHT]") != std::string::npos &&
          thumbnailHeight != InxMetrics::values.homeCoverHeight) {
        path = UITheme::getCoverThumbPath(book.coverBmpPath, InxMetrics::values.homeCoverHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex], fallbackCoverCaches[bookIndex])) return true;
#else
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex])) return true;
#endif
      }
    }
  }

  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  const int x = bounds.x + (bounds.width - size.width) / 2;
  const int y = bounds.y + (bounds.height - size.height) / 2;
  renderer.drawRect(x, y, size.width, size.height, 2, true);
  renderer.fillRect(x, y + size.height / 3, size.width, size.height * 2 / 3);
  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, x + (size.width - iconSize) / 2, y + size.height / 6 - iconSize / 2, iconSize);
  // Books without a cover (TXT and coverless EPUBs) show their title in white
  // on the black part, wrapped and centred, so placeholders stay tell-apart.
  if (books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size())) {
    const int fontId = size.width >= 90 ? UI_10_FONT_ID : SMALL_FONT_ID;
    constexpr int pad = 4;
    const int areaY = y + size.height / 3 + pad;
    const int areaH = size.height * 2 / 3 - 2 * pad;
    const int lineH = renderer.getLineHeight(fontId);
    const int maxLines = std::max(1, areaH / lineH);
    const auto lines = wrapCoverTitle(renderer, fontId, titleOf((*books)[bookIndex]), size.width - 2 * pad, maxLines);
    int lineY = areaY + std::max(0, (areaH - static_cast<int>(lines.size()) * lineH) / 2);
    const GfxRenderer::ClipScope clip(renderer, x + 2, y + size.height / 3, size.width - 4, size.height * 2 / 3 - 2);
    for (const auto& line : lines) {
      const int lineW = renderer.getTextWidth(fontId, line.c_str());
      renderer.drawText(fontId, x + (size.width - lineW) / 2, lineY, line.c_str(), false);
      lineY += lineH;
    }
  }
  return false;
}

bool InxRecentActivity::prepareNextMissingCover() {
  if (!books || thumbnailHeight <= 0) return false;
  const size_t count = std::min(books->size(), RecentBooksStore::MAX_RECENT_BOOKS);
  if (count == 0) return false;
  // Only covers on screen are generated. Building an off-screen thumbnail
  // (seconds for a JPEG cover) holds the render task, so the next swipe used
  // to wait behind books the user had not reached yet; those now get their
  // thumbnail when they scroll into view.
  const InxRecentLayout currentLayout = layout();
  // On-screen books, at most one page (Icons: 3x3); stack array, no heap.
  std::array<int, 9> onScreen{};
  static_assert(InxRecentGeometry::itemsPerPage(InxRecentLayout::Icons) <= 9, "onScreen holds one page");
  size_t onScreenCount = 0;
  const auto addOnScreen = [&](const int index) {
    if (index >= 0 && static_cast<size_t>(index) < count && onScreenCount < onScreen.size()) {
      onScreen[onScreenCount++] = index;
    }
  };
  addOnScreen(std::max(0, selected));
  switch (currentLayout) {
    // Flow: only the centre book. Generating a side cover held the render
    // task on every swipe into uncached EPUBs; side covers draw any existing
    // thumbnail (or the placeholder) and get theirs once centred.
    case InxRecentLayout::Flow:
    case InxRecentLayout::Cover:
    case InxRecentLayout::Count:
      break;
    default: {
      onScreenCount = 0;
      const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), currentLayout);
      for (int i = 0; i < InxRecentGeometry::itemsPerPage(currentLayout); ++i) addOnScreen(start + i);
      break;
    }
  }
  bool displayed = false;
  for (size_t slot = 0; slot < onScreenCount; ++slot) {
    const size_t index = static_cast<size_t>(onScreen[slot]);
    CoverCacheState& state = targetCoverStates[index];
    switch (state) {
      case CoverCacheState::Unchecked: {
        const std::string& templatePath = (*books)[index].coverBmpPath;
        if (templatePath.empty()) {
          state = CoverCacheState::Unavailable;
          continue;
        }
        state = Storage.exists(UITheme::getCoverThumbPath(templatePath, thumbnailHeight).c_str())
                    ? CoverCacheState::Ready
                    : CoverCacheState::Missing;
        if (state == CoverCacheState::Ready) continue;
        break;
      }
      case CoverCacheState::Ready:
      case CoverCacheState::Unavailable:
        continue;
      case CoverCacheState::Missing:
        break;
    }

    // Show the frame once before the (possibly seconds-long) generation.
    if (!displayed) {
      renderer.displayBuffer();
      displayed = true;
    }
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      state = BookCoverLoader::ensureThumbnail((*books)[index].path, thumbnailHeight).empty()
                  ? CoverCacheState::Unavailable
                  : CoverCacheState::Ready;
    }
    // A coverless EPUB (empty thumb marker) stays a placeholder, which is already
    // on screen; only a new thumbnail needs another render and refresh.
    if (state == CoverCacheState::Ready) {
      requestUpdate();
      return true;
    }
  }
  return displayed;
}

namespace {
// Flow carousel boxes, shared by drawFlow() and indexFromPoint() so drawing
// and hit-testing use one geometry source.
struct FlowRects {
  Rect carousel;
  Rect center;
  Rect left;
  Rect right;
};

FlowRects flowRects(const Rect& content) {
  FlowRects rects;
  const int carouselHeight = std::max(1, content.height * 50 / 100);
  rects.carousel = Rect{content.x, content.y + (InxCompactLayout::enabled ? InxCompactLayout::contentTopGap : 5),
                        content.width, carouselHeight};
  const Rect& carousel = rects.carousel;
  const auto centerSize = InxCoverGeometry::fit(carousel.width, std::max(1, carousel.height * 94 / 100));
  rects.center = Rect{carousel.x + (carousel.width - centerSize.width) / 2,
                      carousel.y + (carousel.height - centerSize.height) / 2, centerSize.width, centerSize.height};
  const Rect& center = rects.center;
  const auto sideSize = InxCoverGeometry::fit(carousel.width, std::max(1, center.height * 90 / 100));
  const int sideTop = center.y + (center.height - sideSize.height) / 2;
  const int sideGap = std::max(kGap, content.width * 4 / 100);
  rects.left = Rect{center.x - sideSize.width - sideGap, sideTop, sideSize.width, sideSize.height};
  rects.right = Rect{center.x + center.width + sideGap, sideTop, sideSize.width, sideSize.height};
  return rects;
}

bool contains(const Rect& rect, const int x, const int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}
}  // namespace

int InxRecentActivity::indexFromPoint(const int x, const int y) const {
  if (!books || books->empty()) return -1;
  const Rect content = contentRect(renderer);
  if (x < content.x || x >= content.x + content.width || y < content.y || y >= content.y + content.height) return -1;

  const InxRecentLayout currentLayout = layout();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), currentLayout);
  int columns = 1;
  int rows = 1;
  switch (currentLayout) {
    case InxRecentLayout::Grid:
      columns = 2;
      rows = 2;
      break;
    case InxRecentLayout::List:
      rows = 5;
      break;
    case InxRecentLayout::Icons:
      columns = 3;
      rows = 3;
      break;
    case InxRecentLayout::Flow: {
      // A side cover opens that book; the rest of the page opens the centre one.
      const FlowRects rects = flowRects(content);
      const bool inCarousel = contains(rects.carousel, x, y);
      const int previous = flowNeighbour(-1);
      const int next = flowNeighbour(1);
      if (inCarousel && previous >= 0 && contains(rects.left, x, y)) return previous;
      if (inCarousel && next >= 0 && contains(rects.right, x, y)) return next;
      return selected;
    }
    case InxRecentLayout::Cover:
      return selected;
    case InxRecentLayout::Count:
      return -1;
  }

  const int cellsWidth = currentLayout == InxRecentLayout::Grid
                             ? gridCellsWidth(content, static_cast<int>(books->size()))
                             : content.width;
  if (x >= content.x + cellsWidth) return -1;  // the grid's scrollbar strip
  const int column = std::min(columns - 1, (x - content.x) * columns / std::max(1, cellsWidth));
  const int row = std::min(rows - 1, (y - content.y) * rows / std::max(1, content.height));
  const int index = start + row * columns + column;
  return index < static_cast<int>(books->size()) ? index : -1;
}

void InxRecentActivity::loop() {
  if (!books || books->empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int touched = indexFromPoint(x, y);
    // Flow: moving the selection on touch-down would re-centre the carousel
    // and the release would then hit a different book; the tap opens it.
    if (touched >= 0 && touched != selected && layout() != InxRecentLayout::Flow) {
      selected = touched;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0) {
      selected = touched;
      openSelected();
    }
    return;
  }

  const int count = static_cast<int>(books->size());
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) || swipe == MappedInputManager::SwipeDir::Up ||
      swipe == MappedInputManager::SwipeDir::Left) {
    selected = (selected + 1) % count;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) || swipe == MappedInputManager::SwipeDir::Down ||
      swipe == MappedInputManager::SwipeDir::Right) {
    selected = (selected + count - 1) % count;
    requestUpdate();
    return;
  }
}

int InxRecentActivity::flowNeighbour(const int direction) const {
  const int count = books ? static_cast<int>(books->size()) : 0;
  if (count < 2) return -1;
  return (selected + direction + count) % count;
}

void InxRecentActivity::drawFlow(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const RecentBook& book = (*books)[selected];
  const FlowRects rects = flowRects(content);
  const Rect& carousel = rects.carousel;
  const Rect& center = rects.center;
  drawSparseInk(renderer, carousel);
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(center.height));
  {
    const GfxRenderer::ClipScope clip(renderer, carousel.x, carousel.y, carousel.width, carousel.height);
    // Navigation wraps, so the carousel does too: the first book shows the
    // last one on its left.
    const int previous = flowNeighbour(-1);
    const int next = flowNeighbour(1);
    if (previous >= 0) drawBookCover(previous, rects.left);
    if (next >= 0) drawBookCover(next, rects.right);
    drawBookCover(selected, center);
    if (showSelection) drawThickFrame(renderer, center);
  }

  constexpr bool compact = InxCompactLayout::enabled;
  // Compact: 5 clear rows between the carousel's dither and the divider.
  const int dividerY = carousel.y + carousel.height + (compact ? 5 : 10);
  renderer.drawLine(content.x, dividerY, content.x + content.width - 1, dividerY, true);
  const int textY = dividerY + (compact ? 8 : 15);
  const int textBottom = drawBookText(renderer, book, kPagePadding, textY, content.width - kPagePadding * 2, true);

  const ReadingBookStats* stats = statsAt(selected);
  const uint8_t progress = progressOf(stats);
  // Compact: the bar follows the title/author block instead of a fixed offset.
  const int progressY = compact ? textBottom + 6 : textY + 58;
  // Compact: a third longer than the half-width bar (UT37 review).
  const int progressWidth = std::max(24, (content.width - kPagePadding * 2) * (compact ? 2 : 1) / (compact ? 3 : 2));
  drawMiniProgress(renderer, Rect{kPagePadding, progressY, progressWidth, kProgressHeight}, progress);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress));
  renderer.drawText(SMALL_FONT_ID, kPagePadding + progressWidth + 12,
                    progressY - (renderer.getLineHeight(SMALL_FONT_ID) - kProgressHeight) / 2, percent);

  const int metricsTop = progressY + (compact ? 18 : 34);
  const int metricWidth = (content.width - kPagePadding * 2 - kGap) / 2;
  const int metricHeight = std::max(1, (content.y + content.height - metricsTop) / 2);
  const std::string total = stats ? ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs) : "0m";
  const std::string last = stats ? ReadingStatsAnalytics::formatDurationHm(stats->lastSessionMs) : "0m";
  char sessions[16];
  char chapter[8];
  snprintf(sessions, sizeof(sessions), "%u", stats ? static_cast<unsigned>(stats->sessions) : 0U);
  snprintf(chapter, sizeof(chapter), "%u%%", stats ? static_cast<unsigned>(stats->chapterProgressPercent) : 0U);
  drawMetric(renderer, kPagePadding, metricsTop, total.c_str(), tr(STR_TOTAL_TIME), metricWidth);
  drawMetric(renderer, kPagePadding + metricWidth + kGap, metricsTop, sessions, tr(STR_SESSIONS), metricWidth);
  drawMetric(renderer, kPagePadding, metricsTop + metricHeight, last.c_str(), tr(STR_LAST_SESSION), metricWidth);
  drawMetric(renderer, kPagePadding + metricWidth + kGap, metricsTop + metricHeight, chapter, tr(STR_CHAPTER_PROGRESS),
             metricWidth);
}

void InxRecentActivity::drawGrid(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int count = static_cast<int>(books->size());
  const int start = InxRecentGeometry::pageStart(selected, count, layout());
  const int cellWidth = gridCellsWidth(content, count) / 2;
  const int cellHeight = content.height / 2;
  for (int slot = 0; slot < 4 && start + slot < count; ++slot) {
    const int index = start + slot;
    const int column = slot % 2;
    const int row = slot / 2;
    // UT37: the first row keeps contentTopGap under the header rule.
    const int cellTop = InxCompactLayout::enabled ? InxCompactLayout::contentTopGap : kGap / 2;
    const Rect cell{content.x + column * cellWidth + kGap / 2, content.y + row * cellHeight + cellTop,
                    cellWidth - kGap, cellHeight - kGap};
    if (showSelection && index == selected) drawSparseInk(renderer, cell);
    const Rect cover = fitCoverRect(Rect{cell.x + kGap, cell.y + kGap, cell.width - kGap * 2, cell.height - kGap * 2});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    if (showSelection && index == selected) drawThickFrame(renderer, cover);
    const int barWidth = std::max(24, cover.width - 30);
    const int barX = cover.x + (cover.width - barWidth) / 2;
    const int barY = cover.y + cover.height - 18;
    renderer.fillRect(barX - 2, barY - 2, barWidth + 4, kProgressHeight + 4, false);
    drawMiniProgress(renderer, Rect{barX, barY, barWidth, kProgressHeight}, progressOf(statsAt(index)));
  }
  // Same side scrollbar as the app grid and the lists (no-op within one page).
  GUI.drawSideScrollBar(renderer, content, count, start, InxRecentGeometry::itemsPerPage(InxRecentLayout::Grid));
}

void InxRecentActivity::drawList(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int rowHeight = content.height / 5;
  for (int slot = 0; slot < 5 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const Rect row{content.x, content.y + slot * rowHeight, content.width, rowHeight};
    if (showSelection && index == selected) drawSparseInk(renderer, row);
    const Rect cover = fitCoverRect(Rect{row.x + kPagePadding, row.y + 5, 88, row.height - 10});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    const int textX = cover.x + cover.width + 14;
    const int textWidth = row.x + row.width - kPagePadding - textX;
    int barY = row.y + row.height - 15;
    if constexpr (InxCompactLayout::enabled) {
      // Centre title, author and bar as one block so the bar never covers the author.
      constexpr int barGap = 4;
      const int block = renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + barGap +
                        kProgressHeight;
      const int textBottom = drawBookText(renderer, (*books)[index], textX, row.y + std::max(0, (row.height - block) / 2),
                                          textWidth, true);
      barY = std::min(textBottom + barGap, row.y + row.height - kProgressHeight - 2);
    } else {
      drawBookText(renderer, (*books)[index], textX, row.y + 14, textWidth, true);
    }
    drawMiniProgress(renderer, Rect{textX, barY, std::max(24, textWidth * 80 / 100), kProgressHeight},
                     progressOf(statsAt(index)));
    if (slot + 1 < 5 && start + slot + 1 < static_cast<int>(books->size())) {
      drawDottedSeparator(renderer, row.x + kGap, row.y + row.height - 1, row.width - kGap * 2);
    }
  }
}

void InxRecentActivity::drawIcons(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 3;
  const int cellHeight = content.height / 3;
  for (int slot = 0; slot < 9 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 3;
    const int row = slot / 3;
    const Rect cell{content.x + column * cellWidth + 5, content.y + row * cellHeight + 5, cellWidth - 10,
                    cellHeight - 10};
    const Rect cover = fitCoverRect(Rect{cell.x + 4, cell.y + 4, cell.width - 8, cell.height - 8});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    drawProgressBadge(renderer, cover, progressOf(statsAt(index)));
    if (showSelection && index == selected)
      drawThickFrame(renderer, Rect{cover.x - 2, cover.y - 2, cover.width + 4, cover.height + 4});
  }
}

void InxRecentActivity::drawCover(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  constexpr int progressGap = 10;
  const int progressBlockHeight = progressGap + 8;
  const int targetWidth = std::max(1, content.width * 78 / 100);
  const Rect cover = fitCoverRect(Rect{content.x + (content.width - targetWidth) / 2, content.y + 6, targetWidth,
                                       std::max(1, content.height - progressBlockHeight - 12)});
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
  drawBookCover(selected, cover);
  if (showSelection) drawThickFrame(renderer, cover);
  const int barWidth = std::max(24, cover.width * 80 / 100);
  drawMiniProgress(renderer,
                   Rect{cover.x + (cover.width - barWidth) / 2, cover.y + cover.height + progressGap, barWidth, 8},
                   progressOf(statsAt(selected)));
}

void InxRecentActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  drawPageHeader(Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));
  const Rect content = contentRect(renderer);

  if (!books || books->empty()) {
    UITheme::drawCenteredWrappedText(renderer, content, UI_12_FONT_ID, tr(STR_NO_RECENT_BOOKS), 2);
  } else {
    switch (layout()) {
      case InxRecentLayout::Flow:
        drawFlow(content);
        break;
      case InxRecentLayout::Grid:
        drawGrid(content);
        break;
      case InxRecentLayout::List:
        drawList(content);
        break;
      case InxRecentLayout::Icons:
        drawIcons(content);
        break;
      case InxRecentLayout::Cover:
        drawCover(content);
        break;
      case InxRecentLayout::Count:
        break;
    }
    const InxRecentLayout current = layout();
    if (InxCompactLayout::enabled && (current == InxRecentLayout::Flow || current == InxRecentLayout::Cover)) {
      // One book per page, paged sideways: bottom position bar, 3 px clear
      // below its thumb.
      position_bar::drawHorizontal(renderer, content.x + kPagePadding,
                                   renderer.getScreenHeight() - 3 - position_bar::kThumbWidth + 1,
                                   content.width - kPagePadding * 2, static_cast<int>(books->size()), 1, selected);
    }
  }

  const auto labels = mainTabButtonLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "", tr(STR_OPEN),
                                          books && books->size() > 1, false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // UT37 shows the battery in the top strip; book info belongs to the reader only.
  if constexpr (!InxCompactLayout::enabled) {
    GUI.drawBatteryRight(renderer,
                         Rect{renderer.getScreenWidth() - kHomeBatteryRightMargin - kHomeBatteryWidth,
                              renderer.getScreenHeight() - 30, kHomeBatteryWidth, kHomeBatteryHeight},
                         SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  }
  if (prepareNextMissingCover()) return;
  renderer.displayBuffer();
}
