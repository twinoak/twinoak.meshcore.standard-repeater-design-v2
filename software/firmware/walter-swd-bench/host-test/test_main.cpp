// Scenarios for the bench tool against the simulated nRF52840. Exit code = number of failed scenarios.
//   build/swdtest      all scenarios
//   build/swdtest -v   also echo the tool's console output
#include <stdio.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "image.h"
#include "nrf52.h"
#include "sim_target.h"
#include "swd.h"

namespace {
bool gVerbose = false;
int gFailed = 0;
constexpr uint32_t APPROTECT_IDX = (0x10001208u - 0x10001000u) / 4u;
constexpr uint32_t AAD0 = 0x41414430u, AAE0 = 0x41414530u, AAF0 = 0x41414630u;

#define EXPECT(cond)                                         \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("    expected: %s  (line %d)\n", #cond, __LINE__); \
      ok = false;                                            \
    }                                                        \
  } while (0)

bool has(const SimNrf52& t, const char* s) { return t.log.find(s) != std::string::npos; }

// Flash: every image word in place, everything else erased. UICR: the image's words, APPROTECT apart.
bool holdsImage(const SimNrf52& t, const Image& img) {
  std::vector<uint32_t> f(t.flash.size(), 0xFFFFFFFFu), u(t.uicr.size(), 0xFFFFFFFFu);
  for (uint32_t s = 0; s < img.nseg; s++)
    for (uint32_t k = 0; k < img.seg[s].words; k++) {
      uint32_t a = img.seg[s].addr + 4u * k;
      if (a < 0x00100000u)
        f[a / 4] = img.seg[s].data[k];
      else
        u[(a - 0x10001000u) / 4] = img.seg[s].data[k];
    }
  for (size_t i = 0; i < f.size(); i++)
    if (t.flash[i] != f[i]) {
      printf("    flash 0x%08zX: 0x%08X, expected 0x%08X\n", i * 4, t.flash[i], f[i]);
      return false;
    }
  for (size_t i = 0; i < u.size(); i++)
    if (i != APPROTECT_IDX && t.uicr[i] != u[i]) {
      printf("    UICR 0x%08zX: 0x%08X, expected 0x%08X\n", 0x10001000u + i * 4, t.uicr[i], u[i]);
      return false;
    }
  return true;
}

void scenario(const char* name, SimConfig cfg, const std::function<bool(SimNrf52&, Nrf52&, Swd&)>& body) {
  SimNrf52 t(cfg);
  t.echo = gVerbose;
  Swd s(t);
  Nrf52 n(s, t);
  if (gVerbose) printf("==== %s\n", name);
  bool ok = body(t, n, s);
  if (t.contention) printf("    SWDIO driven by both ends %d times\n", t.contention);
  if (t.hostErrors) printf("    %d protocol violations by the host\n", t.hostErrors);
  ok = ok && !t.contention && !t.hostErrors;
  printf("%s  %-58s %7.1f s at 100 kHz, %d WAITs\n", ok ? "PASS" : "FAIL", name,
         t.edges * 10e-6 + t.ms() / 1000.0, t.waitsGiven);
  if (!ok) {
    gFailed++;
    if (!gVerbose) printf("---- console ----\n%s-----------------\n", t.log.c_str());
  }
}

// Every scenario that ends provisioned checks the same things.
bool provisioned(SimNrf52& t, int rc, uint32_t wantApprotect, bool wantOpen) {
  bool ok = true;
  EXPECT(rc == 0);
  EXPECT(holdsImage(t, kBuiltinImage));
  EXPECT(t.uicr[APPROTECT_IDX] == wantApprotect);
  EXPECT(t.eraseAlls == 1);
  EXPECT(t.pinResets == 1);
  EXPECT(t.doubleWrites == 0);
  EXPECT(t.ffWrites == 0);
  EXPECT(t.locked == !wantOpen);
  EXPECT(has(t, wantOpen ? "port after reset: open" : "port after reset: closed"));
  return ok;
}
}  // namespace

