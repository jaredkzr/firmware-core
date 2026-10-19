/**
 * AirGradient Go — Home chart history
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "go_chart_history.h"

#include <cmath>
#include <cstring>
#include <limits>

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

// Bump when the ChartHistory layout or bucket width changes.
constexpr uint32_t CHART_HISTORY_MAGIC = 0x43484903; // "CHI" v3
constexpr uint32_t TIME_UNKNOWN_S = UINT32_MAX;
constexpr int16_t EMPTY = INT16_MIN;

// Fixed-point scale per series (value * scale stored as int16).
constexpr float SCALE[CHART_SERIES_COUNT] = {
    10.0f,  // Pm25: 0.1 ug/m3
    1.0f,   // Co2: ppm
    100.0f, // Temp: 0.01 C
    100.0f, // Humidity: 0.01 %
    10.0f,  // Pm1: 0.1 ug/m3
    10.0f,  // Pm10: 0.1 ug/m3
    10.0f,  // Pressure: 0.1 hPa
    1.0f,   // Altitude: m
};

/// Extract one series from a measurement; false when the field is invalid.
bool series_value(const MeasuresAGo &m, ChartSeries series, float &out) {
  switch (series) {
  case ChartSeries::Pm25:
    out = m.pm_a.pm_25;
    return m.pm_a.pm_25 != MeasuresInvalid::PM;
  case ChartSeries::Co2:
    out = static_cast<float>(m.co2.co2);
    return m.co2.co2 != MeasuresInvalid::CO2;
  case ChartSeries::Temp:
    out = m.temp_hum_a.temperature;
    return m.temp_hum_a.temperature != MeasuresInvalid::TEMPERATURE;
  case ChartSeries::Humidity:
    out = m.temp_hum_a.humidity;
    return m.temp_hum_a.humidity != MeasuresInvalid::HUMIDITY;
  case ChartSeries::Pm1:
    out = m.pm_a.pm_01;
    return m.pm_a.pm_01 != MeasuresInvalid::PM;
  case ChartSeries::Pm10:
    out = m.pm_a.pm_10;
    return m.pm_a.pm_10 != MeasuresInvalid::PM;
  case ChartSeries::Pressure:
    out = m.pressure.pressure;
    return m.pressure.is_pressure_valid();
  case ChartSeries::Altitude:
    out = m.pressure.altitude;
    return m.pressure.is_altitude_valid();
  case ChartSeries::Count:
  default:
    return false;
  }
}

int16_t encode(float value, float scale) {
  const float scaled = std::round(value * scale);
  constexpr float lo = static_cast<float>(std::numeric_limits<int16_t>::min() + 1);
  constexpr float hi = static_cast<float>(std::numeric_limits<int16_t>::max());
  if (scaled < lo) {
    return static_cast<int16_t>(lo);
  }
  if (scaled > hi) {
    return static_cast<int16_t>(hi);
  }
  return static_cast<int16_t>(scaled);
}

void clear_slot(int16_t (&slot)[CHART_SERIES_COUNT]) {
  for (auto &value : slot) {
    value = EMPTY;
  }
}

// Zero-initialized on every reset except a deep-sleep wake: an empty history.
RTC_DATA_ATTR ChartHistory s_rtc_chart_history = {};

} // namespace

ChartHistory &rtc_chart_history() { return s_rtc_chart_history; }

void ChartHistory::clear() { std::memset(this, 0, sizeof(*this)); }

void ChartHistory::record(const MeasuresAGo &m, uint32_t now_s) {
  if (now_s == TIME_UNKNOWN_S) {
    return;
  }
  const uint32_t bucket = now_s / CHART_HISTORY_BUCKET_S;

  const bool restart = magic != CHART_HISTORY_MAGIC || now_s < last_sample_s ||
                       bucket - head_bucket >= CHART_HISTORY_BUCKETS;
  if (restart) {
    clear();
    magic = CHART_HISTORY_MAGIC;
    head_bucket = bucket;
    first_sample_s = now_s;
    for (auto &slot : slots) {
      clear_slot(slot);
    }
  } else if (bucket > head_bucket) {
    for (uint32_t b = head_bucket; b < bucket; ++b) {
      head_slot = static_cast<uint16_t>((head_slot + 1) % CHART_HISTORY_BUCKETS);
      clear_slot(slots[head_slot]);
    }
    head_bucket = bucket;
    std::memset(head_count, 0, sizeof(head_count));
    std::memset(head_sum, 0, sizeof(head_sum));
    // The oldest retained bucket may have just been overwritten.
    if (bucket >= CHART_HISTORY_BUCKETS - 1) {
      const uint32_t oldest_start_s =
          (bucket - (CHART_HISTORY_BUCKETS - 1)) * CHART_HISTORY_BUCKET_S;
      if (first_sample_s < oldest_start_s) {
        first_sample_s = oldest_start_s;
      }
    }
  }

  for (uint8_t i = 0; i < CHART_SERIES_COUNT; ++i) {
    float value = 0.0f;
    if (!series_value(m, static_cast<ChartSeries>(i), value)) {
      continue;
    }
    head_sum[i] += value;
    ++head_count[i];
    slots[head_slot][i] = encode(head_sum[i] / static_cast<float>(head_count[i]), SCALE[i]);
  }
  last_sample_s = now_s;
}

ChartSeriesView ChartHistory::read(ChartSeries series, uint32_t now_s, uint32_t bridge_s,
                                   float *out, uint16_t max_count) const {
  ChartSeriesView view{};
  const auto index = static_cast<uint8_t>(series);
  if (magic != CHART_HISTORY_MAGIC || index >= CHART_SERIES_COUNT || out == nullptr ||
      max_count == 0 || now_s == TIME_UNKNOWN_S || now_s < last_sample_s) {
    return view;
  }

  // Buckets inside the span ending now, limited to what the ring holds.
  const uint32_t now_bucket = now_s / CHART_HISTORY_BUCKET_S;
  const uint16_t window = max_count < CHART_HISTORY_BUCKETS ? max_count : CHART_HISTORY_BUCKETS;
  if (now_bucket - head_bucket >= window) {
    return view; // everything held is older than the span
  }
  const uint32_t span_first = now_bucket >= window - 1u ? now_bucket - (window - 1u) : 0;
  const uint32_t ring_first =
      head_bucket >= CHART_HISTORY_BUCKETS - 1u ? head_bucket - (CHART_HISTORY_BUCKETS - 1u) : 0;
  const uint32_t lo = span_first > ring_first ? span_first : ring_first;

  auto slot_value = [&](uint32_t bucket) -> int16_t {
    const uint32_t back = head_bucket - bucket;
    const uint16_t slot =
        static_cast<uint16_t>((head_slot + CHART_HISTORY_BUCKETS - (back % CHART_HISTORY_BUCKETS)) %
                              CHART_HISTORY_BUCKETS);
    return slots[slot][index];
  };

  uint32_t first = 0;
  uint32_t last = 0;
  bool found = false;
  for (uint32_t b = lo; b <= head_bucket; ++b) {
    if (slot_value(b) != EMPTY) {
      if (!found) {
        first = b;
        found = true;
      }
      last = b;
    }
  }
  if (!found) {
    return view;
  }

  const uint16_t count = static_cast<uint16_t>(last - first + 1);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  int prev_valid = -1;
  for (uint16_t i = 0; i < count; ++i) {
    const int16_t raw = slot_value(first + i);
    if (raw == EMPTY) {
      out[i] = nan;
      continue;
    }
    out[i] = static_cast<float>(raw) / SCALE[index];
    // Bridge the gap since the previous value when it is short enough.
    const int gap = static_cast<int>(i) - prev_valid;
    if (prev_valid >= 0 && gap > 1 &&
        static_cast<uint32_t>(gap) * CHART_HISTORY_BUCKET_S <= bridge_s) {
      const float start = out[prev_valid];
      for (int k = 1; k < gap; ++k) {
        out[prev_valid + k] =
            start + (out[i] - start) * static_cast<float>(k) / static_cast<float>(gap);
      }
    }
    prev_valid = i;
  }

  uint32_t oldest_s = first * CHART_HISTORY_BUCKET_S;
  if (oldest_s < first_sample_s) {
    oldest_s = first_sample_s;
  }
  uint32_t newest_s = last_sample_s;
  if (last != head_bucket) {
    const uint32_t bucket_end_s = last * CHART_HISTORY_BUCKET_S + CHART_HISTORY_BUCKET_S - 1;
    if (bucket_end_s < newest_s) {
      newest_s = bucket_end_s;
    }
  }
  if (oldest_s > newest_s) {
    oldest_s = newest_s;
  }

  view.count = count;
  view.oldest_age_s = now_s - oldest_s;
  view.newest_age_s = now_s - newest_s;
  return view;
}
