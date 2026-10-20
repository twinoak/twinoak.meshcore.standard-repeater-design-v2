#include "sim_target.h"

#include <stdio.h>

#include <algorithm>

namespace {
constexpr uint32_t ACK_OK = 1, ACK_WAIT = 2, ACK_FAULT = 4;
constexpr uint32_t STICKYORUN = 1u << 1, STICKYCMP = 1u << 4, STICKYERR = 1u << 5, WDATAERR = 1u << 7;
constexpr uint32_t UICR_BASE = 0x10001000u;
constexpr uint32_t APPROTECT_IDX = (0x10001208u - UICR_BASE) / 4u;
inline uint32_t par(uint32_t v) { return (uint32_t)__builtin_popcount(v) & 1u; }
}  // namespace

SimNrf52::SimNrf52(const SimConfig& c)
    : flash(0x40000, 0), uicr(0x400, 0xFFFFFFFFu), locked(c.locked), cfg_(c), writes_(0x40400, 0) {
  uint32_t x = 0x2545F491u;  // factory firmware ("RUI3"): every flash word non-blank
  for (auto& w : flash) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w = x | 1u;
  }
  uicr[(0x10001014u - UICR_BASE) / 4] = 0x000F4000u;
  uicr[(0x10001200u - UICR_BASE) / 4] = 18;  // PSELRESET
  uicr[(0x10001204u - UICR_BASE) / 4] = 18;
  if (legacy())
    uicr[APPROTECT_IDX] = locked ? 0xFFFFFF00u : 0xFFFFFFFFu;
  else
    uicr[APPROTECT_IDX] = locked ? 0xFFFFFFFFu : 0x5Au;
}

bool SimNrf52::legacy() const {
  uint32_t c = (cfg_.variant >> 8) & 0xFFu;
  return c >= 'A' && c <= 'D';
}

void SimNrf52::out(const char* line) {
  log += line;
  log += '\n';
  if (echo) puts(line);
}

void SimNrf52::swclk(bool h) {
  if (!clk_ && h) edge();
  clk_ = h;
}

bool SimNrf52::swdioIn() {
  if (tgtDrive_) return tgtLevel_;
  if (hostDrive_) return hostLevel_;
  return true;  // nRF pull-up
}

void SimNrf52::swdioDrive(bool d) {
  if (d && tgtDrive_) contention++;
  hostDrive_ = d;
}

void SimNrf52::nreset(bool a) {
  if (a) {
    nresetLow_ = true;
  } else if (nresetLow_) {
    nresetLow_ = false;
    pinReset();
  }
}

void SimNrf52::releaseAll() {
  hostDrive_ = false;
  clk_ = false;  // nRF pull-down on SWDCLK
  nreset(false);
}

// One rising SWCLK edge: the target samples SWDIO, then changes what it drives.
void SimNrf52::edge() {
  edges++;
  if (hostDrive_ && tgtDrive_) contention++;
  bool bit = hostDrive_ ? hostLevel_ : (tgtDrive_ ? tgtLevel_ : true);
  if (!tgtDrive_ && bit) {
    if (++ones_ >= 50) {
      st_ = RESETTING;
      return;
    }
  } else {
    ones_ = 0;
  }
  switch (st_) {
    case RESETTING:
      st_ = IDLE;  // the first low bit after >= 50 highs
      needDpidr_ = true;
      break;
    case LOCKOUT:
      break;  // a protocol error: only a line reset gets out
    case IDLE:
      if (bit) {
        st_ = REQ;
        req_ = 1;
        reqN_ = 1;
      }
      break;
    case REQ:
      req_ |= (uint32_t)bit << reqN_;
      if (++reqN_ == 8) startTransaction();
      break;
    case TRN1:
      tgtDrive_ = true;
      tgtLevel_ = ack_ & 1u;
      ackIdx_ = 1;
      st_ = ACK;
      break;
    case ACK:
      if (ackIdx_ < 3) {
        tgtLevel_ = (ack_ >> ackIdx_) & 1u;
        ackIdx_++;
      } else if (ack_ == ACK_OK && rnw_) {
        tgtLevel_ = rdata_ & 1u;
        idx_ = 1;
        st_ = RDATA;
      } else {
        tgtDrive_ = false;
        st_ = (ack_ == ACK_OK) ? WTRN : TRN_TO_HOST;
      }
      break;
    case RDATA:
      if (idx_ < 32) {
        tgtLevel_ = (rdata_ >> idx_) & 1u;
        idx_++;
      } else if (idx_ == 32) {
        tgtLevel_ = par(rdata_);
        idx_++;
      } else {
        tgtDrive_ = false;
        st_ = TRN_TO_HOST;
      }
      break;
    case WTRN:
      st_ = WDATA;
      idx_ = 0;
      wdata_ = 0;
      break;
    case WDATA:
      if (!hostDrive_) hostErrors++;
      if (idx_ < 32) {
        wdata_ |= (uint32_t)bit << idx_;
        idx_++;
      } else {
        if ((uint32_t)bit != par(wdata_)) {
          sticky_ |= WDATAERR;
          hostErrors++;
        } else {
          finishWrite(wdata_);
        }
        st_ = IDLE;
      }
      break;
    case TRN_TO_HOST:
      st_ = IDLE;
      break;
  }
}

