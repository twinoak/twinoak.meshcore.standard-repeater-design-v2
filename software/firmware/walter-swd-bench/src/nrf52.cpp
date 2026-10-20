#include "nrf52.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace nrf;

void Nrf52::log(const char* fmt, ...) {
  char b[176];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  l_.out(b);
}

void Nrf52::variantStr(uint32_t v, char o[5]) {
  for (int i = 0; i < 4; i++) {
    char c = (char)((v >> (24 - 8 * i)) & 0xFFu);
    o[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  o[4] = 0;
}

ProtectKind Nrf52::protectKind(uint32_t variant) {
  // INFO.VARIANT is four ASCII characters, "AA" + build code: AAC0, AAD0, AAD1, AAF0 ...
  // Build code A..D (up to revision 2): legacy APPROTECT, open while UICR.APPROTECT is erased.
  // Build code F (revision 3, IN-141): improved APPROTECT, closed unless UICR.APPROTECT = 0x5A.
  // E (engineering) and anything newer: not in the table, the tool stops.
  if ((variant >> 16) != 0x4141u) return ProtectKind::Unknown;
  switch ((variant >> 8) & 0xFFu) {
    case 'A':
    case 'B':
    case 'C':
    case 'D':
      return ProtectKind::Legacy;
    case 'F':
      return ProtectKind::Improved;
    default:
      return ProtectKind::Unknown;
  }
}

const char* Nrf52::protectName(ProtectKind k) {
  switch (k) {
    case ProtectKind::Legacy: return "revision 1/2, legacy APPROTECT";
    case ProtectKind::Improved: return "revision 3, improved APPROTECT";
    default: return "NOT IN THE TABLE";
  }
}

int Nrf52::attach(bool* open, bool verbose) {
  uint32_t dpidr = 0;
  if (s_.connect(&dpidr)) {
    log("SWD: %s", s_.error());
    return -1;
  }
  if (verbose) log("DPIDR            0x%08X%s", (unsigned)dpidr, dpidr == DPIDR_NRF52 ? "  (nRF52 SW-DP)" : "");
  if (dpidr != DPIDR_NRF52) {
    log("DPIDR 0x%08X is not an nRF52840's (0x2BA01477) - stopping", (unsigned)dpidr);
    return -1;
  }
  uint32_t idr = 0, ps = 0;
  if (s_.apRead(CTRL_AP, CTRL_IDR, &idr) || idr != CTRL_AP_IDR) {
    log("CTRL-AP IDR 0x%08X, expected 0x02880000 - stopping (%s)", (unsigned)idr, s_.error());
    return -1;
  }
  if (s_.apRead(CTRL_AP, CTRL_APPROTECTSTATUS, &ps)) {
    log("CTRL-AP APPROTECTSTATUS: %s", s_.error());
    return -1;
  }
  *open = (ps & 1u) != 0;
  if (verbose) log("access port      %s", *open ? "open" : "LOCKED (APPROTECT)");
  return 0;
}

int Nrf52::identify(Identity* id) {
  struct {
    uint32_t addr;
    uint32_t* dst;
  } reads[] = {
      {FICR_PART, &id->part},          {FICR_VARIANT, &id->variant},     {FICR_PACKAGE, &id->package},
      {FICR_RAM, &id->ramKb},          {FICR_FLASH, &id->flashKb},       {FICR_DEVICEID0, &id->dev0},
      {FICR_DEVICEID1, &id->dev1},     {UICR_NRFFW0, &id->nrffw0},       {UICR_NRFFW1, &id->nrffw1},
      {UICR_APPROTECT, &id->approtect}, {UICR_REGOUT0, &id->regout0},    {0x00000000u, &id->word0},
      {0x00001000u, &id->word1000},    {SD_MAGIC_ADDR, &id->sdMagic},    {SD_ID_ADDR, &id->sdId},
      {SD_VERSION_ADDR, &id->sdVersion},
  };
  for (auto& r : reads) {
    if (s_.read32(r.addr, r.dst)) {
      log("read 0x%08X: %s", (unsigned)r.addr, s_.error());
      return -1;
    }
  }
  return 0;
}

void Nrf52::printIdentity(const Identity& id) {
  char var[5];
  variantStr(id.variant, var);
  log("INFO.PART        0x%08X  %s", (unsigned)id.part, id.part == PART_NRF52840 ? "nRF52840" : "NOT an nRF52840");
  log("INFO.VARIANT     0x%08X  \"%s\" = %s", (unsigned)id.variant, var, protectName(protectKind(id.variant)));
  log("DEVICEID         %08X%08X", (unsigned)id.dev1, (unsigned)id.dev0);
  log("RAM / flash      %u KB / %u KB, package 0x%X", (unsigned)id.ramKb, (unsigned)id.flashKb, (unsigned)id.package);
  log("UICR.NRFFW[0]    0x%08X  %s", (unsigned)id.nrffw0,
      id.nrffw0 == 0xFFFFFFFFu ? "(no bootloader registered)" : "(bootloader start)");
  log("UICR.NRFFW[1]    0x%08X", (unsigned)id.nrffw1);
  log("UICR.APPROTECT   0x%08X", (unsigned)id.approtect);
  log("UICR.REGOUT0     0x%08X", (unsigned)id.regout0);
  log("flash 0x00000000 0x%08X, 0x00001000 0x%08X%s", (unsigned)id.word0, (unsigned)id.word1000,
      id.word0 == 0xFFFFFFFFu ? "  (erased)" : "");
  if (id.sdMagic == SD_MAGIC)
    log("SoftDevice       S%u %u.%u.%u", (unsigned)id.sdId, (unsigned)(id.sdVersion / 1000000u),
        (unsigned)(id.sdVersion / 1000u % 1000u), (unsigned)(id.sdVersion % 1000u));
  else
    log("SoftDevice       none at 0x1000 (no info struct)");
}

int Nrf52::info(const Image& img) {
  bool open = false;
  if (attach(&open, true)) return -1;
  if (!open) {
    log("Nothing more is readable until 'provision yes' erases the chip (the factory firmware locked it,");
    log("or it is a revision 3 part, which ships locked).");
    return 0;
  }
  Identity id{};
  if (identify(&id)) return -1;
  printIdentity(id);
  if (img.nseg)
    log("built-in image   bootloader at 0x%08X: %s", (unsigned)img.bootloaderAddr,
        id.nrffw0 == img.bootloaderAddr ? "the radio registers the same address ('verify' checks every word)"
                                        : "the radio does not have it");
  return 0;
}

int Nrf52::nvmcReady() {
  for (int i = 0; i < 200; i++) {
    uint32_t r = 0;
    if (s_.read32(NVMC_READY, &r)) {
      log("NVMC READY: %s", s_.error());
      return -1;
    }
    if (r & 1u) return 0;
    l_.sleepMs(1);
  }
  log("NVMC not ready after 200 ms");
  return -1;
}

int Nrf52::nvmcConfig(uint32_t v) {
  if (s_.write32(NVMC_CONFIG, v)) {
    log("NVMC CONFIG <- %u: %s", (unsigned)v, s_.error());
    return -1;
  }
  return nvmcReady();
}

// CTRL-AP ERASEALL: flash, UICR and RAM, works on a locked part. A revision 3 part stays open until the
// next pin, power or brownout reset; a legacy part may need a (soft) reset to re-read its erased UICR.
// Which one applies is read back from APPROTECTSTATUS, not assumed.
int Nrf52::eraseAll() {
  uint32_t t0 = l_.ms(), busy = 1, ps = 0;
  if (s_.apWrite(CTRL_AP, CTRL_ERASEALL, 1)) {
    log("ERASEALL: %s", s_.error());
    return -1;
  }
  while (busy) {
    l_.sleepMs(10);
    if (s_.apRead(CTRL_AP, CTRL_ERASEALLSTATUS, &busy)) {
      log("ERASEALLSTATUS: %s", s_.error());
      return -1;
    }
    if (busy && l_.ms() - t0 > 5000) {
      log("ERASEALL still busy after 5 s - stopping");
      return -1;
    }
  }
  if (s_.apRead(CTRL_AP, CTRL_APPROTECTSTATUS, &ps)) {
    log("APPROTECTSTATUS: %s", s_.error());
    return -1;
  }
  log("  erased in %u ms, access port %s", (unsigned)(l_.ms() - t0), (ps & 1u) ? "open" : "still closed");
  if (ps & 1u) return 0;

  log("  soft reset through the CTRL-AP (a legacy part re-reads its erased UICR)");
  if (s_.apWrite(CTRL_AP, CTRL_RESET, 1)) return -1;
  l_.sleepMs(10);
  if (s_.apWrite(CTRL_AP, CTRL_RESET, 0)) return -1;
  l_.sleepMs(10);
  bool open = false;
  if (attach(&open, false)) return -1;
  if (!open) {
    log("access port still closed after ERASEALL + reset - stopping");
    return -1;
  }
  return 0;
}

int Nrf52::program(const Image& img, ProtectKind kind) {
  if (nvmcConfig(1)) return -1;
  uint32_t done = 0, step = img.dataWords / 10u + 1u, next = step;
  for (uint32_t s = 0; s < img.nseg; s++) {
    const ImageSegment& g = img.seg[s];
    uint32_t i = 0;
    while (i < g.words) {
      if (g.data[i] == 0xFFFFFFFFu) {  // erased already: never written
        i++;
        continue;
      }
      uint32_t addr = g.addr + 4u * i;
      uint32_t room = (0x400u - (addr & 0x3FFu)) / 4u;
      uint32_t j = i;
      while (j < g.words && j - i < room && g.data[j] != 0xFFFFFFFFu) j++;
      if (s_.writeBlock(addr, g.data + i, j - i)) {
        log("write at 0x%08X: %s", (unsigned)addr, s_.error());
        return -1;
      }
      if (nvmcReady()) return -1;
      done += j - i;
      uint32_t end = addr + 4u * (j - i);
      i = j;
      if (done >= next) {
        log("  %3u%%  0x%08X", (unsigned)(100u * done / img.dataWords), (unsigned)end);
        next += step;
      }
      l_.sleepMs(1);  // let the Walter's other tasks run
    }
  }
  log("  100%%  %u words", (unsigned)done);
  if (kind == ProtectKind::Improved) {
    if (s_.write32(UICR_APPROTECT, APPROTECT_HWDISABLED) || nvmcReady()) {
      log("UICR.APPROTECT: %s", s_.error());
      return -1;
    }
    log("  UICR.APPROTECT <- 0x0000005A (HwDisabled: keeps a revision 3 port open)");
  }
  return nvmcConfig(0);
}

int Nrf52::verify(const Image& img) {
  static uint32_t buf[256];
  uint32_t bad = 0, checked = 0;
  for (uint32_t s = 0; s < img.nseg; s++) {
    const ImageSegment& g = img.seg[s];
    for (uint32_t off = 0; off < g.words;) {
      uint32_t n = g.words - off;
      if (n > 256u) n = 256u;
      uint32_t addr = g.addr + 4u * off;
      if (s_.readBlock(addr, buf, n)) {
        log("read at 0x%08X: %s", (unsigned)addr, s_.error());
        return -1;
      }
      for (uint32_t k = 0; k < n; k++) {
        if (buf[k] != g.data[off + k]) {
          if (bad < 8)
            log("  differs at 0x%08X: image 0x%08X, radio 0x%08X", (unsigned)(addr + 4u * k),
                (unsigned)g.data[off + k], (unsigned)buf[k]);
          bad++;
        }
      }
      checked += n;
      off += n;
      l_.sleepMs(1);
    }
  }
  if (bad) {
    log("verify FAILED: %u of %u words differ", (unsigned)bad, (unsigned)checked);
    return -1;
  }
  log("  verify OK: %u words, image CRC32 0x%08X", (unsigned)checked, (unsigned)img.crc32);
  return 0;
}

int Nrf52::verifyCommand(const Image& img) {
  if (!img.nseg) {
    log("no image built in (images/*.hex, then rebuild)");
    return -1;
  }
  bool open = false;
  if (attach(&open, false)) return -1;
  if (!open) {
    log("access port LOCKED: the radio can't be read");
    return -1;
  }
  uint32_t ap = 0;
  if (s_.read32(UICR_APPROTECT, &ap)) {
    log("UICR.APPROTECT: %s", s_.error());
    return -1;
  }
  log("UICR.APPROTECT   0x%08X", (unsigned)ap);
  return verify(img);
}

int Nrf52::provision(const Image& img) {
  if (!img.nseg) {
    log("no image built in: put the RAK4630 bootloader + S140 6.1.1 .hex in images/ and rebuild");
    return -1;
  }
  log("[1/6] attach");
  bool open = false;
  if (attach(&open, true)) return -1;
  Identity id{};
  if (open) {
    if (identify(&id)) return -1;
    printIdentity(id);
    if (id.part != PART_NRF52840) {
      log("not an nRF52840 - stopping, nothing erased");
      return -1;
    }
    if (protectKind(id.variant) == ProtectKind::Unknown) {
      log("INFO.VARIANT is not in the table (nrf52.cpp protectKind) - stopping, nothing erased");
      return -1;
    }
  } else {
    log("  locked: the identity is readable only after the erase");
  }

  log("[2/6] erase everything (CTRL-AP ERASEALL): factory firmware, UICR, RAM");
  if (eraseAll()) return -1;
  if (identify(&id)) return -1;
  if (!open) printIdentity(id);
  ProtectKind kind = protectKind(id.variant);
  if (id.part != PART_NRF52840 || kind == ProtectKind::Unknown) {
    log("part/variant not in the table - stopping. The radio is erased, nothing is lost: extend the");
    log("table and run 'provision yes' again (keep it powered and un-reset if it is a revision 3).");
    return -1;
  }
  if (id.word0 != 0xFFFFFFFFu || id.nrffw0 != 0xFFFFFFFFu || id.approtect != 0xFFFFFFFFu) {
    log("not blank after the erase (flash[0] 0x%08X, NRFFW[0] 0x%08X, APPROTECT 0x%08X) - stopping",
        (unsigned)id.word0, (unsigned)id.nrffw0, (unsigned)id.approtect);
    return -1;
  }
  if (s_.write32(DHCSR, 0xA05F0003u)) {  // halt: nothing runs while the flash fills
    log("halt: %s", s_.error());
    return -1;
  }

  log("[3/6] write %u words (%s)", (unsigned)img.dataWords, img.source);
  if (program(img, kind)) return -1;

  log("[4/6] verify");
  if (verify(img)) return -1;
  uint32_t ap = 0, wantAp = kind == ProtectKind::Improved ? APPROTECT_HWDISABLED : 0xFFFFFFFFu;
  if (s_.read32(UICR_APPROTECT, &ap) || ap != wantAp) {
    log("UICR.APPROTECT reads 0x%08X, expected 0x%08X - stopping", (unsigned)ap, (unsigned)wantAp);
    return -1;
  }

  log("[5/6] pin reset (the only reset of the procedure)");
  s_.write32(DHCSR, 0xA05F0000u);  // release the halt and debug: run freely after the reset
  l_.releaseAll();
  l_.nreset(true);
  l_.sleepMs(20);
  l_.nreset(false);
  l_.sleepMs(1500);  // the bootloader starts, finds no application, waits in DFU mode

  log("[6/6] attach again");
  bool openAfter = false;
  if (attach(&openAfter, false)) {
    log("no SWD answer after the reset. The image was verified before it; check the RAK on USB.");
    l_.releaseAll();
    return -1;
  }
  if (openAfter) {
    uint32_t bl = 0;
    s_.read32(UICR_NRFFW0, &bl);
    log("  access port open after the reset: the Walter can update this radio over SWD");
    log("  UICR.NRFFW[0] 0x%08X", (unsigned)bl);
  } else {
    log("  access port CLOSED after the reset: this bootloader does not open APPROTECT on a revision 3");
    log("  part (SYSTEM.md 10.2). The radio is fine for UF2/USB; the Walter's SWD updates need that fixed.");
  }
  l_.releaseAll();
  char var[5];
  variantStr(id.variant, var);
  log("PROVISIONED  DEVICEID %08X%08X  %s  image CRC32 0x%08X  port after reset: %s", (unsigned)id.dev1,
      (unsigned)id.dev0, var, (unsigned)img.crc32, openAfter ? "open" : "closed");
  log("Next: the RAK adapter's USB-C to the laptop: a UF2 drive and a COM port appear (DFU mode).");
  return 0;
}
