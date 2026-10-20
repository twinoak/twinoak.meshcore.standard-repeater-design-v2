// nRF52840 over SWD: identity, CTRL-AP erase, NVMC programming, verify, and the one-time provisioning
// of a factory RAK4630 (RUI3) with the bootloader + SoftDevice image.
#pragma once
#include <stdint.h>

#include "image.h"
#include "link.h"
#include "swd.h"

namespace nrf {
constexpr uint32_t DPIDR_NRF52 = 0x2BA01477u;

constexpr uint8_t AHB_AP = 0;
constexpr uint8_t CTRL_AP = 1;
constexpr uint8_t CTRL_RESET = 0x00;
constexpr uint8_t CTRL_ERASEALL = 0x04;
constexpr uint8_t CTRL_ERASEALLSTATUS = 0x08;
constexpr uint8_t CTRL_APPROTECTSTATUS = 0x0C;  // bit 0: 1 = port open, 0 = APPROTECT active
constexpr uint8_t CTRL_IDR = 0xFC;
constexpr uint32_t CTRL_AP_IDR = 0x02880000u;

constexpr uint32_t FLASH_END = 0x00100000u;
constexpr uint32_t FICR_DEVICEID0 = 0x10000060u;
constexpr uint32_t FICR_DEVICEID1 = 0x10000064u;
constexpr uint32_t FICR_PART = 0x10000100u;
constexpr uint32_t FICR_VARIANT = 0x10000104u;
constexpr uint32_t FICR_PACKAGE = 0x10000108u;
constexpr uint32_t FICR_RAM = 0x1000010Cu;
constexpr uint32_t FICR_FLASH = 0x10000110u;
constexpr uint32_t UICR_BASE = 0x10001000u;
constexpr uint32_t UICR_END = 0x10002000u;
constexpr uint32_t UICR_NRFFW0 = 0x10001014u;  // bootloader start address (read by the MBR)
constexpr uint32_t UICR_NRFFW1 = 0x10001018u;  // MBR parameter page
constexpr uint32_t UICR_APPROTECT = 0x10001208u;
constexpr uint32_t UICR_REGOUT0 = 0x10001304u;
constexpr uint32_t APPROTECT_HWDISABLED = 0x0000005Au;
constexpr uint32_t NVMC_READY = 0x4001E400u;
constexpr uint32_t NVMC_CONFIG = 0x4001E504u;  // 0 read, 1 write enable
constexpr uint32_t DHCSR = 0xE000EDF0u;
constexpr uint32_t SD_MAGIC_ADDR = 0x00003004u;  // MBR size 0x1000 + SD info struct 0x2000 + 4
constexpr uint32_t SD_MAGIC = 0x51B1E5DBu;
constexpr uint32_t SD_ID_ADDR = 0x00003010u;
constexpr uint32_t SD_VERSION_ADDR = 0x00003014u;
constexpr uint32_t PART_NRF52840 = 0x00052840u;
}  // namespace nrf

// Which access-port protection the part has, by FICR INFO.VARIANT (Nordic IN-141). A lookup, not a
// guess: a variant outside the table stops the tool until the table is extended.
enum class ProtectKind { Legacy, Improved, Unknown };

struct Identity {
  uint32_t part, variant, package, ramKb, flashKb, dev0, dev1;
  uint32_t nrffw0, nrffw1, approtect, regout0;
  uint32_t word0, word1000, sdMagic, sdId, sdVersion;
};

class Nrf52 {
 public:
  Nrf52(Swd& swd, Link& link) : s_(swd), l_(link) {}

  // Connect, check DPIDR and the CTRL-AP, read APPROTECTSTATUS.
  int attach(bool* open, bool verbose);
  int identify(Identity* id);  // needs an open port
  void printIdentity(const Identity& id);

  // Console commands. Each returns 0 on success; the log says what happened.
  int info(const Image& img);
  int provision(const Image& img);  // erases EVERYTHING on the radio
  int verifyCommand(const Image& img);

  static ProtectKind protectKind(uint32_t variant);
  static const char* protectName(ProtectKind k);
  static void variantStr(uint32_t v, char out[5]);

 private:
  int eraseAll();
  int program(const Image& img, ProtectKind kind);
  int verify(const Image& img);
  int nvmcConfig(uint32_t v);
  int nvmcReady();
  void log(const char* fmt, ...);

  Swd& s_;
  Link& l_;
};
