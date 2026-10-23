#pragma once

#include <cstdint>

// Fixed frontlight dimming steps. UT37 offers level 0 (dark) plus ten lit steps
// between 4% and 20% of the FrontlightManager perceptual percent scale; other
// boards keep the free 1-100% range. Values are flash constants (constexpr), no
// RAM cost.
namespace frontlight_levels {

#if FREEINK_DEVICE_UT37
inline constexpr bool kStepped = true;
#else
inline constexpr bool kStepped = false;
#endif

// Index = the level shown to the user (0..10). 2% and below never lit the LED
// (10-bit duty <= 2), so level 0 is an explicit 0% rather than a dead dim step.
inline constexpr uint8_t kPercents[] = {0, 4, 6, 8, 9, 11, 13, 15, 16, 18, 20};
inline constexpr int kCount = static_cast<int>(sizeof(kPercents) / sizeof(kPercents[0]));
inline constexpr int kMaxLevel = kCount - 1;
// First-ever brightness (fresh settings): the dimmest lit step.
inline constexpr uint8_t kDefaultPercent = kPercents[1];

// Index of the step closest to `percent` (ties go to the dimmer step).
constexpr int indexForPercent(const uint8_t percent) {
  int best = 0;
  for (int i = 1; i < kCount; ++i) {
    const int bestDiff = percent > kPercents[best] ? percent - kPercents[best] : kPercents[best] - percent;
    const int diff = percent > kPercents[i] ? percent - kPercents[i] : kPercents[i] - percent;
    if (diff < bestDiff) best = i;
  }
  return best;
}

constexpr uint8_t percentForIndex(const int index) {
  return kPercents[index < 0 ? 0 : (index >= kCount ? kCount - 1 : index)];
}

// The brightness a board actually uses for a requested percent: snapped to a
// step on stepped boards, otherwise capped at 100.
constexpr uint8_t normalize(const uint8_t percent) {
  if constexpr (kStepped) return percentForIndex(indexForPercent(percent));
  return percent > 100 ? 100 : percent;
}

static_assert(indexForPercent(0) == 0 && indexForPercent(100) == kCount - 1, "out-of-range percents clamp to the ends");
static_assert(indexForPercent(10) == 4 && indexForPercent(17) == 8, "ties resolve to the dimmer step");
static_assert(indexForPercent(3) == 1, "a saved 3% (the previous floor) lands on level 1, not dark");

}  // namespace frontlight_levels
