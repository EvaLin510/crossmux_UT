#include "FrontlightPanelActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <FrontlightLevels.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "CrossPointSettings.h"
#include "BleInput.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "util/AutoPageTurn.h"
#include "components/icons/customListIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/panelIcons24.h"
#include "components/icons/stepIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_BRIGHTNESS = 1;
constexpr fui::ActionId ACTION_WARMTH = 2;
constexpr fui::ActionId ACTION_TOGGLE = 3;
constexpr fui::ActionId ACTION_BRIGHTNESS_STEP = 4;
constexpr fui::ActionId ACTION_WARMTH_STEP = 5;
constexpr fui::ActionId ACTION_TILE = 6;  // value = tile index
constexpr fui::ActionId ACTION_AUTO_TURN_STEP = 7;
constexpr fui::ActionId ACTION_AUTO_TURN_TOGGLE = 8;

// iOS-style geometry. The panel is a card hanging from the top of the screen:
// a grabber, full-width slider pills, then a 2-column tile grid. The chrome
// itself is fui::sheet / fui::sliderRow / fui::tileGrid; these constants only
// size the bands, and computePanelBottom() mirrors them.
constexpr int16_t kPanelSideMargin = 16;
constexpr int16_t kGrabberHeight = 5;     // fui::SheetProps default, mirrored here
constexpr int16_t kSliderRowHeight = 56;  // the pill itself (finger-sized)
constexpr int16_t kTileHeight = 84;
constexpr int16_t kTileGap = 16;
constexpr int kTileCols = 2;
// One percent per press, on the -/+ buttons and on the physical Left/Right keys
// alike (both repeat while held), so a level can be set exactly.
constexpr int BRIGHTNESS_STEP = 1;
// The dimmest setting is 1%, not 0: turning the light off is what the lamp
// button next to the slider is for, so a 0% "on" level would only be a second,
// worse way to reach the same place.
constexpr uint8_t MIN_BRIGHTNESS = 1;

#if FREEINK_DEVICE_UT37
// 240 px wide panel: no room for a capsule between finger-sized step buttons,
// so the rows are [-] level [+] [lamp] with no caption line.
constexpr bool kCompactRows = true;
#else
constexpr bool kCompactRows = false;
#endif
constexpr int16_t kCompactStepSize = 40;
// Compact rows and tiles draw every box 3 px inside its layout cell on all
// four sides; the cells (and so the panel height and spacing) are unchanged.
constexpr int16_t kCompactBoxInset = 3;
// UT37 stacked sheet, ink to ink: 22 px under the light row and above the
// grabber, 18 px between tile rows, 72 px tiles. The grabber sits 6 px above
// the 2 px bottom rule, and the sheet ends there so the page shows below it.
constexpr int16_t kCompactSectionGap = 22;
constexpr int16_t kCompactTileInkGap = 18;
constexpr int16_t kCompactTileInkHeight = 72;
constexpr int16_t kCompactGrabberToRule = 6;
constexpr int16_t kSheetRuleWidth = 2;  // fui::SheetProps default, mirrored here
// fui::Screen::tileGrid() leaves spaceSm under the grid.
constexpr int16_t kTileGridTrailingGap = 4;

// The brightness level as shown to the user, beside the caption or between the
// compact step buttons. Single formatting point for the brightness readout.
// Stepped boards (UT37) show the step number, e.g. "3/10".
void formatBrightnessReadout(char* out, const size_t size, const uint8_t value) {
  if constexpr (frontlight_levels::kStepped) {
    snprintf(out, size, "%d/%d", frontlight_levels::indexForPercent(value) + 1, frontlight_levels::kCount);
  } else {
    snprintf(out, size, "%u%%", static_cast<unsigned>(value));
  }
}

uint8_t percentFromPermille(const int16_t permille) {
  int value = (static_cast<int>(permille) * 100 + 500) / 1000;
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return static_cast<uint8_t>(value);
}
}  // namespace

FrontlightPanelActivity::FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FrontlightPanel", renderer, mappedInput), UiAppHost(renderer) {}

