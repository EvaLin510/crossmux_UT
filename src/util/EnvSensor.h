#pragma once

// Ambient temperature / humidity for faces that show it (Standby desk clock).
// The default implementation (EnvSensor.cpp) is a weak stub that reports "no
// sensor"; a board driver (UT37: AHT30 on I2C 0x38) provides the real one.
namespace env_sensor {

// True with fresh values when a sensor answered; false leaves both untouched.
bool read(float& celsius, float& humidityPercent);

}  // namespace env_sensor
