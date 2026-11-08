#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "FontInstaller.h"
#include "SdCardFont.h"
#include "activities/UiListActivity.h"

// Installs the complete WQYZhengHei family (every point size) from a fixed
// release URL when the reader hits a glyph the built-in Chinese subset lacks.
// The file table (size + CRC32) is compiled in, so no manifest is fetched or
// parsed and the activity holds no heap beyond the HTTP download itself.
class FontDownloadActivity final : public UiListActivity {
 public:
  // FromHome: offered at boot when the family is missing; every exit returns Home.
  enum class StartMode : uint8_t { Normal, ResumeFontLoadError, FromHome };

  explicit FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                StartMode startMode = StartMode::Normal);

#ifdef ENABLE_CHINESE_VERSION
  static bool wasChineseFontPromptShownThisBoot();
  static void suppressChineseFontPromptThisBoot();
#endif

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    // The download is synchronous and blocks the main loop until it completes,
    // so activityManager.preventAutoSleep() is never polled during downloading.
    return state_ == DOWNLOADING || state_ == ERROR;
  }
  bool skipLoopDelay() override { return true; }

  struct FontFile {
    uint8_t pointSize;
    uint32_t size;
    uint32_t crc32;
  };

 private:
  enum State {
    WIFI_SELECTION,
    DOWNLOADING,
    ERROR,
  };

  enum class DownloadResult : uint8_t { Success, Cancelled, Failed };
  enum class AutomaticError : uint8_t { Download, SettingsSave, FontLoad };
  enum class ExitRoute : uint8_t { Home, Reader, ReaderSuppressPrompt, ReaderPreloadChineseFont };

  State state_ = WIFI_SELECTION;
  StartMode startMode_;
  FontInstaller fontInstaller_;

  // Download progress
  size_t currentFileIndex_ = 0;
  size_t downloadProgress_ = 0;
  size_t downloadTotal_ = 0;
  uint8_t lastProgressRefreshPercent_ = 0;
  bool cancelRequested_ = false;
  bool readingStatsReleased_ = false;
  uint8_t targetPointSize_ = 0;
  AutomaticError automaticError_ = AutomaticError::Download;
  ExitRoute exitRoute_ = ExitRoute::Home;
  // Set when the cancel came from the home gesture (consumed by the download
  // callback's own input pump); exit to home after the abort unwinds.
  bool goHomeRequested_ = false;

  static constexpr freeink::ui::ActionId ACTION_CANCEL_DOWNLOAD = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_BACK_TO_READER = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_RETRY_DOWNLOAD = ACTION_USER + 2;
  static void onCancelDownload(const freeink::ui::ActionEvent& event, void* user);
  static void onBackToReader(const freeink::ui::ActionEvent& event, void* user);
  static void onRetryDownload(const freeink::ui::ActionEvent& event, void* user);

  int listCount() const override { return 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
  // Every state consumes the loop pass here; there is no list.
  bool handleCustomInput() override;

  void startWifiSelection();
  void onWifiSelectionComplete(bool success);
  void startDownload();
  void finishAutomaticFlow(ExitRoute route);
  const char* automaticErrorText() const;
  void resetDownloadProgress(size_t total);
  void updateDownloadProgress(size_t completed);
  void finishDownloadProgress();
  DownloadResult downloadFile(const FontFile& file);
  DownloadResult downloadFamily();
  void selectDownloadedFont();
  void retryDownloadOperation();
  static bool computeFileCrc32(const char* path, uint32_t& outCrc);
  bool isVerifiedFontFile(const char* path, const FontFile& file);
};