void FrontlightPanelActivity::onEnter() {
  Activity::onEnter();

  // A stored 0% predates the 1% floor (or came from the web settings): show it
  // as the floor rather than a level the slider can no longer produce. onExit
  // persists that, which is the intent — 0 is not a brightness any more.
  brightness = std::max(MIN_BRIGHTNESS, Frontlight.brightness());
  warmth = Frontlight.warmth();
  lightOn = Frontlight.isOn();
  lightOnChanged = false;
#if FREEINK_DEVICE_EEGO_A4
  // The first frame draws over the reader's gray AA page; a HALF refresh keeps
  // the overlay clean instead of ghosting. Other boards keep the standard
  // FAST_REFRESH first frame (X4 Pro reference behavior).
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
#endif

  // The panel is pushed over the activity it was opened from; a reader
  // anywhere in the stack means a book is open underneath.
  showAutoTurn = activityManager.isReaderActivity();
  autoTurnRateAtEnter = SETTINGS.autoPageTurnRate;

  resetUi();
  app.on(ACTION_BRIGHTNESS, &FrontlightPanelActivity::onBrightnessEvent, this);
  app.on(ACTION_WARMTH, &FrontlightPanelActivity::onWarmthEvent, this);
  app.on(ACTION_TOGGLE, &FrontlightPanelActivity::onToggleEvent, this);
  app.on(ACTION_BRIGHTNESS_STEP, &FrontlightPanelActivity::onBrightnessStepEvent, this);
  app.on(ACTION_WARMTH_STEP, &FrontlightPanelActivity::onWarmthStepEvent, this);
  app.on(ACTION_TILE, &FrontlightPanelActivity::onTileEvent, this);
  app.on(ACTION_AUTO_TURN_STEP, &FrontlightPanelActivity::onAutoTurnStepEvent, this);
  app.on(ACTION_AUTO_TURN_TOGGLE, &FrontlightPanelActivity::onAutoTurnToggleEvent, this);
  app.setScreen(&FrontlightPanelActivity::panelScreen, this);
  requestUpdate();
}

void FrontlightPanelActivity::persistLightSettings() {
  // brightness/warmth are always restored unconditionally on boot (see
  // main.cpp), so they never diverge from SETTINGS at onEnter() — comparing
  // against SETTINGS here only fires on a genuine user change. lightOn has
  // no such guarantee (see lightOnChanged's declaration), so it's gated on
  // the user actually having touched it this session instead.
  const bool changed = SETTINGS.frontlightBrightness != brightness || SETTINGS.frontlightWarmth != warmth ||
                       (lightOnChanged && SETTINGS.frontlightOn != (lightOn ? 1 : 0)) ||
                       SETTINGS.autoPageTurnRate != autoTurnRateAtEnter;
  if (changed) {
    SETTINGS.frontlightBrightness = brightness;
    SETTINGS.frontlightWarmth = warmth;
    if (lightOnChanged) SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
  }
}

void FrontlightPanelActivity::onExit() {
  persistLightSettings();
  Activity::onExit();
#if FREEINK_DEVICE_EEGO_A4
  // The overlay pops back to the reader's gray AA page; force a clean first
  // frame so the panel's white region does not ghost over the text.
  renderer.requestNextFullRefresh();
#endif
}

void FrontlightPanelActivity::onBrightnessEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  if (event.dragPermille < 0) return;
  self->brightness = frontlight_levels::normalize(std::max(MIN_BRIGHTNESS, percentFromPermille(event.dragPermille)));
  Frontlight.setBrightness(self->brightness);
  if (!self->lightOn) {
    self->lightOn = true;
    self->lightOnChanged = true;
    Frontlight.setOn(true);
  }
}

void FrontlightPanelActivity::onWarmthEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  if (event.dragPermille < 0) return;
  self->warmth = percentFromPermille(event.dragPermille);
  Frontlight.setWarmth(self->warmth);
}

void FrontlightPanelActivity::onToggleEvent(const fui::ActionEvent&, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->toggleLight();
}

void FrontlightPanelActivity::onBrightnessStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->adjustBrightness(event.value);
}

void FrontlightPanelActivity::onWarmthStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->adjustWarmth(event.value);
}

void FrontlightPanelActivity::onTileEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->runTile(event.value);
}

void FrontlightPanelActivity::onAutoTurnStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->adjustAutoTurnRate(event.value);
}

