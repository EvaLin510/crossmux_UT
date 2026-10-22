// UT37 ambient sensor: AHT30 (I2C 0x38) on the shared touch/RTC bus (GPIO13/12).
// Overrides the weak env_sensor::read() stub in EnvSensor.cpp.
#if FREEINK_DEVICE_UT37

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <Wire.h>

#include "EnvSensor.h"

namespace env_sensor {
namespace {

constexpr uint8_t AHT30_ADDR = 0x38;
constexpr uint8_t AHT30_STATUS_BUSY = 0x80;
constexpr unsigned long MEASURE_WAIT_MS = 80;     // datasheet: >= 75 ms per conversion
constexpr unsigned long CACHE_MS = 30000;         // ambient values change slowly
constexpr unsigned long RETRY_AFTER_FAIL_MS = 60000;

float cachedCelsius = 0.0f;
float cachedHumidity = 0.0f;
bool cacheValid = false;
unsigned long lastAttemptMs = 0;

// CRC-8, polynomial 0x31, init 0xFF (AHT2x/AHT30 datasheet).
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : crc << 1;
  }
  return crc;
}

bool measure(float& celsius, float& humidity) {
  const auto& s = BoardConfig::ACTIVE.sensors;
  Wire.begin(s.i2cSda, s.i2cScl, s.i2cHz);  // no-op when the touch driver already owns the bus

  Wire.beginTransmission(AHT30_ADDR);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  delay(MEASURE_WAIT_MS);

  uint8_t frame[7] = {};
  if (Wire.requestFrom(AHT30_ADDR, static_cast<uint8_t>(sizeof(frame))) != sizeof(frame)) return false;
  for (uint8_t& b : frame) b = static_cast<uint8_t>(Wire.read());
  if ((frame[0] & AHT30_STATUS_BUSY) != 0 || crc8(frame, 6) != frame[6]) return false;

  const uint32_t rawHumidity = (static_cast<uint32_t>(frame[1]) << 12) | (static_cast<uint32_t>(frame[2]) << 4) |
                               (frame[3] >> 4);
  const uint32_t rawTemperature =
      (static_cast<uint32_t>(frame[3] & 0x0F) << 16) | (static_cast<uint32_t>(frame[4]) << 8) | frame[5];
  humidity = static_cast<float>(rawHumidity) * 100.0f / 1048576.0f;
  celsius = static_cast<float>(rawTemperature) * 200.0f / 1048576.0f - 50.0f;
  return true;
}

}  // namespace

bool read(float& celsius, float& humidityPercent) {
  const unsigned long now = millis();
  const unsigned long wait = cacheValid ? CACHE_MS : RETRY_AFTER_FAIL_MS;
  if (lastAttemptMs == 0 || now - lastAttemptMs >= wait) {
    lastAttemptMs = now == 0 ? 1 : now;
    float c = 0.0f;
    float h = 0.0f;
    if (measure(c, h)) {
      cachedCelsius = c;
      cachedHumidity = h;
      cacheValid = true;
    } else {
      LOG_DBG("ENV", "AHT30 read failed");
      cacheValid = false;
    }
  }
  if (!cacheValid) return false;
  celsius = cachedCelsius;
  humidityPercent = cachedHumidity;
  return true;
}

}  // namespace env_sensor

#endif  // FREEINK_DEVICE_UT37
