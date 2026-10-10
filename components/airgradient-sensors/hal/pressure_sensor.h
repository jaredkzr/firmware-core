/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef PRESSURE_SENSOR_H
#define PRESSURE_SENSOR_H

#include "measures_types.h"

class PressureSensor {
public:
  virtual ~PressureSensor() = default;

  virtual bool init() = 0;
  virtual bool read(PressureData &out) = 0;

  virtual bool supports_temp_hum() const { return false; }
  virtual TempHumData temp_hum_data() = 0;

  /// Stop background measurement and leave the sensor in standby before a
  /// cold ESP deep sleep. The next init() must restart measurement. Default
  /// no-op that succeeds, for sensors without a standby mode. Returns false
  /// only when the standby command failed.
  virtual bool standby() { return true; }

private:
};

#endif // !PRESSURE_SENSOR_H
