// TwinOak Walter SWD bench tool: the Walter provisions a factory RAK4630 through the production chain
// (Walter adapter -> LTE D-sub -> connector B -> ribbon -> connector A, 470 ohm -> LoRa D-sub -> RAK).
// A bench tool, not the Walter's production firmware: see README.md.
#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

#include "driver/gpio.h"
#include "image.h"
#include "link.h"
#include "nrf52.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "swd.h"

namespace {

// Walter GPIO -> D-sub pin, LTE side (INTERCONNECT.md, frozen 2026-10-11)
constexpr gpio_num_t PIN_NRESET = GPIO_NUM_1;  // 3  open-drain only: LOW or Hi-Z, never HIGH
constexpr gpio_num_t PIN_SWCLK = GPIO_NUM_2;   // 4
constexpr gpio_num_t PIN_SWDIO = GPIO_NUM_4;   // 5
constexpr gpio_num_t PIN_KILL = GPIO_NUM_5;    // 6  HIGH = LoRa rail off; floating = on
constexpr gpio_num_t PIN_TX = GPIO_NUM_7;      // 8  -> RAK P0.15 (Serial1 RX)
constexpr gpio_num_t PIN_RX = GPIO_NUM_8;      // 9  <- RAK P0.16 (Serial1 TX)
constexpr uint32_t BIT_SWCLK = 1u << PIN_SWCLK;
constexpr uint32_t BIT_SWDIO = 1u << PIN_SWDIO;
constexpr uint64_t RAK_LINES = (1ULL << PIN_NRESET) | (1ULL << PIN_SWCLK) | (1ULL << PIN_SWDIO) |
                               (1ULL << PIN_TX) | (1ULL << PIN_RX);

void configure(uint64_t mask, gpio_mode_t mode, bool pullup) {
  gpio_config_t c = {};
  c.pin_bit_mask = mask;
  c.mode = mode;
  c.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  c.pull_down_en = GPIO_PULLDOWN_DISABLE;
  c.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&c);
}

class WalterLink : public Link {
 public:
  void setKhz(uint32_t khz) {
    khz_ = khz;
    halfUs_ = 500u / khz;
    if (!halfUs_) halfUs_ = 1;
  }
  uint32_t khz() const { return khz_; }
  uint32_t lastResetMs() const { return lastReset_; }

  void swclk(bool h) override {
    take();
    REG_WRITE(h ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, BIT_SWCLK);
  }
  void swdio(bool h) override {
    take();
    REG_WRITE(h ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, BIT_SWDIO);
  }
  bool swdioIn() override { return (REG_READ(GPIO_IN_REG) & BIT_SWDIO) != 0; }
  void swdioDrive(bool d) override {
    take();
    REG_WRITE(d ? GPIO_ENABLE_W1TS_REG : GPIO_ENABLE_W1TC_REG, BIT_SWDIO);
  }
  void halfPeriod() override { delayMicroseconds(halfUs_); }

  void nreset(bool assert) override {
    if (assert) {
      gpio_set_level(PIN_NRESET, 0);
      gpio_set_direction(PIN_NRESET, GPIO_MODE_OUTPUT_OD);
    } else {
      gpio_set_direction(PIN_NRESET, GPIO_MODE_INPUT);
      lastReset_ = millis();
    }
  }

  void releaseAll() override {
    gpio_reset_pin(PIN_TX);  // detach the UART from the GPIO matrix if the bridge ran
    gpio_reset_pin(PIN_RX);
    configure(RAK_LINES, GPIO_MODE_INPUT, false);
    taken_ = false;
  }

  void sleepMs(uint32_t ms) override { delay(ms); }
  uint32_t ms() override { return millis(); }
  void out(const char* line) override { Serial.println(line); }

 private:
  void take() {  // SWCLK out, SWDIO in/out (Hi-Z until driven); both idle high
    if (taken_) return;
    taken_ = true;
    REG_WRITE(GPIO_OUT_W1TS_REG, BIT_SWCLK | BIT_SWDIO);
    configure(1ULL << PIN_SWCLK, GPIO_MODE_OUTPUT, false);
    configure(1ULL << PIN_SWDIO, GPIO_MODE_INPUT_OUTPUT, true);
    REG_WRITE(GPIO_ENABLE_W1TC_REG, BIT_SWDIO);
  }

  uint32_t khz_ = 125, halfUs_ = 4, lastReset_ = 0;
  bool taken_ = false;
};

WalterLink gLink;
Swd gSwd(gLink);
Nrf52 gNrf(gSwd, gLink);
char gLine[96];
size_t gLen = 0;

