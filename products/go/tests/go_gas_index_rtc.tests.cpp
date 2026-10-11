/**
 * Tests for the gas-index algorithm state kept across deep sleep.
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include <catch2/catch_test_macros.hpp>

#include "go_gas_index_rtc.h"
#include "go_types.h"
#include "hal/tvoc_nox_sensor.h"

namespace {

class FakeTvocNox : public TVOCNOxSensor {
public:
  bool init() override { return true; }
  bool read(TVOCNOxData &out) override {
    out = {MeasuresInvalid::TVOC, 25000, MeasuresInvalid::NOX, 18000};
    return true;
  }
};

/// A configured algorithm whose state is recognisable after a round trip.
TvocNoxIndexState saved_state(SensorManager &sm) {
  REQUIRE(sm.configure_tvoc_nox_index(10000));
  TvocNoxIndexState state{};
  REQUIRE(sm.get_tvoc_nox_index_state(state));
  state.voc.m_Mean_Variance_Estimator___Mean = 1.25f;
  state.nox.m_Mean_Variance_Estimator___Mean = 2.5f;
  return state;
}

} // namespace

TEST_CASE("RTC gas-index state", "[GasIndexRtc]") {
  FakeTvocNox sensor;
  Sensors sensors{};
  sensors.tvoc_nox = &sensor;
  clear_rtc_gas_index();

  SECTION("load returns the saved state and its age") {
    SensorManager sm(sensors);
    const TvocNoxIndexState state = saved_state(sm);
    save_rtc_gas_index(state, 1000);

    TvocNoxIndexState loaded{};
    uint32_t age_s = 0;
    REQUIRE(load_rtc_gas_index(1060, loaded, age_s));
    CHECK(age_s == 60);
    CHECK(loaded.voc.m_Mean_Variance_Estimator___Mean == 1.25f);
    CHECK(loaded.nox.m_Mean_Variance_Estimator___Mean == 2.5f);
  }

  SECTION("load rejects missing, stale, future, and unknown-time state") {
    TvocNoxIndexState loaded{};
    uint32_t age_s = 0;
    CHECK_FALSE(load_rtc_gas_index(1000, loaded, age_s));

    SensorManager sm(sensors);
    save_rtc_gas_index(saved_state(sm), 1000);
    CHECK(load_rtc_gas_index(1000 + GAS_INDEX_RESUME_MAX_GAP_S, loaded, age_s));
    CHECK_FALSE(load_rtc_gas_index(1001 + GAS_INDEX_RESUME_MAX_GAP_S, loaded, age_s));
    CHECK_FALSE(load_rtc_gas_index(999, loaded, age_s));
    CHECK_FALSE(load_rtc_gas_index(TRACKING_TIME_INVALID_S, loaded, age_s));

    clear_rtc_gas_index();
    CHECK_FALSE(load_rtc_gas_index(1000, loaded, age_s));
  }

  SECTION("a duty-cycled sample resumes retimed to the real gap") {
    SensorManager previous(sensors);
    save_rtc_gas_index(saved_state(previous), 1000);

    SensorManager sm(sensors);
    REQUIRE(resume_gas_index_for_sample(sm, 1075, 60000, 12, 12));
    TvocNoxIndexState state{};
    REQUIRE(sm.get_tvoc_nox_index_state(state));
    CHECK(state.voc.mSamplingInterval == 75.0f);
    CHECK(state.voc.m_Mean_Variance_Estimator___Mean == 1.25f);
  }

  SECTION("a duty-cycled sample with nothing saved starts fresh at the interval") {
    SensorManager sm(sensors);
    REQUIRE(resume_gas_index_for_sample(sm, 1000, 60000, 12, 12));
    TvocNoxIndexState state{};
    REQUIRE(sm.get_tvoc_nox_index_state(state));
    CHECK(state.voc.mSamplingInterval == 60.0f);
    CHECK(state.voc.mUptime == 0.0f);
  }

  SECTION("the sampler resumes only into an unconfigured algorithm") {
    SensorManager previous(sensors);
    save_rtc_gas_index(saved_state(previous), 1000);

    SensorManager sm(sensors);
    REQUIRE(resume_gas_index_for_sampler(sm, 1030));
    TvocNoxIndexState state{};
    REQUIRE(sm.get_tvoc_nox_index_state(state));
    CHECK(state.voc.mSamplingInterval == 10.0f);
    CHECK(state.voc.m_Mean_Variance_Estimator___Mean == 1.25f);

    // A promoted fast path already holds the newer algorithm.
    CHECK_FALSE(resume_gas_index_for_sampler(sm, 1030));

    SensorManager stale(sensors);
    CHECK_FALSE(resume_gas_index_for_sampler(stale, 1001 + GAS_INDEX_RESUME_MAX_GAP_S));
    CHECK_FALSE(stale.is_tvoc_nox_index_configured());
  }

  SECTION("no TVOC/NOx sensor leaves the manager unconfigured") {
    Sensors none{};
    SensorManager sm(none);
    CHECK_FALSE(resume_gas_index_for_sample(sm, 1000, 60000, 12, 12));
    CHECK_FALSE(resume_gas_index_for_sampler(sm, 1000));
  }
}
