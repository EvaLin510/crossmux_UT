#pragma once

#include <cstdint>

// Fixed frontlight dimming steps. UT37 offers ten steps between 4% and 20% of
// the FrontlightManager perceptual percent scale; other boards keep the free
// 1-100% range. Values are flash constants (constexpr), no RAM cost.
namespace frontlight_levels {

#if FREEINK_DEVICE_UT37
inline constexpr bool kStepped = true;
#else
inline constexpr bool kStepped = false;
#endif

inline constexpr uint8_t kPercents[] = {4, 6, 8, 9, 11, 13, 15, 16, 18, 20};
inline constexpr int kCount = static_cast<int>(sizeof(kPercents) / sizeof(kPercents[0]));
// First-ever brightness (fresh settings): the dimmest step.
inline constexpr uint8_t kDefaultPercent = kPercents[0];

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
static_assert(indexForPercent(10) == 3 && indexForPercent(17) == 7, "ties resolve to the dimmer step");

}  // namespace frontlight_levels
