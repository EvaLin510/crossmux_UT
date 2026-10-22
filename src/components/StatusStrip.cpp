#include "StatusStrip.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace status_strip {

namespace {
constexpr int kSideInset = 8;
// Clock: 2 px between glyph inks, matching the face's tightest digit pair.
constexpr int kClockGlyphGap = 2;

// The face that actually draws SMALL_FONT_ID text (the replacing bitmap UI
// face on UT37).
const EpdFontFamily* smallFace(const GfxRenderer& renderer) {
  int fontId = renderer.getFallbackFontId(SMALL_FONT_ID);
  if (fontId == 0) fontId = SMALL_FONT_ID;
  const auto it = renderer.getFontMap().find(fontId);
  return it == renderer.getFontMap().end() ? nullptr : &it->second;
}
}  // namespace

int digitInkHeight(const GfxRenderer& renderer) {
  const EpdFontFamily* face = smallFace(renderer);
  const EpdGlyph* digit = face ? face->getGlyph('0', EpdFontFamily::REGULAR) : nullptr;
  return digit ? digit->height : 0;
}

int drawInkRun(const GfxRenderer& renderer, const int x, const int inkTop, const char* text, const int gap,
               const bool draw) {
  const EpdFontFamily* face = smallFace(renderer);
  if (!face || !text) return 0;
  const EpdGlyph* digit = face->getGlyph('0', EpdFontFamily::REGULAR);
  if (!digit) return 0;
  // Line-box top that puts the digit ink at inkTop; every glyph shares that
  // baseline, so ':' keeps its own vertical place.
  const int lineY = inkTop - renderer.getFontAscenderSize(SMALL_FONT_ID) + digit->top;
  int pen = x;
  for (const char* p = text; *p; ++p) {
    const EpdGlyph* glyph = face->getGlyph(static_cast<unsigned char>(*p), EpdFontFamily::REGULAR);
    if (!glyph) continue;
    if (pen != x) pen += gap;
    if (draw && glyph->width > 0) {
      const char one[2] = {*p, '\0'};
      renderer.drawText(SMALL_FONT_ID, pen - glyph->left, lineY, one, true);
    }
    pen += glyph->width;
  }
  return pen - x;
}

void draw(const GfxRenderer& renderer, const int top, const int left, const int right) {
  const int contentTop = top + kOuterGap;
  const auto sb = SETTINGS.statusBarSpec();
  if (FREEINK_DEVICE_UT37 || sb.showsClock()) {
    char timeBuf[9];
    if (!TimeUtils::formatCurrentTime(timeBuf, sizeof(timeBuf), sb.clock12h)) {
      std::snprintf(timeBuf, sizeof(timeBuf), "--:--");
    }
    const int inkTop = contentTop + (kContentHeight - digitInkHeight(renderer)) / 2;
    drawInkRun(renderer, left + kSideInset, inkTop, timeBuf, kClockGlyphGap);
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool showPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // drawBatteryRight() offsets the icon 6 px down from rect.y.
  const int batteryY = contentTop + (kContentHeight - metrics.batteryHeight) / 2 - 6;
  GUI.drawBatteryRight(
      renderer, Rect{right - kSideInset - metrics.batteryWidth, batteryY, metrics.batteryWidth, metrics.batteryHeight},
      showPercentage);
}

}  // namespace status_strip
