#pragma once
#include <Arduino.h>

// RV-3028-C7 RTC (RAK adapter, 0x52) and INA3221 (connector board B, 0x42).
// Both are optional: everything degrades gracefully when absent so the same
// firmware runs on a bare RAK4631 on the bench.

bool tele_init();          // probe the bus; returns true if at least one device found
bool rtc_ok();
bool ina_ok();

// Wall-clock time. Backed by the RV-3028 UNIX-time counter when present,
// otherwise by millis() offset from a `time <epoch>` command. Returns 0 when
// time has never been set (Walter should then re-stamp on receipt).
uint32_t time_epoch();
bool time_set(uint32_t epoch);   // also writes the RTC when present

// INA3221 channel read, ch = 1..3 (ch1 panel, ch2 battery, ch3 load), each with
// its own shunt value (config.h). bus_v in volts (measured at IN-: VIN / VBAT / VLOAD+),
// cur_ma in mA: ch1 positive = panel into charger, ch2 positive = charging (negative =
// discharging), ch3 positive = load draw.
bool ina_read(uint8_t ch, float* bus_v, float* cur_ma);
