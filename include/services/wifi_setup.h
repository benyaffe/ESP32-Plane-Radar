#pragma once

#include <cstdint>

/** True when the next boot should show the setup screen first (after credential reset). */
bool wifiShowsSetupScreenOnBoot();
void wifiResetCredentialsAndReboot();
/** Boot flow: connect with UI, open portal only if saved creds fail. */
bool wifiSetupConnect();
/** Reconnect using saved creds; never opens the captive portal. */
bool wifiReconnect();
/** Keeps the LAN config portal alive; call every loop() iteration. */
void wifiLoop();
bool wifiBootButtonPressed();
/** GPIO + interrupt setup; call once early in setup(). */
void bootButtonInit();
/** Latched short tap (survives blocking HTTP/display work). */
bool bootButtonConsumeTap();
/** Call each loop iteration; triggers WiFi reset on long hold. */
void bootButtonPollLongPress();

/** Tap-pattern discriminator.
 *  Single = cycle range, Double = cycle focus, Triple = enter/exit
 *  weather-map view. Discrimination window is ~400 ms — the tradeoff for
 *  triple support is that Single fires ~150 ms later than the earlier
 *  single-vs-double-only design. */
enum class BootTap : uint8_t { None, Single, Double, Triple };
/** Consume the pending tap event (if any). Call once per loop after
 *  bootButtonPollLongPress(). */
BootTap bootButtonConsumeEvent();
