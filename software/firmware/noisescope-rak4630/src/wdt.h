#pragma once
#include <Arduino.h>
#include <nrf_wdt.h>
#include "config.h"

// nRF52840 hardware watchdog. Once started it cannot be stopped or
// reconfigured until reset — which is exactly what we want in a firmware
// that must never strand a remote node.

static inline void wdt_init() {
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
                    (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = WDT_TIMEOUT_S * 32768UL;
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}

static inline void wdt_feed() {
  NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}
