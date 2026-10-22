#include "DeskClockFace.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <builtinFonts/standby_clock_44.h>
#include <time.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "StandbyTime.h"
#include "fontIds.h"
#include "util/EnvSensor.h"
#include "util/TimeUtils.h"

namespace {

// Own id, away from the hashed built-in ids (fontIds.h).
constexpr int kClockFontId = 0x434C4B2C;  // "CLK,"
const EpdFont kClockFont(&standby_clock_44);
const EpdFontFamily kClockFontFamily(&kClockFont);

constexpr StrId kWeekdays[7] = {StrId::STR_WEEKDAY_0, StrId::STR_WEEKDAY_1, StrId::STR_WEEKDAY_2,
                                StrId::STR_WEEKDAY_3, StrId::STR_WEEKDAY_4, StrId::STR_WEEKDAY_5,
                                StrId::STR_WEEKDAY_6};

// 24 px thermometer: stem with a bulb, plus three scale ticks on the right.
void drawThermometer(const GfxRenderer& r, const int x, const int y) {
  r.drawRoundedRect(x + 3, y, 7, 17, 2, 3, true);
  r.drawArc(6, x + 6, y + 18, -1, 1, 2, true);
  r.drawArc(6, x + 6, y + 18, 1, 1, 2, true);
  r.drawArc(6, x + 6, y + 18, -1, -1, 2, true);
  r.drawArc(6, x + 6, y + 18, 1, -1, 2, true);
  r.fillRect(x + 5, y + 8, 3, 10, true);
  for (int i = 0; i < 3; ++i) r.fillRect(x + 14, y + 3 + i * 4, 7, 2, true);
}

// 24 px water drop outline: two sides from the tip into a round base.
void drawDroplet(const GfxRenderer& r, const int x, const int y) {
  const int cx = x + 12;
  constexpr int kRadius = 8;
  const int baseY = y + 15;
  r.drawLine(cx, y, cx - kRadius, baseY, 2, true);
  r.drawLine(cx, y, cx + kRadius, baseY, 2, true);
  r.drawArc(kRadius, cx, baseY, -1, 1, 2, true);
  r.drawArc(kRadius, cx, baseY, 1, 1, 2, true);
}

// Ink extent of a clock string relative to its pen x. The bounds clamp minX
// to the pen origin (so width == maxX), and the leading edge comes from the
// first glyph.
void inkSpan(const char* text, int& inkLeft, int& inkRight) {
  int w = 0, h = 0;
  kClockFont.getTextDimensions(text, &w, &h);
  const EpdGlyph* first = kClockFont.getGlyph(static_cast<uint8_t>(text[0]));
  inkLeft = first ? first->left : 0;
  inkRight = w;
}

// Draws HH:MM with the same visible gap on both sides of the colon. With
// proportional digits the colon's advance alone leaves it 1-3 px off-centre
// depending on the neighbouring side bearings. Returns the ink width.
constexpr int kColonGap = 10;
int clockInkWidth(const char* hh, const char* mm) {
  int hl, hr, cl, cr, ml, mr;
  inkSpan(hh, hl, hr);
  inkSpan(":", cl, cr);
  inkSpan(mm, ml, mr);
  return (hr - hl) + kColonGap + (cr - cl) + kColonGap + (mr - ml);
}
void drawClock(const GfxRenderer& r, const int inkX, const int y, const char* hh, const char* mm) {
  int hl, hr, cl, cr, ml, mr;
  inkSpan(hh, hl, hr);
  inkSpan(":", cl, cr);
  inkSpan(mm, ml, mr);
  int x = inkX;
  r.drawText(kClockFontId, x - hl, y, hh);
  x += (hr - hl) + kColonGap;
  r.drawText(kClockFontId, x - cl, y, ":");
  x += (cr - cl) + kColonGap;
  r.drawText(kClockFontId, x - ml, y, mm);
}

}  // namespace

void DeskClockFace::onEnter() {
  startMs_ = millis();
  lastMin_ = -1;
  updatesSinceClean_ = 0;
  haveReading_ = env_sensor::read(celsius_, humidity_);
}

void DeskClockFace::onExit() {
  if (fontOwner_) fontOwner_->removeFont(kClockFontId);
  fontOwner_ = nullptr;
}

void DeskClockFace::ensureFont(GfxRenderer& renderer) {
  if (fontOwner_ == &renderer) return;
  renderer.insertFont(kClockFontId, kClockFontFamily);
  fontOwner_ = &renderer;
}

