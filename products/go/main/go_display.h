#ifndef GO_DISPLAY_H
#define GO_DISPLAY_H

#include <atomic>
#include <cstdint>

#include "go_display_geometry.h"
#include "go_types.h"
#include "go_display_refresh.h"
#include "measures_types.h"
#include "rtos.h"
#include "services/provisioning_qr.h"

// ---------------------------------------------------------------------------
// Host-compatible types (no ESP-IDF dependency)
// ---------------------------------------------------------------------------

enum class Screen : uint8_t {
  Home,
  MainMenu,
  TrackingMenu,
  Settings,
  Operations,
  DisplayTouch,
  SettingsChoice,
  TagList,
  About,
  Confirm,
  ShutdownUser,           ///< Goodbye screen for user-initiated power-off
  ShutdownDischarge,      ///< Safety-trip shutdown — battery critically low (OverDischarge)
  ShutdownTemperature,    ///< Safety-trip shutdown — battery overheated
  ShutdownTemperatureLow, ///< Safety-trip shutdown — battery temperature too low
  PairingPasskey,         ///< Shows 6-digit BLE pairing passkey
  Provisioning,           ///< Stationary Wi-Fi provisioning page (QR + status + actions)
  ProvisioningConfirm,    ///< Yes/No confirmation overlay for Provisioning actions
  Info,                   ///< Generic single-text presentation surface (bring-up narration, etc.)
  GettingStarted,         ///< One-time first-boot guide (setup QR + single action row)
  HardwareTest,           ///< Hardware Test submenu (diagnostics, FG learning, melody preview)
  PeripheralTest,         ///< Guided actuator steps + bulk AQ sensor test + summary
  GpsTest,                ///< Live GPS status: TTFF, fix, satellites, HDOP, position, UTC
  AccelTest,              ///< Live accelerometer: WHO_AM_I, X/Y/Z, magnitude, pass/fail

  // --- Fuel-gauge learning (factory path) ---
  FgLearnCharging,   ///< Learning: charging to full
  FgLearnResting,    ///< Learning: charge off, capturing OCV1
  FgLearnUnplug,     ///< Learning: unplug charger to discharge
  DischargeComplete, ///< Learning: EDV reached, final frame before ship
  FgLearnVerifying,  ///< Learning: re-plugged, checking pass criteria
  FgLearnComplete,   ///< Learning: verified pass (terminal)
  FgLearnFailed,     ///< Learning: rejected (terminal, sticky)
  ShutdownCharging,  ///< USB-powered cold boot; application remains off
  ShutdownCharged,   ///< Charging-only mode; charger reports charge complete
  TrackingStatus,    ///< Read-only current-session and GPS details
};

enum class Metric : uint8_t {
  None,
  Pm25,
  Co2,
  Temp,
  Humidity,
  Pm1,
  Pm10,
  Pressure,
  Altitude,
};

inline constexpr uint8_t MAX_LIST_ROWS = 10;

struct ListRow {
  char text[48];
  bool disabled = false;
};

// ---------------------------------------------------------------------------
// Fuel-gauge learning dashboard (factory path)
// ---------------------------------------------------------------------------

/// Compile-time design capacity used for the FCC-drift label (avoids a
/// per-paint read). Confirm against the configured cell on the shipped board.
inline constexpr uint16_t FG_LEARNING_DESIGN_CAPACITY_MAH = 2000;

/// Full-refresh heartbeat for the learning dashboard. The runner also paints
/// on every stage transition.
inline constexpr uint32_t FG_LEARNING_DISPLAY_REFRESH_MS = 60000; // 60 s

/// Host-safe aggregate built directly by FgLearningRunner (no UIManager) and
/// rendered by DisplayService::_draw_fg_learning_dashboard().
struct FgLearningDashboardData {
  bool valid = false; ///< false -> render "FG: NO DATA"
  uint8_t cycle = 0;  ///< 1-based current cycle
  uint8_t cycle_target = 0;
  uint8_t soc_pct = 0;
  uint16_t voltage_mv = 0;
  int16_t current_ma = 0; ///< signed: + charging, - discharging
  uint16_t remaining_mah = 0;
  uint16_t full_charge_mah = 0;
  uint16_t design_capacity_mah = 0;
  float temperature_c = 0.0f;
  bool flag_fc = false;
  bool flag_chg = false;
  bool flag_dsg = false;
  bool qmax_up = false;
  bool res_up = false;
  bool ocv_taken = false;
  uint16_t charge_current_ma = 0;      ///< programmed ICHG
  uint8_t bms_charging_state = 0;      ///< BmsChargingState raw enum value
  uint32_t stage_elapsed_ms = 0;       ///< wall-clock in the current stage (this boot)
  bool external_input_present = false; ///< charger plugged (drives Unplug vs Discharging banner)
  const char *fail_reason = nullptr;   ///< static string; shown on Failed/Complete, nullptr if none
};

