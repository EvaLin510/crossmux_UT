#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderActivity.h"
#include "ReaderFontPreview.h"
#include "components/OptionPopup.h"

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  uint8_t pageTurnRate = 15;
  int8_t pendingManualTurn = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingSyncSaveError = false;
  bool pendingSyncLaunchError = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  bool automaticPageTurnActive = false;
  // Rolling auto page turn (UT37): the next page replaces the current one line
  // by line from the top over the turn interval. rollEpoch is the
  // lastPageTurnTime the current roll started from; a different value means the
  // page changed and the roll restarts.
  unsigned long rollEpoch = 0UL;
  uint8_t rollStep = 0;
  uint8_t rollLineCount = 0;
  bool rollSupported = true;
  bool showBookmarkMessage = false;
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool currentPageBookmarked = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool pendingReadFolderMove = false;

#ifdef ENABLE_CHINESE_VERSION
  std::atomic<uint32_t> pendingMissingChineseCodepoint_{0};
  char wereadBookId_[64] = {};
  bool clearInitialProgressAfterSave_ = false;
  bool maybeOfferCompleteChineseFont();
#endif

  // Modal option picker for the SD font preload prompt.
  OptionPopup overlayPopup;
  ReaderFontPreview fontPreview;
  enum class FontPromptState { Idle, Asking, Accepted };
  FontPromptState fontPromptState = FontPromptState::Idle;
  bool fontPromptWaitForBackRelease = false;

  // Footnote support
  FootnoteList currentPageFootnotes;
  PageLinkList currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
    // Fixed-size return stack; offsets survive session CSS fallback without heap storage.
    std::optional<uint32_t> visibleTextOffset;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  bool buildTickHeapGate();
  void prepareChapterBuild();
  bool needsPartialRebuild() const;
  bool updateChapterBuild();
  bool stylesDisabledForSession_ = false;
  int renderedSpineIndex_ = -1;
  int failedBuildSpine_ = -1;
  ReaderRenderSpec effectiveRenderSpec(uint16_t width, uint16_t height) const;
  bool handleBuildFailure(const char* stage, Section::BuildError error);
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr size_t RENDER_MIN_MAX_ALLOC = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  bool jumpToFraction(float fraction);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void openReaderMenu();
  // Back from a screen opened by a touch long-press returns to the page, not the menu.
  void reopenMenuAfterCancel();
  void runTouchLongPress(uint8_t action);
  bool openedFromShortcut = false;
  void finishFontPreview();
  void handleFontPreloadPrompt();
  void render(RenderLock&& lock) override;
  void openDictionaryWordSelect();
  bool launchKOReaderSync();
#ifdef ENABLE_CHINESE_VERSION
  bool launchWeReadSync();
#endif
  unsigned long confirmLongPressThreshold() const;
  void toggleAutoPageTurn(uint8_t requestedPageTurnRate);
  // Stop auto turn here and in the control center (util/AutoPageTurn.h).
  void stopAutoPageTurn();
  // Follow a start/stop or speed change made in the control center.
  void syncAutoPageTurn();
  // Rolling timer step; returns false when this page must turn as a whole.
  bool handleRollingAutoTurn();
  // Draws one rolling frame; false (and rolling off for this page) when the
  // next page is in another chapter or either page has images.
  bool renderRollingFrame(int marginTop, int marginRight, int marginBottom, int marginLeft);
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  bool loadBook() override;
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return epub ? epub->getThumbBmpPath() : ""; }
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~EpubReaderActivity() override;

  void onExit() override;
  void loop() override;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;
  bool preventAutoSleep() override { return automaticPageTurnActive; }
  bool deferBluetoothStart() const override;

  CrossPointPosition getCurrentPosition() const;
};