void FrontlightPanelActivity::onAutoTurnToggleEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FrontlightPanelActivity*>(user);
  // The reader under the panel picks the change up on its next loop(), which
  // only runs once the panel is gone: starting closes the sheet straight away.
  const bool start = !auto_page_turn::isActive();
  auto_page_turn::setActive(start);
  if (start) {
    self->close();
  } else {
    self->requestUpdate();
  }
}

void FrontlightPanelActivity::runTile(const int idx) {
  switch (idx) {
    case 0:  // Night mode (inverted output polarity, applied to the whole UI)
      SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
      SETTINGS.saveToFile();
      // Inversion rewrites every pixel; take the clean waveform so the panel
      // does not keep a ghost of the old polarity.
      cleanRefreshPending = true;
      requestUpdate();
      break;
    case 1: {  // Bluetooth on/off, the same switch as Settings > Bluetooth
      SETTINGS.bluetoothEnabled = SETTINGS.bluetoothEnabled ? 0 : 1;
      SETTINGS.saveToFile();
      if (SETTINGS.bluetoothEnabled) {
        bleinput::StartResult result;
        {
          RenderLock lock;
          result = bleinput::ensureStarted(renderer, bleinput::StartContext::Explicit);
        }
        if (result != bleinput::StartResult::Started && result != bleinput::StartResult::AlreadyRunning) {
          LOG_ERR("PANEL", "Bluetooth start failed (%u)", static_cast<unsigned>(result));
        }
      } else {
        bleinput::stop();
      }
      requestUpdate();
      break;
    }
    case 2:  // Cycle the reading orientation
#if FREEINK_DEVICE_UT37
      // UT37 offers portrait and the two landscapes only (no upside-down).
      SETTINGS.orientation = SETTINGS.orientation == CrossPointSettings::PORTRAIT       ? CrossPointSettings::LANDSCAPE_CW
                             : SETTINGS.orientation == CrossPointSettings::LANDSCAPE_CW ? CrossPointSettings::LANDSCAPE_CCW
                                                                                        : CrossPointSettings::PORTRAIT;
#else
      SETTINGS.orientation = static_cast<uint8_t>((SETTINGS.orientation + 1) % 4);
#endif
      SETTINGS.saveToFile();
      // Only the setting changes: turning the renderer cropped the portrait-only
      // screens the panel opens over. The reader reflows on its next loop().
      requestUpdate();
      break;
    case 3:  // System Settings. Replacing clears the stack, so an open book closes
      // through its normal onExit (progress is saved) before Settings opens.
      activityManager.goToSettings();
      break;
    default:
      break;
  }
}

void FrontlightPanelActivity::adjustBrightness(const int delta) {
  int next = static_cast<int>(brightness) + delta;
  if constexpr (frontlight_levels::kStepped) {
    // One press moves one step; delta is +/-BRIGHTNESS_STEP (1).
    next = frontlight_levels::percentForIndex(frontlight_levels::indexForPercent(brightness) + delta);
  }
  if (next < MIN_BRIGHTNESS) next = MIN_BRIGHTNESS;
  if (next > 100) next = 100;
  if (next == brightness) return;
  brightness = static_cast<uint8_t>(next);
  Frontlight.setBrightness(brightness);
  if (!lightOn) {
    lightOn = true;
    lightOnChanged = true;
    Frontlight.setOn(true);
  }
  requestUpdate();
}

void FrontlightPanelActivity::adjustAutoTurnRate(const int delta) {
  const uint8_t next = auto_page_turn::stepRate(SETTINGS.autoPageTurnRate, delta);
  if (next == SETTINGS.autoPageTurnRate) return;
  SETTINGS.autoPageTurnRate = next;  // saved in onExit; a running reader re-times on its next loop()
  requestUpdate();
}

void FrontlightPanelActivity::adjustWarmth(const int delta) {
  int next = static_cast<int>(warmth) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  if (next == warmth) return;
  warmth = static_cast<uint8_t>(next);
  Frontlight.setWarmth(warmth);
  requestUpdate();
}

void FrontlightPanelActivity::toggleLight() {
  lightOn = !lightOn;
  lightOnChanged = true;
  Frontlight.setOn(lightOn);
  requestUpdate();
}

void FrontlightPanelActivity::close() { finish(); }

