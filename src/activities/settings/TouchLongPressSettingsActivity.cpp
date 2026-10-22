#include "TouchLongPressSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
const StrId zoneNames[CrossPointSettings::TOUCH_LONG_PRESS_ZONES] = {
    StrId::STR_ZONE_TOP_LEFT,    StrId::STR_ZONE_TOP_CENTER, StrId::STR_ZONE_TOP_RIGHT,
    StrId::STR_ZONE_MIDDLE_LEFT, StrId::STR_ZONE_CENTER,     StrId::STR_ZONE_MIDDLE_RIGHT,
    StrId::STR_ZONE_BOTTOM_LEFT, StrId::STR_ZONE_BOTTOM_CENTER, StrId::STR_ZONE_BOTTOM_RIGHT,
};

// Indexed by TOUCH_LONG_PRESS_ACTION; the reader-menu wording is reused.
const StrId actionNames[TouchLongPressSettingsActivity::OPTION_COUNT] = {
    StrId::STR_NONE_OPT,           StrId::STR_READER_MENU,       StrId::STR_MENU_TOC,
    StrId::STR_FOOTNOTES,          StrId::STR_MENU_BOOKMARK_LIST, StrId::STR_MENU_ADD_REMOVE_BOOKMARK,
    StrId::STR_TEXT_SETTINGS,      StrId::STR_MENU_DICT_LOOKUP,  StrId::STR_AUTO_TURN,
    StrId::STR_GO_TO_PERCENT,      StrId::STR_GO_HOME_BUTTON,    StrId::STR_SYNC_PROGRESS,
};
static_assert(sizeof(actionNames) / sizeof(actionNames[0]) == CrossPointSettings::TOUCH_LONG_PRESS_ACTION_COUNT,
              "one label per TOUCH_LONG_PRESS_ACTION");
}  // namespace

StrId TouchLongPressSettingsActivity::actionLabel(const uint8_t action) {
  return action < OPTION_COUNT ? actionNames[action] : StrId::STR_NONE_OPT;
}

TouchLongPressSettingsActivity::TouchLongPressSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TouchLongPressSettings", renderer, mappedInput) {}

void TouchLongPressSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (SETTINGS.touchLongPress[i] >= OPTION_COUNT) SETTINGS.touchLongPress[i] = CrossPointSettings::TLP_NONE;
    rowItems_[i].label = I18N.get(zoneNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

bool TouchLongPressSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void TouchLongPressSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  if (index < 0 || index >= ZONE_COUNT) return;
  nav.selected = index;
  // The popup covers the list; a lingering flash would gray an unrelated row.
  app.clearTapFlash();
  optionPopup.show(zoneNames[index], actionNames, OPTION_COUNT, SETTINGS.touchLongPress[index], [index](int idx) {
    if (idx < 0 || idx >= OPTION_COUNT) return;
    SETTINGS.touchLongPress[index] = static_cast<uint8_t>(idx);
    SETTINGS.saveToFile();
  });
  requestUpdate();
}

void TouchLongPressSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < ZONE_COUNT; i++) {
    rowValues_[i] = I18N.get(actionLabel(SETTINGS.touchLongPress[i]));
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(ZONE_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void TouchLongPressSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_TOUCH_LONG_PRESS));
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
