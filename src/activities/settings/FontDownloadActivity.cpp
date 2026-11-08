#include "FontDownloadActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "ReadingStatsStore.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CrossMuxEndpoints.h"
#include "network/HttpDownloader.h"

namespace fui = freeink::ui;

namespace {

constexpr uint8_t kDownloadProgressStepPercent = 10;

// Every published WQYZhengHei_<pt>.cpfont. Sizes and CRC32 (zlib) must match
// the files committed under CrossMuxEndpoints::WQY_ZHENGHEI_FONT_BASE_URL;
// update both together when the font files are regenerated.
constexpr FontDownloadActivity::FontFile kFontFiles[] = {
    {8, 1020390, 0x95B81B1Au},  {10, 1128032, 0xB7AA1986u}, {12, 1273611, 0x894C92A9u},
    {14, 1400030, 0xEBC344F5u}, {16, 1619394, 0x189649BEu},
};
constexpr size_t kFontFileCount = sizeof(kFontFiles) / sizeof(kFontFiles[0]);

constexpr size_t totalFontBytes() {
  size_t total = 0;
  for (const auto& file : kFontFiles) total += file.size;
  return total;
}

// Closest published size to `pt`; ties resolve to the smaller size.
constexpr uint8_t nearestPublishedPointSize(const uint8_t pt) {
  uint8_t best = kFontFiles[0].pointSize;
  for (const auto& file : kFontFiles) {
    const int bestDistance = best > pt ? best - pt : pt - best;
    const int distance = file.pointSize > pt ? file.pointSize - pt : pt - file.pointSize;
    if (distance < bestDistance) best = file.pointSize;
  }
  return best;
}

constexpr uint8_t downloadProgressPercent(const size_t completed, const size_t total) {
  if (total == 0) return 0;
  const size_t boundedCompleted = completed < total ? completed : total;
  return static_cast<uint8_t>(static_cast<uint64_t>(boundedCompleted) * 100 / total);
}

constexpr bool shouldRefreshDownloadProgress(const uint8_t previous, const uint8_t current) {
  return current < 100 && current >= previous + kDownloadProgressStepPercent;
}

static_assert(nearestPublishedPointSize(12) == 12);
static_assert(nearestPublishedPointSize(13) == 12);
static_assert(nearestPublishedPointSize(20) == 16);
static_assert(nearestPublishedPointSize(1) == 8);
static_assert(downloadProgressPercent(9, 100) == 9);
static_assert(downloadProgressPercent(110, 100) == 100);
static_assert(!shouldRefreshDownloadProgress(0, 9));
static_assert(shouldRefreshDownloadProgress(0, 10));
static_assert(!shouldRefreshDownloadProgress(90, 100));  // The verified final frame is drawn synchronously.

#ifdef ENABLE_CHINESE_VERSION
std::atomic<bool> chineseFontPromptShownThisBoot{false};
#endif

}  // namespace

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const StartMode startMode)
    : UiListActivity("FontDownload", renderer, mappedInput),
      startMode_(startMode),
      fontInstaller_(sdFontSystem.registry()) {}

#ifdef ENABLE_CHINESE_VERSION
bool FontDownloadActivity::wasChineseFontPromptShownThisBoot() {
  return chineseFontPromptShownThisBoot.load(std::memory_order_relaxed);
}

void FontDownloadActivity::suppressChineseFontPromptThisBoot() {
  chineseFontPromptShownThisBoot.store(true, std::memory_order_relaxed);
}
#endif

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_CANCEL_DOWNLOAD, &FontDownloadActivity::onCancelDownload, this);
  app.on(ACTION_BACK_TO_READER, &FontDownloadActivity::onBackToReader, this);
  app.on(ACTION_RETRY_DOWNLOAD, &FontDownloadActivity::onRetryDownload, this);
  targetPointSize_ = nearestPublishedPointSize(SETTINGS.fontPointSize);
#ifdef ENABLE_CHINESE_VERSION
  suppressChineseFontPromptThisBoot();
#endif
  if (startMode_ == StartMode::ResumeFontLoadError) {
    state_ = ERROR;
    automaticError_ = AutomaticError::FontLoad;
    requestUpdate();
    return;
  }

  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CHINESE_FONT_INCOMPLETE),
                                                              tr(STR_DOWNLOAD_FULL_CHINESE_FONT),
                                                              ConfirmationActivity::BodyPlacement::PopupTitle);
  if (!confirmation) {
    LOG_ERR("FONT", "OOM allocating ConfirmationActivity (%zu bytes)", sizeof(ConfirmationActivity));
    finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
    } else {
      startWifiSelection();
    }
  });
}