bool FrontlightPanelActivity::handleHomeGesture() {
  close();
  return true;
}

void FrontlightPanelActivity::loop() {
  const auto touch = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) {
      if (touch.event.dragPermille >= 0) draggingSlider = true;
      return;
    }
    // Swipe up dismisses the sheet, the way it was pulled down from the top
    // edge. draggingSlider keeps a fast slider flick from closing it.
    if (!draggingSlider && mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Up) {
      close();
      return;
    }
    // panelBottom > 0 guards the frame the sheet opens in: the release that
    // opened it (a status-bar tap) is still in the input snapshot when the panel
    // runs its first loop(), and panelBottom is only known once render() has
    // measured the layout — so at 0 that release read as "tapped below the
    // sheet" and closed it again before it was ever drawn.
    // The doubled grabber band at the sheet's foot holds no controls, so a tap
    // there closes the sheet too (a bigger target than the strip below it,
    // which landscape barely has).
    if (touch.snap.touchReleased && !draggingSlider && panelBottom > 0 &&
        touch.snap.touchY >= panelBottom - panelBottomBand) {
      close();
      return;
    }
  }
  if (draggingSlider) {
    if (!touch.snap.touchHeld) draggingSlider = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleLight();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this] { adjustBrightness(-BRIGHTNESS_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this] { adjustBrightness(BRIGHTNESS_STEP); });
}

