#pragma once

#include <Txt.h>
#include <TxtEncoding.h>

#include <array>
#include <string>
#include <vector>

#include "BookmarkEntry.h"
#include "CrossPointSettings.h"
#include "ReaderActivity.h"

class TxtReaderActivity final : public ReaderActivity {
  enum class PageMode : uint8_t { Indexed, Direct };

  struct TxtLine {
    std::string text;
    bool indented = false;
  };
  static_assert(sizeof(TxtLine) <= sizeof(std::string) + alignof(std::string));

  static constexpr size_t DIRECT_PAGE_HISTORY_SIZE = 32;

  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  unsigned long openStartMs = 0;
  bool firstPageLogged = false;
  bool endOfBook = false;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  // Chapter jumps use a bounded local history instead of paginating from the
  // start of the book. On ESP32 this adds a fixed 128 bytes to the Activity.
  std::array<size_t, DIRECT_PAGE_HISTORY_SIZE> directPageOffsets{};
  size_t directPageIndex = 0;
  size_t directPageCount = 0;
  int directReturnPage = 0;
  PageMode pageMode = PageMode::Indexed;
  std::vector<TxtLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;
  bool indexComplete = false;
  bool indexCacheDirty = false;
  // Auto page turn, started/stopped from the control center (util/AutoPageTurn.h).
  bool autoTurnActive = false;
  uint8_t autoTurnRate = 0;
  unsigned long autoTurnStart = 0UL;
  // Rolling update (UT37): the next page replaces the current one line by line
  // over the interval. nextPageLines holds that page while rolling (one page of
  // text, a few KB, freed when auto turn stops); rollEpoch is the autoTurnStart
  // the roll belongs to.
  std::vector<TxtLine> nextPageLines;
  unsigned long rollEpoch = 0UL;
  uint8_t rollStep = 0;
  uint8_t rollLineCount = 0;
  bool rollSupported = true;
  size_t currentPageEndOffset = 0;
#ifdef ENABLE_CHINESE_VERSION
  txt_encoding::Encoding textEncoding = txt_encoding::Encoding::Unknown;
#else
  txt_encoding::Encoding textEncoding = txt_encoding::Encoding::Utf8;
#endif

  // Bookmarks share the EPUB per-book file; a TXT entry's visibleTextOffset is
  // the bookmarked page's starting byte offset. Loaded once per open (a few
  // dozen bytes per entry); the vector is the single source for the menu row,
  // the status-bar flag and toggling.
  std::vector<BookmarkEntry> cachedBookmarks;
  bool currentPageBookmarked = false;
  bool showBookmarkMessage = false;
  bool bookmarkRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  // A screen opened by a touch long-press goes back to the page on cancel,
  // not to the reader menu. openReaderMenu() clears it.
  bool openedFromShortcut = false;

  // Reused for the Activity lifetime: 8KB is too large for the render task's 8KB stack.
  std::unique_ptr<uint8_t[]> pageBuffer;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int paragraphIndentWidth = 0;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  // Draws rows [from, to) of `lines` in their row slots on the page.
  void drawTextLines(const std::vector<TxtLine>& lines, size_t from, size_t to) const;
  // Follows the control center; true when this loop's input was consumed.
  bool handleAutoPageTurn();
  void stopAutoPageTurn();
  // One rolling frame; false (and rolling off for this page) at the end of the book.
  bool renderRollingFrame();
  void renderStatusBar() const;

  void initializeReader();
  void probeTextEncoding();
  bool loadPageAtOffset(size_t offset, std::vector<TxtLine>& outLines, size_t& nextOffset);
  bool advancePageIndex(size_t nextOffset);
  bool extendIndexToPage(size_t targetPage);
  // Leave Direct mode for the indexed page holding the current direct page's
  // start, laying out the index up to it first. False (still Direct) if the
  // index cannot reach it.
  bool indexDirectPosition();
  void goToSourceOffset(size_t sourceOffset, int returnPage);
  size_t getCurrentSourceOffset() const;
  int getDisplayPageNumber() const;
  void openReaderMenu();
  void onReaderMenuConfirm(int action);
  void reopenMenuAfterCancel();
  void runTouchLongPress(uint8_t action);
  void toggleBookmark();
  void updateBookmarkFlag();
  void openBookmarks();
  void openDictionaryWordSelect();
  bool launchKOReaderSync();
  // Left edge of a drawn line (indent + alignment), shared by drawing and the
  // dictionary's word boxes so both use one geometry.
  int lineStartX(const TxtLine& line) const;
  void openChapterSelection();
  // Drop the pagination so the next render re-measures the viewport (font,
  // margins or orientation changed) and resumes at the saved byte offset.
  void resetLayout();
  void applyReaderOrientation(uint8_t orientation);
  void jumpToPercent(int percent);
  void updateTotalPages();
  int getProgressPercent() const;
  bool loadPageIndexCache();
  bool savePageIndexCache();
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  void onExit() override;
  bool preventAutoSleep() override { return autoTurnActive; }
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override { return endOfBook; }
  void onReturnFromEndOfBook() override { endOfBook = false; }
};