struct DisplayValues {
  // --- Sensor readings (channel A) ---
  int co2_ppm = MeasuresInvalid::CO2;
  float pm25_ugm3 = MeasuresInvalid::PM;
  float temperature_c = MeasuresInvalid::TEMPERATURE;
  float humidity_pct = MeasuresInvalid::HUMIDITY;
  float pm1_ugm3 = MeasuresInvalid::PM;
  float pm10_ugm3 = MeasuresInvalid::PM;
  float pressure_hpa = MeasuresInvalid::PRESSURE;
  float altitude_m = MeasuresInvalid::ALTITUDE;

  // --- Battery ---
  uint8_t battery_pct = 0xFF; // 0xFF = no data
  bool is_battery_charging = false;
  bool is_plugged_in = false;

  // --- Status flags ---
  bool locked = true;
  bool ble_enabled = false;
  bool ble_connected = false;
  bool wifi_enabled = false;   // show the Wi-Fi icon (Stationary mode)
  bool wifi_connected = false; // connected vs disconnected glyph
  bool gps_enabled = true;
  bool gps_fix = false;
  TrackingState tracking_state = TrackingState::Idle;
  bool display_off = false;
  bool use_fahrenheit = false;
  bool use_feet = false;
  bool pm_use_usaqi = false;

  // --- Screen navigation ---
  Screen screen = Screen::Home;
  Metric active_metric = Metric::None;

  // --- List/menu content ---
  ListRow rows[MAX_LIST_ROWS] = {};
  uint8_t row_count = 0;
  uint8_t selected_row = 0;
  bool show_separator_after_back = false;

  // --- About screen ---
  const char *about_title = nullptr;
  const char *about_firmware = nullptr;
  const char *about_serial = nullptr;
  const char *about_hardware = nullptr;

  // --- Chart data ---
  const float *chart_samples = nullptr;
  uint8_t chart_count = 0;
  float chart_min = 0.0f;
  float chart_max = 0.0f;
  /// Age in seconds of the oldest / newest plotted sample;
  /// TRACKING_TIME_INVALID_S when the cache times are unknown.
  uint32_t chart_oldest_age_s = TRACKING_TIME_INVALID_S;
  uint32_t chart_newest_age_s = TRACKING_TIME_INVALID_S;

  // --- Snackbar ---
  const char *snackbar_text = nullptr;

  // --- BLE pairing ---
  uint32_t ble_passkey = 0; ///< 6-digit passkey for PairingPasskey screen

  // --- Stationary networking (Provisioning screen only — Home conveys
  // network state purely through the status-bar Wi-Fi icon per spec) ---
  const char *provisioning_status = nullptr; ///< Transport-specific instructions
  uint8_t provisioning_transport = 0;        ///< ProvisioningTransport value

  /// Connected-state IP for the Provisioning success message.  Network
  /// byte order (low byte = first octet) to match WifiGotIpCallback /
  /// WifiStaticIpConfig / format_ipv4_be.  Non-zero overrides the
  /// status-line text with "Connected! a.b.c.d".
  uint32_t provisioning_connected_ip = 0;

  /// 0 = switch transport, 1 = cancel setup (drives ProvisioningConfirm question).
  uint8_t provisioning_confirm_kind = 0;

  /// 0 = No (default), 1 = Yes (drives ProvisioningConfirm button highlight).
  uint8_t provisioning_confirm_index = 0;

  /// WifiOnly captive-portal AP SSID for Provisioning instruction L1.
  /// Borrowed pointer (UIManager owns).  Null -> placeholder.
  const char *provisioning_ap_ssid = nullptr;

  /// WifiOnly captive-portal AP password for Provisioning instruction L2.
  /// Borrowed pointer.  Null -> placeholder.
  const char *provisioning_ap_password = nullptr;

  /// QR for the Provisioning / Getting Started pages (mutually exclusive).
  /// Borrowed; UIManager re-encodes on entry. Null/empty skips the QR area.
  const AirgradientProvisioning::QrCode *qr = nullptr;

