#include "SleepClock.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "activities/apps/standby/DeskClockFace.h"

namespace sleep_clock {

namespace {
// Static rather than heap: the face is a few dozen bytes, and it must outlive
// SleepActivity, which is replaced while the clock sleep loop keeps drawing.
DeskClockFace face;
}  // namespace

void begin() {
  face.onEnter();
  // Prime the minute anchor so the first loop tick only fires on a new minute.
  (void)face.tick();
}

bool tick(bool& cleanGhosting) {
  const auto result = face.tick();
  cleanGhosting = result == StandbyFace::TickResult::RedrawWithGhostCleanup;
  return result != StandbyFace::TickResult::None;
}

void draw(GfxRenderer& renderer, const bool cleanGhosting) {
  const auto savedOrientation = renderer.getOrientation();
  // The desk-clock face is landscape-only (same orientation Standby used).
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);

  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const Rect viewport{left, top, renderer.getScreenWidth() - left - right,
                      renderer.getScreenHeight() - top - bottom};

  renderer.clearScreen();
  {
    const GfxRenderer::ClipScope clip(renderer, viewport.x, viewport.y, viewport.width, viewport.height);
    face.render(renderer, viewport);
  }
  renderer.displayBuffer(cleanGhosting ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  renderer.setOrientation(savedOrientation);
}

uint32_t secondsUntilNextMinute() {
  const uint32_t seconds = face.secondsUntilNextWake();
  return seconds == 0 ? 1 : seconds;
}

}  // namespace sleep_clock