void FontDownloadActivity::startWifiSelection() {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  if (!readingStatsReleased_) {
    RenderLock lock(*this);
    if (!READING_STATS.releaseMemoryForNetwork()) {
      finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
      return;
    }
    readingStatsReleased_ = true;
  }
#endif
  if (!startActivityForResultWith<WifiSelectionActivity>(
          [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); })) {
    finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
  }
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  const bool wifiWasEnabled = WiFi.getMode() != WIFI_MODE_NULL;
  if (wifiWasEnabled) {
    WiFi.disconnect(false);
    delay(30);
  }
  if (exitRoute_ == ExitRoute::ReaderPreloadChineseFont) {
    LOG_INF("FONT", "Network phase complete: free=%u, maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
  // A cancelled/failed Wi-Fi launch still needs to reload the saved statistics.
  if (!wifiWasEnabled && !readingStatsReleased_ && exitRoute_ != ExitRoute::ReaderPreloadChineseFont) return;

  switch (exitRoute_) {
    case ExitRoute::Home:
      silentRestart();
      return;
    case ExitRoute::Reader:
      silentRestartToReader();
      return;
    case ExitRoute::ReaderSuppressPrompt:
      silentRestartToReader(true);
      return;
    case ExitRoute::ReaderPreloadChineseFont:
      silentRestartToReaderAndPreloadChineseFont(targetPointSize_);
      return;
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
    return;
  }
  NetworkStartup::prepare(renderer);
  startDownload();
}

void FontDownloadActivity::startDownload() {
  automaticError_ = AutomaticError::Download;
  currentFileIndex_ = 0;
  resetDownloadProgress(totalFontBytes());
  const DownloadResult result = downloadFamily();
  if (result == DownloadResult::Success) {
    finishDownloadProgress();
    selectDownloadedFont();
  } else if (result == DownloadResult::Cancelled && !goHomeRequested_) {
    finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
  }
}

void FontDownloadActivity::finishAutomaticFlow(const ExitRoute route) {
  // Launched from Home there is no book to resume; a declined prompt pops back
  // to Home, and a finished download restarts there to load the new family.
  exitRoute_ = startMode_ == StartMode::FromHome ? ExitRoute::Home : route;
  finish();
}

// --- Download ---

// Standard CRC32 matching zlib/Python zlib.crc32().
bool FontDownloadActivity::computeFileCrc32(const char* path, uint32_t& outCrc) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) {
    return false;
  }
  constexpr size_t BUF_SIZE = 128;
  uint8_t buf[BUF_SIZE];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, BUF_SIZE);
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  outCrc = crc;
  return true;
}

bool FontDownloadActivity::isVerifiedFontFile(const char* path, const FontFile& file) {
  {
    HalFile existing;
    if (!Storage.openFileForRead("FONT", path, existing) || existing.fileSize() != file.size) return false;
  }

  uint32_t actualCrc = 0;
  return fontInstaller_.validateCpfontFile(path) && computeFileCrc32(path, actualCrc) && actualCrc == file.crc32;
}

void FontDownloadActivity::resetDownloadProgress(const size_t total) {
  downloadProgress_ = 0;
  downloadTotal_ = total;
  lastProgressRefreshPercent_ = 0;
}

void FontDownloadActivity::updateDownloadProgress(const size_t completed) {
  downloadProgress_ = std::min(completed, downloadTotal_);
  const uint8_t percent = downloadProgressPercent(downloadProgress_, downloadTotal_);
  if (!shouldRefreshDownloadProgress(lastProgressRefreshPercent_, percent)) return;
  lastProgressRefreshPercent_ = percent;
  requestUpdate(true);
}

void FontDownloadActivity::finishDownloadProgress() {
  downloadProgress_ = downloadTotal_;
  lastProgressRefreshPercent_ = 100;
  requestUpdateAndWait();
}

