#pragma once

#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>

class GfxRenderer;

// Low-PPI panels (UT37: 3.7" 240x416, ~150 PPI) use built-in 1-bit bitmap UI
// faces (see builtinFonts/all.h, CROSSMUX_BITMAP_UI_FONT). When the complete
// bitmap family is on the SD card, it is chained behind them for the rare
// characters the built-in common-character subset omits.
#if FREEINK_DEVICE_UT37
#define CROSSMUX_SD_UI_FONT 1
#else
#define CROSSMUX_SD_UI_FONT 0
#endif

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  static constexpr const char* COMPLETE_CHINESE_NOTO_SANS_FAMILY = "NotoSansSC";
  // SD families tried, in order, as the bitmap UI face:
  // /.fonts/<name>/ or /fonts/<name>/ holding <any>_{10,12,14}.cpfont.
  static constexpr const char* SD_UI_FONT_FAMILIES[] = {"WQYZhengHei", "WenQuanDian"};

  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Discover SD card fonts and load user's saved selection. Call once during setup.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer, bool allowFlashCache = true);

  /// Release the resident SD font without changing the saved selection.
  void releaseLoadedFont(GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + reader point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registryDirty_.store(true, std::memory_order_release); }

  /// Chinese builds replace the duplicate built-in reader face with the
  /// complete SD-card Noto Sans family when it is installed.
  /// Returns true when the saved selection changed.
  bool adoptCompleteChineseNotoSans();

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() {
    if (registryDirty_.exchange(false, std::memory_order_acquire)) {
      registry_.discover();
      adoptCompleteChineseNotoSans();
    }
  }

 private:
  // In global builds, load size-matched SD CJK fallbacks for the built-in UI
  // fonts. CN builds already embed their UI glyphs and keep only the reader
  // size resident to preserve contiguous heap.
  void setupUiFallbacks(GfxRenderer& renderer);

#if CROSSMUX_SD_UI_FONT
  // Load the first installed SD_UI_FONT_FAMILIES entry once at boot and chain
  // it behind the built-in UI faces. Owned by uiManager_ so reader reloads
  // never unload it.
  void setupSdUiFont(GfxRenderer& renderer);
  SdCardFontManager uiManager_;
  bool sdUiFontActive_ = false;
#endif

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  std::atomic<bool> registryDirty_{false};
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