void SimNrf52::startTransaction() {
  uint32_t start = req_ & 1u, apndp = (req_ >> 1) & 1u, rnw = (req_ >> 2) & 1u, a2 = (req_ >> 3) & 1u,
           a3 = (req_ >> 4) & 1u, p = (req_ >> 5) & 1u, stop = (req_ >> 6) & 1u, park = (req_ >> 7) & 1u;
  if (!start || stop || !park || p != par(req_ & 0x1Eu)) {
    st_ = LOCKOUT;  // not a request: no answer
    return;
  }
  ap_ = apndp;
  rnw_ = rnw;
  a_ = (uint8_t)((a2 << 2) | (a3 << 3));
  if (needDpidr_ && !(!ap_ && rnw_ && a_ == 0)) {
    hostErrors++;  // after a line reset only a DPIDR read is answered
    st_ = LOCKOUT;
    return;
  }
  ack_ = respond();
  st_ = TRN1;
}

uint32_t SimNrf52::respond() {
  if (!ap_) {
    if (!rnw_) return ACK_OK;  // DP writes happen after the data phase; ABORT never waits
    switch (a_) {
      case 0x0:
        rdata_ = cfg_.dpidr;
        needDpidr_ = false;
        return ACK_OK;
      case 0x4: {
        uint32_t v = pwrReq_ | sticky_;
        if (pwrReq_ & (1u << 28)) v |= 1u << 29;
        if (pwrReq_ & (1u << 30)) v |= 1u << 31;
        rdata_ = v;
        return ACK_OK;
      }
      case 0x8:
        rdata_ = 0;  // RESEND
        return ACK_OK;
      default:  // RDBUFF
        if (busy_ > 0) {
          busy_--;
          waitsGiven++;
          return ACK_WAIT;
        }
        rdata_ = rdbuff_;
        return ACK_OK;
    }
  }
  if (busy_ > 0) {
    busy_--;
    waitsGiven++;
    return ACK_WAIT;
  }
  if (sticky_ & (STICKYERR | WDATAERR)) return ACK_FAULT;
  if ((pwrReq_ & 0x50000000u) != 0x50000000u) {
    hostErrors++;  // AP access before the debug power-up
    return ACK_FAULT;
  }
  if (rnw_) {  // posted: this read returns the previous one; a fault only sets STICKYERR
    uint32_t v = 0xDEADBEEFu;
    if (!apRead(&v)) {
      sticky_ |= STICKYERR;
      v = 0xDEADBEEFu;
    }
    rdata_ = rdbuff_;
    rdbuff_ = v;
  }
  return ACK_OK;
}

void SimNrf52::finishWrite(uint32_t v) {
  if (!ap_) {
    switch (a_) {
      case 0x0:  // ABORT
        if (v & 1u) busy_ = 0;
        if (v & (1u << 1)) sticky_ &= ~STICKYCMP;
        if (v & (1u << 2)) sticky_ &= ~STICKYERR;
        if (v & (1u << 3)) sticky_ &= ~WDATAERR;
        if (v & (1u << 4)) sticky_ &= ~STICKYORUN;
        break;
      case 0x4:
        pwrReq_ = v & 0x50000000u;
        break;
      case 0x8:
        select_ = v;
        break;
      default:
        break;
    }
    return;
  }
  if (!apWrite(v)) sticky_ |= STICKYERR;
}

bool SimNrf52::apRead(uint32_t* v) {
  uint32_t apsel = select_ >> 24, reg = (select_ & 0xF0u) | a_;
  if (apsel == 0) {
    if (reg == 0xFC) {
      *v = 0x24770011u;
      return true;
    }
    if (locked) return false;
    switch (reg) {
      case 0x00: *v = csw_; return true;
      case 0x04: *v = tar_; return true;
      case 0x0C: {
        if ((csw_ & 7u) != 2u) hostErrors++;
        bool ok = memRead(tar_, v);
        if ((csw_ & 0x30u) == 0x10u) tar_ = (tar_ & ~0x3FFu) | ((tar_ + 4u) & 0x3FFu);
        return ok;
      }
      default: *v = 0; return true;
    }
  }
  if (apsel == 1) {
    switch (reg) {
      case 0x00: *v = ctrlResetHeld_ ? 1u : 0u; return true;
      case 0x08:
        if (eraseBusy_ > 0) {
          eraseBusy_--;
          *v = 1;
        } else {
          *v = 0;
        }
        return true;
      case 0x0C: *v = locked ? 0u : 1u; return true;
      case 0xFC: *v = 0x02880000u; return true;
      default: *v = 0; return true;
    }
  }
  *v = 0;
  return true;
}