FontDownloadActivity::DownloadResult FontDownloadActivity::downloadFile(const FontFile& file) {
  const char* familyName = SdCardFontSystem::DOWNLOADABLE_CHINESE_FAMILY;
  char fileName[64];
  snprintf(fileName, sizeof(fileName), "%s_%u.cpfont", familyName, static_cast<unsigned>(file.pointSize));
  char destPath[128];
  FontInstaller::buildFontPath(familyName, fileName, destPath, sizeof(destPath));
  char downloadPath[136];
  snprintf(downloadPath, sizeof(downloadPath), "%s.part", destPath);
  const size_t fileStartProgress = downloadProgress_;

  if (isVerifiedFontFile(destPath, file)) {
    Storage.remove(downloadPath);
    LOG_INF("FONT", "Skipping verified file: %s", fileName);
    currentFileIndex_++;
    updateDownloadProgress(fileStartProgress + file.size);
    return DownloadResult::Success;
  }

  std::string url = std::string(CrossMuxEndpoints::WQY_ZHENGHEI_FONT_BASE_URL) + fileName;
  NetworkStartup::prepare(renderer);
  const auto result = HttpDownloader::downloadToFile(
      url, downloadPath,
      [this, fileStartProgress, fileSize = static_cast<size_t>(file.size)](size_t downloaded, size_t) {
        mappedInput.update();
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
          cancelRequested_ = true;
        }
        // The callback owns this input snapshot while the main loop is blocked.
        if (mappedInput.wasHomeGesture()) {
          cancelRequested_ = true;
          goHomeRequested_ = true;
        }
        UiAppHost::routeTouch(mappedInput);
        updateDownloadProgress(fileStartProgress + std::min(downloaded, fileSize));
      },
      &cancelRequested_);

  if (result == HttpDownloader::ABORTED) {
    Storage.remove(downloadPath);
    return DownloadResult::Cancelled;
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Download failed: %s (%d)", fileName, result);
    Storage.remove(downloadPath);
    return DownloadResult::Failed;
  }

  uint32_t actualCrc = 0;
  if (!computeFileCrc32(downloadPath, actualCrc)) {
    LOG_ERR("FONT", "Failed to open file for CRC check: %s", downloadPath);
    Storage.remove(downloadPath);
    return DownloadResult::Failed;
  }
  if (actualCrc != file.crc32) {
    LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", fileName, static_cast<unsigned>(actualCrc),
            static_cast<unsigned>(file.crc32));
    Storage.remove(downloadPath);
    return DownloadResult::Failed;
  }
  if (!fontInstaller_.validateCpfontFile(downloadPath)) {
    LOG_ERR("FONT", "Invalid .cpfont: %s", downloadPath);
    Storage.remove(downloadPath);
    return DownloadResult::Failed;
  }

  char backupPath[136];
  snprintf(backupPath, sizeof(backupPath), "%s.bak", destPath);
  if (Storage.exists(backupPath)) {
    if (Storage.exists(destPath)) {
      Storage.remove(backupPath);
    } else if (!Storage.rename(backupPath, destPath)) {
      LOG_ERR("FONT", "Failed to recover font file: %s", fileName);
      return DownloadResult::Failed;
    }
  }
  const bool hadPrevious = Storage.exists(destPath);
  if ((hadPrevious && !Storage.rename(destPath, backupPath)) || !Storage.rename(downloadPath, destPath)) {
    if (hadPrevious && !Storage.exists(destPath)) Storage.rename(backupPath, destPath);
    Storage.remove(downloadPath);
    LOG_ERR("FONT", "Failed to install font file: %s", fileName);
    return DownloadResult::Failed;
  }
  if (hadPrevious) Storage.remove(backupPath);
  currentFileIndex_++;
  updateDownloadProgress(fileStartProgress + file.size);
  return DownloadResult::Success;
}

FontDownloadActivity::DownloadResult FontDownloadActivity::downloadFamily() {
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    cancelRequested_ = false;
    goHomeRequested_ = false;
  }
  requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(SdCardFontSystem::DOWNLOADABLE_CHINESE_FAMILY)) {
    LOG_ERR("FONT", "Failed to create font directory");
    RenderLock lock(*this);
    state_ = ERROR;
    return DownloadResult::Failed;
  }

  for (const auto& file : kFontFiles) {
    const auto result = downloadFile(file);
    if (result != DownloadResult::Success) {
      if (goHomeRequested_) {
        onGoHome();
        return result;
      }
      if (result == DownloadResult::Failed) {
        RenderLock lock(*this);
        state_ = ERROR;
      }
      return result;
    }
  }

  fontInstaller_.refreshRegistry();
  return DownloadResult::Success;
}