  /// First-boot guide shows a single bold Enter-hold instruction.
  bool getting_started_from_boot = false;

  // --- Fuel-gauge learning dashboard (factory path) ---
  bool show_fg_dashboard = false;
  FgLearningDashboardData fg_dashboard{};

  // --- Info screen (generic single-text page) ---
  /// Active source string for Screen::Info.  Plain ASCII.  Newlines are
  /// honored as hard breaks; longer runs auto-wrap.  Pointer must remain
  /// valid through the next DisplayValues snapshot.  Null/empty renders a
  /// blank canvas.
  const char *info_text = nullptr;
};

// ---------------------------------------------------------------------------
// RTC display snapshot — saved after the last paint before deep sleep, loaded
// on wake. Scalar data only; no pointers.  Estimated size: ~50 bytes.
// display_values_from_snapshot() rebuilds the saved frame exactly when the
// frame used no fields beyond these (fast-path frames, locked Home).
// ---------------------------------------------------------------------------

struct RtcDisplaySnapshot {
  // Sensor values
  int co2_ppm;
  float pm25_ugm3;
  float temperature_c;
  float humidity_pct;
  float pm1_ugm3;
  float pm10_ugm3;
  float pressure_hpa;
  float altitude_m;

  // Battery
  uint8_t battery_pct;
  bool is_battery_charging;
  bool is_plugged_in;

  // Status flags
  bool locked;
  bool gps_enabled;
  bool gps_fix;
  TrackingState tracking_state;
  bool ble_enabled;

  // Rendering settings
  bool use_fahrenheit;
  bool use_feet;
  bool pm_use_usaqi;
};

/// Captures the scalar fields of a painted frame for RTC storage.
inline RtcDisplaySnapshot snapshot_from_display_values(const DisplayValues &values) {
  RtcDisplaySnapshot s{};
  s.co2_ppm = values.co2_ppm;
  s.pm25_ugm3 = values.pm25_ugm3;
  s.temperature_c = values.temperature_c;
  s.humidity_pct = values.humidity_pct;
  s.pm1_ugm3 = values.pm1_ugm3;
  s.pm10_ugm3 = values.pm10_ugm3;
  s.pressure_hpa = values.pressure_hpa;
  s.altitude_m = values.altitude_m;
  s.battery_pct = values.battery_pct;
  s.is_battery_charging = values.is_battery_charging;
  s.is_plugged_in = values.is_plugged_in;
  s.locked = values.locked;
  s.gps_enabled = values.gps_enabled;
  s.gps_fix = values.gps_fix;
  s.tracking_state = values.tracking_state;
  s.ble_enabled = values.ble_enabled;
  s.use_fahrenheit = values.use_fahrenheit;
  s.use_feet = values.use_feet;
  s.pm_use_usaqi = values.pm_use_usaqi;
  return s;
}

/// Rebuilds the DisplayValues a snapshot was saved from. Fields the snapshot
/// does not carry keep their DisplayValues defaults, so the result renders
/// the saved frame exactly only when that frame did not depend on them;
/// DisplayService verifies this against the RTC frame hash before relying on it.
inline DisplayValues display_values_from_snapshot(const RtcDisplaySnapshot &snapshot) {
  DisplayValues v{};
  v.co2_ppm = snapshot.co2_ppm;
  v.pm25_ugm3 = snapshot.pm25_ugm3;
  v.temperature_c = snapshot.temperature_c;
  v.humidity_pct = snapshot.humidity_pct;
  v.pm1_ugm3 = snapshot.pm1_ugm3;
  v.pm10_ugm3 = snapshot.pm10_ugm3;
  v.pressure_hpa = snapshot.pressure_hpa;
  v.altitude_m = snapshot.altitude_m;
  v.battery_pct = snapshot.battery_pct;
  v.is_battery_charging = snapshot.is_battery_charging;
  v.is_plugged_in = snapshot.is_plugged_in;
  v.locked = snapshot.locked;
  v.gps_enabled = snapshot.gps_enabled;
  v.gps_fix = snapshot.gps_fix;
  v.tracking_state = snapshot.tracking_state;
  v.ble_enabled = snapshot.ble_enabled;
  v.use_fahrenheit = snapshot.use_fahrenheit;
  v.use_feet = snapshot.use_feet;
  v.pm_use_usaqi = snapshot.pm_use_usaqi;
  v.screen = Screen::Home;
  v.display_off = false;
  return v;
}

