#include "swd.h"

#include <stdarg.h>
#include <stdio.h>

using namespace swd;

namespace {
constexpr uint32_t ACK_OK = 1, ACK_WAIT = 2, ACK_FAULT = 4;
constexpr int kWaitRetries = 2000;  // ~1 s at 100 kHz
constexpr int kIdleCycles = 8;      // after every transaction, so posted accesses complete

inline uint32_t parity32(uint32_t v) { return (uint32_t)__builtin_popcount(v) & 1u; }
}  // namespace

const char* swd::errName(int e) {
  switch (e) {
    case OK: return "ok";
    case ERR_WAIT: return "WAIT (target busy)";
    case ERR_FAULT: return "FAULT";
    case ERR_PROTOCOL: return "no ACK (nobody answered)";
    case ERR_PARITY: return "parity error";
    case ERR_TIMEOUT: return "timeout";
    default: return "?";
  }
}

int Swd::fail(int code, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err_, sizeof err_, fmt, ap);
  va_end(ap);
  return code;
}

// The host changes SWDIO while SWCLK is high; the target samples on the rising edge.
void Swd::writeBits(uint32_t v, int n) {
  for (int i = 0; i < n; i++) {
    l_.swdio((v >> i) & 1u);
    l_.swclk(false);
    l_.halfPeriod();
    l_.swclk(true);
    l_.halfPeriod();
  }
}

// The target changes SWDIO after the rising edge; the host samples at the end of the low phase.
uint32_t Swd::readBits(int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) {
    l_.swclk(false);
    l_.halfPeriod();
    if (l_.swdioIn()) v |= 1u << i;
    l_.swclk(true);
    l_.halfPeriod();
  }
  return v;
}

void Swd::cycle() {
  l_.swclk(false);
  l_.halfPeriod();
  l_.swclk(true);
  l_.halfPeriod();
}

void Swd::lineReset() {
  l_.swdioDrive(true);
  writeBits(0xFFFFFFFFu, 32);
  writeBits(0xFFFFFFFFu, 24);  // 56 ones
}

int Swd::raw(bool ap, bool read, uint8_t a, uint32_t* data) {
  uint32_t req = 0x81u | (ap ? 0x02u : 0u) | (read ? 0x04u : 0u) | ((a & 0x04u) << 1) | ((a & 0x08u) << 1);
  req |= parity32(req & 0x1Eu) << 5;

  l_.swdioDrive(true);
  writeBits(req, 8);
  l_.swdioDrive(false);
  cycle();  // turnaround: the target takes the line
  uint32_t ack = readBits(3);

  if (ack == ACK_OK && read) {
    uint32_t v = readBits(32);
    uint32_t p = readBits(1);
    cycle();  // turnaround: back to the host
    l_.swdioDrive(true);
    writeBits(0, kIdleCycles);
    if (parity32(v) != p) return ERR_PARITY;
    *data = v;
    return OK;
  }
  cycle();  // turnaround: the target lets go after its ACK
  l_.swdioDrive(true);
  if (ack == ACK_OK) {
    writeBits(*data, 32);
    writeBits(parity32(*data), 1);
    writeBits(0, kIdleCycles);
    return OK;
  }
  writeBits(0, kIdleCycles);
  if (ack == ACK_WAIT) return ERR_WAIT;
  if (ack == ACK_FAULT) return ERR_FAULT;
  return ERR_PROTOCOL;
}

int Swd::transfer(bool ap, bool read, uint8_t a, uint32_t* data) {
  const char* who = ap ? "AP" : "DP";
  const char* dir = read ? "read" : "write";
  for (int i = 0; i < kWaitRetries; i++) {
    int r = raw(ap, read, a, data);
    if (r == ERR_WAIT) {
      waits_++;
      continue;
    }
    if (r == OK) return OK;
    if (r == ERR_FAULT) {
      uint32_t cs = 0, clear = 0x1Eu;  // clear STKCMP, STKERR, WDERR, ORUNERR
      raw(false, true, DP_CTRLSTAT, &cs);
      raw(false, false, DP_ABORT, &clear);
      return fail(r, "%s %s 0x%02X: FAULT (CTRL/STAT 0x%08X)", who, dir, a, (unsigned)cs);
    }
    selectValid_ = false;  // the line is in an unknown state; connect() again
    cswValid_ = false;
    return fail(r, "%s %s 0x%02X: %s", who, dir, a, errName(r));
  }
  uint32_t clear = 0x1Fu;  // DAPABORT + clear sticky errors
  raw(false, false, DP_ABORT, &clear);
  return fail(ERR_WAIT, "%s %s 0x%02X: WAIT %d times, aborted", who, dir, a, kWaitRetries);
}

