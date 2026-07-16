#pragma once

#include <cstdint>

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();

// Toggle the panel + backlight for the night-mode quiet-hours feature.
// `on=false` sets brightness to 0 (LED off). `on=true` restores
// brightness to 255; caller re-renders after.
void displaySetPowered(bool on);
