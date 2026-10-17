#include <doctest/doctest.h>
#include "screen_mirror/touch_lease.h"

using namespace esphome::screen_mirror;

TEST_CASE("Touch Lease: Production TouchLeaseManager single client lifecycle") {
  TouchLeaseManager mgr;

  // Touch Down at t = 1000
  CHECK(mgr.can_process_down(10, 1000, false) == true);
  mgr.record_down(10, 1000);
  CHECK(mgr.get_active_client() == 10);
  CHECK(mgr.get_last_down_time() == 1000);

  // Touch Move at t = 1050
  CHECK(mgr.can_process_move(10) == true);

  // Touch Up at t = 1100
  CHECK(mgr.can_process_up(10) == true);
  mgr.record_up();
  CHECK(mgr.get_active_client() == 0); // Released
}

TEST_CASE("Touch Lease: Production TouchLeaseManager multi-user contention") {
  TouchLeaseManager mgr;

  // Client A presses down at t = 1000
  CHECK(mgr.can_process_down(10, 1000, false) == true);
  mgr.record_down(10, 1000);
  CHECK(mgr.get_active_client() == 10);

  // Client B tries to press down at t = 1500 (< 3000ms lease, touch is down)
  CHECK(mgr.can_process_down(20, 1500, true) == false);
  CHECK(mgr.get_active_client() == 10); // Still owned by Client A

  // Client B tries to send move at t = 1600
  CHECK(mgr.can_process_move(20) == false);

  // Client B tries to send up at t = 1700
  CHECK(mgr.can_process_up(20) == false);

  // Client A moves at t = 1800
  CHECK(mgr.can_process_move(10) == true);

  // Client A releases touch at t = 2000
  CHECK(mgr.can_process_up(10) == true);
  mgr.record_up();
  CHECK(mgr.get_active_client() == 0);

  // Client B can now acquire lease immediately
  CHECK(mgr.can_process_down(20, 2050, false) == true);
  mgr.record_down(20, 2050);
  CHECK(mgr.get_active_client() == 20);
}

TEST_CASE("Touch Lease: Production TouchLeaseManager timeout takeover on unresponsive client") {
  TouchLeaseManager mgr;

  // Client A presses down at t = 1000
  CHECK(mgr.can_process_down(10, 1000, false) == true);
  mgr.record_down(10, 1000);
  CHECK(mgr.get_active_client() == 10);

  // Client A hangs (never sends UP). At t = 4500 (3500ms > 3000ms), Client B sends DOWN
  CHECK(mgr.can_process_down(20, 4500, true) == true);
  mgr.record_down(20, 4500);
  CHECK(mgr.get_active_client() == 20); // Lease successfully reassigned to Client B
}

TEST_CASE("Touch Lease: Production TouchLeaseManager client disconnect immediately clears lease") {
  TouchLeaseManager mgr;

  // Client A acquires lease
  CHECK(mgr.can_process_down(10, 1000, false) == true);
  mgr.record_down(10, 1000);
  CHECK(mgr.get_active_client() == 10);

  // Client A abruptly disconnects
  mgr.handle_disconnect(10);
  CHECK(mgr.get_active_client() == 0);

  // Client B can immediately acquire lease without waiting 3 seconds
  CHECK(mgr.can_process_down(20, 1050, false) == true);
  mgr.record_down(20, 1050);
  CHECK(mgr.get_active_client() == 20);
}
