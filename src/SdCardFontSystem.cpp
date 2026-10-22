#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "fontIds.h"

namespace {

// Point the reader font size at a size the given family actually ships, and
// persist the change so the settings UI and the loaded font never disagree.
// Guarded by the value-change check: a no-op snap must not write SPIFFS.
void snapFontPointSizeTo(const uint8_t availablePointSize) {
  if (availablePointSize == 0 || availablePointSize == SETTINGS.fontPointSize) return;
  LOG_DBG("SDFS", "Font size %u unavailable, snapping to %u", SETTINGS.fontPointSize, availablePointSize);
  SETTINGS.fontPointSize = availablePointSize;
  SETTINGS.saveToFile();
}

#if !defined(ENABLE_CHINESE_VERSION) || CROSSMUX_SD_UI_FONT
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};
#endif

#if CROSSMUX_SD_UI_FONT
// SD bitmap UI face sizes, matching the built-in wqy_zhenghei_10/12/14 faces
// (13/14/15 px CJK cells) that main.cpp registers for the same UI font ids.
constexpr UiFontSize kSdUiFontSizes[] = {
    {SMALL_FONT_ID, 10},
    {UI_10_FONT_ID, 12},
    {UI_12_FONT_ID, 14},
};
#endif

#ifndef ENABLE_CHINESE_VERSION
// Global-build UI fonts and their physical point sizes (at 150 DPI, matching
// the SD-font converter). CN builds already embed Simplified-Chinese UI fonts
// and deliberately avoid keeping three additional SD font sizes resident.
constexpr UiFontSize kUiFontSizes[] = {
    {SMALL_FONT_ID, 8},
    {UI_10_FONT_ID, 10},
    {UI_12_FONT_ID, 12},
};
#endif

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.discover();
  adoptCompleteChineseNotoSans();
#if CROSSMUX_SD_UI_FONT
  // Any font without its own CJK fallback (reader faces reused for UI titles,
  // e.g. reading-stats book titles) falls back to the 15 px bitmap UI face,
  // which itself chains to the SD family for rare characters.
  renderer.setDefaultCjkFallbackFont(renderer.getFallbackFontId(UI_12_FONT_ID));
  setupSdUiFont(renderer);
#endif

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t pointSize) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, pointSize);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      if (manager_.loadFamily(*family, renderer, SETTINGS.fontPointSize, SETTINGS.sdFontFlashPreload != 0)) {
        snapFontPointSizeTo(manager_.currentPointSize());
        setupUiFallbacks(renderer);
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.clearSdFontFamily();
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.clearSdFontFamily();
    }
  }

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer, bool allowFlashCache) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    registry_.discover();
    adoptCompleteChineseNotoSans();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const bool preferFlash = allowFlashCache && SETTINGS.sdFontFlashPreload != 0;

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    // Back on a built-in family, which exists only at BUILTIN_READER_POINT_SIZES:
    // a size inherited from an SD family has to come back into that set.
    snapFontPointSizeTo(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES),
                                               SETTINGS.fontPointSize));
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.clearSdFontFamily();
      return;
    }
    const auto* selected = family->findNearestSize(SETTINGS.fontPointSize);
    const uint8_t wantedPt = selected ? selected->pointSize : 0;
    // Snap before the early return: the wanted size can already be loaded while
    // the setting still names a size this family does not ship.
    snapFontPointSizeTo(wantedPt);
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, SETTINGS.fontPointSize, preferFlash)) {
      snapFontPointSizeTo(manager_.currentPointSize());
      setupUiFallbacks(renderer);
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.clearSdFontFamily();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.clearSdFontFamily();
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) { manager_.unloadAll(renderer); }

bool SdCardFontSystem::adoptCompleteChineseNotoSans() {
#ifdef ENABLE_CHINESE_VERSION
  if (SETTINGS.contentProfile != CrossPointSettings::ContentProfile::China || SETTINGS.sdFontFamilyName[0] != '\0' ||
      !registry_.findFamily(COMPLETE_CHINESE_NOTO_SANS_FAMILY))
    return false;

  strncpy(SETTINGS.sdFontFamilyName, COMPLETE_CHINESE_NOTO_SANS_FAMILY, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.sdFontFlashPreload = 0;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("SDFS", "Failed to save automatic NotoSansSC selection");
  }
  LOG_INF("SDFS", "Using installed NotoSansSC in place of the Chinese built-in font");
  return true;
#else
  return false;
#endif
}

