#pragma once

#include <algorithm>

// Clamp a progress or score value into 0..100. Shared by the reading-stats
// store, the reading profile and the readers.
constexpr int clampPercent(const int percent) { return std::clamp(percent, 0, 100); }