void say(const char* fmt, ...) {
  char b[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  Serial.println(b);
}

void imageSummary() {
  const Image& m = kBuiltinImage;
  if (!m.nseg) {
    say("built-in image: NONE. Put the RAK4630 bootloader + S140 6.1.1 .hex in images/ and rebuild.");
    return;
  }
  say("built-in image: %s", m.source);
  say("  %u segments, %u words to write, CRC32 0x%08X, bootloader at 0x%08X", (unsigned)m.nseg,
      (unsigned)m.dataWords, (unsigned)m.crc32, (unsigned)m.bootloaderAddr);
  if (m.sdVersion)
    say("  SoftDevice S%u %u.%u.%u", (unsigned)m.sdId, (unsigned)(m.sdVersion / 1000000u),
        (unsigned)(m.sdVersion / 1000u % 1000u), (unsigned)(m.sdVersion % 1000u));
  for (uint32_t i = 0; i < m.nseg; i++)
    say("  0x%08X .. 0x%08X", (unsigned)m.seg[i].addr, (unsigned)(m.seg[i].addr + 4u * m.seg[i].words - 1u));
}

void help() {
  say("TwinOak Walter SWD bench tool (built " __DATE__ " " __TIME__ ")");
  say("  probe           passive check of the RAK lines (nothing driven)");
  say("  idcode          SWD line reset + DPIDR read: does the chain work?");
  say("  info            identity, UICR, protection state of the radio");
  say("  provision yes   ERASE the whole radio, write the built-in image, verify, reset");
  say("  verify          compare the radio against the built-in image");
  say("  reset           pulse nRESET low 20 ms (refused within 1 s of the last one)");
  say("  power off|on|cycle   LoRa rail through the kill line");
  say("  uart            bridge to the RAK's Serial1, 115200 8N1 (Ctrl-X ends)");
  say("  speed [kHz]     SWD clock, 10..500 (now %u)", (unsigned)gLink.khz());
  say("  image           the built-in image");
  say("Pins: nRESET GPIO1 (D-sub 3), SWCLK GPIO2 (4), SWDIO GPIO4 (5), KILL GPIO5 (6), TX GPIO7 (8), RX GPIO8 (9).");
  say("Between commands every RAK line is Hi-Z.");
}

int levelWith(gpio_num_t p, gpio_pull_mode_t pull) {
  gpio_set_direction(p, GPIO_MODE_INPUT);
  gpio_set_pull_mode(p, pull);
  delay(3);
  int v = gpio_get_level(p);
  gpio_set_pull_mode(p, GPIO_FLOATING);
  return v;
}

// Reads every line twice, against the Walter's weak pull-down and pull-up (~45 k). A line the RAK
// holds reads the same both times; a line nobody holds follows the pull.
void probe() {
  struct {
    const char* name;
    gpio_num_t pin;
    int dsub;
    const char* expect;
  } lines[] = {
      {"nRESET", PIN_NRESET, 3, "HIGH (nRF pull-up)"},
      {"SWCLK", PIN_SWCLK, 4, "LOW (nRF pull-down)"},
      {"SWDIO", PIN_SWDIO, 5, "HIGH (nRF pull-up)"},
      {"RAK TX", PIN_RX, 9, "HIGH if the RAK firmware has Serial1 open, else floating"},
  };
  gLink.releaseAll();
  int high = 0, low = 0, floating = 0;
  say("line    D-sub  pull-down  pull-up  reads       expected");
  for (auto& ln : lines) {
    int dn = levelWith(ln.pin, GPIO_PULLDOWN_ONLY);
    int up = levelWith(ln.pin, GPIO_PULLUP_ONLY);
    const char* m = (dn && up) ? "held HIGH" : (!dn && !up) ? "held LOW" : (!dn && up) ? "floating" : "odd";
    high += dn && up;
    low += !dn && !up;
    floating += !dn && up;
    say("%-7s %-6d %-10d %-8d %-11s %s", ln.name, ln.dsub, dn, up, m, ln.expect);
  }
  gLink.releaseAll();
  if (high >= 2 && low <= 1)
    say("=> the RAK is powered and the chain is connected");
  else if (low == 4)
    say("=> everything held LOW: the RAK is unpowered (its ESD diodes clamp the lines). 'power on'? SW1? battery?");
  else if (floating == 4)
    say("=> nothing on any line: the chain is open (D-subs, ribbon) or the board isn't there");
  else
    say("=> mixed: check the line(s) that differ from 'expected'");
}

void cmdSpeed(const char* arg) {
  if (arg) {
    long k = strtol(arg, nullptr, 10);
    if (k < 10 || k > 500) {
      say("speed: 10..500 kHz (125 is the default and the nRF52840's specified minimum; the chain is good to ~300)");
      return;
    }
    gLink.setKhz((uint32_t)k);
  }
  say("SWD clock %u kHz", (unsigned)gLink.khz());
}

void cmdIdcode() {
  uint32_t id = 0;
  if (gSwd.connect(&id)) {
    say("no SWD answer: %s", gSwd.error());
    say("try: probe; speed 20; power on");
    return;
  }
  say("DPIDR 0x%08X%s", (unsigned)id,
      id == nrf::DPIDR_NRF52 ? "  nRF52840 SW-DP: the SWD chain works" : "  (not an nRF52840's 0x2BA01477)");
}

void cmdReset() {
  if (gLink.lastResetMs() && millis() - gLink.lastResetMs() < 1000) {
    say("refused: the last reset was < 1 s ago (two within 0.5 s put the RAK in its UF2 bootloader)");
    return;
  }
  gLink.releaseAll();
  gLink.nreset(true);
  delay(20);
  gLink.nreset(false);
  say("nRESET pulsed low for 20 ms");
}

void killOff() {
  gLink.releaseAll();  // tristate before the kill: no back-feed into a dead rail
  gpio_set_level(PIN_KILL, 1);
  gpio_set_direction(PIN_KILL, GPIO_MODE_OUTPUT);
}

void killOn() { configure(1ULL << PIN_KILL, GPIO_MODE_INPUT, false); }

void cmdPower(const char* arg) {
  if (arg && !strcmp(arg, "off")) {
    killOff();
    say("KILL high: LoRa rail OFF");
  } else if (arg && !strcmp(arg, "on")) {
    killOn();
    say("KILL released: LoRa rail ON");
  } else if (arg && !strcmp(arg, "cycle")) {
    killOff();
    delay(1000);
    killOn();
    delay(500);
    say("LoRa rail off 1 s, on again");
  } else {
    say("power off | on | cycle");
  }
}

void uartBridge() {
  gLink.releaseAll();
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  say("bridge to the RAK's Serial1 (D-sub 8/9, 115200 8N1). Ctrl-X ends.");
  for (;;) {
    while (Serial.available()) {
      int c = Serial.read();
      if (c == 0x18) {
        Serial1.end();
        gLink.releaseAll();
        say("\r\nbridge closed");
        return;
      }
      Serial1.write((uint8_t)c);
    }
    while (Serial1.available()) Serial.write((uint8_t)Serial1.read());
    delay(1);
  }
}

void dispatch(char* line) {
  char* cmd = strtok(line, " \t");
  if (!cmd) return;
  char* arg = strtok(nullptr, " \t");
  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    help();
  } else if (!strcmp(cmd, "probe")) {
    probe();
  } else if (!strcmp(cmd, "idcode")) {
    cmdIdcode();
  } else if (!strcmp(cmd, "info")) {
    gNrf.info(kBuiltinImage);
  } else if (!strcmp(cmd, "provision")) {
    if (!arg || strcmp(arg, "yes")) {
      say("'provision yes' erases the WHOLE radio (factory firmware, its keys and settings, UICR) and");
      say("writes the built-in image:");
      imageSummary();
    } else {
      uint32_t t0 = millis();
      int r = gNrf.provision(kBuiltinImage);
      say("%s after %u s", r ? "provision FAILED" : "provision done", (unsigned)((millis() - t0) / 1000u));
    }
  } else if (!strcmp(cmd, "verify")) {
    gNrf.verifyCommand(kBuiltinImage);
  } else if (!strcmp(cmd, "reset")) {
    cmdReset();
  } else if (!strcmp(cmd, "power")) {
    cmdPower(arg);
  } else if (!strcmp(cmd, "uart")) {
    uartBridge();
  } else if (!strcmp(cmd, "speed")) {
    cmdSpeed(arg);
  } else if (!strcmp(cmd, "image")) {
    imageSummary();
  } else {
    say("unknown command '%s' (help)", cmd);
  }
  gLink.releaseAll();
}

}  // namespace

void setup() {
  gLink.releaseAll();
  killOn();  // floating kill line = LoRa rail on (connector A R13 holds the gate low)
  gLink.setKhz(125);  // nRF52840 PS: f_SWDCLK minimum 0.125 MHz
  Serial.begin(115200);
  delay(300);
  help();
  imageSummary();
  Serial.print("> ");
}

void loop() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (gLen) {
        Serial.println();
        gLine[gLen] = 0;
        gLen = 0;
        dispatch(gLine);
        Serial.print("> ");
      }
      continue;
    }
    if (c == 8 || c == 127) {
      if (gLen) {
        gLen--;
        Serial.print("\b \b");
      }
      continue;
    }
    if (c >= 32 && c < 127 && gLen < sizeof gLine - 1) {
      gLine[gLen++] = (char)c;
      Serial.write((uint8_t)c);
    }
  }
  delay(2);
}