FrontlightPanelActivity::PanelLayout FrontlightPanelActivity::computeLayout() const {
  const auto tokens = uiThemeTokens(uiTarget);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int16_t lineHeight = uiTarget.lineHeight(tokens.smallText.font);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  PanelLayout l;
  // Landscape is too short for the stacked layout (240 px on UT37), so the
  // light/auto rows and the tiles sit side by side.
  l.twoColumn = screenW > screenH && mappedInput.hasTouch();
  // Slim battery band + the air around it; landscape drops it for height.
#if FREEINK_DEVICE_UT37
  // Clock/battery strip in both layouts (matches buildPanelScreen()).
  l.headerBand = static_cast<int16_t>(UITheme::kReaderTopStripHeight + tokens.spaceMd);
#else
  l.headerBand = l.twoColumn ? tokens.spaceMd
                             : static_cast<int16_t>(tokens.spaceMd + std::max<int>(metrics.batteryHeight, lineHeight) +
                                                    tokens.spaceMd);
#endif
  // Screen::sliderRow reserves caption + spaceMd + control band, then a
  // spaceMd gap; addSliderRow() adds one more spaceMd of air after each row.
  // Compact rows: the step band + its take gap + the trailing spacer.
  const int16_t compactRow = static_cast<int16_t>(kCompactStepSize + 2 * tokens.spaceMd);
  l.lightRowHeight = kCompactRows ? compactRow
                                  : static_cast<int16_t>(lineHeight + tokens.spaceMd + kSliderRowHeight +
                                                         2 * tokens.spaceMd);
  if (Frontlight.present()) {
    const int rows = Frontlight.hasColorTemperature() ? 2 : 1;
    l.lightBlock = static_cast<int16_t>(rows * l.lightRowHeight + tokens.spaceSm);
  }
  // The auto row is always compact; stacked, it gets a leading spacer.
  if (showAutoTurn) l.autoBlock = static_cast<int16_t>(compactRow + (l.twoColumn ? 0 : tokens.spaceMd));
  // Twice the original grabber band (content air + grabber + air to the edge).
  l.bottomBand = static_cast<int16_t>(2 * tokens.spaceLg + kGrabberHeight + 2 * (tokens.spaceLg + tokens.spaceMd));

  const bool hasTiles = mappedInput.hasTouch();
  const int tileRows = (kTileCount + kTileCols - 1) / kTileCols;
  constexpr int16_t kMinTileHeight = 40;
  if (l.twoColumn) {
    l.tileGap = tokens.spaceMd;
    const int leftHeight = l.lightBlock + l.autoBlock;
    // Tiles fill the left column's height; labels only need ~4 CJK cells.
    const int fit = (leftHeight - tokens.spaceSm - (tileRows - 1) * l.tileGap) / tileRows;
    l.tileHeight = static_cast<int16_t>(std::clamp<int>(fit, kMinTileHeight, kTileHeight));
    const int contentW = screenW - 2 * kPanelSideMargin;
    l.tileColumnWidth = static_cast<int16_t>(std::clamp<int>(contentW * 2 / 5, 120, contentW / 2));
    l.tileBlock = hasTiles ? static_cast<int16_t>(fui::tileGridHeight(kTileCount, kTileCols, l.tileHeight, l.tileGap) +
                                                  tokens.spaceSm)
                           : 0;
    l.height = static_cast<int16_t>(l.headerBand + std::max(leftHeight, static_cast<int>(l.tileBlock)) + l.bottomBand);
  } else {
    l.tileGap = kTileGap;
    // Tiles take what the screen has left, up to their roomy default height.
    const int rest = screenH - l.headerBand - l.lightBlock - l.autoBlock - l.bottomBand - tokens.spaceSm -
                     (tileRows - 1) * l.tileGap;
    l.tileHeight = static_cast<int16_t>(std::clamp<int>(rest / tileRows, kMinTileHeight, kTileHeight));
    l.tileBlock = hasTiles ? static_cast<int16_t>(fui::tileGridHeight(kTileCount, kTileCols, l.tileHeight, l.tileGap) +
                                                  tokens.spaceSm)
                           : 0;
    l.height = static_cast<int16_t>(l.headerBand + l.lightBlock + l.tileBlock + l.autoBlock + l.bottomBand);
  }
  if (kCompactRows && hasTiles) {
    // Same tile block height (buildPanelScreen adds the outer inset back as
    // air): each tile loses kCompactBoxInset on every side.
    l.tileHeight = static_cast<int16_t>(l.tileHeight - 2 * kCompactBoxInset);
    l.tileGap = static_cast<int16_t>(l.tileGap + 2 * kCompactBoxInset);
  }
  if (kCompactRows && !l.twoColumn) {
    // Fixed UT37 stack (see kCompactSectionGap): the sheet's height follows
    // its content instead of filling the screen. Rows are tracked by their
    // ink (each box kCompactBoxInset inside its cell).
    const int16_t boxInk = static_cast<int16_t>(kCompactStepSize - 2 * kCompactBoxInset);
    int inkBottom = l.headerBand - 1;
    if (Frontlight.present()) inkBottom = l.headerBand + kCompactBoxInset + boxInk - 1;
    if (hasTiles) {
      l.tileHeight = kCompactTileInkHeight;
      l.tileGap = kCompactTileInkGap;
      inkBottom += kCompactSectionGap + tileRows * kCompactTileInkHeight + (tileRows - 1) * kCompactTileInkGap;
    }
    if (showAutoTurn) {
      // Grid trailing gap + the cell inset added back + spacer + the row's inset.
      inkBottom += kTileGridTrailingGap + kCompactBoxInset + tokens.spaceMd + kCompactBoxInset + boxInk;
    }
    l.bottomBand = static_cast<int16_t>(kCompactSectionGap + kGrabberHeight + kCompactGrabberToRule + kSheetRuleWidth);
    l.height = static_cast<int16_t>(inkBottom + 1 + l.bottomBand);
  }
  if (l.height > screenH) l.height = static_cast<int16_t>(screenH);
  return l;
}

void FrontlightPanelActivity::panelScreen(UiScreen& screen, void* user) {
  static_cast<FrontlightPanelActivity*>(user)->buildPanelScreen(screen);
}

