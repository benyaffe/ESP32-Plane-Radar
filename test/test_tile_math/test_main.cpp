// Unity tests for data::tile::tileOfLatLon — the on-device mirror of
// the Python tile_scheme.tile_of() function.
//
// Contract: for any lat/lon, the returned (x, y) must fall inside the
// world grid at zoom z, and must match what scripts/tile_scheme.py
// would compute (verified via the sample known-tile fixtures below).
//
// Run via `pio test -e native_test`.

#include <cstdint>
#include <unity.h>

#include "data/tile_math.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

void setUp(void) {}
void tearDown(void) {}

void test_tiles_per_side_doubles_per_zoom(void) {
  TEST_ASSERT_EQUAL_UINT32(1u, data::tile::tilesPerSide(0));
  TEST_ASSERT_EQUAL_UINT32(8u, data::tile::tilesPerSide(3));
  TEST_ASSERT_EQUAL_UINT32(128u, data::tile::tilesPerSide(7));
}

void test_render_zoom_constant_matches_pipeline(void) {
  // Pipeline (scripts/tile_scheme.py::ZOOM_LEVELS) ships z=3, 5, 7.
  // The device renders from z=7 tiles (~310 km each) — comfortably
  // wider than the widest 25 nm range preset. If this constant drifts
  // out of the pipeline's set, the device would ask for tiles that
  // don't exist on the CDN.
  TEST_ASSERT_EQUAL_UINT8(7, data::tile::kRenderZoom);
}

void test_sf_bay_area_maps_to_known_tile(void) {
  // Sutro Tower (37.7552, -122.4528). Python:
  //   x = floor((-122.4528 + 180) / 2.8125) = 20
  //   y = floor((90 - 37.7552) / 1.40625) = 37
  uint16_t x = 0, y = 0;
  data::tile::tileOfLatLon(7, 37.7552, -122.4528, &x, &y);
  TEST_ASSERT_EQUAL_UINT16(20, x);
  TEST_ASSERT_EQUAL_UINT16(37, y);
}

void test_prime_meridian_zero_lat_zero_lon_z3(void) {
  // (0, 0) at z=3 (8×8 grid, 45°×22.5° tiles) — should be (4, 4).
  uint16_t x = 0, y = 0;
  data::tile::tileOfLatLon(3, 0.0, 0.0, &x, &y);
  TEST_ASSERT_EQUAL_UINT16(4, x);
  TEST_ASSERT_EQUAL_UINT16(4, y);
}

void test_northwest_corner_maps_to_tile_zero_zero(void) {
  uint16_t x = 0, y = 0;
  data::tile::tileOfLatLon(3, 89.999, -180.0, &x, &y);
  TEST_ASSERT_EQUAL_UINT16(0, x);
  TEST_ASSERT_EQUAL_UINT16(0, y);
}

void test_southeast_corner_maps_to_last_tile(void) {
  uint16_t x = 0, y = 0;
  data::tile::tileOfLatLon(3, -89.999, 179.999, &x, &y);
  TEST_ASSERT_EQUAL_UINT16(7, x);
  TEST_ASSERT_EQUAL_UINT16(7, y);
}

void test_exact_poles_clamp_to_edge_row(void) {
  uint16_t x = 0, y = 0;
  data::tile::tileOfLatLon(3, 90.0, 0.0, &x, &y);
  TEST_ASSERT_EQUAL_UINT16(0, y);
  data::tile::tileOfLatLon(3, -90.0, 0.0, &x, &y);
  TEST_ASSERT_EQUAL_UINT16(7, y);
}

void test_longitude_wrap(void) {
  // 181° longitude equals -179° after wrap. Both must land in the
  // same tile.
  uint16_t x_east = 0, y = 0;
  uint16_t x_ref = 0;
  data::tile::tileOfLatLon(3, 0.0, 181.0, &x_east, &y);
  data::tile::tileOfLatLon(3, 0.0, -179.0, &x_ref, &y);
  TEST_ASSERT_EQUAL_UINT16(x_ref, x_east);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_tiles_per_side_doubles_per_zoom);
  RUN_TEST(test_render_zoom_constant_matches_pipeline);
  RUN_TEST(test_sf_bay_area_maps_to_known_tile);
  RUN_TEST(test_prime_meridian_zero_lat_zero_lon_z3);
  RUN_TEST(test_northwest_corner_maps_to_tile_zero_zero);
  RUN_TEST(test_southeast_corner_maps_to_last_tile);
  RUN_TEST(test_exact_poles_clamp_to_edge_row);
  RUN_TEST(test_longitude_wrap);
  return UNITY_END();
}