bool SimNrf52::apWrite(uint32_t v) {
  uint32_t apsel = select_ >> 24, reg = (select_ & 0xF0u) | a_;
  if (apsel == 0) {
    if (locked) return false;
    switch (reg) {
      case 0x00: csw_ = v; return true;
      case 0x04: tar_ = v; return true;
      case 0x0C: {
        if ((csw_ & 7u) != 2u) hostErrors++;
        bool ok = memWrite(tar_, v);
        if ((csw_ & 0x30u) == 0x10u) tar_ = (tar_ & ~0x3FFu) | ((tar_ + 4u) & 0x3FFu);
        return ok;
      }
      default: return true;
    }
  }
  if (apsel == 1) {
    switch (reg) {
      case 0x00:
        if (v & 1u) {
          ctrlResetHeld_ = true;
        } else if (ctrlResetHeld_) {
          ctrlResetHeld_ = false;
          softReset();
        }
        return true;
      case 0x04:
        if (v & 1u) eraseAll();
        return true;
      default: return true;
    }
  }
  return true;
}

bool SimNrf52::memRead(uint32_t a, uint32_t* v) {
  if (a & 3u) {
    hostErrors++;
    return false;
  }
  if (a < 0x00100000u) {
    *v = flash[a / 4];
    return true;
  }
  if (a >= 0x10000000u && a < 0x10001000u) {
    switch (a) {
      case 0x10000010u: *v = 4096; break;
      case 0x10000014u: *v = 256; break;
      case 0x10000060u: *v = 0x89ABCDEFu; break;
      case 0x10000064u: *v = 0x01234567u; break;
      case 0x10000100u: *v = 0x00052840u; break;
      case 0x10000104u: *v = cfg_.variant; break;
      case 0x10000108u: *v = 0x2004u; break;
      case 0x1000010Cu: *v = 256; break;
      case 0x10000110u: *v = 1024; break;
      default: *v = 0xFFFFFFFFu; break;
    }
    return true;
  }
  if (a >= UICR_BASE && a < 0x10002000u) {
    *v = uicr[(a - UICR_BASE) / 4];
    return true;
  }
  if (a == 0x4001E400u || a == 0x4001E408u) {
    *v = 1;
    return true;
  }
  if (a == 0x4001E504u) {
    *v = nvmcConfig_;
    return true;
  }
  if (a == 0xE000EDF0u) {
    *v = (halted_ ? (1u << 17) | 3u : 0u);
    return true;
  }
  if (a >= 0x20000000u && a < 0x20040000u) {
    *v = ram_[a];
    return true;
  }
  return false;  // bus error
}

bool SimNrf52::memWrite(uint32_t a, uint32_t v) {
  if (a & 3u) {
    hostErrors++;
    return false;
  }
  bool isFlash = a < 0x00100000u, isUicr = a >= UICR_BASE && a < 0x10002000u;
  if (isFlash || isUicr) {
    if (nvmcConfig_ != 1u) {
      hostErrors++;  // flash write without NVMC write enable
      return false;
    }
    uint32_t idx = isFlash ? a / 4 : 0x40000u + (a - UICR_BASE) / 4;
    uint32_t& cell = isFlash ? flash[a / 4] : uicr[(a - UICR_BASE) / 4];
    if (writes_[idx]++) doubleWrites++;
    if (v == 0xFFFFFFFFu) ffWrites++;
    cell &= v;  // flash only clears bits
    busy_ = cfg_.waitAfterFlashWrite;
    return true;
  }
  if (a == 0x4001E504u) {
    nvmcConfig_ = v & 3u;
    return true;
  }
  if (a == 0xE000EDF0u) {
    if ((v >> 16) == 0xA05Fu) halted_ = (v & 3u) == 3u;
    return true;
  }
  if (a == 0xE000ED0Cu) {
    if ((v >> 16) == 0x05FAu && (v & 4u)) softReset();
    return true;
  }
  if (a >= 0x20000000u && a < 0x20040000u) {
    ram_[a] = v;
    return true;
  }
  return false;
}

void SimNrf52::eraseAll() {
  eraseAlls++;
  std::fill(flash.begin(), flash.end(), 0xFFFFFFFFu);
  std::fill(uicr.begin(), uicr.end(), 0xFFFFFFFFu);
  std::fill(writes_.begin(), writes_.end(), 0);
  ram_.clear();
  eraseBusy_ = cfg_.eraseBusyPolls;
  if (!legacy()) locked = false;  // revision 3: open until the next pin/power/brownout reset
  // legacy: the erased UICR takes effect at the next reset (conservative model)
}

void SimNrf52::softReset() {
  halted_ = false;
  if (legacy()) locked = (uicr[APPROTECT_IDX] & 0xFFu) != 0xFFu;
}

void SimNrf52::pinReset() {
  pinResets++;
  halted_ = false;
  pwrReq_ = 0;
  sticky_ = 0;
  busy_ = 0;
  st_ = LOCKOUT;  // the DP needs a line reset again
  tgtDrive_ = false;
  if (legacy())
    locked = (uicr[APPROTECT_IDX] & 0xFFu) != 0xFFu;
  else
    locked = !((uicr[APPROTECT_IDX] & 0xFFu) == 0x5Au && flash[0] != 0xFFFFFFFFu && cfg_.bootloaderOpensPort);
}