StandbyFace::TickResult DeskClockFace::tick() {
  const uint32_t nowMin = standby_time::getMinuteTick(startMs_);
  if (static_cast<int32_t>(nowMin) == lastMin_) return TickResult::None;
  lastMin_ = static_cast<int32_t>(nowMin);
  // Temperature and humidity move slowly; once a minute is plenty.
  haveReading_ = env_sensor::read(celsius_, humidity_);

  // Ghost cleanup follows the reader's refresh-frequency setting, one minute
  // redraw per page; "Never" (INT_MAX) never reaches the threshold.
  if (++updatesSinceClean_ < SETTINGS.getRefreshFrequency()) return TickResult::Redraw;
  updatesSinceClean_ = 0;
  return TickResult::RedrawWithGhostCleanup;
}

void DeskClockFace::render(GfxRenderer& renderer, const Rect& viewport) {
  ensureFont(renderer);

  unsigned hh = 0;
  unsigned mm = 0;
  standby_time::getNowHHMM(startMs_, hh, mm);
  char hhText[4];
  char mmText[4];
  std::snprintf(hhText, sizeof(hhText), "%02u", hh % 100u);
  std::snprintf(mmText, sizeof(mmText), "%02u", mm % 100u);

  char tempText[16];
  char humText[16];
  if (haveReading_) {
    std::snprintf(tempText, sizeof(tempText), "%.2f C", static_cast<double>(celsius_));
    std::snprintf(humText, sizeof(humText), "%.2f %%", static_cast<double>(humidity_));
  } else {
    std::snprintf(tempText, sizeof(tempText), "--.-- C");
    std::snprintf(humText, sizeof(humText), "--.-- %%");
  }

  // Date and weekday need a real clock; before the first sync they stay blank.
  char dateText[48] = "";
  std::tm local{};
  if (TimeUtils::isClockValid() && TimeUtils::getLocalDateTime(static_cast<uint32_t>(time(nullptr)), local)) {
    std::snprintf(dateText, sizeof(dateText), "%d-%d-%d %s", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  I18N.get(kWeekdays[local.tm_wday % 7]));
  }

  const int timeW = clockInkWidth(hhText, mmText);
  const int timeH = renderer.getTextHeight(kClockFontId);
  const int textH = renderer.getLineHeight(UI_12_FONT_ID);
  constexpr int kIcon = 24;
  constexpr int kIconGap = 8;

  // Landscape only (StandbyActivity forces it):
  // [ HH:MM | thermometer temp / drop humidity ], date underneath.
  const int split = viewport.x + viewport.width * 64 / 100;
  const int bandTop = viewport.y + viewport.height * 18 / 100;
  const int bandBottom = viewport.y + viewport.height * 70 / 100;
  const int timeX = viewport.x + std::max(0, (split - viewport.x - timeW) / 2);
  const int timeY = bandTop + std::max(0, (bandBottom - bandTop - timeH) / 2);
  drawClock(renderer, timeX, timeY, hhText, mmText);
  renderer.drawLine(split, bandTop, split, bandBottom, 2, true);

  const int rightX = split + 14;
  const int rowGap = (bandBottom - bandTop) / 2;
  const int row1 = bandTop + (rowGap - kIcon) / 2;
  const int row2 = row1 + rowGap;
  drawThermometer(renderer, rightX, row1);
  renderer.drawText(UI_12_FONT_ID, rightX + kIcon + kIconGap, row1 + (kIcon - textH) / 2, tempText);
  drawDroplet(renderer, rightX, row2);
  renderer.drawText(UI_12_FONT_ID, rightX + kIcon + kIconGap, row2 + (kIcon - textH) / 2, humText);

  const int footY = bandBottom + (viewport.y + viewport.height - bandBottom - textH) / 2;
  if (dateText[0]) {
    const int dateW = renderer.getTextWidth(UI_12_FONT_ID, dateText);
    renderer.drawText(UI_12_FONT_ID, viewport.x + std::max(0, (split - viewport.x - dateW) / 2), footY, dateText);
  }
}

uint32_t DeskClockFace::secondsUntilNextWake() const {
  uint32_t sec;
  if (standby_time::isSynced()) {
    sec = static_cast<uint32_t>(time(nullptr) % 60);
  } else {
    sec = static_cast<uint32_t>(((millis() - startMs_) / 1000u) % 60u);
  }
  const uint32_t remaining = (sec == 0) ? 60u : (60u - sec);
  return remaining < 1u ? 1u : remaining;
}
