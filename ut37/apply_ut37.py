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
        '      while (!Serial && millis() - diagStart < 6000) delay(10);\n'
        '      delay(300);\n'
        '      char logBuf[96];\n'
        '      snprintf(logBuf, sizeof(logBuf), "[UT37] SD gate GPIO%d level=%d (powerActiveHigh=%d)\\n", (int)BoardConfig::ACTIVE.sd.powerEnable,\n'
        '                    digitalRead(BoardConfig::ACTIVE.sd.powerEnable), (int)BoardConfig::ACTIVE.sd.powerActiveHigh);\n'
        '      Serial.print(logBuf);\n'
        '      const bool retryOk = Storage.begin();\n'
        '      snprintf(logBuf, sizeof(logBuf), "[UT37] SD retry %s\\n", retryOk ? "OK" : "FAILED");\n'
        '      Serial.print(logBuf);\n'
        '    }\n'
        '#endif\n')
    return s.replace(old, new, 1)
edit("src/main.cpp", maindiag)
