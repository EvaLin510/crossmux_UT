"""Add a UT37 device profile to a crosspoint-simulator checkout.

Usage: python3 ut37/sim/patch_simulator_ut37.py <simulator-checkout>

The upstream simulator (pinned in platformio.ini) has no UT37 profile. This adds
SIMULATOR_DEVICE_UT37: 416x240 landscape framebuffer (240x416 portrait panel,
UC8253), touch, RTC and frontlight, one function key. Idempotent; each edit
asserts its anchor so a simulator bump fails loudly instead of half-patching.
"""

import pathlib
import sys

MARK = "SIMULATOR_DEVICE_UT37"


def edit(path, pairs):
    s = path.read_text(encoding="utf-8")
    if MARK in s:
        return False
    for old, new in pairs:
        if old not in s:
            sys.exit(f"{path}: anchor not found:\n{old}")
        s = s.replace(old, new, 1)
    path.write_text(s, encoding="utf-8")
    return True


def main():
    src = pathlib.Path(sys.argv[1]) / "src"

    edit(src / "BoardConfig.h", [
        ("#if (defined(SIMULATOR_DEVICE_X3) + defined(SIMULATOR_DEVICE_X4_PRO) +",
         "#if (defined(SIMULATOR_DEVICE_UT37) +                                    \\\n"
         "     defined(SIMULATOR_DEVICE_X3) + defined(SIMULATOR_DEVICE_X4_PRO) +"),
        ("#if (defined(SIMULATOR_DEVICE_STICKY) ||",
         "#if (defined(SIMULATOR_DEVICE_UT37) ||                                    \\\n"
         "     defined(SIMULATOR_DEVICE_STICKY) ||"),
        ("#undef FREEINK_DEVICE_MOFEI_M4\n",
         "#undef FREEINK_DEVICE_MOFEI_M4\n#undef FREEINK_DEVICE_UT37\n"),
        ("#if defined(SIMULATOR_DEVICE_PAPERMONO)\n#define FREEINK_DEVICE_X4 0",
         "#if defined(SIMULATOR_DEVICE_UT37)\n"
         "#define FREEINK_DEVICE_X4 0\n#define FREEINK_DEVICE_X3 0\n"
         "#define FREEINK_DEVICE_X4PRO 0\n#define FREEINK_DEVICE_X4CLASSIC 0\n"
         "#define FREEINK_DEVICE_STICKY 0\n#define FREEINK_DEVICE_PAPERMONO 0\n"
         "#define FREEINK_DEVICE_EEGO_A4 0\n#define FREEINK_DEVICE_MURPHY_M4 0\n"
         "#define FREEINK_DEVICE_MOFEI_M4 0\n#define FREEINK_DEVICE_UT37 1\n"
         "// run_ut37_sim.sh also passes -DFREEINK_DEVICE_UT37=1 to every TU.\n"
         "#define FREEINK_CAP_TOUCH 1\n#define FREEINK_CAP_FRONTLIGHT 1\n"
         "#elif defined(SIMULATOR_DEVICE_PAPERMONO)\n#define FREEINK_DEVICE_X4 0"),
        ("#define FREEINK_MCU_C3",
         "#ifndef FREEINK_DEVICE_UT37\n#define FREEINK_DEVICE_UT37 0\n#endif\n\n#define FREEINK_MCU_C3"),
        ("  PaperMono,\n};", "  PaperMono,\n  Ut37,\n};"),
        ("#if defined(SIMULATOR_DEVICE_PAPERMONO)\ninline BoardProfile ACTIVE = PAPER_MONO;",
         "// SIMULATOR_DEVICE_UT37: single function key (power), no up/down pins.\n"
         "inline constexpr BoardProfile UT37 = {\n"
         "    Board::Ut37, \"ut37\", DisplayController::UC8253, 0, {-1, -1}};\n\n"
         "#if defined(SIMULATOR_DEVICE_UT37)\ninline BoardProfile ACTIVE = UT37;\n"
         "#elif defined(SIMULATOR_DEVICE_PAPERMONO)\ninline BoardProfile ACTIVE = PAPER_MONO;"),
        ("  case Board::PaperMono:\n    ACTIVE = PAPER_MONO;\n    return true;\n",
         "  case Board::PaperMono:\n    ACTIVE = PAPER_MONO;\n    return true;\n"
         "  case Board::Ut37:\n    ACTIVE = UT37;\n    return true;\n"),
        ("inline bool isPaperMono() { return ACTIVE.board == Board::PaperMono; }\n",
         "inline bool isPaperMono() { return ACTIVE.board == Board::PaperMono; }\n"
         "inline bool isUt37() { return ACTIVE.board == Board::Ut37; }\n"),
        ("  return isX4Pro() || isEegoA4() || isMurphyM4() || isSticky() ||\n         isPaperMono();",
         "  return isX4Pro() || isEegoA4() || isMurphyM4() || isSticky() ||\n         isPaperMono() || isUt37();"),
        ("inline bool hasPwmFrontlight() {\n  return isX4Pro() || isMurphyM4() || isPaperMono();",
         "inline bool hasPwmFrontlight() {\n  return isX4Pro() || isMurphyM4() || isPaperMono() || isUt37();"),
    ])

    edit(src / "EInkDisplay.h", [
        ("#if defined(SIMULATOR_DEVICE_EEGO_A4)\n  static constexpr uint16_t DISPLAY_WIDTH = 768;",
         "#if defined(SIMULATOR_DEVICE_UT37)\n"
         "  // UT37: 240x416 portrait panel, landscape framebuffer like the firmware.\n"
         "  static constexpr uint16_t DISPLAY_WIDTH = 416;\n"
         "  static constexpr uint16_t DISPLAY_HEIGHT = 240;\n"
         "#elif defined(SIMULATOR_DEVICE_EEGO_A4)\n  static constexpr uint16_t DISPLAY_WIDTH = 768;"),
    ])

    edit(src / "HalDisplay.cpp", [
        ("static constexpr int SIMULATOR_WINDOW_SCALE = 1;",
         "// SIMULATOR_DEVICE_UT37: the 240x416 panel is too small to use at 1:1 on a\n"
         "// desktop; 2x keeps every panel pixel an exact 2x2 block.\n"
         "#if defined(SIMULATOR_DEVICE_UT37)\nstatic constexpr int SIMULATOR_WINDOW_SCALE = 2;\n#else\n"
         "static constexpr int SIMULATOR_WINDOW_SCALE = 1;\n#endif"),
        # Window pixels are scaled; draw (and mouse) coordinates stay in panel
        # pixels, so the logical size must be the unscaled panel (twice: resize
        # path and window creation).
        *[("SDL_RenderSetLogicalSize(sdl_renderer, winW, winH);",
           "SDL_RenderSetLogicalSize(sdl_renderer, winW / SIMULATOR_WINDOW_SCALE,\n"
           "                           winH / SIMULATOR_WINDOW_SCALE);")] * 2,
        ("#if defined(SIMULATOR_DEVICE_EEGO_A4)\nstatic constexpr const char *WINDOW_TITLE",
         "#if defined(SIMULATOR_DEVICE_UT37)\n"
         "static constexpr const char *WINDOW_TITLE = \"Simulator - UT37 (UC8253)\";\n"
         "#elif defined(SIMULATOR_DEVICE_EEGO_A4)\nstatic constexpr const char *WINDOW_TITLE"),
    ])

    edit(src / "HalGPIO.cpp", [
        ("void HalGPIO::begin() {\n#if defined(SIMULATOR_DEVICE_EEGO_A4)",
         "void HalGPIO::begin() {\n#if defined(SIMULATOR_DEVICE_UT37)\n"
         "  _deviceType = DeviceType::X4;\n"
         "  BoardConfig::selectDevice(BoardConfig::Board::Ut37);\n"
         "#elif defined(SIMULATOR_DEVICE_EEGO_A4)"),
        ("  const bool powerOnly = board == BoardConfig::Board::EegoA4 ||",
         "  const bool powerOnly = board == BoardConfig::Board::EegoA4 ||\n"
         "                         board == BoardConfig::Board::Ut37 ||"),
    ])

    edit(src / "HalClock.cpp", [
        ("#if defined(SIMULATOR_DEVICE_X3) || defined(SIMULATOR_DEVICE_X4_PRO) ||",
         "#if defined(SIMULATOR_DEVICE_UT37) ||                                     \\\n"
         "    defined(SIMULATOR_DEVICE_X3) || defined(SIMULATOR_DEVICE_X4_PRO) ||"),
    ])
    edit(src / "HalPowerManager.h", [
        ("  bool onEinkBusyWaitSlice(int8_t, uint8_t) { return false; }",
         "  // SIMULATOR_DEVICE_UT37 light-sleep API. Idle sleep always refuses so the\n"
         "  // main loop keeps polling SDL input. Clock sleep paints the clock once,\n"
         "  // then reports the side key so the loop exits instead of spinning.\n"
         "  enum class LightSleepWakeReason : uint8_t { Timer, PowerButton, Failed };\n"
         "  enum class IdleWake : uint8_t { None, Timer, Input };\n"
         "  IdleWake idleLightSleep(uint32_t) { return IdleWake::None; }\n"
         "  LightSleepWakeReason clockLightSleep(uint32_t) const {\n"
         "    static bool drawn = false;\n"
         "    if (drawn)\n      return LightSleepWakeReason::PowerButton;\n"
         "    drawn = true;\n    return LightSleepWakeReason::Timer;\n  }\n"
         "  bool onEinkBusyWaitSlice(int8_t, uint8_t) { return false; }"),
    ])

    # UT37 main.cpp keeps power-off state in RTC memory across deep sleep. On the
    # host a deep sleep is a fresh process, so plain statics are equivalent.
    attr = src / "esp_attr.h"
    if not attr.exists():
        attr.write_text(
            "#pragma once\n// Host stub (SIMULATOR_DEVICE_UT37 patch).\n"
            "#ifndef IRAM_ATTR\n#define IRAM_ATTR\n#endif\n"
            "#ifndef DRAM_ATTR\n#define DRAM_ATTR\n#endif\n"
            "#ifndef RTC_DATA_ATTR\n#define RTC_DATA_ATTR\n#endif\n"
            "#ifndef RTC_NOINIT_ATTR\n#define RTC_NOINIT_ATTR\n#endif\n",
            encoding="utf-8")
    print("UT37 simulator profile applied")


if __name__ == "__main__":
    main()
