#include "HalSystem.h"

#include <BoardConfig.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "driver/temperature_sensor.h"
#include "esp_debug_helpers.h"
#include "esp_mac.h"
#include "esp_private/esp_cpu_internal.h"
#include "esp_private/esp_system_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_timer.h"

#define MAX_PANIC_STACK_DEPTH 32
#define PANIC_CAPTURE_MAGIC 0x50414E49u

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR HalSystem::StackFrame panicStack[MAX_PANIC_STACK_DEPTH];
// RTC_NOINIT is uninitialized on cold boot, so only this exact marker proves a
// panic diagnostic was captured before the reset.
RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker;

// What the device was doing when it reset. Uninitialized on cold boot, so the
// values count only while crashContextMarker holds its magic.
#define CRASH_CONTEXT_MAGIC 0x43525858u
RTC_NOINIT_ATTR uint32_t crashContextMarker;
RTC_NOINIT_ATTR uint32_t crashCpuMhz;
RTC_NOINIT_ATTR uint32_t crashLightSleep;
RTC_NOINIT_ATTR int32_t crashUtcOffsetSeconds;
RTC_NOINIT_ATTR uint32_t crashUtcOffsetKnown;

extern "C" {

void __real_panic_abort(const char* message);
void __real_panic_print_backtrace(const void* frame, int core);

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";
void IRAM_ATTR __wrap_panic_abort(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  // IRAM-safe bounded copy (strncpy is not IRAM-safe in panic context)
  int i = 0;
  for (; i < (int)sizeof(panicMessage) - 1 && message[i]; i++) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_abort(message);
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    __real_panic_print_backtrace(frame, core);
    return;
  }

#if !__riscv
  __real_panic_print_backtrace(frame, core);
  return;
#else
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }

  // Copied from components/esp_system/port/arch/riscv/panic_arch.c
  uint32_t sp = (uint32_t)((RvExcFrame*)frame)->sp;
  const int per_line = 8;
  int depth = 0;
  for (int x = 0; x < 1024; x += per_line * sizeof(uint32_t)) {
    uint32_t* spp = (uint32_t*)(sp + x);
    // panic_print_hex(sp + x);
    // panic_print_str(": ");
    panicStack[depth].sp = sp + x;
    for (int y = 0; y < per_line; y++) {
      // panic_print_str("0x");
      // panic_print_hex(spp[y]);
      // panic_print_str(y == per_line - 1 ? "\r\n" : " ");
      panicStack[depth].spp[y] = spp[y];
    }

    depth++;
    if (depth >= MAX_PANIC_STACK_DEPTH) {
      break;
    }
  }
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_print_backtrace(frame, core);
#endif
}
}

namespace HalSystem {

namespace {
// begin() snapshot of the previous run; later note*() calls overwrite the RTC copy.
struct PreviousRunContext {
  bool valid = false;
  uint32_t cpuMhz = 0;
  bool lightSleep = false;
  bool utcOffsetKnown = false;
  int32_t utcOffsetSeconds = 0;
};
PreviousRunContext previousRun;
esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON (power-on)";
    case ESP_RST_EXT:
      return "EXT (external pin)";
    case ESP_RST_SW:
      return "SW (esp_restart)";
    case ESP_RST_PANIC:
      return "PANIC (exception or abort)";
    case ESP_RST_INT_WDT:
      return "INT_WDT (interrupt watchdog)";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT (task watchdog)";
    case ESP_RST_WDT:
      return "WDT (other watchdog)";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP (deep-sleep wake)";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT (supply voltage drop)";
    case ESP_RST_SDIO:
      return "SDIO";
    case ESP_RST_CPU_LOCKUP:
      return "CPU_LOCKUP (CPU locked up, no handler ran)";
    default:
      return "UNKNOWN";
  }
}

// Wall clock is trusted only after 2024-01-01 (the RTC/NTP has set it).
constexpr time_t MIN_VALID_EPOCH = 1704067200;
}  // namespace

void begin() {
  bootResetReason = esp_reset_reason();
  if (crashContextMarker == CRASH_CONTEXT_MAGIC) {
    previousRun.valid = true;
    previousRun.cpuMhz = crashCpuMhz;
    previousRun.lightSleep = crashLightSleep != 0;
    previousRun.utcOffsetKnown = crashUtcOffsetKnown != 0;
    previousRun.utcOffsetSeconds = crashUtcOffsetSeconds;
  } else {
    crashUtcOffsetKnown = 0;
  }
  crashCpuMhz = getCpuFrequencyMhz();
  crashLightSleep = 0;
  crashContextMarker = CRASH_CONTEXT_MAGIC;

  // On a panic reboot, preserve diagnostics until checkPanic() has tried to write them to the SD card.
  // Ordinary boots clear any stale retained diagnostics.
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    // Panic reboot: preserve logs and panic info, but clamp logHead in case the
    // panic occurred before begin() ever ran (e.g. in a static constructor).
    // If logHead was out of range, logMessages is also garbage — clear it so
    // getLastLogs() does not dump corrupt data into the crash report.
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
  }
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);
    auto file = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      const size_t written = file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      if (written == panicInfo.size()) {
        // Keep the crash data for CrashActivity, but mark it consumed so a
        // later watchdog reset cannot be mistaken for this panic.
        panicCaptureMarker = 0;
        LOG_INF("SYS", "Dumped panic info to SD card");
      } else {
        LOG_ERR("SYS", "Failed to write complete crash report (%zu of %zu bytes)", written, panicInfo.size());
      }
    } else {
      LOG_ERR("SYS", "Failed to open crash_report.txt for writing");
    }
  }
}

