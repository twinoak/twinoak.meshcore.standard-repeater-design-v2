// A simulated nRF52840 on the far end of the SWD lines, bit by bit: SW-DP (line reset, DPIDR-first,
// turnaround, ACK, parity, sticky errors, posted AP reads, WAIT), AHB-AP (CSW, TAR with 1 KB wrap, DRW),
// CTRL-AP (ERASEALL, RESET, APPROTECTSTATUS), NVMC write rules, FICR, UICR, DHCSR and both kinds of
// APPROTECT. It implements Link, so the real swd.cpp / nrf52.cpp run against it unchanged.
#pragma once
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "link.h"

struct SimConfig {
  uint32_t dpidr = 0x2BA01477u;
  uint32_t variant = 0x41414630u;   // "AAF0": revision 3, improved APPROTECT
  bool locked = false;              // factory firmware closed the port
  bool bootloaderOpensPort = true;  // the bootloader copies UICR.APPROTECT -> APPROTECT.DISABLE (MDK >= 8.40.2)
  int waitAfterFlashWrite = 2;      // AP accesses answered WAIT after every flash/UICR word write
  int eraseBusyPolls = 3;           // ERASEALLSTATUS reads that still say busy
};

class SimNrf52 : public Link {
 public:
  explicit SimNrf52(const SimConfig& c);

  // Link
  void swclk(bool high) override;
  void swdio(bool high) override { hostLevel_ = high; }
  bool swdioIn() override;
  void swdioDrive(bool drive) override;
  void halfPeriod() override {}
  void nreset(bool assert) override;
  void releaseAll() override;
  void sleepMs(uint32_t ms) override { now_ += ms; }
  uint32_t ms() override { return now_; }
  void out(const char* line) override;

  // What the test inspects
  std::vector<uint32_t> flash;  // 1 MB
  std::vector<uint32_t> uicr;   // 4 KB
  bool locked;
  std::string log;
  bool echo = false;
  int contention = 0;       // both ends drove SWDIO at once
  int hostErrors = 0;       // things only a host bug causes
  int doubleWrites = 0;     // a flash word written twice without an erase
  int ffWrites = 0;         // 0xFFFFFFFF written (wasted, harmless)
  int waitsGiven = 0;
  int pinResets = 0;
  int eraseAlls = 0;
  uint64_t edges = 0;        // SWCLK cycles: x 10 us = time at 100 kHz

 private:
  enum State { RESETTING, LOCKOUT, IDLE, REQ, TRN1, ACK, RDATA, WTRN, WDATA, TRN_TO_HOST };
  void edge();
  void startTransaction();
  uint32_t respond();  // ACK for the request just parsed; for OK reads it sets rdata_
  void finishWrite(uint32_t v);
  bool apRead(uint32_t* v);
  bool apWrite(uint32_t v);
  bool memRead(uint32_t addr, uint32_t* v);
  bool memWrite(uint32_t addr, uint32_t v);
  void eraseAll();
  void softReset();
  void pinReset();
  bool legacy() const;

  SimConfig cfg_;
  uint32_t now_ = 0;
  // line
  bool clk_ = false, hostDrive_ = false, hostLevel_ = true, tgtDrive_ = false, tgtLevel_ = true;
  bool nresetLow_ = false;
  int ones_ = 0;
  State st_ = LOCKOUT;
  uint32_t req_ = 0, reqN_ = 0, ack_ = 0, ackIdx_ = 0, rdata_ = 0, wdata_ = 0, idx_ = 0;
  bool ap_ = false, rnw_ = false;
  uint8_t a_ = 0;
  bool needDpidr_ = true;
  // DP
  uint32_t pwrReq_ = 0, sticky_ = 0, select_ = 0, rdbuff_ = 0;
  int busy_ = 0;
  // APs
  uint32_t csw_ = 0, tar_ = 0;
  bool ctrlResetHeld_ = false;
  int eraseBusy_ = 0;
  // core / NVMC
  uint32_t nvmcConfig_ = 0;
  bool halted_ = false;
  std::vector<uint8_t> writes_;  // per flash/UICR word since the last erase
  std::map<uint32_t, uint32_t> ram_;
};