void FrontlightPanelActivity::addSliderRow(UiScreen& screen, const char* label, const char* readout,
                                           const uint8_t value, const fui::ActionId sliderAction,
                                           const fui::ActionId stepAction, const bool showToggle) {
  if constexpr (kCompactRows) {
    addCompactStepRow(screen, readout, stepAction, showToggle ? ACTION_TOGGLE : fui::NO_ACTION,
                      lightOn ? icon_panel_light_on_24 : icon_panel_light_off_24);
    return;
  }

  // rowProps is a member (fui::SliderRowProps embeds a 324-byte StyleSet, well
  // past the 256-byte budget a local gets — AGENTS.md). Every field that
  // varies between the two rows is reassigned here; the rest keep their
  // constructed defaults, which already match the panel's card language.
  rowProps.label = label;
  rowProps.value = readout;
  rowProps.sliderValue = value;
  rowProps.sliderAction = sliderAction;
  rowProps.decrement = stepAction;
  rowProps.increment = stepAction;
  rowProps.decrementValue = -BRIGHTNESS_STEP;
  rowProps.incrementValue = BRIGHTNESS_STEP;
  if (showToggle) {
    // Lamp on/off after the +: the sliders set the level, this kills the light
    // outright. Filled glyph = on, outline = off.
    rowProps.toggleAction = ACTION_TOGGLE;
    rowProps.toggleIcon = fui::bitmapFromIcon(lightOn ? icon_sun_filled_32 : icon_sun_32);
  } else {
    rowProps.toggleAction = fui::NO_ACTION;
    rowProps.toggleIcon = fui::BitmapRef{};
  }
  screen.sliderRow(rowProps, kSliderRowHeight);
  // The wrapper's own trailing gap is one spaceMd; double it so the rows
  // breathe — a control band this tall reads cramped at the list cadence.
  screen.spacer(screen.theme().spaceMd);
}

void FrontlightPanelActivity::addCompactStepRow(UiScreen& screen, const char* readout,
                                                const fui::ActionId stepAction, const fui::ActionId toggleAction,
                                                const freeink::Icon& toggleIcon) {
  const auto& theme = screen.theme();
  const fui::Rect band = screen.takeTop(kCompactStepSize, theme.spaceMd);
  // Cells are kCompactStepSize wide, spaceMd apart; each box is drawn
  // kCompactBoxInset inside its cell. minTouchSize grows a 34 px box by 5 px
  // per side, still short of the neighbouring cell's hit region.
  const int16_t gap = theme.spaceMd;
  const int16_t size = kCompactStepSize;
  const auto cell = [&band](const int16_t x) {
    return fui::Rect{static_cast<int16_t>(x + kCompactBoxInset), static_cast<int16_t>(band.y + kCompactBoxInset),
                     static_cast<int16_t>(kCompactStepSize - 2 * kCompactBoxInset),
                     static_cast<int16_t>(kCompactStepSize - 2 * kCompactBoxInset)};
  };

  // stepProps is a member: fui::ButtonProps embeds a 324-byte StyleSet.
  stepProps.label = nullptr;
  stepProps.styles = fui::sliderRowStepStyles(fui::resolveRadius(theme.controlRadius, 18));
  stepProps.inputMask = fui::InputTouch;
  stepProps.action = stepAction;

  stepProps.icon = fui::bitmapFromIcon(icon_step_minus_16);
  stepProps.value = -BRIGHTNESS_STEP;
  screen.button(stepProps, cell(band.x));

  int16_t right = band.right();
  if (toggleAction != fui::NO_ACTION) {
    stepProps.icon = fui::bitmapFromIcon(toggleIcon);
    stepProps.action = toggleAction;
    stepProps.value = 0;
    screen.button(stepProps, cell(static_cast<int16_t>(right - size)));
    right = static_cast<int16_t>(right - size - gap);
    stepProps.action = stepAction;
  }
  const int16_t plusX = static_cast<int16_t>(right - size);
  stepProps.icon = fui::bitmapFromIcon(icon_step_plus_16);
  stepProps.value = BRIGHTNESS_STEP;
  screen.button(stepProps, cell(plusX));

  // Level readout centred in the space between - and +.
  fui::TextStyle readoutStyle = theme.titleText;
  readoutStyle.align = fui::TextAlign::Center;
  const int16_t lineHeight = screen.target().lineHeight(readoutStyle.font);
  const int16_t readoutX = static_cast<int16_t>(band.x + size + gap);
  const int16_t readoutWidth = static_cast<int16_t>(plusX - gap - readoutX);
  if (readoutWidth > 0) {
    screen.target().text(fui::Rect{readoutX, static_cast<int16_t>(band.y + (size - lineHeight) / 2), readoutWidth,
                                   lineHeight},
                         readout, readoutStyle);
  }
  screen.spacer(theme.spaceMd);
}

