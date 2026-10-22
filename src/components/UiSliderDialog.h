#pragma once
#include <algorithm>
#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/UiAppHost.h"
#include "components/icons/stepIcons.h"

// Shared slider-dialog screen: a centered value readout above a drag slider
// with -/+ fine-step tap zones at the row ends, a Cancel/OK pair on touch
// boards, and two step-hint lines on button boards. Used by the interval and
// percent selection dialogs, which differ only in how they format the readout
// and hints and what the actions do.
struct UiSliderDialogSpec {
  const char* readout = nullptr;  // preformatted current-value text
  int value = 0;                  // slider position (0-based within max)
  int max = 1;                    // slider range
  freeink::ui::ActionId sliderAction = 0;
  freeink::ui::ActionId stepAction = 0;  // dispatched with value -1 / +1
  freeink::ui::ActionId cancelAction = 0;
  freeink::ui::ActionId okAction = 0;
  // Step hints for button boards (small step, large step); skipped on touch.
  const char* hintLine1 = nullptr;
  const char* hintLine2 = nullptr;
};

inline void buildSliderDialogScreen(UiAppHost::UiScreen& screen, const GfxRenderer& renderer,
                                    const MappedInputManager& mappedInput, const UiSliderDialogSpec& spec) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the title band render() paints, dropped down
  // to where the legacy fixed layout placed the readout.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 4),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Value readout, centered above the slider.
  fui::TextStyle readout = theme.titleText;
  readout.align = fui::TextAlign::Center;
  const int16_t readoutLh = screen.target().lineHeight(readout.font);
  screen.target().text(screen.takeTop(readoutLh, theme.spaceLg), spec.readout, readout);

#if FREEINK_DEVICE_UT37
  constexpr bool kButtonSteps = true;
#else
  constexpr bool kButtonSteps = false;
#endif
  // Slider row: -/+ tap zones at the row ends for fine steps (a full-row-height
  // square each, comfortable touch targets), with the drag slider between them.
  // A small gap keeps the slider's (min-touch-expanded) hit rect from overlapping.
  // UT37's 240 px row uses 4 px side margins and a 36 px row (30 px visible
  // step buttons), so the slider is about twice as long as with the default
  // 2 x spaceLg margins.
  const int16_t sideMargin = kButtonSteps ? 4 : static_cast<int16_t>(theme.spaceLg * 2);
  const fui::Insets sideInset{0, sideMargin, 0, sideMargin};
  const int16_t rowHeight = kButtonSteps ? 36 : theme.rowHeight;
  const fui::Rect row = screen.takeTop(rowHeight, theme.spaceLg).inset(sideInset);
  const int16_t stepW = kButtonSteps ? std::min<int16_t>(row.height, 40) : row.height;
  const fui::Rect minusHit{row.x, row.y, stepW, row.height};
  const fui::Rect plusHit{static_cast<int16_t>(row.right() - stepW), row.y, stepW, row.height};
  if constexpr (kButtonSteps) {
    // UT37: rounded-square buttons, 3 px inside each tap square, with
    // body-text-size glyphs (11 px, 1 px strokes) instead of a text "-".
    // minTouchSize grows the 30 px box to 44 px, ending 4 px past the 36 px
    // tap square; the slider rect starts 6 px past it (sideGap below), so the
    // hit regions do not overlap.
    // ButtonProps embeds a 324-byte StyleSet, so it is static, not a local.
    static fui::ButtonProps stepButton;
    constexpr int16_t kInset = 3;
    stepButton.label = nullptr;
    stepButton.styles = fui::sliderRowStepStyles(fui::resolveRadius(theme.controlRadius, 6));
    // 1 px frame (UT37 review: one pixel lighter than the SDK's 2 px).
    stepButton.styles.normal.borderWidth = 1;
    stepButton.styles.selected.borderWidth = 1;
    stepButton.styles.focused.borderWidth = 1;
    stepButton.styles.active.borderWidth = 1;
    stepButton.styles.disabled.borderWidth = 1;
    stepButton.inputMask = fui::InputTouch;
    stepButton.action = spec.stepAction;
    const auto box = [](const fui::Rect& r) {
      return fui::Rect{static_cast<int16_t>(r.x + kInset), static_cast<int16_t>(r.y + kInset),
                       static_cast<int16_t>(r.width - 2 * kInset), static_cast<int16_t>(r.height - 2 * kInset)};
    };
    stepButton.icon = fui::bitmapFromIcon(icon_step_minus_11);
    stepButton.value = -1;
    screen.button(stepButton, box(minusHit));
    stepButton.icon = fui::bitmapFromIcon(icon_step_plus_11);
    stepButton.value = +1;
    screen.button(stepButton, box(plusHit));
  } else {
    fui::TextStyle glyph = theme.bodyText;
    glyph.align = fui::TextAlign::Center;
    const int16_t glyphLh = screen.target().lineHeight(glyph.font);
    const int16_t glyphY = static_cast<int16_t>(row.y + (row.height - glyphLh) / 2);
    screen.target().text(fui::Rect{minusHit.x, glyphY, stepW, glyphLh}, "-", glyph);
    screen.target().text(fui::Rect{plusHit.x, glyphY, stepW, glyphLh}, "+", glyph);
    screen.frame().hit(minusHit, spec.stepAction, -1, fui::InputTouch);
    screen.frame().hit(plusHit, spec.stepAction, +1, fui::InputTouch);
  }

  fui::SliderProps props;
  props.value = spec.value;
  props.max = spec.max;
  props.action = spec.sliderAction;
  props.inputMask = fui::InputTouch | fui::InputDrag;
  if constexpr (kButtonSteps) {
    // UT37: half-width knob (7 px instead of 14) with rounded corners.
    props.knobWidth = 7;
    props.radius = 2;
  }
  const int16_t sideGap = static_cast<int16_t>(stepW + (kButtonSteps ? 6 : theme.spaceSm));
  fui::slider(screen.frame(), row.inset(fui::Insets{0, sideGap, 0, sideGap}), props);

  if (mappedInput.hasTouch()) {
    // Touch devices drive the slider directly and confirm/cancel on screen; the
    // physical-button step hints are hidden there — same rule as GUI.drawButtonHints.
    addDialogCancelOk(screen, spec.cancelAction, spec.okAction);
    return;
  }

  // Two-line step hint (front buttons = fine step, side buttons = coarse step),
  // preformatted by the caller so the layout doesn't depend on a separator
  // hidden in translated text.
  fui::TextStyle hint = theme.smallText;
  hint.align = fui::TextAlign::Center;
  const int16_t hintLh = screen.target().lineHeight(hint.font);
  if (spec.hintLine1) screen.target().text(screen.takeTop(hintLh, theme.spaceSm), spec.hintLine1, hint);
  if (spec.hintLine2) screen.target().text(screen.takeTop(hintLh), spec.hintLine2, hint);
}
