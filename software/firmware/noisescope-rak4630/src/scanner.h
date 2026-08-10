#pragma once
#include <Arduino.h>

// All radio work lives here. One activity at a time; MODE_MONITOR is the
// resting state (LoRa RX on the home channel, continuous RSSI sampling —
// i.e. exactly what the MeshCore radio sees when it idles).

enum ScanMode : uint8_t { MODE_MONITOR = 0, MODE_SWEEP, MODE_HIST, MODE_DWELL, MODE_CAD };

struct MonStats {
  uint32_t n;
  float avg, mx, mn;
  float dc;      // fraction of samples above threshold
  float th;      // threshold used (prev window min + BURST_TH_DELTA_DB)
};

bool scan_init();
void scan_service();               // call every loop() iteration
ScanMode scan_mode();
const char* scan_mode_name();

// passes = 0 means "run until stop"
bool scan_start_sweep(float f0, float f1, float step_khz, uint16_t dwell_ms, int passes);
bool scan_start_hist(float f0, float f1, float step_khz, uint16_t samples, int passes);
bool scan_start_dwell(float f_mhz, uint32_t secs, uint32_t win_ms);
bool scan_start_cad(float f_mhz, uint16_t count);
void scan_stop();                  // abort current activity, return to monitor

bool scan_set_home(float f_mhz, float bw_khz, uint8_t sf, uint8_t cr);
bool scan_set_boost(bool on);
bool scan_get_boost();
void scan_get_home(float* f, float* bw, uint8_t* sf, uint8_t* cr);
bool scan_radio_ok();

// Monitor-window statistics (reset=true clears the accumulator)
bool scan_get_monitor_stats(MonStats* s, bool reset);

// Implemented in main.cpp: drains serial RX during blocking scan steps so
// commands are never lost (they execute between steps, from loop()).
extern void input_poll();
