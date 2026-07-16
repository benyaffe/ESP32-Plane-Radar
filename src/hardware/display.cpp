#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}

void displaySetPowered(bool on) {
  // Backlight only — leaving the panel content intact means wake is a
  // cheap brightness bump with no re-init. Any renders that landed
  // during sleep are hidden by the dark backlight until we come back.
  tft.setBrightness(on ? 255 : 0);
}