void FontDownloadActivity::selectDownloadedFont() {
  const char* familyName = SdCardFontSystem::DOWNLOADABLE_CHINESE_FAMILY;
  char previousFamily[sizeof(SETTINGS.sdFontFamilyName)];
  strncpy(previousFamily, SETTINGS.sdFontFamilyName, sizeof(previousFamily));
  previousFamily[sizeof(previousFamily) - 1] = '\0';
  const uint8_t previousFlashPreload = SETTINGS.sdFontFlashPreload;
  const uint8_t previousPointSize = SETTINGS.fontPointSize;

  strncpy(SETTINGS.sdFontFamilyName, familyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.sdFontFlashPreload = 0;
  SETTINGS.fontPointSize = targetPointSize_;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("FONT", "Failed to save selected family %s", familyName);
    strncpy(SETTINGS.sdFontFamilyName, previousFamily, sizeof(SETTINGS.sdFontFamilyName));
    SETTINGS.sdFontFlashPreload = previousFlashPreload;
    SETTINGS.fontPointSize = previousPointSize;
    RenderLock lock(*this);
    state_ = ERROR;
    automaticError_ = AutomaticError::SettingsSave;
    return;
  }

  finishAutomaticFlow(ExitRoute::ReaderPreloadChineseFont);
}

void FontDownloadActivity::retryDownloadOperation() {
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    startWifiSelection();
  } else {
    onWifiSelectionComplete(true);
  }
}

// --- Input handling ---

void FontDownloadActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!mappedInput.hasTouch()) return;
  fui::FooterAction actions[2];
  uint8_t count = 0;
  switch (state_) {
    case DOWNLOADING:
      actions[count++] = {tr(STR_CANCEL), ACTION_CANCEL_DOWNLOAD};
      break;
    case ERROR:
      actions[count++] = {tr(STR_BACK), ACTION_BACK_TO_READER};
      actions[count++] = {tr(STR_RETRY), ACTION_RETRY_DOWNLOAD};
      break;
    case WIFI_SELECTION:
      break;
  }
  if (count > 0) screen.footer(actions, count);
}

void FontDownloadActivity::onCancelDownload(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FontDownloadActivity*>(user);
  if (self->state_ == DOWNLOADING) self->cancelRequested_ = true;
}

void FontDownloadActivity::onBackToReader(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FontDownloadActivity*>(user);
  if (self->state_ != ERROR) return;
  self->app.clearTapFlash();
  self->finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
}

void FontDownloadActivity::onRetryDownload(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FontDownloadActivity*>(user);
  if (self->state_ != ERROR) return;
  self->app.clearTapFlash();
  self->closeRouting();
  self->retryDownloadOperation();
  self->requestUpdateAndWait();
}

bool FontDownloadActivity::handleCustomInput() {
  const auto touch = UiAppHost::routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return true;

  if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      closeRouting();
      retryDownloadOperation();
      requestUpdateAndWait();
    }
  }
  return true;
}

// --- Rendering ---

const char* FontDownloadActivity::automaticErrorText() const {
  switch (automaticError_) {
    case AutomaticError::Download:
      return tr(STR_AUTO_FONT_DOWNLOAD_FAILED);
    case AutomaticError::SettingsSave:
      return tr(STR_AUTO_FONT_SETTINGS_SAVE_FAILED);
    case AutomaticError::FontLoad:
      return tr(STR_AUTO_FONT_LOAD_FAILED);
  }
  return tr(STR_AUTO_FONT_DOWNLOAD_FAILED);
}

void FontDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state_ == DOWNLOADING) {
    const size_t displayedFile = std::min(currentFileIndex_ + 1, kFontFileCount);
    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + SdCardFontSystem::DOWNLOADABLE_CHINESE_FAMILY +
                             " (" + std::to_string(displayedFile) + "/" + std::to_string(kFontFileCount) + ")";
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const Rect content = SubpageLayout::contentRect(safeArea, metrics);
    const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
    const int sectionGap = SubpageLayout::sectionGap(metrics);
    const int blockHeight = lineHeight + sectionGap + GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight);
    int y = SubpageLayout::centeredTop(content, blockHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y, statusText.c_str());
    y += lineHeight + sectionGap;
    GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight}, downloadProgress_,
                        downloadTotal_);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_FONT_INSTALL_FAILED), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, automaticErrorText());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (state_ == DOWNLOADING || state_ == ERROR) renderUi();

  renderer.displayBuffer();
}
