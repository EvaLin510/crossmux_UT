#pragma once

#include <cstdint>

#include "StandbyFace.h"

// Desk-clock Standby face modelled on the UT37 maker's firmware: a large HH:MM,
// a divider, temperature and humidity beside it, and the date and weekday
// along the bottom. Landscape only.
class DeskClockFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  TickResult tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override { return StrId::STR_FACE_DESK_CLOCK; }
  uint32_t secondsUntilNextWake() const override;

 private:
  // The big-digit font is registered while the face is shown and dropped on
  // exit, so it costs no font-map slot elsewhere.
  GfxRenderer* fontOwner_ = nullptr;
  uint32_t startMs_ = 0;  // millis() anchor for the pre-sync fallback display
  int32_t lastMin_ = -1;
  // Minute redraws since the last ghost-clearing one; each counts as one page
  // against the user's refresh-frequency setting.
  int updatesSinceClean_ = 0;
  bool haveReading_ = false;
  float celsius_ = 0.0f;
  float humidity_ = 0.0f;

  void ensureFont(GfxRenderer& renderer);
};