void clearPanic() {
  panicCaptureMarker = 0;
  panicMessage[0] = '\0';
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  clearLastLogs();
}

const char* getDeviceModel() { return BoardConfig::ACTIVE.name; }

const char* getChipModel() { return ESP.getChipModel(); }

bool getDeviceId(DeviceId& out) {
  out.fill(0);
  if (esp_efuse_mac_get_default(out.data()) != ESP_OK) {
    LOG_ERR("SYS", "Failed to read eFuse device ID");
    return false;
  }
  return true;
}

bool getWifiStationMac(DeviceId& out) {
  out.fill(0);
  if (esp_read_mac(out.data(), ESP_MAC_WIFI_STA) != ESP_OK) {
    LOG_ERR("SYS", "Failed to read Wi-Fi station MAC address");
    return false;
  }
  return true;
}

bool getChipTemperatureCelsius(float& out) {
  out = 0.0f;
  temperature_sensor_handle_t sensor = nullptr;
  const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

  // ESP-IDF owns two short-lived allocations here (about 112 bytes of payload);
  // its opaque handle has no supported stack/static alternative.
  if (temperature_sensor_install(&config, &sensor) != ESP_OK) {
    LOG_ERR("SYS", "Failed to install chip temperature sensor");
    return false;
  }
  if (temperature_sensor_enable(sensor) != ESP_OK) {
    LOG_ERR("SYS", "Failed to enable chip temperature sensor");
    if (temperature_sensor_uninstall(sensor) != ESP_OK) {
      LOG_ERR("SYS", "Failed to uninstall chip temperature sensor after enable failure");
    }
    return false;
  }

  const bool readOk = temperature_sensor_get_celsius(sensor, &out) == ESP_OK;
  if (!readOk) LOG_ERR("SYS", "Failed to read chip temperature");

  const bool disableOk = temperature_sensor_disable(sensor) == ESP_OK;
  if (!disableOk) LOG_ERR("SYS", "Failed to disable chip temperature sensor");

  const bool uninstallOk = temperature_sensor_uninstall(sensor) == ESP_OK;
  if (!uninstallOk) LOG_ERR("SYS", "Failed to uninstall chip temperature sensor");

  return readOk && disableOk && uninstallOk;
}

uint64_t getUptimeSeconds() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL; }

HeapInfo getHeapInfo() {
  return {static_cast<uint32_t>(ESP.getFreeHeap()), static_cast<uint32_t>(ESP.getHeapSize()),
          static_cast<uint32_t>(ESP.getMaxAllocHeap()), static_cast<uint32_t>(ESP.getFreePsram()),
          static_cast<uint32_t>(ESP.getPsramSize())};
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    std::string info;

    info += "CrossPoint version: " CROSSPOINT_VERSION;
    info += "\n\nReset reason: ";
    info += resetReasonName(bootResetReason);

    // The device restarts straight after the fault, so now minus uptime is the
    // crash time to within the boot duration (about a second).
    info += "\nCrash time (approx.): ";
    const time_t now = time(nullptr);
    if (now >= MIN_VALID_EPOCH) {
      const int32_t offset = previousRun.utcOffsetKnown ? previousRun.utcOffsetSeconds : 0;
      const time_t crashLocal = now - static_cast<time_t>(getUptimeSeconds()) + offset;
      std::tm crashTm{};
      char timeText[48];
      if (gmtime_r(&crashLocal, &crashTm)) {
        const int32_t offsetMinutes = (offset < 0 ? -offset : offset) / 60;
        snprintf(timeText, sizeof(timeText), "%04d-%02d-%02d %02d:%02d:%02d UTC%c%02d:%02d", crashTm.tm_year + 1900,
                 crashTm.tm_mon + 1, crashTm.tm_mday, crashTm.tm_hour, crashTm.tm_min, crashTm.tm_sec,
                 offset < 0 ? '-' : '+', static_cast<int>(offsetMinutes / 60), static_cast<int>(offsetMinutes % 60));
        info += timeText;
      } else {
        info += "unknown";
      }
    } else {
      info += "unknown (clock not set)";
    }

    info += "\nCPU frequency at crash: ";
    info += previousRun.valid ? std::to_string(previousRun.cpuMhz) + " MHz" : "unknown";
    info += "\nIn light sleep at crash: ";
    info += previousRun.valid ? (previousRun.lightSleep ? "yes" : "no") : "unknown";
    info += "\n\nPanic reason: " + std::string(panicMessage);
    info += "\n\nLast logs:\n" + getLastLogs();
    info += "\n\nStack memory:\n";

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
      if (panicStack[i].sp == 0) {
        break;
      }
      info += "0x" + toHex(panicStack[i].sp) + ": ";
      for (size_t j = 0; j < 8; j++) {
        info += "0x" + toHex(panicStack[i].spp[j]) + " ";
      }
      info += "\n";
    }

    return info;
  }
}

void noteCpuFrequency(const uint32_t mhz) { crashCpuMhz = mhz; }

void noteLightSleep(const bool asleep) { crashLightSleep = asleep ? 1 : 0; }

void rememberUtcOffsetSeconds(const int32_t offsetSeconds) {
  crashUtcOffsetSeconds = offsetSeconds;
  crashUtcOffsetKnown = 1;
}

bool isRebootFromPanic() {
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP) {
    return true;
  }

  const bool watchdogReset =
      resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
  return watchdogReset && panicCaptureMarker == PANIC_CAPTURE_MAGIC;
}

}  // namespace HalSystem
