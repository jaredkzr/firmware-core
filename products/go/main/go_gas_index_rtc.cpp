/**
 * AirGradient Go — gas-index algorithm state across deep sleep
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "go_gas_index_rtc.h"

#include "ag_log.h"
#include "go_types.h"

// Host test builds have no RTC attribute; define it away.
#if !defined(TEST_HOST) && defined(__has_include)
#if __has_include("esp_attr.h")
#include "esp_attr.h"
#endif
#endif

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

static constexpr const char *TAG = "GasIndexRtc";

namespace {

// Bump when TvocNoxIndexState's layout changes.
constexpr uint32_t RTC_GAS_INDEX_MAGIC = 0x47494401; // "GID" v1

struct RtcGasIndex {
  uint32_t magic;
  uint32_t sample_s;
  TvocNoxIndexState state;
};

// Zero-initialized on every reset except a deep-sleep wake: nothing to resume.
RTC_DATA_ATTR RtcGasIndex s_rtc_gas_index = {};

} // namespace

void save_rtc_gas_index(const TvocNoxIndexState &state, uint32_t sample_s) {
  s_rtc_gas_index.state = state;
  s_rtc_gas_index.sample_s = sample_s;
  s_rtc_gas_index.magic = RTC_GAS_INDEX_MAGIC;
}

void clear_rtc_gas_index() { s_rtc_gas_index.magic = 0; }

bool load_rtc_gas_index(uint32_t now_s, TvocNoxIndexState &state, uint32_t &age_s) {
  if (s_rtc_gas_index.magic != RTC_GAS_INDEX_MAGIC) {
    return false;
  }
  const uint32_t sample_s = s_rtc_gas_index.sample_s;
  if (now_s == TRACKING_TIME_INVALID_S || sample_s == TRACKING_TIME_INVALID_S || now_s < sample_s ||
      now_s - sample_s > GAS_INDEX_RESUME_MAX_GAP_S) {
    return false;
  }
  state = s_rtc_gas_index.state;
  age_s = now_s - sample_s;
  return true;
}

bool resume_gas_index_for_sample(SensorManager &sm, uint32_t now_s, uint32_t interval_ms,
                                 int tvoc_learning_offset, int nox_learning_offset) {
  if (!sm.has_tvoc_nox_sensor()) {
    return false;
  }
  TvocNoxIndexState state{};
  uint32_t age_s = 0;
  if (load_rtc_gas_index(now_s, state, age_s)) {
    const uint32_t gap_ms = (age_s > 0 ? age_s : 1) * 1000;
    if (sm.restore_tvoc_nox_index(state, gap_ms)) {
      (void)sm.set_tvoc_nox_learning_offsets(tvoc_learning_offset, nox_learning_offset);
      return true;
    }
  }
  AG_LOGI(TAG, "no recent gas-index state; starting a fresh algorithm");
  // configure_tvoc_nox_index() accepts Sensirion's tested cadences only;
  // retime afterwards to the duty-cycled interval.
  if (!sm.configure_tvoc_nox_index(SGP41_INDEX_SAMPLING_INTERVAL_MS, tvoc_learning_offset,
                                   nox_learning_offset)) {
    return false;
  }
  (void)sm.set_tvoc_nox_sampling_interval(interval_ms);
  return true;
}

bool resume_gas_index_for_sampler(SensorManager &sm, uint32_t now_s) {
  if (!sm.has_tvoc_nox_sensor() || sm.is_tvoc_nox_index_configured()) {
    return false;
  }
  TvocNoxIndexState state{};
  uint32_t age_s = 0;
  if (!load_rtc_gas_index(now_s, state, age_s)) {
    return false;
  }
  return sm.restore_tvoc_nox_index(state, SGP41_INDEX_SAMPLING_INTERVAL_MS);
}
