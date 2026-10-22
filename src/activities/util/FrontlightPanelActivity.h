#pragma once

#include <Icon.h>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Top-anchored control center opened by a top-edge down-swipe, a status-bar tap,
// or a button bound to "Control Center" (iOS Control Center style): a grabber,
// the frontlight brightness/warmth sliders (on boards with a light), and a grid
// of quick-setting tiles — night mode, Bluetooth, reading orientation, and a
// shortcut to system Settings — and, while a book is open,
// an auto page turn row at the bottom ([-] speed [+] [book]). The
// frontlight controls are always there: they are what the panel is for. Pure
// 1-bit: no dithered fills, selection reads as a filled tile. The grabber sits
// along the panel's bottom edge, the edge the sheet is dragged from.
class FrontlightPanelActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;

  uint8_t brightness = 60;
  uint8_t warmth = 50;
  bool lightOn = false;
  // lightOn is seeded from the live hardware state (Frontlight.isOn()), which
  // legitimately diverges from the saved SETTINGS.frontlightOn preference —
  // e.g. after a wake with frontlightRestoreOnWake off, the light stays off
  // live while the saved "was on" preference is deliberately kept (see
  // main.cpp's restoreLightOn). brightness/warmth have no such divergence
  // (always restored unconditionally on boot), so only lightOn needs a
  // touched-by-the-user flag: onExit() must not persist a mirror that never
  // reflected user intent in the first place.
  bool lightOnChanged = false;
  bool draggingSlider = false;
  int panelBottom = 0;
  int panelBottomBand = 0;  // height of the grabber band at the sheet's foot
  // The auto page turn row exists only while a reader is under the panel.
  bool showAutoTurn = false;
  // SETTINGS.autoPageTurnRate when the panel opened; onExit saves a change.
  uint8_t autoTurnRateAtEnter = 0;

  // Quick-setting tiles, in grid order (2 columns): night mode, Bluetooth,
  // orientation, Settings. Fixed set — shown on touch boards, absent elsewhere.
  static constexpr int kTileCount = 4;

  // fui::SliderRowProps and fui::TileGridProps embed a 324-byte fui::StyleSet,
  // so the props the render path fills in live here instead of on the stack
  // (AGENTS.md: locals stay under 256 bytes). The components take them by
  // const reference and draw immediately, so one instance per call site is
  // enough — every field either is reassigned on each use or keeps its
  // constructed default.
  freeink::ui::SliderRowProps rowProps;
  freeink::ui::ButtonProps stepProps;
  freeink::ui::TileGridProps gridProps;
  freeink::ui::TileGridItem gridItems[kTileCount];

  static void panelScreen(UiScreen& screen, void* user);
  static void onBrightnessEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onToggleEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onBrightnessStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onTileEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onAutoTurnStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onAutoTurnToggleEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildPanelScreen(UiScreen& screen);
  // One slider row: a caption line (name + live percentage) above
  // [-] [draggable 1-bit capsule] [+], plus a lamp on/off button after the +
  // when showToggle is set (the brightness row).
  void addSliderRow(UiScreen& screen, const char* label, const char* readout, uint8_t value,
                    freeink::ui::ActionId sliderAction, freeink::ui::ActionId stepAction, bool showToggle);
  // Compact row: [-] readout [+] [toggle], no caption. UT37 uses it for the
  // light rows; every board uses it for the auto page turn row. toggleIcon is
  // ignored when toggleAction is NO_ACTION.
  void addCompactStepRow(UiScreen& screen, const char* readout, freeink::ui::ActionId stepAction,
                         freeink::ui::ActionId toggleAction, const freeink::Icon& toggleIcon);
  // One layout source for measuring (render) and building (buildPanelScreen),
  // so the sheet height and the drawn rows never disagree.
  struct PanelLayout {
    bool twoColumn = false;    // landscape: light/auto rows left, tiles right
    int16_t headerBand = 0;    // battery band (portrait only) incl. its air
    int16_t lightRowHeight = 0;
    int16_t lightBlock = 0;    // all light rows + the trailing spaceSm
    int16_t autoBlock = 0;     // auto page turn row (0 when hidden)
    int16_t tileHeight = 0;
    int16_t tileGap = 0;
    int16_t tileBlock = 0;     // tile grid + its take gap (0 without touch)
    int16_t tileColumnWidth = 0;
    int16_t bottomBand = 0;    // grabber band; a tap here also closes
    int16_t height = 0;        // whole sheet
  };
  PanelLayout computeLayout() const;
  int computePanelBottom() const { return computeLayout().height; }
  void adjustBrightness(int delta);
  void adjustWarmth(int delta);
  void adjustAutoTurnRate(int delta);
  void toggleLight();
  void runTile(int idx);
  // Copy the panel's live brightness/warmth/lightOn into SETTINGS and save if
  // anything actually changed. onExit() runs it on every way out.
  void persistLightSettings();
  void close();

  // One-shot: a tile that rewrote the whole frame (night mode) re-drives it
  // with the ghost-cleanup waveform on the next render. The "refresh" tile does
  // not use this — it closes the panel and promotes the repaint underneath
  // instead (GfxRenderer::promoteNextRefresh).
  bool cleanRefreshPending = false;

 public:
  explicit FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
};
