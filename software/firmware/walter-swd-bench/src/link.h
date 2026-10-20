// Everything the SWD engine needs from the board it runs on.
//
// The Walter build implements it on the Walter's GPIOs (main.cpp); the host test implements it on a
// simulated nRF52840 (host-test/sim_target.cpp). swd.cpp and nrf52.cpp only ever talk to this.
#pragma once
#include <stdint.h>

class Link {
 public:
  virtual ~Link() {}

  // SWD lines. Bit timing is the engine's job; these only set and read levels.
  virtual void swclk(bool high) = 0;
  virtual void swdio(bool high) = 0;        // output level, effective while the Walter drives SWDIO
  virtual bool swdioIn() = 0;
  virtual void swdioDrive(bool drive) = 0;  // true: the Walter drives SWDIO; false: Hi-Z
  virtual void halfPeriod() = 0;            // half an SWCLK period

  // nRESET is open-drain: assert pulls it low, release leaves it Hi-Z. It is never driven high.
  virtual void nreset(bool assert) = 0;

  // Every RAK-facing line Hi-Z (SWCLK, SWDIO, nRESET, UART). The kill line is not part of this.
  virtual void releaseAll() = 0;

  // Services.
  virtual void sleepMs(uint32_t ms) = 0;
  virtual uint32_t ms() = 0;
  virtual void out(const char* line) = 0;   // one console line, without the newline
};
