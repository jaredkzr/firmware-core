/**
 * AirGradient Go — BTHome v2 sensor broadcast
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "go_bthome.h"

#include <cmath>

#include "go_settings.h"

// Host test builds have no RTC attribute; define it away.
#if !defined(TEST_HOST) && defined(__has_include)
#if __has_include("esp_attr.h")
#include "esp_attr.h"
#endif
#endif

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

namespace {

// --- AD structures ---
constexpr uint8_t AD_TYPE_FLAGS = 0x01;
constexpr uint8_t AD_TYPE_SERVICE_DATA_16 = 0x16;
// LE General Discoverable + BR/EDR not supported, as BTHome devices send.
constexpr uint8_t AD_FLAGS_VALUE = 0x06;

// --- BTHome v2 service data ---
constexpr uint16_t BTHOME_SERVICE_UUID = 0xFCD2;
// Version 2 (bits 5-7), unencrypted, sent at a regular interval.
constexpr uint8_t BTHOME_DEVICE_INFO = 0x40;

// Object ids (https://bthome.io/format/), in the ascending order they are sent.
constexpr uint8_t OBJ_PACKET_ID = 0x00;   // uint8
constexpr uint8_t OBJ_BATTERY = 0x01;     // uint8, 1 %
constexpr uint8_t OBJ_TEMPERATURE = 0x02; // sint16, 0.01 °C
constexpr uint8_t OBJ_PM25 = 0x0D;        // uint16, 1 µg/m³
constexpr uint8_t OBJ_PM10 = 0x0E;        // uint16, 1 µg/m³
constexpr uint8_t OBJ_CO2 = 0x12;         // uint16, 1 ppm
constexpr uint8_t OBJ_HUMIDITY = 0x2E;    // uint8, 1 %
constexpr uint8_t OBJ_COUNT_U16 = 0x3D;   // uint16 — TVOC index

constexpr float TEMPERATURE_SCALE = 100.0f;
constexpr float PERCENT_MAX = 100.0f;

// Writes little-endian objects and AD structures into a fixed buffer.
class Writer {
public:
  explicit Writer(uint8_t *out) : _out(out) {}

  void u8(uint8_t v) { _out[_len++] = v; }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v & 0xFF));
    u8(static_cast<uint8_t>(v >> 8));
  }
  size_t len() const { return _len; }
  void patch(size_t at, uint8_t v) { _out[at] = v; }

private:
  uint8_t *_out;
  size_t _len = 0;
};

// Rounds a non-negative reading into [0, max].
uint32_t round_clamped(float v, float max) {
  if (!(v > 0.0f)) {
    return 0;
  }
  return static_cast<uint32_t>(std::lround(v < max ? v : max));
}

bool is_gas_index_valid(int index) { return index >= 0 && index <= BTHOME_GAS_INDEX_MAX; }

// Bump when BthomeReadings' layout changes.
constexpr uint32_t RTC_BTHOME_MAGIC = 0x42544802; // "BTH" v2

struct RtcBthome {
  uint32_t magic;
  uint8_t packet_id;
  BthomeReadings readings;
};

// Zero-initialized on every reset except a deep-sleep wake: nothing recorded.
RTC_DATA_ATTR RtcBthome s_rtc_bthome = {};

// --- Scan response ---
constexpr uint8_t AD_TYPE_MANUFACTURER_DATA = 0xFF;

// Bump when BthomeUnits' layout changes.
constexpr uint32_t RTC_BTHOME_UNITS_MAGIC = 0x42545501; // "BTU" v1

struct RtcBthomeUnits {
  uint32_t magic;
  BthomeUnits units;
};

RTC_DATA_ATTR RtcBthomeUnits s_rtc_bthome_units = {};

} // namespace

BthomeReadings bthome_readings_from(const MeasuresAGo &corrected, float battery_pct) {
  BthomeReadings r{};
  if (corrected.temp_hum_a.is_temp_valid()) {
    r.temperature_c = corrected.temp_hum_a.temperature;
  }
  if (corrected.temp_hum_a.is_hum_valid()) {
    r.humidity_pct = corrected.temp_hum_a.humidity;
  }
  if (corrected.pm_a.is_pm_25_valid()) {
    r.pm25_ugm3 = corrected.pm_a.pm_25;
  }
  if (corrected.pm_a.is_pm_10_valid()) {
    r.pm10_ugm3 = corrected.pm_a.pm_10;
  }
  if (corrected.co2.is_valid()) {
    r.co2_ppm = corrected.co2.co2;
  }
  if (corrected.tvoc_nox.is_tvoc_index_valid() &&
      is_gas_index_valid(corrected.tvoc_nox.tvoc_index)) {
    r.tvoc_index = corrected.tvoc_nox.tvoc_index;
  }
  MeasuresPower power{};
  power.battery_percentage = battery_pct;
  if (power.is_battery_percentage_valid()) {
    r.battery_pct = battery_pct;
  }
  return r;
}

size_t encode_bthome_adv(const BthomeReadings &r, uint8_t packet_id, uint8_t *out,
                         size_t out_size) {
  // Every object present takes 29 of the legacy payload's 31 bytes, so a
  // buffer of that size always fits.
  if (out == nullptr || out_size < BTHOME_ADV_MAX_LEN) {
    return 0;
  }

  TempHumData temp_hum{};
  temp_hum.temperature = r.temperature_c;
  temp_hum.humidity = r.humidity_pct;
  PMData pm{};
  pm.pm_25 = r.pm25_ugm3;
  pm.pm_10 = r.pm10_ugm3;
  CO2Data co2{};
  co2.co2 = r.co2_ppm;
  MeasuresPower power{};
  power.battery_percentage = r.battery_pct;

  Writer w(out);
  w.u8(2); // flags AD length
  w.u8(AD_TYPE_FLAGS);
  w.u8(AD_FLAGS_VALUE);

  const size_t service_len_at = w.len();
  w.u8(0); // service data AD length, patched below
  w.u8(AD_TYPE_SERVICE_DATA_16);
  w.u16(BTHOME_SERVICE_UUID);
  w.u8(BTHOME_DEVICE_INFO);

  w.u8(OBJ_PACKET_ID);
  w.u8(packet_id);
  if (power.is_battery_percentage_valid()) {
    w.u8(OBJ_BATTERY);
    w.u8(static_cast<uint8_t>(round_clamped(r.battery_pct, PERCENT_MAX)));
  }
  if (temp_hum.is_temp_valid()) {
    w.u8(OBJ_TEMPERATURE);
    w.u16(static_cast<uint16_t>(
        static_cast<int16_t>(std::lround(r.temperature_c * TEMPERATURE_SCALE))));
  }
  if (pm.is_pm_25_valid()) {
    w.u8(OBJ_PM25);
    w.u16(static_cast<uint16_t>(round_clamped(r.pm25_ugm3, UINT16_MAX)));
  }
  if (pm.is_pm_10_valid()) {
    w.u8(OBJ_PM10);
    w.u16(static_cast<uint16_t>(round_clamped(r.pm10_ugm3, UINT16_MAX)));
  }
  if (co2.is_valid()) {
    w.u8(OBJ_CO2);
    w.u16(static_cast<uint16_t>(r.co2_ppm));
  }
  if (temp_hum.is_hum_valid()) {
    w.u8(OBJ_HUMIDITY);
    w.u8(static_cast<uint8_t>(round_clamped(r.humidity_pct, PERCENT_MAX)));
  }
  if (is_gas_index_valid(r.tvoc_index)) {
    w.u8(OBJ_COUNT_U16);
    w.u16(static_cast<uint16_t>(r.tvoc_index));
  }

  // AD length counts the type byte and everything after it.
  w.patch(service_len_at, static_cast<uint8_t>(w.len() - service_len_at - 1));
  return w.len();
}

BthomeUnits bthome_units_from(const GoSettings &settings) {
  BthomeUnits units{};
  units.fahrenheit = settings.use_fahrenheit;
  units.pm_us_aqi = settings.pm_use_usaqi;
  units.altitude_feet = settings.use_feet;
  return units;
}

size_t encode_bthome_scan_rsp(const BthomeUnits &units, uint8_t *out, size_t out_size) {
  if (out == nullptr || out_size < BTHOME_SCAN_RSP_LEN) {
    return 0;
  }
  uint8_t flags = 0;
  if (units.fahrenheit) {
    flags |= BTHOME_UNITS_FLAG_FAHRENHEIT;
  }
  if (units.pm_us_aqi) {
    flags |= BTHOME_UNITS_FLAG_PM_US_AQI;
  }
  if (units.altitude_feet) {
    flags |= BTHOME_UNITS_FLAG_ALTITUDE_FEET;
  }
  Writer w(out);
  w.u8(BTHOME_SCAN_RSP_LEN - 1); // AD length counts the type byte and after
  w.u8(AD_TYPE_MANUFACTURER_DATA);
  w.u16(BTHOME_UNITS_COMPANY_ID);
  w.u8(BTHOME_UNITS_VERSION);
  w.u8(flags);
  return w.len();
}

bool set_rtc_bthome_units(const BthomeUnits &units) {
  const bool changed =
      s_rtc_bthome_units.magic != RTC_BTHOME_UNITS_MAGIC || s_rtc_bthome_units.units != units;
  s_rtc_bthome_units.units = units;
  s_rtc_bthome_units.magic = RTC_BTHOME_UNITS_MAGIC;
  return changed;
}

bool load_rtc_bthome_units(BthomeUnits &units) {
  if (s_rtc_bthome_units.magic != RTC_BTHOME_UNITS_MAGIC) {
    return false;
  }
  units = s_rtc_bthome_units.units;
  return true;
}

void save_rtc_bthome(const BthomeReadings &readings) {
  // A fresh record starts at packet id 0; later ones count up, wrapping.
  const bool had_record = s_rtc_bthome.magic == RTC_BTHOME_MAGIC;
  s_rtc_bthome.packet_id = had_record ? static_cast<uint8_t>(s_rtc_bthome.packet_id + 1) : 0;
  s_rtc_bthome.readings = readings;
  s_rtc_bthome.magic = RTC_BTHOME_MAGIC;
}

bool load_rtc_bthome(BthomeReadings &readings, uint8_t &packet_id) {
  if (s_rtc_bthome.magic != RTC_BTHOME_MAGIC) {
    return false;
  }
  readings = s_rtc_bthome.readings;
  packet_id = s_rtc_bthome.packet_id;
  return true;
}

void clear_rtc_bthome() {
  s_rtc_bthome.magic = 0;
  s_rtc_bthome_units.magic = 0;
}
