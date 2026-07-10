// Tests for services::weather::deriveCategory — the FAA VFR/MVFR/IFR/LIFR
// rule. Pure function; no HTTP, no JSON, no ArduinoJson dependency. Run
// via `pio test -e native_test`.

#include <climits>
#include <unity.h>

#include "services/weather.h"
#include "services/weather_category.h"

// The native_test env compiles map_projection.cpp for every test binary;
// this test doesn't call it, but the linker still needs its stubs.
#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

using services::weather::Category;
using services::weather::deriveCategory;

void setUp(void) {}
void tearDown(void) {}

void test_no_ceiling_and_high_vis_is_VFR(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::VFR),
                    static_cast<int>(deriveCategory(INT32_MAX, 10)));
}

void test_ceiling_3000_ft_is_MVFR_upper_boundary(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::MVFR),
                    static_cast<int>(deriveCategory(3000, 10)));
}

void test_ceiling_3001_ft_is_VFR_just_above_MVFR(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::VFR),
                    static_cast<int>(deriveCategory(3001, 10)));
}

void test_ceiling_999_ft_is_IFR_just_below_MVFR(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::IFR),
                    static_cast<int>(deriveCategory(999, 10)));
}

void test_ceiling_499_ft_is_LIFR_just_below_IFR(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::LIFR),
                    static_cast<int>(deriveCategory(499, 10)));
}

void test_visibility_5_sm_is_MVFR_upper_boundary(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::MVFR),
                    static_cast<int>(deriveCategory(INT32_MAX, 5)));
}

void test_visibility_2_sm_is_IFR(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::IFR),
                    static_cast<int>(deriveCategory(INT32_MAX, 2)));
}

void test_visibility_0_sm_is_LIFR(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(Category::LIFR),
                    static_cast<int>(deriveCategory(INT32_MAX, 0)));
}

void test_worst_of_ceiling_and_vis_wins_bad_vis(void) {
  // Ceiling VFR (5000), visibility LIFR (0) → LIFR wins.
  TEST_ASSERT_EQUAL(static_cast<int>(Category::LIFR),
                    static_cast<int>(deriveCategory(5000, 0)));
}

void test_worst_of_ceiling_and_vis_wins_bad_ceiling(void) {
  // Ceiling IFR (800), visibility VFR (10) → IFR wins.
  TEST_ASSERT_EQUAL(static_cast<int>(Category::IFR),
                    static_cast<int>(deriveCategory(800, 10)));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_no_ceiling_and_high_vis_is_VFR);
  RUN_TEST(test_ceiling_3000_ft_is_MVFR_upper_boundary);
  RUN_TEST(test_ceiling_3001_ft_is_VFR_just_above_MVFR);
  RUN_TEST(test_ceiling_999_ft_is_IFR_just_below_MVFR);
  RUN_TEST(test_ceiling_499_ft_is_LIFR_just_below_IFR);
  RUN_TEST(test_visibility_5_sm_is_MVFR_upper_boundary);
  RUN_TEST(test_visibility_2_sm_is_IFR);
  RUN_TEST(test_visibility_0_sm_is_LIFR);
  RUN_TEST(test_worst_of_ceiling_and_vis_wins_bad_vis);
  RUN_TEST(test_worst_of_ceiling_and_vis_wins_bad_ceiling);
  return UNITY_END();
}