// ---------------------------------------------------------------------------
// DisplayService (hardware-dependent, excluded from host builds)
// ---------------------------------------------------------------------------

#ifndef TEST_HOST

#include <driver/spi_master.h>

extern "C" {
#include "u8g2.h"
}

class DisplayService {
public:
  struct Config {
    // SPI
    spi_host_device_t spi_host;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_busy;
    int clock_hz = 4000000;

    // Worker task
    uint16_t task_stack_size = 4096;
    uint8_t task_priority = 4;

    // Ghosting cleanup — see go_display_refresh::CleanupPolicy
    uint8_t max_partial_ops = 60;     // ~10 min of 10 s updates if the device never locks
    uint8_t lock_cleanup_min_ops = 5; // skip the lock flash after only a few partials
    uint8_t max_fast_cleanups = 5;    // every 6th cleanup is a Full
  };

  explicit DisplayService(const Config &config);

  /// Initialize display hardware and start worker task.
  ///
  /// When defer_refresh is false (default): performs a synchronous full
  /// refresh, then starts the async worker task.  Unchanged behavior for
  /// run_full_boot() and run_fast_path().
  ///
  /// When defer_refresh is true: renders into the software buffer, starts
  /// the worker task, and posts the initial full refresh as the worker's
  /// first job.  Returns in ~10 ms without waiting for the e-paper refresh.
  /// The worker holds the SPI bus for the duration (~3 s), naturally
  /// serializing any other SPI device (NAND) without explicit coordination.
  ///
  /// `painted` (optional) describes the frame believed to be on the panel from
  /// before deep sleep. If its render matches the hash saved in RTC memory at
  /// paint time, the first refresh is a flash-free Partial against it instead
  /// of a Full.
  bool init(const DisplayValues &initial, bool defer_refresh = false,
            const DisplayValues *painted = nullptr);

  /// Submit a new frame for display.
  /// Renders into framebuffer (fast), then signals worker task.
  /// wait=false and worker busy: returns false (skipped).
  /// wait=true: blocks until worker finishes previous refresh.
  bool update(const DisplayValues &values, bool wait = false);

  /// Synchronous one-shot update for fast-path boot.
  /// Renders and drives SPI inline (blocking). Does not use worker task.
  void update_sync(const DisplayValues &values);

  /// Wait until the most-recently-queued frame has finished painting.
  /// Returns immediately when the worker is idle. Polls once per RTOS tick.
  ///
  /// Must NOT be called from the display worker task itself
  /// (self-deadlocks because _worker_busy clears only when the worker
  /// returns to its loop).  Safe from any other task, including the
  /// orchestrator task — the only caller in this product.
  void flush();

  /// Run the ghosting cleanup (Fast, or Full every few cleanups) on the next
  /// update() if enough Partials have accumulated. Called when the device
  /// locks, so the flash happens as the user looks away.
  void request_cleanup() { _cleanup_requested = true; }

  /// Clear display to white (full refresh). Blocking.
  void clear();

  /// Put EPD controller into deep sleep mode.
  void deep_sleep();

  /// Stop worker task. Call before entering ESP deep sleep.
  void stop();

private:
  static constexpr int BUF_TILE_HEIGHT = static_cast<int>(go_display_geometry::RENDER_ROWS / 8);
  static constexpr size_t BUF_SIZE = go_display_geometry::RENDER_BYTES;
  static constexpr uint32_t WORKER_POLL_MS = 10;

  Config _config;

  // u8g2 instance and render buffer
  u8g2_t _u8g2;
  uint8_t _render_buf[BUF_SIZE];

  // Refresh state
  DisplayValues _prev_values;
  uint8_t _partial_count = 0; // Partials since the last Fast/Full (written by the worker)
  uint8_t _fast_count = 0;    // Fast refreshes since the last Full (written by the worker)
  RefreshMode _pending_mode = RefreshMode::Full;
  bool _pending_full_canvas = false; // Partial writes y=0..249 instead of the body
  bool _cleanup_requested = false;

  // Panel content as of the last queued refresh. The header rows decide
  // whether a Partial must widen to full canvas; the hash skips refreshes of
  // an identical frame. _panel_known is false until the first paint and after
  // a failed refresh, which disables both shortcuts.
  uint8_t _panel_header[go_display_geometry::HEADER_BYTES];
  uint32_t _panel_hash = 0;
  bool _panel_known = false;

