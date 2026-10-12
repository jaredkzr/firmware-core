/**
 * Tests for the BTHome v2 sensor broadcast encoder and its RTC record.
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include <catch2/catch_test_macros.hpp>

#include "go_bthome.h"

#include <cmath>
#include <vector>

namespace {

std::vector<uint8_t> encode(const BthomeReadings &r, uint8_t packet_id) {
  uint8_t buf[BTHOME_ADV_MAX_LEN];
  const size_t len = encode_bthome_adv(r, packet_id, buf, sizeof(buf));
  return std::vector<uint8_t>(buf, buf + len);
}

BthomeReadings full_readings() {
  BthomeReadings r{};
  r.battery_pct = 87.4f;
  r.temperature_c = 23.456f;
  r.pm25_ugm3 = 12.6f;
  r.co2_ppm = 615;
  r.humidity_pct = 45.5f;
  r.tvoc_index = 120;
  r.nox_index = 1;
  return r;
}

} // namespace

TEST_CASE("BTHome: all readings fill the legacy payload exactly", "[BTHome]") {
  const std::vector<uint8_t> expected = {
      0x02, 0x01, 0x06,             // flags
      0x1B, 0x16, 0xD2, 0xFC, 0x40, // service data, UUID 0xFCD2, v2 unencrypted
      0x00, 0x07,                   // packet id
      0x01, 0x57,                   // battery 87 %
      0x02, 0x2A, 0x09,             // temperature 2346 x 0.01 °C
      0x0D, 0x0D, 0x00,             // PM2.5 13 µg/m³
      0x12, 0x67, 0x02,             // CO2 615 ppm
      0x2E, 0x2E,                   // humidity 46 %
      0x3D, 0x78, 0x00,             // count uint16: TVOC index 120
      0x3E, 0x01, 0x00, 0x00, 0x00, // count uint32: NOx index 1
  };
  const auto adv = encode(full_readings(), 7);
  CHECK(adv == expected);
  CHECK(adv.size() == BTHOME_ADV_MAX_LEN);
}

TEST_CASE("BTHome: invalid readings are omitted", "[BTHome]") {
  const std::vector<uint8_t> expected = {
      0x02, 0x01, 0x06,             //
      0x06, 0x16, 0xD2, 0xFC, 0x40, // service data length covers header + packet id
      0x00, 0xFF,                   // packet id only
  };
  CHECK(encode(BthomeReadings{}, 0xFF) == expected);
}

TEST_CASE("BTHome: NOx keeps its own object without TVOC", "[BTHome]") {
  BthomeReadings r{};
  r.nox_index = 3;
  const auto adv = encode(r, 0);
  REQUIRE(adv.size() == 15);
  CHECK(adv[3] == 0x0B);
  CHECK(adv[10] == 0x3E);
  CHECK(adv[11] == 0x03);
}

TEST_CASE("BTHome: negative temperature is two's complement", "[BTHome]") {
  BthomeReadings r{};
  r.temperature_c = -5.25f;
  const auto adv = encode(r, 0);
  REQUIRE(adv.size() == 13);
  CHECK(adv[10] == 0x02);
  CHECK(static_cast<int16_t>(adv[11] | (adv[12] << 8)) == -525);
}

TEST_CASE("BTHome: large PM2.5 saturates", "[BTHome]") {
  BthomeReadings r{};
  r.pm25_ugm3 = 70000.0f;
  const auto adv = encode(r, 0);
  REQUIRE(adv.size() == 13);
  CHECK(adv[10] == 0x0D);
  CHECK(adv[11] == 0xFF);
  CHECK(adv[12] == 0xFF);
}

TEST_CASE("BTHome: buffer smaller than a legacy payload is rejected", "[BTHome]") {
  uint8_t buf[BTHOME_ADV_MAX_LEN - 1];
  CHECK(encode_bthome_adv(full_readings(), 0, buf, sizeof(buf)) == 0);
  CHECK(encode_bthome_adv(full_readings(), 0, nullptr, BTHOME_ADV_MAX_LEN) == 0);
}

TEST_CASE("BTHome: readings take only validated fields", "[BTHome]") {
  MeasuresAGo m{};
  m.temp_hum_a.temperature = 21.5f;
  m.temp_hum_a.humidity = 150.0f; // out of range
  m.pm_a.pm_25 = 4.0f;
  m.co2.co2 = 20000; // out of range
  m.tvoc_nox.tvoc_index = 100;
  m.tvoc_nox.nox_index = 25000; // raw ticks, not an index

  const BthomeReadings r = bthome_readings_from(m, 64.0f);
  CHECK(r.temperature_c == 21.5f);
  CHECK(r.humidity_pct == MeasuresInvalid::HUMIDITY);
  CHECK(r.pm25_ugm3 == 4.0f);
  CHECK(r.co2_ppm == MeasuresInvalid::CO2);
  CHECK(r.tvoc_index == 100);
  CHECK(r.nox_index == MeasuresInvalid::NOX);
  CHECK(r.battery_pct == 64.0f);

  CHECK(bthome_readings_from(m, MeasuresInvalid::BATTERY_PERCENT).battery_pct ==
        MeasuresInvalid::BATTERY_PERCENT);
  CHECK(bthome_readings_from(m, NAN).battery_pct == MeasuresInvalid::BATTERY_PERCENT);
}

TEST_CASE("BTHome: RTC record advances the packet id per measurement", "[BTHome]") {
  clear_rtc_bthome();
  BthomeReadings r{};
  uint8_t packet_id = 0;
  CHECK_FALSE(load_rtc_bthome(r, packet_id));

  BthomeReadings first{};
  first.co2_ppm = 500;
  save_rtc_bthome(first);
  REQUIRE(load_rtc_bthome(r, packet_id));
  CHECK(packet_id == 0);
  CHECK(r.co2_ppm == 500);

  // A wake that only re-broadcasts loads the same id.
  REQUIRE(load_rtc_bthome(r, packet_id));
  CHECK(packet_id == 0);

  BthomeReadings second{};
  second.co2_ppm = 510;
  save_rtc_bthome(second);
  REQUIRE(load_rtc_bthome(r, packet_id));
  CHECK(packet_id == 1);
  CHECK(r.co2_ppm == 510);

  for (int i = 0; i < 255; ++i) {
    save_rtc_bthome(second);
  }
  REQUIRE(load_rtc_bthome(r, packet_id));
  CHECK(packet_id == 0); // wraps

  clear_rtc_bthome();
  CHECK_FALSE(load_rtc_bthome(r, packet_id));
}
