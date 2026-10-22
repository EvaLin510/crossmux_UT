#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const;
  const BaseTheme& getTheme() const { return *currentTheme; }
  bool usesClassicTabs() const { return currentType == CrossPointSettings::UI_THEME::CLASSIC; }
  bool hasMainTabs() const { return currentType == CrossPointSettings::UI_THEME::INX; }
  bool showSelectionCursor() const;
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  // Wraps only overflowing text, then aligns the complete line block within bounds.
  static void drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black = true,
                                      EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment verticalAlignment = TextVerticalAlignment::CENTER);
  void reload();
  void setTheme(CrossPointSettings::UI_THEME type);
  static int getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight = 0);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  // Reader top strip (UT37: clock left, battery right); 0 on other boards.
  // UT37 draws the strip from the screen's top edge, so the reserve is what is
  // left of it below the oriented viewable margin.
  static int getStatusBarTopHeight(int orientedMarginTop = 0);
  static constexpr int kReaderTopStripHeight = 22;
  // UT37 reader footer: line-box top of the status text, measured up from the
  // panel's bottom edge. The small face inks rows 2..14 of its line, so the ink
  // ends 3 px above the edge (outer gap); Inx reserves 6 px above it (inner gap).
  static constexpr int kReaderFooterTextFromBottom = 18;
  static int getProgressBarHeight();

 private:
  BaseTheme fallbackTheme;
  const ThemeMetrics* currentMetrics = &BaseMetrics::values;
  std::unique_ptr<BaseTheme> ownedTheme;
  BaseTheme* currentTheme = &fallbackTheme;
  CrossPointSettings::UI_THEME currentType = CrossPointSettings::UI_THEME::CLASSIC;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable bool metricsForButtonHints = false;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
