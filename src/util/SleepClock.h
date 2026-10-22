#pragma once

#include <cstdint>

class GfxRenderer;

// The "Clock" sleep screen: the Standby desk-clock face drawn full-screen in
// landscape. SleepActivity paints the first frame; main.cpp's clock sleep loop
// repaints it once a minute between light sleeps.
namespace sleep_clock {

// Reset the face (minute anchor, temperature/humidity reading).
void begin();

// True when the minute changed since the last draw. cleanGhosting asks for the
// half waveform (every ten minutes) instead of the fast one.
bool tick(bool& cleanGhosting);

// Paint the face and refresh the panel. Restores the renderer orientation.
void draw(GfxRenderer& renderer, bool cleanGhosting);

uint32_t secondsUntilNextMinute();

}  // namespace sleep_clock
