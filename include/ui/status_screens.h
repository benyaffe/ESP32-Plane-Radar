#pragma once

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();

/** Persistent "no network" banner: shown when Wi-Fi drops mid-session so the
 *  user gets a clear signal instead of a stale radar frame. Not a partial
 *  render — the whole screen is replaced. */
void statusScreenOffline();

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();

/** Night-mode splash. Shown for a few seconds either before a scheduled
 *  sleep transition or on boot inside the sleep window, giving the user
 *  a chance to tap and stay awake. `schedule_line` is a short descriptor
 *  like "22:00 – 07:00" that appears under the main text; pass "" to
 *  hide it. Dim amber on black so it's not jarring at bedtime. */
void statusScreenNightNotice(const char* schedule_line);
