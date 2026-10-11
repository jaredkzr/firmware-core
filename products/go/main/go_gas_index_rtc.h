/**
 * AirGradient Go — gas-index algorithm state across deep sleep
 *
 * The Sensirion VOC/NOx index is relative to a baseline the algorithm learns
 * over hours, and it reports nothing for its first 45 s. A fresh algorithm on
 * every deep-sleep wake therefore never produces an index. These helpers keep
 * the algorithm state in RTC memory so each wake resumes where the previous
 * one stopped and feeds it one more sample.
 *
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef GO_GAS_INDEX_RTC_H
#define GO_GAS_INDEX_RTC_H

#include <cstdint>

#include "services/sensor_manager.h"

/// Longest gap between two gas samples that still resumes the saved state.
/// Longer gaps (device off, a long Stationary stint) start a fresh algorithm.
inline constexpr uint32_t GAS_INDEX_RESUME_MAX_GAP_S = 60 * 60;

/// Save the algorithm state with the retained-clock time of its last sample.
void save_rtc_gas_index(const TvocNoxIndexState &state, uint32_t sample_s);

/// Forget the saved state.
void clear_rtc_gas_index();

/// Load the saved state when it exists and its last sample is at most
/// GAS_INDEX_RESUME_MAX_GAP_S before @p now_s. @p age_s receives the gap.
bool load_rtc_gas_index(uint32_t now_s, TvocNoxIndexState &state, uint32_t &age_s);

/// Restore the saved algorithm into @p sm, or configure a fresh one, for one
/// duty-cycled sample at @p now_s. A resumed algorithm is retimed to the
/// actual gap since its last sample; a fresh one assumes @p interval_ms.
/// Returns false when no TVOC/NOx sensor is wired or configuration failed.
bool resume_gas_index_for_sample(SensorManager &sm, uint32_t now_s, uint32_t interval_ms,
                                 int tvoc_learning_offset, int nox_learning_offset);

/// Restore the saved algorithm into @p sm for the continuous sampler, which
/// retimes it to its own cadence. Leaves @p sm untouched when nothing usable
/// is saved, so the sampler configures a fresh algorithm.
bool resume_gas_index_for_sampler(SensorManager &sm, uint32_t now_s);

#endif // GO_GAS_INDEX_RTC_H
