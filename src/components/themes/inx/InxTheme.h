#pragma once

#include "InxItemLayout.h"
#include "components/themes/lyra/LyraTheme.h"

namespace InxMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics metrics = LyraMetrics::values;
  metrics.topPadding = 0;
  metrics.batteryBarHeight = 24;
  // UT37: 22 px status strip + 22 px tab icon ink + 6 px gap + 3 px rule.
  metrics.headerHeight = InxCompactLayout::enabled ? 53 : 66;
  metrics.verticalSpacing = 0;
  metrics.contentSidePadding = InxCompactLayout::sidePadding;
  metrics.listRowHeight = InxCompactLayout::rowHeight;
  metrics.listWithSubtitleRowHeight = InxCompactLayout::rowHeight;
  metrics.listRowGap = 0;
  metrics.listRowRadius = 0;
  metrics.listInset = 0;
  metrics.listSidePadding = InxCompactLayout::sidePadding;
  // FreeInkUI lists: 0 = InvertFill, 1 = LightPill (see InxSelection).
  metrics.listSelectionStyle = InxSelection::inverts ? 0 : 1;
  metrics.listScrollWidth = 6;
  metrics.listScrollSide = 0;
  metrics.listTitleBold = false;
  metrics.listSeparatorStyle = 2;
  metrics.listValueMaxWidth = InxCompactLayout::enabled ? 100 : 200;
  metrics.listSelectionCoversScrollReservation = true;
  metrics.menuRowHeight = InxCompactLayout::rowHeight;
  metrics.menuSpacing = 0;
  metrics.scrollBarWidth = 6;
  metrics.scrollBarRightOffset = 2;
  if (InxCompactLayout::enabled) {
    // Feather-style battery hugging "100" in the status-bar font: 23x13 body
    // (1 px frame, 1 px clear around the 19x9 ink), 1 px gap, 2 px nub
    // (drawBatteryWithPercentInside).
    metrics.batteryWidth = 26;
    metrics.batteryHeight = 13;
    // Reader footer: 6 px gap + 13 px text ink + 3 px gap, less the 3 px
    // viewable inset (UITheme::kReaderFooterTextFromBottom places the text).
    metrics.statusBarVerticalMargin = 19;
  }
  return metrics;
}
inline constexpr ThemeMetrics values = makeValues();
}  // namespace InxMetrics

class InxTheme final : public LyraTheme {
 public:
  void drawStatusStrip(const GfxRenderer& renderer, Rect rect) const override;
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  bool tabIndexFromPoint(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, int x, int y,
                         int& index) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  int getListRowStep(bool hasSubtitle) const override;
  int getListPageItems(int contentHeight, bool hasSubtitle) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr,
                bool showSelection = true, const std::function<bool(int index)>& rowHeading = nullptr) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon, int rowSpacing = -1) const override;
  void drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                       int selectedIndex) const override;
  void drawMainTabBar(const GfxRenderer& renderer, Rect rect, MainTab selected) const override;
};
