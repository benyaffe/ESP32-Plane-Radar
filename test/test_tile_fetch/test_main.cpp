// Unity tests for services::tile_fetch::shouldFetch — the pure decision
// function that determines whether to trigger an HTTPS download.
//
// The actual HTTPS fetch can't be exercised from native (no WiFi), but
// the "should we fetch?" contract is: never-fetched → yes; tile key
// changed → yes; same tile → no. Getting this wrong either burns
// bandwidth (fetching every loop) or leaves the map stale after a
// location change.
//
// Run via `pio test -e native_test`.

#include <cstdint>
#include <unity.h>

#include "services/tile_fetch.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

void setUp(void) {}
void tearDown(void) {}

void test_shouldFetch_true_when_never_fetched(void) {
  TEST_ASSERT_TRUE(services::tile_fetch::shouldFetch(
      7, 37.7552, -122.4528, 0, 0, 0, /*have_last=*/false));
}

void test_shouldFetch_false_when_location_still_in_same_tile(void) {
  // SF Bay Area tile at z=7 is (20, 37).
  TEST_ASSERT_FALSE(services::tile_fetch::shouldFetch(
      7, 37.7552, -122.4528, 7, 20, 37, /*have_last=*/true));
}

void test_shouldFetch_false_for_tiny_move_within_tile(void) {
  // Move a few meters (still in tile (20, 37) at z=7).
  TEST_ASSERT_FALSE(services::tile_fetch::shouldFetch(
      7, 37.7600, -122.4500, 7, 20, 37, /*have_last=*/true));
}

void test_shouldFetch_true_when_moved_to_neighbor_tile(void) {
  // NYC is far from SF — different z=7 tile.
  TEST_ASSERT_TRUE(services::tile_fetch::shouldFetch(
      7, 40.6413, -73.7781, 7, 20, 37, /*have_last=*/true));
}

void test_shouldFetch_true_when_zoom_changed(void) {
  // Same lat/lon, but different requested zoom.
  TEST_ASSERT_TRUE(services::tile_fetch::shouldFetch(
      5, 37.7552, -122.4528, 7, 20, 37, /*have_last=*/true));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_shouldFetch_true_when_never_fetched);
  RUN_TEST(test_shouldFetch_false_when_location_still_in_same_tile);
  RUN_TEST(test_shouldFetch_false_for_tiny_move_within_tile);
  RUN_TEST(test_shouldFetch_true_when_moved_to_neighbor_tile);
  RUN_TEST(test_shouldFetch_true_when_zoom_changed);
  return UNITY_END();
}