int Swd::connect(uint32_t* dpidr) {
  selectValid_ = false;
  cswValid_ = false;
  l_.swdio(true);
  l_.swclk(true);
  l_.swdioDrive(true);
  lineReset();
  writeBits(0xE79Eu, 16);  // JTAG-to-SWD select (harmless on an SWD-only DP)
  lineReset();
  writeBits(0, kIdleCycles);

  uint32_t id = 0;
  int r = raw(false, true, DP_DPIDR, &id);  // after a line reset the DP answers nothing else
  if (r) return fail(r, "no answer to the DPIDR read: %s", errName(r));
  if (dpidr) *dpidr = id;

  if ((r = dpWrite(DP_ABORT, 0x1Eu))) return r;
  if ((r = dpWrite(DP_SELECT, 0))) return r;
  if ((r = dpWrite(DP_CTRLSTAT, 0x50000000u))) return r;  // CSYSPWRUPREQ | CDBGPWRUPREQ
  for (int i = 0;; i++) {
    uint32_t cs = 0;
    if ((r = dpRead(DP_CTRLSTAT, &cs))) return r;
    if ((cs & 0xA0000000u) == 0xA0000000u) break;
    if (i >= 100) return fail(ERR_TIMEOUT, "debug power-up not acknowledged (CTRL/STAT 0x%08X)", (unsigned)cs);
    l_.sleepMs(1);
  }
  return OK;
}

int Swd::dpRead(uint8_t a, uint32_t* v) { return transfer(false, true, a, v); }

int Swd::dpWrite(uint8_t a, uint32_t v) {
  int r = transfer(false, false, a, &v);
  if (a == DP_SELECT) {
    select_ = v;
    selectValid_ = (r == OK);
  }
  return r;
}

int Swd::select(uint8_t ap, uint8_t reg) {
  uint32_t v = ((uint32_t)ap << 24) | (reg & 0xF0u);
  if (selectValid_ && v == select_) return OK;
  return dpWrite(DP_SELECT, v);
}

// AP accesses are posted: a faulting read still ACKs OK and RDBUFF returns junk, a faulting write
// ACKs OK. The error only shows as STICKYERR, so every public operation ends with a CTRL/STAT check.
int Swd::sticky(const char* op, uint32_t addr) {
  uint32_t cs = 0;
  int r = dpRead(DP_CTRLSTAT, &cs);
  if (r) return r;
  if (cs & 0xA2u) {  // WDATAERR | STICKYERR | STICKYORUN
    dpWrite(DP_ABORT, 0x1Eu);
    return fail(ERR_FAULT, "%s 0x%08X: sticky error (CTRL/STAT 0x%08X)", op, (unsigned)addr, (unsigned)cs);
  }
  return OK;
}

int Swd::apr(uint8_t ap, uint8_t reg, uint32_t* v) {
  int r;
  uint32_t posted = 0;
  if ((r = select(ap, reg))) return r;
  if ((r = transfer(true, true, reg & 0x0Cu, &posted))) return r;
  return transfer(false, true, DP_RDBUFF, v);
}

int Swd::apw(uint8_t ap, uint8_t reg, uint32_t v) {
  int r;
  if ((r = select(ap, reg))) return r;
  return transfer(true, false, reg & 0x0Cu, &v);
}

int Swd::apRead(uint8_t ap, uint8_t reg, uint32_t* v) {
  int r = apr(ap, reg, v);
  return r ? r : sticky("AP read", ((uint32_t)ap << 24) | reg);
}

int Swd::apWrite(uint8_t ap, uint8_t reg, uint32_t v) {
  int r = apw(ap, reg, v);
  return r ? r : sticky("AP write", ((uint32_t)ap << 24) | reg);
}

int Swd::memSetup() {
  if (cswValid_) return OK;
  int r = apWrite(0, AP_CSW, CSW_WORD_INC);
  cswValid_ = (r == OK);
  return r;
}

int Swd::read32(uint32_t addr, uint32_t* v) {
  int r;
  if ((r = memSetup())) return r;
  if ((r = apw(0, AP_TAR, addr))) return r;
  if ((r = apr(0, AP_DRW, v))) return r;
  return sticky("read", addr);
}

int Swd::write32(uint32_t addr, uint32_t v) {
  int r;
  if ((r = memSetup())) return r;
  if ((r = apw(0, AP_TAR, addr))) return r;
  if ((r = apw(0, AP_DRW, v))) return r;
  return sticky("write", addr);
}

int Swd::readBlock(uint32_t addr, uint32_t* w, uint32_t n) {
  int r;
  if ((r = memSetup())) return r;
  while (n) {
    uint32_t k = (0x400u - (addr & 0x3FFu)) / 4u;  // TAR auto-increment is only good within 1 KB
    if (k > n) k = n;
    if ((r = apw(0, AP_TAR, addr))) return r;
    uint32_t posted = 0;
    if ((r = transfer(true, true, AP_DRW, &posted))) return r;  // returns the previous read: discard
    for (uint32_t i = 1; i < k; i++)
      if ((r = transfer(true, true, AP_DRW, &w[i - 1]))) return r;
    if ((r = transfer(false, true, DP_RDBUFF, &w[k - 1]))) return r;
    if ((r = sticky("read", addr))) return r;
    addr += 4u * k;
    w += k;
    n -= k;
  }
  return OK;
}

int Swd::writeBlock(uint32_t addr, const uint32_t* w, uint32_t n) {
  int r;
  if ((r = memSetup())) return r;
  while (n) {
    uint32_t k = (0x400u - (addr & 0x3FFu)) / 4u;
    if (k > n) k = n;
    if ((r = apw(0, AP_TAR, addr))) return r;
    for (uint32_t i = 0; i < k; i++) {
      uint32_t v = w[i];
      if ((r = transfer(true, false, AP_DRW, &v))) return r;
    }
    if ((r = sticky("write", addr))) return r;
    addr += 4u * k;
    w += k;
    n -= k;
  }
  return OK;
}
