#pragma once

// Bedside-quiet-hours sleep for the display. During the user-configured
// window (e.g. 22:00 → 07:00 local) the backlight is off; a single tap
// on the case wakes the screen for a grace period, then it goes dark
// again if the window is still active. Wi-Fi + ADS-B polling keep
// running — this is a "no glow at night" feature, not a low-power one.

#include <ctime>

namespace services::night_mode {

// Load persisted schedule from NVS. Missing values = disabled (never
// sleep). Safe to call before Wi-Fi / SNTP have come up.
void init();

// hh_mm is HH*100 + MM in 24 h form (e.g. 22:30 → 2230). Pass -1 for
// either arg to disable (empty portal field = disabled). Persists to NVS.
void setSchedule(int sleep_hhmm, int wake_hhmm);

// Current setting (or -1 when disabled). Portal reads these to prefill
// its two time inputs.
int sleepHhmm();
int wakeHhmm();

// True if the panel should be dark right now. `local_epoch` is
// UTC-seconds shifted by the home's utc_offset_seconds (see
// services::outdoor_temp::cached().utcOffsetSec). Returns false when:
//   - either endpoint is -1
//   - local_epoch pre-dates 2024 (SNTP hasn't synced yet)
//   - a recent tap bumped wake_until_epoch past now
bool shouldSleep(std::time_t local_epoch);

// Called from the main loop when the tap sensor latches an event and
// we're in the sleep window: extends the awake window by `seconds`.
void bumpWake(std::time_t local_epoch, int seconds = 60);

// Test hook — clears the wake-until bump and returns the module to its
// disabled defaults. Not called from production code.
void reset();

// Portal helpers: convert between the on-disk int and the HH:MM string
// the <input type="time"> field uses. Empty / malformed input parses to
// -1 (disabled); -1 formats to "".
int parseHhmm(const char* s);
void formatHhmm(int hhmm, char* out, size_t out_len);

}  // namespace services::night_mode