void FrontlightPanelActivity::buildPanelScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  const PanelLayout layout = computeLayout();

  // Sheet chrome first: the card body, the 2px rule along its bottom edge, and
  // the grabber on the edge the sheet is dragged from. Screen::sheet() also
  // clamps the content area to the sheet, so every band below lays out inside
  // it. (No header: the panel is a floating card, and its own grabber says
  // what it is.) The band under the content is twice the old height so the
  // sheet's foot is an easy close target (see loop()).
  fui::SheetProps sheetProps;
  sheetProps.grabberMargin = static_cast<int16_t>(2 * theme.spaceLg);
  sheetProps.grabberInset = static_cast<int16_t>(2 * (theme.spaceLg + theme.spaceMd));
  if (kCompactRows && !layout.twoColumn) {
    sheetProps.grabberMargin = kCompactSectionGap;
    sheetProps.grabberInset = static_cast<int16_t>(kCompactGrabberToRule + kSheetRuleWidth);
  }
  screen.sheet(sheetProps, static_cast<int16_t>(panelBottom));
  screen.insetContent(fui::Insets{0, kPanelSideMargin, 0, kPanelSideMargin});

#if FREEINK_DEVICE_UT37
  // UT37: the same clock/battery strip as the main tabs and the reader, so the
  // clock and battery never disappear while the sheet is open (both layouts).
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    screen.takeTop(static_cast<int16_t>(UITheme::kReaderTopStripHeight), theme.spaceMd);
    UITheme::getInstance().getTheme().drawStatusStrip(
        renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), UITheme::kReaderTopStripHeight});
  }
#else
  if (layout.twoColumn) {
    screen.spacer(theme.spaceMd);
  } else {
    // Reuse the exact battery renderer and header rectangle used by Home. Call
    // the base implementation directly because RoundedRaff suppresses its
    // untitled Home header.
    const auto& metrics = UITheme::getInstance().getMetrics();
    screen.spacer(theme.spaceMd);
    const int16_t bandH = std::max<int16_t>(static_cast<int16_t>(metrics.batteryHeight),
                                            screen.target().lineHeight(theme.smallText.font));
    screen.takeTop(bandH, theme.spaceMd);
    UITheme::getInstance().getTheme().BaseTheme::drawHeader(
        renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.homeTopPadding - metrics.topPadding},
        nullptr);
  }
