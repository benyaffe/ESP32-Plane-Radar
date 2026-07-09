#pragma once

// Optional BME280 (temperature + humidity + pressure). Probes I²C addresses
// 0x77 then 0x76 at init; if neither responds, all future reads return
// valid=false silently. Wire the sensor to the pins in config::kBmeSdaPin
// / kBmeSclPin, otherwise leave the pins unconnected — nothing else on the
// board uses them.

namespace services::env_sensor {

struct Reading {
  float tempF;         // degrees Fahrenheit
  float humidityPct;   // 0..100
  bool  valid;         // true if the sensor answered on the last read
};

/** Probe I²C at 0x77 then 0x76. Safe to call every boot even when no
 *  sensor is present. */
void init();

/** Latest sensor read. `valid` is false when no sensor is attached or the
 *  chip stopped responding mid-run. */
Reading read();

}  // namespace services::env_sensor
