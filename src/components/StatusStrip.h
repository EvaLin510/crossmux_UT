#pragma once

class GfxRenderer;

// The one place that draws the clock and battery. Every screen that shows them
// (main tabs, sub-page headers, the reader, the control center, Standby) calls
// draw(), so both indicators sit at the same spot everywhere: the clock at the
// left and the battery (with percentage) at the right of an kHeight-tall strip.
namespace status_strip {

// Status bars keep 3 px toward the screen edge and 6 px toward the content.
// For this top strip: 3 px gap + 13 px battery + 6 px gap.
inline constexpr int kOuterGap = 3;
inline constexpr int kInnerGap = 6;
inline constexpr int kContentHeight = 13;
inline constexpr int kHeight = kOuterGap + kContentHeight + kInnerGap;

// Strip occupies [top, top + kHeight) between the x bounds [left, right).
// The clock follows Settings > status bar > Clock (always on for UT37); the
// battery is always shown.
void draw(const GfxRenderer& renderer, int top, int left, int right);

// Draws ASCII text in the status-bar face with each glyph's ink box placed
// `gap` px after the previous one, so digits and ':' are evenly spaced (the
// face's advances leave 2-3 px after digits and put ':' 2 px into its cell).
// inkTop is the top row of the digit ink. Returns the ink width; with
// draw=false only measures.
int drawInkRun(const GfxRenderer& renderer, int x, int inkTop, const char* text, int gap, bool draw = true);

// Ink height of the status-bar digits.
int digitInkHeight(const GfxRenderer& renderer);

}  // namespace status_strip
