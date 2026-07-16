// Tests for services::night_mode. Pure function coverage:
//   - HH:MM window math (same-day / overnight / empty / disabled)
//   - clock-not-synced guard
//   - tap-wake grace period
//   - HH:MM string parsing / formatting round-trip

#include <unity.h>

#include <cstring>
#include <ctime>

#include "services/night_mode.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

namespace nm = services::night_mode;

// Helpers ---------------------------------------------------------------

// Build a Unix epoch (UTC) for a given date + HH:MM. Using timegm-style
// math directly so tests don't depend on the local TZ of the machine
// running them. The night_mode module treats its input as an already-
// shifted local epoch, so we just fabricate an epoch that represents
// the local wall-clock the tests care about.
static std::time_t localAt(int year, int month, int day, int hh, int mm) {
  std::tm tm = {};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hh;
  tm.tm_min = mm;
  tm.tm_sec = 0;
#if defined(_WIN32)
  return _mkgmtime(&tm);
#else
  return timegm(&tm);
#endif
}

// A moment safely past SNTP sync but with a stable, well-known wall
// clock. 2026-03-15 is a Sunday, plenty into the year so no year<2024
// or DST-transition weirdness sneaks into the fixtures.
static std::time_t at(int hh, int mm) { return localAt(2026, 3, 15, hh, mm); }

void setUp(void) { nm::reset(); }
void tearDown(void) {}

// Schedule window ---------------------------------------------------------

void test_disabled_schedule_never_sleeps(void) {
  nm::setSchedule(-1, -1);
  TEST_ASSERT_FALSE(nm::shouldSleep(at(3, 0)));
  TEST_ASSERT_FALSE(nm::shouldSleep(at(23, 30)));
}

void test_one_endpoint_disabled_never_sleeps(void) {
  nm::setSchedule(2200, -1);
  TEST_ASSERT_FALSE(nm::shouldSleep(at(23, 0)));
  nm::setSchedule(-1, 700);
  TEST_ASSERT_FALSE(nm::shouldSleep(at(6, 0)));
}

void test_same_day_window_sleeps_only_inside(void) {
  // 14:00 → 15:00, unusual but supported.
  nm::setSchedule(1400, 1500);
  TEST_ASSERT_FALSE(nm::shouldSleep(at(13, 59)));
  TEST_ASSERT_TRUE (nm::shouldSleep(at(14, 0)));
  TEST_ASSERT_TRUE (nm::shouldSleep(at(14, 30)));
  TEST_ASSERT_FALSE(nm::shouldSleep(at(15, 0)));  // wake point is exclusive
  TEST_ASSERT_FALSE(nm::shouldSleep(at(15, 1)));
}

void test_overnight_window_spans_midnight(void) {
  // The common case: 22:00 → 07:00.
  nm::setSchedule(2200, 700);
  TEST_ASSERT_FALSE(nm::shouldSleep(at(21, 59)));
  TEST_ASSERT_TRUE (nm::shouldSleep(at(22, 0)));
  TEST_ASSERT_TRUE (nm::shouldSleep(at(23, 0)));
  TEST_ASSERT_TRUE (nm::shouldSleep(at(0, 30)));
  TEST_ASSERT_TRUE (nm::shouldSleep(at(6, 59)));
  TEST_ASSERT_FALSE(nm::shouldSleep(at(7, 0)));
  TEST_ASSERT_FALSE(nm::shouldSleep(at(12, 0)));
}

void test_empty_window_never_sleeps(void) {
  nm::setSchedule(1000, 1000);
  TEST_ASSERT_FALSE(nm::shouldSleep(at(9, 59)));
  TEST_ASSERT_FALSE(nm::shouldSleep(at(10, 0)));
  TEST_ASSERT_FALSE(nm::shouldSleep(at(10, 30)));
}

// Clock-sync guard --------------------------------------------------------

