#include "services/night_mode.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifndef UNIT_TEST
#ifndef USE_NATIVE
#include <Arduino.h>
#include <Preferences.h>
#endif
#endif

namespace services::night_mode {
namespace {

constexpr char kNamespace[] = "night";
constexpr char kKeySleep[]  = "sleep";
constexpr char kKeyWake[]   = "wake";

// Anything before 2024-01-01 in Unix epoch: assume SNTP hasn't synced.
constexpr std::time_t kMinPlausibleEpoch = 1704067200L;

int s_sleep_hhmm = -1;
int s_wake_hhmm  = -1;
std::time_t s_wake_until_epoch = 0;

bool validHhmm(int v) {
  if (v == -1) return true;
  if (v < 0 || v > 2359) return false;
  const int mm = v % 100;
  const int hh = v / 100;
  return hh <= 23 && mm <= 59;
}

int nowHhmm(std::time_t local_epoch) {
  std::tm tm = {};
#ifdef _WIN32
  gmtime_s(&tm, &local_epoch);
#else
  std::tm* p = std::gmtime(&local_epoch);
  if (p != nullptr) tm = *p;
#endif
  return tm.tm_hour * 100 + tm.tm_min;
}

// The user's window is [sleep, wake). Two shapes:
//   - sleep < wake  → same-day (e.g. 14:00 → 15:00): sleep when in [sleep, wake)
//   - sleep > wake  → overnight (e.g. 22:00 → 07:00): sleep when >= sleep OR < wake
//   - sleep == wake → empty window: never sleep (probably means user forgot to
//                     clear a stale value)
bool insideWindow(int now, int sleep, int wake) {
  if (sleep == wake) return false;
  if (sleep < wake) return now >= sleep && now < wake;
  return now >= sleep || now < wake;
}

void persist() {
#if !defined(UNIT_TEST) && !defined(USE_NATIVE)
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putInt(kKeySleep, s_sleep_hhmm);
  prefs.putInt(kKeyWake, s_wake_hhmm);
  prefs.end();
#endif
}

}  // namespace

void init() {
  s_sleep_hhmm = -1;
  s_wake_hhmm = -1;
  s_wake_until_epoch = 0;
#if !defined(UNIT_TEST) && !defined(USE_NATIVE)
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  const int sleep = prefs.getInt(kKeySleep, -1);
  const int wake  = prefs.getInt(kKeyWake, -1);
  prefs.end();
  if (validHhmm(sleep)) s_sleep_hhmm = sleep;
  if (validHhmm(wake))  s_wake_hhmm  = wake;
#endif
}

void setSchedule(int sleep_hhmm, int wake_hhmm) {
  const int prev_sleep = s_sleep_hhmm;
  const int prev_wake  = s_wake_hhmm;
  s_sleep_hhmm = validHhmm(sleep_hhmm) ? sleep_hhmm : -1;
  s_wake_hhmm  = validHhmm(wake_hhmm)  ? wake_hhmm  : -1;
  s_wake_until_epoch = 0;
  if (prev_sleep == s_sleep_hhmm && prev_wake == s_wake_hhmm) return;
  persist();
}

int sleepHhmm() { return s_sleep_hhmm; }
int wakeHhmm()  { return s_wake_hhmm; }

void reset() {
  s_sleep_hhmm = -1;
  s_wake_hhmm = -1;
  s_wake_until_epoch = 0;
}

bool shouldSleep(std::time_t local_epoch) {
  if (s_sleep_hhmm == -1 || s_wake_hhmm == -1) return false;
  if (local_epoch < kMinPlausibleEpoch) return false;  // clock not synced
  if (local_epoch < s_wake_until_epoch) return false;  // tap-wake grace
  return insideWindow(nowHhmm(local_epoch), s_sleep_hhmm, s_wake_hhmm);
}

void bumpWake(std::time_t local_epoch, int seconds) {
  if (local_epoch < kMinPlausibleEpoch) return;
  if (seconds <= 0) seconds = 15;
  s_wake_until_epoch = local_epoch + seconds;
}

int parseHhmm(const char* s) {
  if (s == nullptr) return -1;
  // Skip leading whitespace.
  while (*s == ' ' || *s == '\t') ++s;
  if (*s == '\0') return -1;
  // Accept HH:MM only. HTML <input type="time"> hands us this format,
  // and we don't need to be clever about anything else.
  const char* p = s;
  int hh = 0;
  int digits_h = 0;
  while (*p >= '0' && *p <= '9' && digits_h < 2) {
    hh = hh * 10 + (*p - '0');
    ++p; ++digits_h;
  }
  if (digits_h == 0 || *p != ':') return -1;
  ++p;
  int mm = 0;
  int digits_m = 0;
  while (*p >= '0' && *p <= '9' && digits_m < 2) {
    mm = mm * 10 + (*p - '0');
    ++p; ++digits_m;
  }
  if (digits_m == 0) return -1;
  if (hh > 23 || mm > 59) return -1;
  return hh * 100 + mm;
}

void formatHhmm(int hhmm, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) return;
  if (hhmm < 0 || !validHhmm(hhmm)) { out[0] = '\0'; return; }
  std::snprintf(out, out_len, "%02d:%02d", hhmm / 100, hhmm % 100);
}

}  // namespace services::night_mode