#if CROSSMUX_SD_UI_FONT
void SdCardFontSystem::setupSdUiFont(GfxRenderer& renderer) {
  const SdCardFontFamilyInfo* family = nullptr;
  for (const char* name : SD_UI_FONT_FAMILIES) {
    family = registry_.findFamily(name);
    if (family) break;
  }
  if (!family) {
    LOG_INF("SDFS", "SD UI font %s not installed; using built-in UI fonts", SD_UI_FONT_FAMILIES[0]);
    return;
  }
  // Distinct id seed: the reader may load the same family and file.
  char idName[48];
  snprintf(idName, sizeof(idName), "%s#ui", family->name.c_str());
  for (const auto& ui : kSdUiFontSizes) {
    const auto* file = family->findFile(ui.pointSize);
    if (!file) {
      LOG_ERR("SDFS", "SD UI font %s has no %u pt file", family->name.c_str(), ui.pointSize);
      continue;
    }
    // The built-in subset (main.cpp) holds only the ~2000 most frequent
    // characters, so book titles and file names reach this face routinely, and
    // list rows measure each title several times (truncation) per redraw.
    // Without a cache every measure re-reads the glyphs from the card. The
    // 1 MiB PSRAM cache is allocated on first use, only while PSRAM keeps
    // PSRAM_FREE_RESERVE free.
    const int fontId = uiManager_.loadStandaloneFile(*file, idName, renderer, true);
    if (fontId == 0) continue;
    const int builtinId = renderer.getFallbackFontId(ui.fontId);
    if (builtinId != 0) {
      // Chained behind the built-in bitmap subset: CJK strings it cannot draw.
      renderer.setFallbackFont(builtinId, fontId);
    } else {
      renderer.setFallbackFont(ui.fontId, fontId, true);
    }
    sdUiFontActive_ = true;
    LOG_INF("SDFS", "UI font %d rare-glyph fallback -> SD bitmap %s", ui.fontId, file->path.c_str());
  }
}
#endif

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
#if CROSSMUX_SD_UI_FONT
  // The SD UI face already covers rare CJK; reader-family sizes would replace it.
  if (sdUiFontActive_) return;
#endif
#ifdef ENABLE_CHINESE_VERSION
  // Unified firmware has built-in 8/10/12pt Simplified-Chinese UI fallbacks.
  // Keep only the selected reader-size SD font resident: loading
  // three more broad-CJK sizes leaves too little contiguous heap for EPUB image
  // decoding and glyph prewarm.
  (void)renderer;
  return;
#else
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;  // no SD family loaded — nothing to fall back to

  const auto* family = registry_.findFamily(familyName);
  if (!family) return;

  // Probe before paying for the UI sizes. Skip Latin-only families and, in the
  // CN build, avoid loading duplicate SD sizes when the built-in UI fonts
  // already cover the same script.
  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;
  // One representative codepoint per script: Han, Hiragana, Katakana, Hangul.
  static constexpr uint32_t kCjkProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
  bool needsFallback = false;
  for (const uint32_t cp : kCjkProbes) {
    if (!readerIt->second.hasCodepoint(cp)) continue;
    for (const auto& ui : kUiFontSizes) {
      const auto primaryIt = renderer.getFontMap().find(ui.fontId);
      if (primaryIt != renderer.getFontMap().end() && !primaryIt->second.hasCodepoint(cp)) {
        needsFallback = true;
        break;
      }
    }
    if (needsFallback) break;
  }
  if (!needsFallback) {
    LOG_DBG("SDFS", "%s adds no CJK UI coverage - skipping fallback sizes", familyName.c_str());
    return;
  }

  for (const auto& ui : kUiFontSizes) {
    const int sdFontId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (sdFontId != 0) {
      renderer.setFallbackFont(ui.fontId, sdFontId);
    } else {
      LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", ui.pointSize, familyName.c_str());
    }
  }
#endif
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*pointSize*/) const {
  // The manager holds exactly one reader-size font, already selected for
  // SETTINGS.fontPointSize, so the size argument is implicit — always return
  // that font's ID. ensureLoaded() must have run for the current settings first.
  return manager_.getFontId(familyName);
}