void test_pre_2024_epoch_never_sleeps(void) {
  // Even with a real overnight window set, an epoch before 2024-01-01
  // means SNTP hasn't handed us reliable time — better to keep the
  // screen on than randomly go dark at boot.
  nm::setSchedule(2200, 700);
  TEST_ASSERT_FALSE(nm::shouldSleep(0));                  // Unix epoch
  TEST_ASSERT_FALSE(nm::shouldSleep(localAt(1970, 1, 1, 23, 0)));
  TEST_ASSERT_FALSE(nm::shouldSleep(localAt(2023, 12, 31, 23, 59)));
}

// Tap wake grace ----------------------------------------------------------

void test_bumpWake_reopens_screen_inside_the_window(void) {
  nm::setSchedule(2200, 700);
  const std::time_t t = at(22, 30);
  TEST_ASSERT_TRUE(nm::shouldSleep(t));
  nm::bumpWake(t, 60);
  TEST_ASSERT_FALSE(nm::shouldSleep(t + 30));   // still inside grace
  TEST_ASSERT_FALSE(nm::shouldSleep(t + 59));
  TEST_ASSERT_TRUE (nm::shouldSleep(t + 61));   // grace expired
}

void test_bumpWake_before_sync_is_ignored(void) {
  // No wake bump can happen before SNTP has locked — otherwise we'd
  // stash a fake wake_until_epoch that later becomes "the past" once
  // real time arrives.
  nm::setSchedule(2200, 700);
  nm::bumpWake(0, 60);                    // pre-sync bump: no-op
  TEST_ASSERT_TRUE(nm::shouldSleep(at(23, 0)));
}

void test_setSchedule_clears_pending_wake_bump(void) {
  nm::setSchedule(2200, 700);
  nm::bumpWake(at(22, 30), 60);
  // Reconfiguring the schedule resets the wake latch so a stale bump
  // from before doesn't keep the display on past the new window.
  nm::setSchedule(2300, 800);
  TEST_ASSERT_TRUE(nm::shouldSleep(at(23, 30)));
}

// HH:MM parse / format ---------------------------------------------------

void test_parseHhmm_valid_shapes(void) {
  TEST_ASSERT_EQUAL_INT(2200, nm::parseHhmm("22:00"));
  TEST_ASSERT_EQUAL_INT(700,  nm::parseHhmm("07:00"));
  TEST_ASSERT_EQUAL_INT(0,    nm::parseHhmm("00:00"));
  TEST_ASSERT_EQUAL_INT(2359, nm::parseHhmm("23:59"));
  TEST_ASSERT_EQUAL_INT(730,  nm::parseHhmm(" 7:30"));  // leading ws + single digit hh
}

void test_parseHhmm_rejects_garbage(void) {
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm(""));
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm(nullptr));
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm("24:00"));
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm("12:60"));
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm("noon"));
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm("1200"));   // missing colon
  TEST_ASSERT_EQUAL_INT(-1, nm::parseHhmm(":30"));    // missing hh
}

void test_formatHhmm_round_trips(void) {
  char buf[16];
  nm::formatHhmm(2200, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("22:00", buf);
  nm::formatHhmm(700, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("07:00", buf);
  nm::formatHhmm(-1, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);          // disabled → empty field
  nm::formatHhmm(9999, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);          // invalid → empty (safe)
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_schedule_never_sleeps);
  RUN_TEST(test_one_endpoint_disabled_never_sleeps);
  RUN_TEST(test_same_day_window_sleeps_only_inside);
  RUN_TEST(test_overnight_window_spans_midnight);
  RUN_TEST(test_empty_window_never_sleeps);
  RUN_TEST(test_pre_2024_epoch_never_sleeps);
  RUN_TEST(test_bumpWake_reopens_screen_inside_the_window);
  RUN_TEST(test_bumpWake_before_sync_is_ignored);
  RUN_TEST(test_setSchedule_clears_pending_wake_bump);
  RUN_TEST(test_parseHhmm_valid_shapes);
  RUN_TEST(test_parseHhmm_rejects_garbage);
  RUN_TEST(test_formatHhmm_round_trips);
  return UNITY_END();
}