int main(int argc, char** argv) {
  gVerbose = argc > 1 && !strcmp(argv[1], "-v");
  printf("image: %u segments, %u words, CRC32 0x%08X, %s\n", kBuiltinImage.nseg, kBuiltinImage.dataWords,
         kBuiltinImage.crc32, kBuiltinImage.source);

  SimConfig rev3;  // defaults: AAF0, open, bootloader opens the port

  scenario("rev 3, factory open", rev3, [](SimNrf52& t, Nrf52& n, Swd&) {
    int rc = n.provision(kBuiltinImage);
    bool ok = provisioned(t, rc, 0x5Au, true);
    EXPECT(t.waitsGiven > 0);
    return ok;
  });

  SimConfig c = rev3;
  c.locked = true;
  scenario("rev 3, factory locked", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    return provisioned(t, n.provision(kBuiltinImage), 0x5Au, true);
  });

  c = rev3;
  c.locked = true;
  c.bootloaderOpensPort = false;
  scenario("rev 3, locked, bootloader leaves the port closed", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    return provisioned(t, n.provision(kBuiltinImage), 0x5Au, false);
  });

  c = rev3;
  c.variant = AAD0;
  c.locked = true;
  scenario("rev 2 (AAD0), factory locked: needs the soft reset", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = provisioned(t, n.provision(kBuiltinImage), 0xFFFFFFFFu, true);
    EXPECT(has(t, "soft reset"));
    return ok;
  });

  c = rev3;
  c.variant = AAD0;
  scenario("rev 2 (AAD0), factory open", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = provisioned(t, n.provision(kBuiltinImage), 0xFFFFFFFFu, true);
    EXPECT(!has(t, "soft reset"));
    return ok;
  });

  c = rev3;
  c.waitAfterFlashWrite = 0;  // what 100 kHz really looks like: a word write (41 us) beats one transaction
  c.eraseBusyPolls = 0;
  scenario("rev 3, no WAITs (the real timing at 100 kHz)", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    return provisioned(t, n.provision(kBuiltinImage), 0x5Au, true);
  });

  c = rev3;
  c.waitAfterFlashWrite = 25;
  c.eraseBusyPolls = 40;
  scenario("rev 3, slow flash: many WAITs", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    return provisioned(t, n.provision(kBuiltinImage), 0x5Au, true);
  });

  c = rev3;
  c.variant = AAE0;
  scenario("unknown variant, open: refuses before erasing", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    uint32_t before = t.flash[0];
    bool ok = true;
    EXPECT(n.provision(kBuiltinImage) != 0);
    EXPECT(t.eraseAlls == 0);
    EXPECT(t.flash[0] == before);
    EXPECT(has(t, "nothing erased"));
    return ok;
  });

  c = rev3;
  c.variant = AAE0;
  c.locked = true;
  scenario("unknown variant, locked: erases, then refuses to write", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = true;
    EXPECT(n.provision(kBuiltinImage) != 0);
    EXPECT(t.eraseAlls == 1);
    EXPECT(t.flash[0] == 0xFFFFFFFFu);
    EXPECT(t.pinResets == 0);
    EXPECT(has(t, "extend the"));
    return ok;
  });

  c = rev3;
  c.dpidr = 0x6BA02477u;
  scenario("another chip's DPIDR: refuses", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = true;
    EXPECT(n.provision(kBuiltinImage) != 0);
    EXPECT(t.eraseAlls == 0);
    return ok;
  });

  scenario("no image built in: refuses", rev3, [](SimNrf52& t, Nrf52& n, Swd&) {
    Image none = {nullptr, 0, 0, 0, 0, 0, 0, "none"};
    bool ok = true;
    EXPECT(n.provision(none) != 0);
    EXPECT(t.eraseAlls == 0);
    return ok;
  });

  scenario("verify finds one changed bit", rev3, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = true;
    EXPECT(n.provision(kBuiltinImage) == 0);
    EXPECT(n.verifyCommand(kBuiltinImage) == 0);
    t.flash[0x1014 / 4] ^= 0x10u;
    EXPECT(n.verifyCommand(kBuiltinImage) != 0);
    EXPECT(has(t, "differs at 0x00001014"));
    return ok;
  });

  c = rev3;
  c.locked = true;
  scenario("info on a locked radio", c, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = true;
    EXPECT(n.info(kBuiltinImage) == 0);
    EXPECT(has(t, "LOCKED"));
    EXPECT(t.eraseAlls == 0);
    return ok;
  });

  scenario("info on an open radio", rev3, [](SimNrf52& t, Nrf52& n, Swd&) {
    bool ok = true;
    EXPECT(n.info(kBuiltinImage) == 0);
    EXPECT(has(t, "\"AAF0\" = revision 3"));
    EXPECT(has(t, "DEVICEID         0123456789ABCDEF"));
    return ok;
  });

  scenario("a bus fault is an error, not data", rev3, [](SimNrf52&, Nrf52& n, Swd& s) {
    bool ok = true, open = false;
    uint32_t v = 0;
    EXPECT(n.attach(&open, false) == 0);
    EXPECT(s.read32(0x50000000u, &v) == swd::ERR_FAULT);
    EXPECT(s.read32(nrf::FICR_PART, &v) == swd::OK);  // sticky error cleared again
    EXPECT(v == 0x00052840u);
    return ok;
  });

  printf("%s: %d scenario(s) failed\n", gFailed ? "FAILED" : "ALL PASSED", gFailed);
  return gFailed;
}
