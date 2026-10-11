#ifndef GO_DISPLAY_REFRESH_H
#define GO_DISPLAY_REFRESH_H

#include <cstddef>
#include <cstdint>

/// Display refresh tier — controls how the e-paper controller updates.
enum class RefreshMode : uint8_t {
  Full,    ///< Full GC waveform (multi-flash). Resets basemap in both RAM planes.
  Fast,    ///< Full-screen 0xC7 waveform (100 °C LUT). Shorter, but still inverts once.
  Partial, ///< Differential 0xFF waveform (no flash). Body-only or full-canvas span.
};

namespace go_display_refresh {

/// Ghosting-cleanup cadence. Partials accumulate ghosting; a cleanup (Fast,
/// escalating to Full) clears it. Cleanups run when the device locks, so the
/// flash happens as the user looks away, with a cap for devices that never lock.
struct CleanupPolicy {
  uint8_t max_partial_ops = 0;      ///< partials before a cleanup is forced
  uint8_t lock_cleanup_min_ops = 0; ///< partials needed before a lock runs a cleanup
  uint8_t max_fast_cleanups = 0;    ///< Fast cleanups before the next one escalates to Full
};

/// Screen-relationship facts for one update, computed by DisplayService from
/// the previous and next frames.  Kept as plain flags so the decision is
/// host-testable without the Screen predicates.
struct RefreshInputs {
  bool prev_in_session = false;   ///< previous screen is a setup-session screen
  bool next_in_session = false;   ///< next screen is a setup-session screen
  bool menu_navigation = false;   ///< prev or next is a menu-navigation screen
  bool both_navigable = false;    ///< prev and next are both navigable screens
  bool header_changed = false;    ///< rendered rows 0..17 differ from the panel
  bool cleanup_requested = false; ///< the device is locking (DisplayService::request_cleanup)
  uint8_t partial_count = 0;      ///< Partials since the last Fast/Full
  uint8_t fast_count = 0;         ///< Fast refreshes since the last Full
};

struct RefreshDecision {
  RefreshMode mode;
  bool full_canvas; ///< Partial writes y=0..249 instead of the body (y=18..249)
};

/// Cleanup refresh: Fast (single inversion) until max_fast_cleanups, then Full.
constexpr RefreshDecision cleanup_refresh(uint8_t fast_count, const CleanupPolicy &policy) {
  return {fast_count >= policy.max_fast_cleanups ? RefreshMode::Full : RefreshMode::Fast, false};
}

/// Selects the refresh tier for one update.
///
/// Everything a user does while unlocked is a flash-free Partial, including
/// status-bar changes (full canvas). Flashing refreshes are limited to the
/// setup-session boundary, system screens (Shutdown*, PairingPasskey), the
/// cleanup when the device locks, and the never-locks cap.
constexpr RefreshDecision select_refresh(const RefreshInputs &in, const CleanupPolicy &policy) {
  if (in.prev_in_session != in.next_in_session) {
    // Crossing the setup-session boundary: clear the prior layout completely.
    return {RefreshMode::Full, false};
  }
  if (in.prev_in_session && in.next_in_session) {
    // Intra-session transitions always repaint the full canvas differentially.
    return {RefreshMode::Partial, true};
  }
  if (in.cleanup_requested && in.partial_count >= policy.lock_cleanup_min_ops) {
    return cleanup_refresh(in.fast_count, policy);
  }
  if (in.menu_navigation) {
    // The cap is deferred, not skipped, while the user is in menus.
    return {RefreshMode::Partial, in.header_changed};
  }
  if (in.partial_count >= policy.max_partial_ops) {
    return cleanup_refresh(in.fast_count, policy);
  }
  if (in.both_navigable) {
    return {RefreshMode::Partial, in.header_changed};
  }
  // Entering or leaving a system screen: a deliberate, visible transition.
  return {RefreshMode::Fast, false};
}

/// Refresh for the first frame after boot. `retained` means the frame on the
/// panel is known (verified against its RTC hash), so a Partial can diff
/// against it instead of repainting from scratch with Full.
constexpr RefreshDecision select_boot_refresh(bool retained, uint8_t partial_count,
                                              uint8_t fast_count, const CleanupPolicy &policy) {
  if (!retained) {
    return {RefreshMode::Full, false};
  }
  if (partial_count >= policy.max_partial_ops) {
    return cleanup_refresh(fast_count, policy);
  }
  return {RefreshMode::Partial, true};
}

/// True when the refresh rewrites the status-bar rows (y=0..17) on the panel.
constexpr bool writes_header(const RefreshDecision &decision) {
  return decision.mode != RefreshMode::Partial || decision.full_canvas;
}

/// FNV-1a hash of a rendered frame. Identifies the panel content across
/// updates and deep sleep (stored in RTC memory) without keeping a copy.
constexpr uint32_t frame_hash(const uint8_t *data, size_t len) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash = (hash ^ data[i]) * 16777619u;
  }
  return hash;
}

} // namespace go_display_refresh

#endif // GO_DISPLAY_REFRESH_H
