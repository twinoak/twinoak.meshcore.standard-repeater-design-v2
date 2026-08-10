#pragma once
// ---------------------------------------------------------------------------
// NoiseScope build-time configuration.
// Every value can be overridden with -D <NAME>=<value> in platformio.ini.
// ---------------------------------------------------------------------------

#ifndef FW_NAME
  #define FW_NAME "noisescope"
#endif
#ifndef FW_VERSION
  #define FW_VERSION "0.1.0"
#endif

// ---- SX1262 wiring (RAK4630/RAK4631, identical to MeshCore's rak4631 variant) ----
#ifndef P_LORA_NSS
  #define P_LORA_NSS    42    // P1.10
#endif
#ifndef P_LORA_DIO_1
  #define P_LORA_DIO_1  47    // P1.15
#endif
#ifndef P_LORA_RESET
  #define P_LORA_RESET  38    // P1.06
#endif
#ifndef P_LORA_BUSY
  #define P_LORA_BUSY   46    // P1.14
#endif
#ifndef P_LORA_SCLK
  #define P_LORA_SCLK   43    // P1.11
#endif
#ifndef P_LORA_MISO
  #define P_LORA_MISO   45    // P1.13
#endif
#ifndef P_LORA_MOSI
  #define P_LORA_MOSI   44    // P1.12
#endif

#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE  1.8f
#ifndef SX126X_CURRENT_LIMIT
  #define SX126X_CURRENT_LIMIT    140.0f
#endif
#ifndef RX_BOOSTED_GAIN_DEFAULT
  #define RX_BOOSTED_GAIN_DEFAULT 1     // match MeshCore rak4631 build (SX126X_RX_BOOSTED_GAIN=1)
#endif

// ---- Home channel (what MeshCore normally listens on; monitor mode uses this) ----
#ifndef HOME_FREQ_MHZ
  #define HOME_FREQ_MHZ 869.618f        // MeshCore DK standard, EU Narrow
#endif
#ifndef HOME_BW_KHZ
  #define HOME_BW_KHZ   62.5f
#endif
#ifndef HOME_SF
  #define HOME_SF       8
#endif
#ifndef HOME_CR
  #define HOME_CR       8
#endif

// ---- Walter management UART ----
// TwinOak RAK4630 adapter: J4/J5 jumpers 1-2 -> Serial1 (P0.15 RX / P0.16 TX, default),
// jumpers 2-3 -> Serial2 (P0.19 / P0.20). Override WALTER_SERIAL to Serial2 if strapped so.
#ifndef WALTER_SERIAL
  #define WALTER_SERIAL Serial1
#endif
#ifndef WALTER_BAUD
  #define WALTER_BAUD   115200          // per INTERCONNECT.md (2x 1000 pF bulkhead filters are fine at this rate)
#endif

// ---- Default sweep (also used by the periodic auto-sweep) ----
#ifndef SWEEP_F0_MHZ
  #define SWEEP_F0_MHZ  863.0f          // full EU868 SRD band
#endif
#ifndef SWEEP_F1_MHZ
  #define SWEEP_F1_MHZ  870.0f
#endif
#ifndef SWEEP_STEP_KHZ
  #define SWEEP_STEP_KHZ 25.0f
#endif
#ifndef SWEEP_DWELL_MS
  #define SWEEP_DWELL_MS 20
#endif
#ifndef AUTO_SWEEP_MIN_DEFAULT
  #define AUTO_SWEEP_MIN_DEFAULT 10     // minutes between unattended sweeps; 0 = off
#endif

// ---- Reporting cadence / limits ----
#ifndef STAT_INTERVAL_MS
  #define STAT_INTERVAL_MS 10000        // heartbeat "stat" line
#endif
#ifndef MON_SAMPLE_US
  #define MON_SAMPLE_US    1000         // monitor/dwell RSSI sampling pace (~1 kHz)
#endif
#ifndef BURST_TH_DELTA_DB
  #define BURST_TH_DELTA_DB 10.0f       // duty-cycle threshold = window min + this
#endif
#define MAX_SWEEP_POINTS  512
#define HIST_BINS         33            // fixed by the SX126x spectral-scan engine

// ---- I2C peripherals on the TwinOak boards ----
#define RTC_ADDR   0x52                 // RV-3028-C7 on the RAK adapter
#define INA_ADDR   0x42                 // INA3221 on connector board B (A0 tied to SDA)
#ifndef INA_SHUNT_MILLIOHM
  #define INA_SHUNT_MILLIOHM 20         // RS2/RS3/RS4 20 mOhm per BOM
#endif
#ifndef I2C_CLOCK_HZ
  #define I2C_CLOCK_HZ 100000           // 1k pullups through bulkhead filter: keep at/below 100 kHz
#endif

// ---- Watchdog ----
#ifndef WDT_TIMEOUT_S
  #define WDT_TIMEOUT_S 8
#endif
