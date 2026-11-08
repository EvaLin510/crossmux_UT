#!/usr/bin/env python3
"""Apply the UT37 board port to a CrossMux checkout (base-independent, idempotent).
Run from the repo root. Edits: platformio.ini (+env), FirmwareBoardTag.cpp, and the
freeink-sdk BoardConfig.h (UT37 profile). Fails loudly if an anchor is missing."""
import pathlib, re, sys

def edit(path, fn):
    p = pathlib.Path(path)
    s = p.read_text(encoding="utf-8")
    t = fn(s)
    if t != s:
        p.write_text(t, encoding="utf-8")
        print("patched", path)
    else:
        print("already ok", path)

def need(s, old, label):
    if old not in s:
        sys.exit(f"ANCHOR NOT FOUND in {label}: {old[:60]!r}")

# 1) platformio.ini
def ini(s):
    if "[env:ut37]" in s:
        return s
    return s.rstrip("\n") + "\n" + pathlib.Path("ut37/ut37.ini").read_text(encoding="utf-8")
edit("platformio.ini", ini)

# 2) board tag
def tag(s):
    if "FREEINK_DEVICE_UT37" in s:
        return s
    old = "#elif FREEINK_DEVICE_MURPHY_M4\n#define CROSSPOINT_BOARD_NAME"
    if old not in s:
        old = "#elif FREEINK_DEVICE_MURPHY\n#define CROSSPOINT_BOARD_NAME"
    need(s, old, "FirmwareBoardTag.cpp")
    return s.replace(old, '#elif FREEINK_DEVICE_UT37\n#define CROSSPOINT_BOARD_NAME "ut37"\n' + old, 1)
edit("src/network/FirmwareBoardTag.cpp", tag)

# 3) SDK BoardConfig.h
PROFILE = '''
// --- UT37 (3.7" 240x416 UC8253, ESP32-S3R8) ---------------------------------
// Pins from SCH_Schematic1_1_2026-08-30. GPIO33-37 belong to the octal PSRAM;
// GPIO19/20 are native USB. Touch is disabled until the controller is confirmed
// on hardware (I2C scan on GPIO12/13, INT on GPIO21).
constexpr BoardProfile UT37 = {
    Board::Ut37,
    "ut37",
    InputStyle::DigitalButtons,
    DisplayController::UC8253,
    416,  // landscape framebuffer, same convention as the Murphy M3 driver
    240,
    {4, 5, 3, 2, 0, 1, PIN_UNASSIGNED},  // sclk, mosi, cs, dc, rst, busy
    0,
    // GPIO42 (TOUCH_WAKE/SD_WAKE) -> R4 -> Q1 gate: Q1 grounds the SD card (SD_GND). Active-HIGH.
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 42, false, 0, true},
    // back, confirm, left, right, up, down, power, powerActiveHigh
    // SIDEKEY=GPIO17 (RTC, wakes from deep sleep) -> power; SIDEKEY1=GPIO46 -> up/function.
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 46, PIN_UNASSIGNED, 17, false},
    14,              // batteryAdc (ADC2_CH3): 330k/100k divider
    PIN_UNASSIGNED,  // charger STAT only drives LED2
    4.3f,            // (330k+100k)/100k
    PIN_UNASSIGNED,
    NO_TOUCH,
    {45, 25000, 10, true},  // LED_PWM
    {AudioOutput::None, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, true,
     PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 18},  // GPIO18 PA_EN/BUZZER (electromagnetic)
    NO_LEDS,
    NO_FLIP,
    {38, 39, 47, 48, 41, 40, 4},  // SDMMC clk, cmd, d0, d1, d2, d3, 4-bit
    NO_GAUGE};

'''
def sdk(s):
    if "FREEINK_DEVICE_UT37" in s:
        return s
    old = "#ifndef FREEINK_DEVICE_DELINK\n#define FREEINK_DEVICE_DELINK 0\n#endif\n"
    need(s, old, "BoardConfig.h flag")
    s = s.replace(old, old + '// UT37: Murphy-family variant; env sets BOTH FREEINK_DEVICE_MURPHY and FREEINK_DEVICE_UT37.\n#ifndef FREEINK_DEVICE_UT37\n#define FREEINK_DEVICE_UT37 0\n#endif\n', 1)
    m = re.search(r"\n  MurphyM4,\n", s)
    need(s, "\n  MurphyM4,\n", "BoardConfig.h enum")
    s = s.replace("\n  MurphyM4,\n", "\n  MurphyM4,\n  Ut37,  // UT37 3.7in reader\n", 1)
    # insert profile right before the de-link comment block
    m = re.search(r"\n// --- de-link", s)
    if not m:
        sys.exit("ANCHOR NOT FOUND in BoardConfig.h: '// --- de-link'")
    s = s[:m.start()] + "\n" + PROFILE.rstrip("\n") + "\n" + s[m.start():]
    old = "#elif FREEINK_DEVICE_MURPHY\nconstexpr BoardProfile DEFAULT_DEVICE = MURPHY_M3;"
    need(s, old, "BoardConfig.h default device")
    s = s.replace(old, "#elif FREEINK_DEVICE_UT37\nconstexpr BoardProfile DEFAULT_DEVICE = UT37;\n" + old, 1)
    old = "#if FREEINK_DEVICE_MURPHY_M4\n    case Board::MurphyM4:"
    need(s, old, "BoardConfig.h selectDevice")
    s = s.replace(old, "#if FREEINK_DEVICE_UT37\n    case Board::Ut37:\n      ACTIVE = UT37;\n      break;\n#endif\n" + old, 1)
    return s
