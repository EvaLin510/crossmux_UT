#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public UiListActivity {
 public:
  enum class Mode : uint8_t {
    Settings,
    Initial,
    Upgrade,
  };

  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::Settings);

  void onEnter() override;

 private:
  int listCount() const override { return visibleCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void drawFooter() override;
  bool isOnboarding() const { return mode_ != Mode::Settings; }

  constexpr static uint8_t totalItems = getLanguageCount();

  // Row storage: totalItems is a compile-time constant, so a fixed-capacity
  // array avoids any heap allocation for the row list. Built once in
  // onEnter() — activateIndex() finishes the activity immediately on
  // selection, so buildScreen() never needs to see a different "Selected"
  // row within one visit. Only I18n::isLanguageAvailable() languages are
  // listed; visibleLanguages maps a row to its Language value.
  freeink::ui::ListItem rowItems[totalItems]{};
  uint8_t visibleLanguages[totalItems]{};
  uint8_t visibleCount = 0;
  const Mode mode_;
};
