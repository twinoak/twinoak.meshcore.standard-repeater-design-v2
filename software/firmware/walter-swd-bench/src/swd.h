// Bit-banged ARM SWD (ADIv5 SW-DP, MEM-AP), written for a slow, filtered line: 470 ohm + ~2.2 nF on
// every signal (INTERCONNECT.md). Static timing, so a pause anywhere only stretches the clock.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "link.h"

namespace swd {
enum Err : int {
  OK = 0,
  ERR_WAIT = -1,      // target answered WAIT too many times
  ERR_FAULT = -2,     // target answered FAULT (sticky error; cleared again)
  ERR_PROTOCOL = -3,  // no valid ACK: nobody answered, or the line is broken
  ERR_PARITY = -4,    // read data with bad parity
  ERR_TIMEOUT = -5,
};

// DP registers, A[3:2]
constexpr uint8_t DP_DPIDR = 0x0;   // read
constexpr uint8_t DP_ABORT = 0x0;   // write
constexpr uint8_t DP_CTRLSTAT = 0x4;
constexpr uint8_t DP_SELECT = 0x8;
constexpr uint8_t DP_RDBUFF = 0xC;

// MEM-AP registers
constexpr uint8_t AP_CSW = 0x00;
constexpr uint8_t AP_TAR = 0x04;
constexpr uint8_t AP_DRW = 0x0C;
constexpr uint8_t AP_IDR = 0xFC;

// CSW: 32-bit access, auto-increment single, debug master, privileged data access.
constexpr uint32_t CSW_WORD_INC = 0x23000052u;

const char* errName(int e);
}  // namespace swd

class Swd {
 public:
  explicit Swd(Link& link) : l_(link) {}

  // Line reset, JTAG-to-SWD, DPIDR read, clear sticky errors, debug + system power-up.
  int connect(uint32_t* dpidr);

  int dpRead(uint8_t a, uint32_t* v);
  int dpWrite(uint8_t a, uint32_t v);
  int apRead(uint8_t ap, uint8_t reg, uint32_t* v);   // complete read (posted read + RDBUFF)
  int apWrite(uint8_t ap, uint8_t reg, uint32_t v);

  // Memory through AP 0 (AHB-AP). Blocks re-write TAR at every 1 KB boundary.
  int read32(uint32_t addr, uint32_t* v);
  int write32(uint32_t addr, uint32_t v);
  int readBlock(uint32_t addr, uint32_t* w, uint32_t n);
  int writeBlock(uint32_t addr, const uint32_t* w, uint32_t n);

  const char* error() const { return err_; }
  uint32_t waits() const { return waits_; }

 private:
  int raw(bool ap, bool read, uint8_t a, uint32_t* data);       // one transaction, no retry
  int transfer(bool ap, bool read, uint8_t a, uint32_t* data);  // WAIT retry, FAULT clean-up
  int select(uint8_t ap, uint8_t reg);
  int apr(uint8_t ap, uint8_t reg, uint32_t* v);  // posted read + RDBUFF, no sticky check
  int apw(uint8_t ap, uint8_t reg, uint32_t v);
  int sticky(const char* op, uint32_t addr);       // AP errors are posted: read CTRL/STAT after
  int memSetup();
  void writeBits(uint32_t v, int n);
  uint32_t readBits(int n);
  void cycle();
  void lineReset();
  int fail(int code, const char* fmt, ...);

  Link& l_;
  uint32_t select_ = 0;
  bool selectValid_ = false;
  bool cswValid_ = false;
  uint32_t waits_ = 0;
  char err_[112] = "";
};