edit("freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h", sdk)

# 4) SDMMC mount: honour sd.powerActiveHigh (the stock sequence assumes an active-LOW gate)
def sdmmc(s):
    if "ACTIVE.sd.powerActiveHigh" in s:
        return s
    old = ("      digitalWrite(sdPwr, HIGH);\n"
           "      delay(80);\n"
           "      digitalWrite(sdPwr, LOW);  // run with the enable held LOW\n")
    need(s, old, "SdmmcBlockDevice.cpp")
    new = ("      // Polarity-aware power cycle: OFF level, wait, then ON level (held while running).\n"
           "      // Active-LOW boards (X4 Pro/X4C) behave exactly as before: HIGH -> LOW.\n"
           "      const bool sdOnHigh = BoardConfig::ACTIVE.sd.powerActiveHigh;\n"
           "      digitalWrite(sdPwr, sdOnHigh ? LOW : HIGH);\n"
           "      delay(80);\n"
           "      digitalWrite(sdPwr, sdOnHigh ? HIGH : LOW);\n")
    return s.replace(old, new, 1)
edit("freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp", sdmmc)

# 5) Diagnostics (UT37 bring-up): when the SD mount fails at boot, wait for a USB serial
#    host (<=6 s), report the SD-power gate state, and retry once so the [SD] lines are
#    visible to the web flasher's serial monitor.
def maindiag(s):
    if "UT37 diag" in s:
        return s
    old = '    LOG_ERR("MAIN", "SD card initialization failed");\n'
    need(s, old, "src/main.cpp")
    new = old + (
        '#if FREEINK_DEVICE_UT37\n'
        '    {  // UT37 diag: capture SD failure details over USB CDC\n'
        '      const unsigned long diagStart = millis();\n'
        '      while (millis() - diagStart < 6000) delay(10);\n'
        '      delay(300);\n'
        '      esp_rom_printf("[UT37] SD gate GPIO%d level=%d (powerActiveHigh=%d)\\n", (int)BoardConfig::ACTIVE.sd.powerEnable,\n'
        '                    digitalRead(BoardConfig::ACTIVE.sd.powerEnable), (int)BoardConfig::ACTIVE.sd.powerActiveHigh);\n'
        '      const bool retryOk = Storage.begin();\n'
        '      esp_rom_printf("[UT37] SD retry %s\\n", retryOk ? "OK" : "FAILED");\n'
        '    }\n'
        '#endif\n')
    return s.replace(old, new, 1)
edit("src/main.cpp", maindiag)

