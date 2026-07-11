#pragma once

#include <cstdint>

// Optional accelerometer-based knock-the-case input.
//
// When present, an ADXL345 (I²C, address 0x53) sits inside the
// enclosure and reports SINGLE_TAP + DOUBLE_TAP events via its
// hardware tap-detection registers. The device's BOOT button is
// buried inside the retro case, so this becomes the user's primary
// input surface: tap the console to adjust the current screen,
// double-tap to advance the ring.
//
// Silent-fail on missing hardware — mirrors services::env_sensor's
// probe-on-init pattern. If no chip answers at 0x53, this module
// stays inert and the BOOT button remains the only input path.

namespace services::tap_sensor {

/** Probe for the chip at boot; configure tap-detection thresholds. */
void init();

/** Poll the chip's INT_SOURCE register; latch any SINGLE_TAP /
 *  DOUBLE_TAP bits into internal flags. Call every loop tick. */
void poll();

/** True if the chip latched a single-tap event since the last call.
 *  Consumes the flag. Always false when no chip is present. */
bool consumeSingleTap();

/** True if the chip latched a double-tap event since the last call.
 *  Consumes the flag. Always false when no chip is present. */
bool consumeDoubleTap();

// --- Pure logic, exposed for off-device unit testing ------------------
// ADXL345 INT_SOURCE bit positions per the datasheet.
constexpr uint8_t kIntSingleTap = 0x40;
constexpr uint8_t kIntDoubleTap = 0x20;

struct TapEvents {
  bool single;
  bool double_tap;
};

/** Interpret one INT_SOURCE byte. When both bits are set the ADXL345
 *  is signaling "this second tap completed a double-tap sequence" —
 *  we prefer Double and drop the redundant Single to avoid firing two
 *  gestures for one pair of knocks. */
TapEvents classifyIntSource(uint8_t src);

}  // namespace services::tap_sensor
