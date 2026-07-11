// Unity tests for services::tap_sensor::classifyIntSource — the
// pure INT_SOURCE-byte → TapEvents mapping the ADXL345 driver leans
// on. Doesn't touch Wire; the whole point of extracting this
// classifier was so the "prefer Double when both bits are set" rule
// is locked against silent regressions.
//
// Run via `pio test -e native_test`.

#include <cstdint>
#include <unity.h>

#include "services/tap_sensor.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

using services::tap_sensor::classifyIntSource;
using services::tap_sensor::kIntDoubleTap;
using services::tap_sensor::kIntSingleTap;
using services::tap_sensor::TapEvents;

void setUp(void) {}
void tearDown(void) {}

void test_empty_source_reports_no_taps(void) {
  const TapEvents e = classifyIntSource(0x00);
  TEST_ASSERT_FALSE(e.single);
  TEST_ASSERT_FALSE(e.double_tap);
}

void test_single_bit_alone_reports_single(void) {
  const TapEvents e = classifyIntSource(kIntSingleTap);
  TEST_ASSERT_TRUE(e.single);
  TEST_ASSERT_FALSE(e.double_tap);
}

void test_double_bit_alone_reports_double(void) {
  const TapEvents e = classifyIntSource(kIntDoubleTap);
  TEST_ASSERT_FALSE(e.single);
  TEST_ASSERT_TRUE(e.double_tap);
}

void test_both_bits_prefers_double(void) {
  // The ADXL345 asserts SINGLE_TAP on the second tap of a double-tap
  // sequence AND asserts DOUBLE_TAP for the same event. Reporting
  // Single here would fire two gestures from one physical pair of
  // knocks — the whole point of this rule is to suppress that.
  const TapEvents e = classifyIntSource(kIntSingleTap | kIntDoubleTap);
  TEST_ASSERT_FALSE(e.single);
  TEST_ASSERT_TRUE(e.double_tap);
}

void test_defensive_all_bits_still_prefers_double(void) {
  // If some other unmasked interrupt shares the byte (activity,
  // freefall, watermark…), the tap classification should still hold.
  const TapEvents e = classifyIntSource(0xFF);
  TEST_ASSERT_FALSE(e.single);
  TEST_ASSERT_TRUE(e.double_tap);
}

void test_unrelated_bits_report_no_taps(void) {
  // Set every bit EXCEPT the two tap bits. Should report nothing.
  const uint8_t src = static_cast<uint8_t>(~(kIntSingleTap | kIntDoubleTap));
  const TapEvents e = classifyIntSource(src);
  TEST_ASSERT_FALSE(e.single);
  TEST_ASSERT_FALSE(e.double_tap);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_source_reports_no_taps);
  RUN_TEST(test_single_bit_alone_reports_single);
  RUN_TEST(test_double_bit_alone_reports_double);
  RUN_TEST(test_both_bits_prefers_double);
  RUN_TEST(test_defensive_all_bits_still_prefers_double);
  RUN_TEST(test_unrelated_bits_report_no_taps);
  return UNITY_END();
}