  // Worker task
  RtosTaskHandle _task_handle = nullptr;
  std::atomic<bool> _running{false};
  std::atomic<bool> _worker_busy{false};

  bool _claim_framebuffer(bool wait);
  void _release_framebuffer();
  go_display_refresh::CleanupPolicy _cleanup_policy() const;

  // Drives one refresh of _render_buf (bus must be held), updates the refresh
  // counters, and records the painted frame in RTC memory.
  esp_err_t _paint(RefreshMode mode, bool full_canvas);
  bool _run_sync(RefreshMode mode, bool full_canvas, const char *what);

  // Render methods
  void _render_frame(const DisplayValues &v);
  bool _is_header_changed() const;
  void _latch_panel(uint32_t hash);

  void _draw_status_bar(const DisplayValues &v);
  void _draw_home(const DisplayValues &v);
  void _draw_menu_overlay(const DisplayValues &v);
  void _draw_full_screen_list(const DisplayValues &v);
  void _draw_snackbar(const DisplayValues &v);
  void _draw_shutdown(Screen s);
  void _draw_pairing_passkey(const DisplayValues &v);
  void _draw_chart(const DisplayValues &v, int plot_x, int plot_y, int plot_w, int plot_h);
  void _draw_chart_panel(const DisplayValues &v);
  void _draw_info(const DisplayValues &v);
  void _draw_provisioning(const DisplayValues &v);
  void _draw_provisioning_confirm(const DisplayValues &v);
  void _draw_getting_started(const DisplayValues &v);
  void _draw_fg_learning_dashboard(const DisplayValues &v);

  // Worker
  static void _worker_entry(void *arg);
  void _worker_loop();
};

// ---------------------------------------------------------------------------
// Free functions — RTC display snapshot persistence (non-TEST_HOST only)
// ---------------------------------------------------------------------------

/// Save display values to RTC memory as a snapshot for the next button wake.
/// Called from prepare_for_sleep() after the final display update.
void save_rtc_display_snapshot(const DisplayValues &values);

/// Load the RTC display snapshot saved before the last deep sleep.
/// Returns true and fills *snapshot_out when a valid snapshot exists.
/// Returns false when no valid snapshot is present (first power-on).
bool load_rtc_display_snapshot(RtcDisplaySnapshot *snapshot_out);

#else // TEST_HOST

// ---------------------------------------------------------------------------
// Host-test stub — no-op implementations so the Orchestrator compiles
// without conditional compilation in its own source files.
// ---------------------------------------------------------------------------

class DisplayService {
public:
  struct Config {};

  explicit DisplayService(const Config &) {}

  bool init(const DisplayValues &values, bool defer_refresh = false,
            const DisplayValues * = nullptr) {
    ++spy_init_count;
    spy_last_screen = values.screen;
    spy_last_init_deferred = defer_refresh;
    return true;
  }

  bool update(const DisplayValues &values, bool wait = false) {
    ++spy_update_count;
    spy_last_update_wait = wait;
    if (spy_worker_busy && !wait) {
      return false;
    }
    spy_worker_busy = false;
    spy_last_screen = values.screen;
    return true;
  }

  void update_sync(const DisplayValues &values) {
    ++spy_sync_update_count;
    spy_last_screen = values.screen;
  }
  void flush() { ++spy_flush_count; }
  void request_cleanup() { ++spy_cleanup_request_count; }
  void clear() {}
  void deep_sleep() { spy_deep_sleep_called = true; }
  void stop() {}

  // Test spies — reset via test_spy::reset() in stubs.
  inline static bool spy_deep_sleep_called = false;
  inline static uint32_t spy_init_count = 0;
  inline static uint32_t spy_update_count = 0;
  inline static uint32_t spy_sync_update_count = 0;
  inline static uint32_t spy_flush_count = 0;
  inline static uint32_t spy_cleanup_request_count = 0;
  inline static Screen spy_last_screen = Screen::Home;
  inline static bool spy_last_init_deferred = false;
  inline static bool spy_last_update_wait = false;
  inline static bool spy_worker_busy = false;
};

// Stub implementations for host builds.
inline void save_rtc_display_snapshot(const DisplayValues &) {}
inline bool load_rtc_display_snapshot(RtcDisplaySnapshot *) { return false; }

#endif // !TEST_HOST

#endif // GO_DISPLAY_H
