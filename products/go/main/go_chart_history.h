/**
 * AirGradient Go — Home chart history
 *
 * Fixed-width time buckets covering the last CHART_HISTORY_SPAN_S, one value
 * per chart metric per bucket (the average of the samples recorded in it).
 * The live instance sits in RTC memory, so the history survives deep sleep,
 * and buckets are keyed by retained-clock time, so the chart's time axis is
 * real time whatever the measurement interval, including gaps.
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef GO_CHART_HISTORY_H
#define GO_CHART_HISTORY_H

#include <cstdint>

#include "measures_types.h"

/// Width of one history bucket.
inline constexpr uint32_t CHART_HISTORY_BUCKET_S = 120;
/// Number of buckets kept: 120 x 2 min = 4 h.
inline constexpr uint16_t CHART_HISTORY_BUCKETS = 120;
/// Time covered by a full history.
inline constexpr uint32_t CHART_HISTORY_SPAN_S = CHART_HISTORY_BUCKET_S * CHART_HISTORY_BUCKETS;

/// Highest valid TVOC/NOx index; larger values are raw ticks, not an index.
inline constexpr int CHART_GAS_INDEX_MAX = 500;

/// One chartable value per series, in recording order.
enum class ChartSeries : uint8_t {
  Pm25,
  Co2,
  Temp,
  Humidity,
  Tvoc,
  Nox,
  Pressure,
  Altitude,
  Count,
};

inline constexpr uint8_t CHART_SERIES_COUNT = static_cast<uint8_t>(ChartSeries::Count);

/// One series read back for plotting.
struct ChartSeriesView {
  /// Number of buckets written to the caller's buffer, oldest first, from
  /// the series' first valid bucket to its last. Gaps are NaN.
  uint16_t count = 0;
  /// Age of the first and last plotted sample at the read time, in seconds.
  uint32_t oldest_age_s = 0;
  uint32_t newest_age_s = 0;
};

/**
 * Plain-data ring of time buckets. Zero-initialized storage is a valid empty
 * history, so the RTC instance needs no constructor.
 */
struct ChartHistory {
  /// Add one (corrected) measurement taken at retained-clock second @p now_s.
  /// Invalid fields are skipped. A clock that moved backwards clears the
  /// history; a jump past the whole span leaves only this sample.
  void record(const MeasuresAGo &m, uint32_t now_s);

  /// Copy @p series into @p out (NaN for empty buckets) as of @p now_s.
  /// Gaps up to @p bridge_s long are filled by linear interpolation so a
  /// sparse measurement interval still draws a continuous line; longer gaps
  /// stay NaN. Returns count 0 when the series has no samples in the span.
  ChartSeriesView read(ChartSeries series, uint32_t now_s, uint32_t bridge_s, float *out,
                       uint16_t max_count) const;

  /// Drop all history.
  void clear();

  // --- Storage (public so the struct stays plain data) ---
  uint32_t magic;          ///< CHART_HISTORY_MAGIC once initialized.
  uint32_t head_bucket;    ///< Bucket number (time / bucket width) of the newest slot.
  uint32_t first_sample_s; ///< Retained time of the oldest sample still held.
  uint32_t last_sample_s;  ///< Retained time of the newest sample.
  uint16_t head_slot;      ///< Ring index of the newest slot.
  uint16_t head_count[CHART_SERIES_COUNT]; ///< Samples averaged into the head bucket.
  float head_sum[CHART_SERIES_COUNT];      ///< Running sums for the head bucket.
  int16_t slots[CHART_HISTORY_BUCKETS][CHART_SERIES_COUNT]; ///< Scaled bucket averages.
};

/// The history kept in RTC memory across deep sleep.
ChartHistory &rtc_chart_history();

#endif // GO_CHART_HISTORY_H
