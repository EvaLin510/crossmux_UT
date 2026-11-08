#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

struct HeapInfo {
  uint32_t freeBytes;
  uint32_t totalBytes;
  uint32_t largestFreeBlockBytes;
  uint32_t freePsramBytes;
  uint32_t totalPsramBytes;
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

using DeviceId = std::array<uint8_t, 6>;
const char* getDeviceModel();
const char* getChipModel();
bool getDeviceId(DeviceId& out);
bool getWifiStationMac(DeviceId& out);
bool getChipTemperatureCelsius(float& out);
uint64_t getUptimeSeconds();
HeapInfo getHeapInfo();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();

// Crash-report context, kept in RTC memory across a panic reset and read back
// once by begin(). Cheap enough to call on every clock change or sleep.
void noteCpuFrequency(uint32_t mhz);
void noteLightSleep(bool asleep);
// Local-time offset for the report's crash time; settings live on the SD card,
// which is not loaded yet when the report is written.
void rememberUtcOffsetSeconds(int32_t offsetSeconds);
}  // namespace HalSystem
