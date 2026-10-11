#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "go_display_refresh.h"

using go_display_refresh::CleanupPolicy;
using go_display_refresh::frame_hash;
using go_display_refresh::RefreshDecision;
using go_display_refresh::RefreshInputs;
using go_display_refresh::select_boot_refresh;
using go_display_refresh::select_refresh;
using go_display_refresh::writes_header;

namespace {

constexpr CleanupPolicy POLICY{/*max_partial_ops=*/60, /*lock_cleanup_min_ops=*/5,
                               /*max_fast_cleanups=*/5};

// Home -> Home with no menu involvement and a fresh cleanup budget.
RefreshInputs home_to_home() {
  RefreshInputs in{};
  in.both_navigable = true;
  return in;
}

} // namespace

TEST_CASE("Status-bar change on Home uses a full-canvas Partial, not Fast") {
  RefreshInputs in = home_to_home();
  in.header_changed = true; // lock/unlock, plug-in, charging bolt

  const RefreshDecision d = select_refresh(in, POLICY);

  CHECK(d.mode == RefreshMode::Partial);
  CHECK(d.full_canvas);
  CHECK(writes_header(d));
}

TEST_CASE("Unchanged status bar on Home uses a body-only Partial") {
  const RefreshDecision d = select_refresh(home_to_home(), POLICY);

  CHECK(d.mode == RefreshMode::Partial);
  CHECK_FALSE(d.full_canvas);
  CHECK_FALSE(writes_header(d));
}

TEST_CASE("Leaving a menu no longer flashes") {
  RefreshInputs in = home_to_home();
  in.partial_count = 30; // a long menu session, below the cap

  CHECK(select_refresh(in, POLICY).mode == RefreshMode::Partial);
}

TEST_CASE("Locking runs the cleanup once enough Partials accumulated") {
  RefreshInputs in = home_to_home();
  in.cleanup_requested = true;

  SECTION("few Partials: no flash") {
    in.partial_count = POLICY.lock_cleanup_min_ops - 1;
    CHECK(select_refresh(in, POLICY).mode == RefreshMode::Partial);
  }

  SECTION("enough Partials: single-flash Fast") {
    in.partial_count = POLICY.lock_cleanup_min_ops;
    const RefreshDecision d = select_refresh(in, POLICY);
    CHECK(d.mode == RefreshMode::Fast);
    CHECK(writes_header(d));
  }

  SECTION("after max_fast_cleanups Fasts: escalates to Full") {
    in.partial_count = POLICY.lock_cleanup_min_ops;
    in.fast_count = POLICY.max_fast_cleanups;
    CHECK(select_refresh(in, POLICY).mode == RefreshMode::Full);
  }

  SECTION("locking from a menu still cleans up") {
    in.menu_navigation = true;
    in.partial_count = POLICY.lock_cleanup_min_ops;
    CHECK(select_refresh(in, POLICY).mode == RefreshMode::Fast);
  }
}

TEST_CASE("Partial cap forces a cleanup for a device that never locks") {
  RefreshInputs in = home_to_home();
  in.header_changed = true;
  in.partial_count = POLICY.max_partial_ops;

  SECTION("Fast first") { CHECK(select_refresh(in, POLICY).mode == RefreshMode::Fast); }

  SECTION("Full once Fasts are used up") {
    in.fast_count = POLICY.max_fast_cleanups;
    CHECK(select_refresh(in, POLICY).mode == RefreshMode::Full);
  }
}

TEST_CASE("Menu navigation defers the cap and widens to full canvas on header change") {
  RefreshInputs in = home_to_home();
  in.menu_navigation = true;
  in.partial_count = POLICY.max_partial_ops;

  SECTION("header unchanged") {
    const RefreshDecision d = select_refresh(in, POLICY);
    CHECK(d.mode == RefreshMode::Partial);
    CHECK_FALSE(d.full_canvas);
  }

  SECTION("header changed") {
    in.header_changed = true;
    const RefreshDecision d = select_refresh(in, POLICY);
    CHECK(d.mode == RefreshMode::Partial);
    CHECK(d.full_canvas);
  }
}

TEST_CASE("Transition to a non-navigable screen uses Fast") {
  RefreshInputs in = home_to_home();
  in.both_navigable = false; // e.g. Home -> PairingPasskey or Shutdown*

  const RefreshDecision d = select_refresh(in, POLICY);

  CHECK(d.mode == RefreshMode::Fast);
  CHECK(writes_header(d));
}

TEST_CASE("Setup-session boundary forces Full in both directions") {
  RefreshInputs in = home_to_home();
  in.both_navigable = false;
  in.cleanup_requested = true;

  SECTION("entering") { in.next_in_session = true; }
  SECTION("leaving") { in.prev_in_session = true; }

  CHECK(select_refresh(in, POLICY).mode == RefreshMode::Full);
}

TEST_CASE("Intra-session transitions are full-canvas Partials regardless of budget") {
  RefreshInputs in{};
  in.prev_in_session = true;
  in.next_in_session = true;
  in.partial_count = POLICY.max_partial_ops;
  in.cleanup_requested = true;

  const RefreshDecision d = select_refresh(in, POLICY);

  CHECK(d.mode == RefreshMode::Partial);
  CHECK(d.full_canvas);
}

TEST_CASE("Boot refresh is a full-canvas Partial only when the panel is retained") {
  SECTION("unknown panel: Full") {
    CHECK(select_boot_refresh(false, 0, 0, POLICY).mode == RefreshMode::Full);
  }

  SECTION("retained panel: Partial") {
    const RefreshDecision d = select_boot_refresh(true, 10, 0, POLICY);
    CHECK(d.mode == RefreshMode::Partial);
    CHECK(d.full_canvas);
  }

  SECTION("retained panel at the cap: cleanup") {
    CHECK(select_boot_refresh(true, POLICY.max_partial_ops, 0, POLICY).mode == RefreshMode::Fast);
    CHECK(
        select_boot_refresh(true, POLICY.max_partial_ops, POLICY.max_fast_cleanups, POLICY).mode ==
        RefreshMode::Full);
  }
}

TEST_CASE("Frame hash distinguishes frames that differ by one pixel") {
  uint8_t a[4000];
  uint8_t b[4000];
  std::memset(a, 0xFF, sizeof(a));
  std::memset(b, 0xFF, sizeof(b));

  CHECK(frame_hash(a, sizeof(a)) == frame_hash(b, sizeof(b)));

  b[1234] = 0xFE; // one black pixel
  CHECK(frame_hash(a, sizeof(a)) != frame_hash(b, sizeof(b)));
}
