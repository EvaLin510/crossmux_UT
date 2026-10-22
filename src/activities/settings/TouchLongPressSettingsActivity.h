#pragma once
#include <string>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Reader > Touch long-press: one row per 3x3 screen zone, each picking the
// action a long press there runs while reading (CrossPointSettings::touchLongPress).
class TouchLongPressSettingsActivity final : public UiListActivity {
 public:
  explicit TouchLongPressSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

  // Picker order (also the label table order); values are TOUCH_LONG_PRESS_ACTION.
  static constexpr int OPTION_COUNT = CrossPointSettings::TOUCH_LONG_PRESS_ACTION_COUNT;
  static StrId actionLabel(uint8_t action);

 private:
  static constexpr int ZONE_COUNT = CrossPointSettings::TOUCH_LONG_PRESS_ZONES;

  OptionPopup optionPopup;

  int listCount() const override { return ZONE_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

  // Fixed-capacity row storage: the zone count is a compile-time constant, so
  // no heap growth per render; only the value text is refreshed.
  std::string rowValues_[ZONE_COUNT];
  freeink::ui::ListItem rowItems_[ZONE_COUNT]{};
};
