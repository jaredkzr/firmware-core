/**
 * Tests for the Home chart history (time buckets kept across deep sleep).
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "go_chart_history.h"

namespace {

constexpr uint32_t B = CHART_HISTORY_BUCKET_S;
constexpr uint32_t T0 = 100 * B; // bucket-aligned start time

MeasuresAGo pm(float value) {
  MeasuresAGo m{};
  m.pm_a.pm_25 = value;
  return m;
}

} // namespace

TEST_CASE("ChartHistory", "[ChartHistory]") {
  ChartHistory h{};
  h.clear();
  float out[CHART_HISTORY_BUCKETS];

  SECTION("empty history reads nothing") {
    CHECK(h.read(ChartSeries::Pm25, T0, 0, out, CHART_HISTORY_BUCKETS).count == 0);
  }

  SECTION("samples in one bucket are averaged") {
    h.record(pm(10.0f), T0);
    h.record(pm(20.0f), T0 + 30);
    h.record(pm(30.0f), T0 + 60);
    const auto view = h.read(ChartSeries::Pm25, T0 + 70, 0, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == 1);
    CHECK(out[0] == Catch::Approx(20.0f));
    CHECK(view.oldest_age_s == 70); // first sample, not the bucket start
    CHECK(view.newest_age_s == 10);
  }

  SECTION("values keep their fixed-point precision") {
    MeasuresAGo m{};
    m.temp_hum_a.temperature = 21.37f;
    m.temp_hum_a.humidity = 45.5f;
    m.pressure = {1013.2f, 85.0f};
    m.co2.co2 = 612;
    h.record(m, T0);
    REQUIRE(h.read(ChartSeries::Temp, T0, 0, out, 1).count == 1);
    CHECK(out[0] == Catch::Approx(21.37f).margin(0.005f));
    REQUIRE(h.read(ChartSeries::Pressure, T0, 0, out, 1).count == 1);
    CHECK(out[0] == Catch::Approx(1013.2f).margin(0.05f));
    REQUIRE(h.read(ChartSeries::Co2, T0, 0, out, 1).count == 1);
    CHECK(out[0] == Catch::Approx(612.0f));
    // Fields not measured have no history.
    CHECK(h.read(ChartSeries::Tvoc, T0, 0, out, 1).count == 0);
  }

  SECTION("short gaps are bridged, long gaps stay NaN") {
    h.record(pm(10.0f), T0);
    h.record(pm(40.0f), T0 + 3 * B); // two empty buckets between
    auto view = h.read(ChartSeries::Pm25, T0 + 3 * B, 3 * B, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == 4);
    CHECK(out[1] == Catch::Approx(20.0f));
    CHECK(out[2] == Catch::Approx(30.0f));

    view = h.read(ChartSeries::Pm25, T0 + 3 * B, 2 * B, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == 4);
    CHECK(std::isnan(out[1]));
    CHECK(std::isnan(out[2]));
  }

  SECTION("the window covers the last four hours") {
    for (uint32_t i = 0; i < CHART_HISTORY_BUCKETS + 30; ++i) {
      h.record(pm(static_cast<float>(i)), T0 + i * B);
    }
    const uint32_t now = T0 + (CHART_HISTORY_BUCKETS + 29) * B;
    const auto view = h.read(ChartSeries::Pm25, now, 0, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == CHART_HISTORY_BUCKETS);
    CHECK(out[0] == Catch::Approx(30.0f));
    CHECK(out[CHART_HISTORY_BUCKETS - 1] == Catch::Approx(CHART_HISTORY_BUCKETS + 29.0f));
    CHECK(view.oldest_age_s == (CHART_HISTORY_BUCKETS - 1) * B);
    CHECK(view.newest_age_s == 0);
  }

  SECTION("samples age out of the window while no new ones arrive") {
    h.record(pm(10.0f), T0);
    h.record(pm(20.0f), T0 + 10 * B);
    // Read 4 h after the first sample: only the second is still in range.
    const auto view =
        h.read(ChartSeries::Pm25, T0 + CHART_HISTORY_SPAN_S, 0, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == 1);
    CHECK(out[0] == Catch::Approx(20.0f));
    CHECK(
        h.read(ChartSeries::Pm25, T0 + 10 * B + CHART_HISTORY_SPAN_S, 0, out, CHART_HISTORY_BUCKETS)
            .count == 0);
  }

  SECTION("a smaller buffer keeps the newest buckets") {
    for (uint32_t i = 0; i < 10; ++i) {
      h.record(pm(static_cast<float>(i)), T0 + i * B);
    }
    const auto view = h.read(ChartSeries::Pm25, T0 + 9 * B, 0, out, 4);
    REQUIRE(view.count == 4);
    CHECK(out[0] == Catch::Approx(6.0f));
    CHECK(out[3] == Catch::Approx(9.0f));
  }

  SECTION("a jump past the whole span keeps only the new sample") {
    h.record(pm(10.0f), T0);
    h.record(pm(50.0f), T0 + CHART_HISTORY_SPAN_S + B);
    const auto view =
        h.read(ChartSeries::Pm25, T0 + CHART_HISTORY_SPAN_S + B, 0, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == 1);
    CHECK(out[0] == Catch::Approx(50.0f));
  }

  SECTION("a clock that moved backwards restarts the history") {
    h.record(pm(10.0f), T0 + 5 * B);
    h.record(pm(30.0f), T0);
    const auto view = h.read(ChartSeries::Pm25, T0, 0, out, CHART_HISTORY_BUCKETS);
    REQUIRE(view.count == 1);
    CHECK(out[0] == Catch::Approx(30.0f));
    // Reading with a clock behind the newest sample returns nothing.
    CHECK(h.read(ChartSeries::Pm25, T0 - 1, 0, out, CHART_HISTORY_BUCKETS).count == 0);
  }

  SECTION("gas index values above 500 are not charted") {
    MeasuresAGo m{};
    m.tvoc_nox.tvoc_index = 29806;
    m.tvoc_nox.nox_index = 1;
    h.record(m, T0);
    CHECK(h.read(ChartSeries::Tvoc, T0, 0, out, 1).count == 0);
    CHECK(h.read(ChartSeries::Nox, T0, 0, out, 1).count == 1);
  }

  SECTION("clear empties the history") {
    h.record(pm(10.0f), T0);
    h.clear();
    CHECK(h.read(ChartSeries::Pm25, T0, 0, out, CHART_HISTORY_BUCKETS).count == 0);
  }
}

TEST_CASE("RTC chart history starts empty", "[ChartHistory]") {
  float out[1];
  CHECK(rtc_chart_history().read(ChartSeries::Pm25, T0, 0, out, 1).count == 0);
}