#endif

  // Two columns: point the screen's content area at one column at a time.
  const fui::Rect body = screen.body();
  const fui::Rect safe = screen.frame().safeRect();
  const auto useContent = [&screen, &safe](const fui::Rect& r) {
    screen.setContentMargin(fui::Insets{static_cast<int16_t>(r.y - safe.y),
                                        static_cast<int16_t>(safe.right() - r.right()),
                                        static_cast<int16_t>(safe.bottom() - r.bottom()),
                                        static_cast<int16_t>(r.x - safe.x)});
  };
  if (layout.twoColumn) {
    const int16_t leftW = static_cast<int16_t>(body.width - layout.tileColumnWidth - theme.spaceMd);
    useContent(fui::Rect{body.x, body.y, leftW, body.height});
  }

  if (Frontlight.present()) {
    // Readouts are formatted here; the row draws before addSliderRow() returns
    // (immediate mode), so stack buffers are safe.
    char brightnessText[12];
    formatBrightnessReadout(brightnessText, sizeof(brightnessText), brightness);
    addSliderRow(screen, tr(STR_BRIGHTNESS), brightnessText, brightness, ACTION_BRIGHTNESS, ACTION_BRIGHTNESS_STEP,
                 /*showToggle=*/true);
    if (Frontlight.hasColorTemperature()) {
      char warmthText[8];
      snprintf(warmthText, sizeof(warmthText), "%u%%", static_cast<unsigned>(warmth));
      addSliderRow(screen, tr(STR_WARMTH), warmthText, warmth, ACTION_WARMTH, ACTION_WARMTH_STEP,
                   /*showToggle=*/false);
    }
    // UT37 stacked: the row's own trailing gaps already make the 22 px.
    if (!kCompactRows || layout.twoColumn) screen.spacer(theme.spaceSm);
  }

  // Auto page turn, only while a book is open underneath: [-] speed [+] [book].
  // The book shows a lifting page while turning. Stacked, it is the last row;
  // in two columns it sits under the light rows.
  const auto addAutoTurnRow = [&] {
    if (!showAutoTurn) return;
    char rateText[24];
    snprintf(rateText, sizeof(rateText), tr(STR_AUTO_TURN_RATE_FMT),
             static_cast<unsigned>(SETTINGS.autoPageTurnRate));
    // The toggle keeps the turning-page glyph whether or not auto turn runs
    // (UT37 review: one fixed icon).
    addCompactStepRow(screen, rateText, ACTION_AUTO_TURN_STEP, ACTION_AUTO_TURN_TOGGLE, icon_panel_auto_read_on_24);
  };
  if (layout.twoColumn) {
    addAutoTurnRow();
    useContent(fui::Rect{static_cast<int16_t>(body.right() - layout.tileColumnWidth), body.y, layout.tileColumnWidth,
                         body.height});
  }

  // Quick-setting tiles. Two columns of finger-sized cards; a tile whose
  // setting is currently on draws filled (StateChecked -> selected style).
  // Touch boards only — the tiles are touch targets.
  if (mappedInput.hasTouch()) {
    static constexpr StrId kOrientNames[4] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                              StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
    // The orientation tile is labelled with just the current mode ("Portrait"):
    // the mode names say what the tile is about on their own.
    const char* orientLabel = I18N.get(kOrientNames[SETTINGS.orientation % 4]);
    const char* labels[kTileCount] = {tr(STR_NIGHT_MODE), tr(STR_BLUETOOTH), orientLabel, tr(STR_SETTINGS_TITLE)};
    // A tile whose switch is on draws filled.
    const fui::State states[kTileCount] = {SETTINGS.screenInverted ? fui::StateChecked : fui::StateNormal,
                                           SETTINGS.bluetoothEnabled ? fui::StateChecked : fui::StateNormal,
                                           fui::StateNormal, fui::StateNormal};

    for (int id = 0; id < kTileCount; ++id) {
      gridItems[id].label = labels[id];
      gridItems[id].value = static_cast<int16_t>(id);
      gridItems[id].state = states[id];
    }
    gridProps.items = gridItems;
    gridProps.count = static_cast<uint16_t>(kTileCount);
    gridProps.action = ACTION_TILE;
    gridProps.columns = kTileCols;
    gridProps.tileHeight = layout.tileHeight;
    gridProps.gap = layout.tileGap;
    if constexpr (kCompactRows) {
      // Outer edges of the grid move in by kCompactBoxInset too.
      const fui::Rect area = screen.body();
      useContent(fui::Rect{static_cast<int16_t>(area.x + kCompactBoxInset),
                           static_cast<int16_t>(area.y + kCompactBoxInset),
                           static_cast<int16_t>(area.width - 2 * kCompactBoxInset),
                           static_cast<int16_t>(area.height - kCompactBoxInset)});
      screen.tileGrid(gridProps);
      const fui::Rect after = screen.body();
      useContent(fui::Rect{static_cast<int16_t>(after.x - kCompactBoxInset),
                           static_cast<int16_t>(after.y + kCompactBoxInset),
                           static_cast<int16_t>(after.width + 2 * kCompactBoxInset),
                           static_cast<int16_t>(after.height - kCompactBoxInset)});
    } else {
      screen.tileGrid(gridProps);
    }
  }

  if (!layout.twoColumn) {
    screen.spacer(theme.spaceMd);
    addAutoTurnRow();
  }
}

void FrontlightPanelActivity::render(RenderLock&&) {
  const PanelLayout layout = computeLayout();
  panelBottom = layout.height;
  panelBottomBand = layout.bottomBand;

  // fui::sheet draws the card body, its bottom rule, and the grabber during
  // renderUi(); the battery band at the card's top is part of the screen build.
  renderUi();

  // A tile that rewrote the whole frame (night mode) re-drives every pixel
  // once; ordinary repaints stay on the fast path. HALF: strong enough to
  // flip the whole frame's polarity without the FULL waveform's blackout
  // flash. Any faint residue clears with the panel's dedicated refresh tile
  // or the next scheduled clean refresh.
#if FREEINK_DEVICE_EEGO_A4
  renderer.displayBuffer(cleanRefreshPending ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
#else
  renderer.displayBuffer(cleanRefreshPending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
#endif
  cleanRefreshPending = false;
}