# 6) UT37 external-power detect. No charger-STAT / VBUS pin reaches the MCU, and the SDK's
#    battery.isCharging() infers charging from raw ADC and false-fires when the divider
#    rebounds after wake (89% + charge icon, then 90%). Use USB SOF (definite) plus a slow,
#    debounced ADC trend that cannot report charging until it has >=60 s of settled samples.
UT37_POWER = r'''
#if FREEINK_DEVICE_UT37
// UT37: SOF = host present (definite). Otherwise infer a charger from a slow rising battery
// voltage: samples >=20 s apart, ring of 6, history dropped after a >120 s gap (sleep/wake),
// so nothing can be reported until ~60 s of settled data exists.
static bool ut37ExternalPower() {
  if (usbHostSofActive()) return true;
  constexpr int N = 6;
  constexpr unsigned long STEP_MS = 20000, GAP_RESET_MS = 120000;
  constexpr int RISE_MV = 25, STEP_DIP_MV = 5, UNPLUG_DIP_MV = 15;
  static int mv[N];
  static int count = 0;
  static unsigned long lastSampleMs = 0, lastCallMs = 0;
  static bool latched = false;
  const unsigned long now = millis();
  if (lastCallMs != 0 && now - lastCallMs > GAP_RESET_MS) {  // slept: divider still rebounding
    count = 0;
    latched = false;
    lastSampleMs = 0;
  }
  lastCallMs = now;
  if (lastSampleMs == 0 || now - lastSampleMs >= STEP_MS) {
    int32_t sum = 0;
    int ok = 0;
    for (int i = 0; i < 16; ++i) {
      const int v = analogReadMilliVolts(14);
      if (v > 300) { sum += v; ++ok; }  // ADC2 returns ~0 while Wi-Fi owns it
      delayMicroseconds(200);
    }
    if (ok >= 8) {
      lastSampleMs = now;
      const int battMv = static_cast<int>(sum / ok * 43 / 10);  // 330k/100k divider
      if (count == N) {
        for (int i = 1; i < N; ++i) mv[i - 1] = mv[i];
        count = N - 1;
      }
      mv[count++] = battMv;
      if (count >= 4) {
        bool rising = (mv[count - 1] - mv[0]) >= RISE_MV;
        for (int i = 1; i < count; ++i) rising = rising && (mv[i] - mv[i - 1]) >= -STEP_DIP_MV;
        int peak = mv[0];
        for (int i = 1; i < count; ++i) if (mv[i] > peak) peak = mv[i];
        if (rising) latched = true;
        else if (latched && peak - mv[count - 1] >= UNPLUG_DIP_MV) latched = false;
      }
    }
  }
  return latched;
}
#endif

'''
def gpio_ut37(s):
    if "ut37ExternalPower" in s:
        return s
    old = "#if FREEINK_DEVICE_WAVESHARE_EPAPER_397 || FREEINK_DEVICE_METALIO_EINK4\n// Keep the last positive USB Serial/JTAG SOF"
    need(s, old, "HalGPIO.cpp SOF guard")
    s = s.replace(old, old.replace("METALIO_EINK4", "METALIO_EINK4 || FREEINK_DEVICE_UT37"), 1)
    anchor = "bool HalGPIO::isUsbConnected() const {\n"
    need(s, anchor, "HalGPIO.cpp isUsbConnected")
    s = s.replace(anchor, UT37_POWER.lstrip("\n") + anchor, 1)
    s = s.replace(anchor, anchor + "#if FREEINK_DEVICE_UT37\n  return ut37ExternalPower();\n#endif\n", 1)
    return s
edit("lib/hal/HalGPIO.cpp", gpio_ut37)

# 7) Pull-down from status bar band only
import re
def mim_ut37(s):
    if "UT37 status-bar band" in s:
        return s
    pat = re.compile(r"fui::edgeSwipe\(\s*edge,\s*sx,\s*sy,\s*ex,\s*ey,\s*([^;]*?)\)\s*;", re.S)
    m = pat.search(s)
    if not m:
        sys.exit("ANCHOR NOT FOUND: fui::edgeSwipe(edge, sx, sy, ex, ey, ...) in MappedInputManager.cpp")
    new = ("fui::edgeSwipe(edge, sx, sy, ex, ey, " + m.group(1) + ",\n"
           "      (edge == fui::ScreenEdge::Top) ? 44.0f / static_cast<float>(renderer.getScreenHeight()) : -1.0f);"
           "  // UT37 status-bar band")
    return s[:m.start()] + new + s[m.end():]
edit("src/MappedInputManager.cpp", mim_ut37)

# 8) WeRead: drop bottom-edge swipe-up -> home/back
def weread_home_ut37(s):
    if 'name.rfind("WeRead", 0) != 0' in s:
        return s
    old = "!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()"
    need(s, old, "ActivityManager.cpp wasHomeGesture")
    return s.replace(old, '!currentActivity->isHomeActivity() && currentActivity->name.rfind("WeRead", 0) != 0 &&\n        mappedInput.wasHomeGesture()', 1)
edit("src/activities/ActivityManager.cpp", weread_home_ut37)