#pragma once

#include <cstdint>

// Reader auto page turn shared between the readers and the control center.
// The speed (pages per minute) persists in SETTINGS.autoPageTurnRate; whether
// it is running is runtime-only and ends when the book closes. The control
// center flips it while a reader sits underneath, and the reader picks the
// change up on its next loop.
namespace auto_page_turn {

// Speeds offered by the control center's -/+ (pages per minute).
inline constexpr uint8_t kRates[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30};
inline constexpr int kRateCount = static_cast<int>(sizeof(kRates) / sizeof(kRates[0]));
inline constexpr uint8_t kMaxRate = 30;

// Rolling auto turn (UT37) reveals the next page this many lines per frame.
// Each frame is a full fast refresh (~440 ms of panel power whatever changed),
// so a 15-line page costs 3 roll frames + the turn instead of 14 + the turn.
inline constexpr uint8_t kRollLinesPerStep = 4;

// Lines of the next page to show `elapsed` ms into a `duration` ms interval:
// line k appears at k/n of it, released in groups of kRollLinesPerStep.
constexpr uint8_t rollLinesShown(const unsigned long elapsed, const unsigned long duration,
                                 const unsigned long lines) {
  return static_cast<uint8_t>(elapsed * lines / duration / kRollLinesPerStep * kRollLinesPerStep);
}

static_assert(rollLinesShown(0, 15000, 15) == 0 && rollLinesShown(3999, 15000, 15) == 0 &&
                  rollLinesShown(4000, 15000, 15) == 4 && rollLinesShown(14999, 15000, 15) == 12,
              "roll reveals four lines at a time");

inline bool& activeFlag() {
  static bool active = false;
  return active;
}

inline bool isActive() { return activeFlag(); }
inline void setActive(const bool active) { activeFlag() = active; }

// Index of the offered speed nearest to `rate` (ties go to the slower speed).
constexpr int indexForRate(const uint8_t rate) {
  int best = 0;
  for (int i = 1; i < kRateCount; ++i) {
    const int bestDiff = rate > kRates[best] ? rate - kRates[best] : kRates[best] - rate;
    const int diff = rate > kRates[i] ? rate - kRates[i] : kRates[i] - rate;
    if (diff < bestDiff) best = i;
  }
  return best;
}

// One step faster (+1) or slower (-1) along kRates, clamped to the ends.
constexpr uint8_t stepRate(const uint8_t rate, const int delta) {
  int index = indexForRate(rate) + delta;
  if (index < 0) index = 0;
  if (index >= kRateCount) index = kRateCount - 1;
  return kRates[index];
}

static_assert(stepRate(3, 1) == 4 && stepRate(30, 1) == 30 && stepRate(1, -1) == 1, "steps clamp at the ends");

}  // namespace auto_page_turn
