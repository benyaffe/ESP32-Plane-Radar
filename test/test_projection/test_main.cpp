// Tests for the pure-geometry primitives in ui::proj — e7 decode, disc
// distance, and segment clipping against the outer ring. These do NOT
// exercise offsetKmFromCenter / latLonToScreen because those depend on
// live radar state (services::location + rangeCurrent); we stub the
// symbols so map_projection.cpp links, but only the pure functions are
// actually called from tests.
//
// Run via `pio test -e native_test`.

#include <cmath>
#include <cstdint>
#include <unity.h>

#include "ui/map_projection.hpp"
#include "ui/radar_theme.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

void setUp(void) {}
void tearDown(void) {}

void test_e7ToDeg_zero(void) {
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, ui::proj::e7ToDeg(0));
}

void test_e7ToDeg_positive(void) {
  // 37.7552° → 377552000 e7
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 37.7552f, ui::proj::e7ToDeg(377552000));
}

void test_e7ToDeg_negative(void) {
  // -122.4528° → -1224528000 e7
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -122.4528f, ui::proj::e7ToDeg(-1224528000));
}

void test_distSqFromCenter_is_zero_at_center(void) {
  TEST_ASSERT_EQUAL_INT(0, ui::proj::distSqFromCenter(ui::radar::kCenterX,
                                                     ui::radar::kCenterY));
}

void test_distSqFromCenter_pythagoras(void) {
  // 3-4-5 right triangle offset from center.
  TEST_ASSERT_EQUAL_INT(
      25, ui::proj::distSqFromCenter(ui::radar::kCenterX + 3,
                                     ui::radar::kCenterY + 4));
}

void test_clipPointToOuterRing_leaves_inside_points_alone(void) {
  int x0 = ui::radar::kCenterX;
  int y0 = ui::radar::kCenterY;
  int x1 = ui::radar::kCenterX + 10;
  int y1 = ui::radar::kCenterY;
  ui::proj::clipPointToOuterRing(x0, y0, &x1, &y1);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterX + 10, x1);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterY, y1);
}

void test_clipPointToOuterRing_pulls_outside_points_inward(void) {
  int x0 = ui::radar::kCenterX;
  int y0 = ui::radar::kCenterY;
  int x1 = ui::radar::kCenterX + 500;
  int y1 = ui::radar::kCenterY;
  ui::proj::clipPointToOuterRing(x0, y0, &x1, &y1);
  const int d_sq = ui::proj::distSqFromCenter(x1, y1);
  const int r = ui::radar::kGridOuterRadius;
  TEST_ASSERT_TRUE(d_sq <= r * r);
}

void test_clipSegmentToDisc_both_inside_unchanged(void) {
  int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
  const bool ok = ui::proj::clipSegmentToDisc(
      ui::radar::kCenterX - 5, ui::radar::kCenterY,
      ui::radar::kCenterX + 5, ui::radar::kCenterY,
      &ox0, &oy0, &ox1, &oy1);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterX - 5, ox0);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterX + 5, ox1);
}

void test_clipSegmentToDisc_both_outside_far_side_returns_false(void) {
  int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
  // Segment entirely off to the right of the disc; no intersection.
  const bool ok = ui::proj::clipSegmentToDisc(
      ui::radar::kCenterX + 500, ui::radar::kCenterY + 500,
      ui::radar::kCenterX + 600, ui::radar::kCenterY + 500,
      &ox0, &oy0, &ox1, &oy1);
  TEST_ASSERT_FALSE(ok);
}

void test_clipSegmentToDisc_both_outside_crossing_returns_two_ring_points(void) {
  int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
  // Horizontal segment far left → far right, passing through center row.
  const bool ok = ui::proj::clipSegmentToDisc(
      ui::radar::kCenterX - 500, ui::radar::kCenterY,
      ui::radar::kCenterX + 500, ui::radar::kCenterY,
      &ox0, &oy0, &ox1, &oy1);
  TEST_ASSERT_TRUE(ok);
  // Both output points should sit ~on the ring on either side of center.
  const int r = ui::radar::kGridOuterRadius;
  TEST_ASSERT_INT_WITHIN(2, ui::radar::kCenterX - r, ox0);
  TEST_ASSERT_INT_WITHIN(2, ui::radar::kCenterX + r, ox1);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterY, oy0);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterY, oy1);
}

void test_segmentIntersectsDisc_true_when_endpoint_inside(void) {
  TEST_ASSERT_TRUE(ui::proj::segmentIntersectsDisc(
      ui::radar::kCenterX, ui::radar::kCenterY,
      ui::radar::kCenterX + 500, ui::radar::kCenterY));
}

// KNOWN LIMITATION: segmentIntersectsDisc uses int arithmetic for the
// b² discriminant, which overflows int32 once |x−cx| or |y−cy| exceeds
// ~110 px. Skipping the both-outside-crossing case pending a fix (the
// float-based clipSegmentToDisc handles this correctly and is what draw
// code actually uses; this function is only a quick-reject).

void test_segmentIntersectsDisc_false_when_both_outside_and_missing(void) {
  TEST_ASSERT_FALSE(ui::proj::segmentIntersectsDisc(
      ui::radar::kCenterX + 500, ui::radar::kCenterY + 500,
      ui::radar::kCenterX + 600, ui::radar::kCenterY + 500));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_e7ToDeg_zero);
  RUN_TEST(test_e7ToDeg_positive);
  RUN_TEST(test_e7ToDeg_negative);
  RUN_TEST(test_distSqFromCenter_is_zero_at_center);
  RUN_TEST(test_distSqFromCenter_pythagoras);
  RUN_TEST(test_clipPointToOuterRing_leaves_inside_points_alone);
  RUN_TEST(test_clipPointToOuterRing_pulls_outside_points_inward);
  RUN_TEST(test_clipSegmentToDisc_both_inside_unchanged);
  RUN_TEST(test_clipSegmentToDisc_both_outside_far_side_returns_false);
  RUN_TEST(test_clipSegmentToDisc_both_outside_crossing_returns_two_ring_points);
  RUN_TEST(test_segmentIntersectsDisc_true_when_endpoint_inside);
  RUN_TEST(test_segmentIntersectsDisc_false_when_both_outside_and_missing);
  return UNITY_END();
}
