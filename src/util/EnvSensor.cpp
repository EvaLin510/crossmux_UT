#include "EnvSensor.h"

namespace env_sensor {

// Weak so a board driver can define the real read() without touching callers.
__attribute__((weak)) bool read(float& celsius, float& humidityPercent) {
  (void)celsius;
  (void)humidityPercent;
  return false;
}

}  // namespace env_sensor
