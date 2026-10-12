/**
 * AirGradient Go — BTHome v2 sensor broadcast
 *
 * Encodes the latest readings as a BTHome v2 advertisement (https://bthome.io)
 * that BleService sends as a non-connectable broadcast next to the connectable
 * data-service advertisement. Passive scanners (Home Assistant, ESPHome
 * Bluetooth proxies, Theengs) decode it natively, so a listener can record
 * measurements without connecting and keeping the device awake.
 *
 * The readings live in RTC memory with the BTHome packet id, so connect-window
 * wakes during Portable Sleep re-broadcast the last measurement under the same
 * packet id (receivers drop the repeats) without measuring again.
 *
 * The broadcast is scannable: its scan response carries the display units the
 * user picked on the Go (manufacturer data, see encode_bthome_scan_rsp()), so
 * an app can show readings the way the Go does. The BTHome payload itself is
 * full and stays in the units BTHome defines.
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef GO_BTHOME_H
#define GO_BTHOME_H

#include <cstddef>
#include <cstdint>

#include "measures_types.h"

struct GoSettings;

/// Legacy advertising payload limit; the broadcast is a legacy PDU.
inline constexpr size_t BTHOME_ADV_MAX_LEN = 31;

/// Highest valid TVOC/NOx index (Sensirion gas index range 1-500).
inline constexpr int BTHOME_GAS_INDEX_MAX = 500;

/// Readings carried by the broadcast, as shown on the display (corrections
/// applied). Fields start at invalid sentinels; invalid fields are omitted
/// from the advertisement.
struct BthomeReadings {
  float temperature_c = MeasuresInvalid::TEMPERATURE;
  float humidity_pct = MeasuresInvalid::HUMIDITY;
  float pm25_ugm3 = MeasuresInvalid::PM;
  int co2_ppm = MeasuresInvalid::CO2;
  int tvoc_index = MeasuresInvalid::TVOC;
  int nox_index = MeasuresInvalid::NOX;
  float battery_pct = MeasuresInvalid::BATTERY_PERCENT;
};

/// Display units chosen on the Go (GoSettings use_fahrenheit, pm_use_usaqi,
/// use_feet). Presentation only: the BTHome payload keeps BTHome's units.
struct BthomeUnits {
  bool fahrenheit = false;
  bool pm_us_aqi = false;
  bool altitude_feet = false;

  bool operator==(const BthomeUnits &other) const {
    return fahrenheit == other.fahrenheit && pm_us_aqi == other.pm_us_aqi &&
           altitude_feet == other.altitude_feet;
  }
  bool operator!=(const BthomeUnits &other) const { return !(*this == other); }
};

/// The display units from the Go's settings.
BthomeUnits bthome_units_from(const GoSettings &settings);

/// Manufacturer data in the scan response: company id 0xFFFF (no company id
/// assigned), then a format version and a flag byte.
inline constexpr uint16_t BTHOME_UNITS_COMPANY_ID = 0xFFFF;
inline constexpr uint8_t BTHOME_UNITS_VERSION = 1;
inline constexpr uint8_t BTHOME_UNITS_FLAG_FAHRENHEIT = 0x01;
inline constexpr uint8_t BTHOME_UNITS_FLAG_PM_US_AQI = 0x02;
inline constexpr uint8_t BTHOME_UNITS_FLAG_ALTITUDE_FEET = 0x04;
/// Length of the scan response encode_bthome_scan_rsp() writes.
inline constexpr size_t BTHOME_SCAN_RSP_LEN = 6;

/// Readings from corrected measures plus the battery level, which the
/// measurement paths poll separately from the sensors.
BthomeReadings bthome_readings_from(const MeasuresAGo &corrected, float battery_pct);

/// Encode a complete BTHome v2 advertisement (flags + service data) into
/// @p out. Objects appear in ascending object-id order as the format requires:
/// packet id, battery, temperature, PM2.5, CO2, humidity, TVOC index (count
/// uint16) and NOx index (count uint32, a different object id so the two never
/// swap places when one is missing). Returns the length, or 0 when @p out_size
/// is smaller than BTHOME_ADV_MAX_LEN. Pure logic; testable on host.
size_t encode_bthome_adv(const BthomeReadings &readings, uint8_t packet_id, uint8_t *out,
                         size_t out_size);

/// Encode the scan response: one manufacturer-specific AD structure
/// [len 5, type 0xFF, company id 0xFFFF (LE), version, flags]. Returns
/// BTHOME_SCAN_RSP_LEN, or 0 when @p out_size is smaller. Pure logic.
size_t encode_bthome_scan_rsp(const BthomeUnits &units, uint8_t *out, size_t out_size);

/// Record the Go's display units for the scan response. Kept in RTC memory
/// apart from the measurement, so it survives deep sleep and can change
/// without advancing the packet id. Returns true when the units changed.
bool set_rtc_bthome_units(const BthomeUnits &units);

/// Load the recorded display units. False when none have been recorded since
/// the last reset other than a deep-sleep wake.
bool load_rtc_bthome_units(BthomeUnits &units);

/// Record a new measurement for the broadcast and advance the packet id.
void save_rtc_bthome(const BthomeReadings &readings);

/// Load the last recorded measurement and its packet id. False when nothing
/// has been recorded since the last reset other than a deep-sleep wake.
bool load_rtc_bthome(BthomeReadings &readings, uint8_t &packet_id);

/// Forget the recorded measurement and units (host tests).
void clear_rtc_bthome();

#endif // GO_BTHOME_H
