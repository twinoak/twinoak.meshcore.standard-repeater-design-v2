// ---------------------------------------------------------------------------
// NoiseScope — SX1262 spectrum/noise scanner for the TwinOak RAK4630 adapter
//
// Diagnostic firmware, temporarily flashed in place of MeshCore when a site
// needs RF debugging. Streams NDJSON over the Walter management UART (and USB
// CDC when attached). Never transmits — RX only.
//
// See PROTOCOL.md for the line format and command set.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include "config.h"
#include "out.h"
#include "wdt.h"
#include "telemetry.h"
#include "scanner.h"

static uint32_t s_stat_next_ms = 0;
static uint32_t s_auto_min = AUTO_SWEEP_MIN_DEFAULT;
static uint32_t s_auto_last_ms = 0;
static char s_reset_reason[8] = "por";

// ---- serial line input (two independent sources) ----

struct LineBuf {
  char buf[160];
  uint8_t len = 0;
  bool ready = false;
};
static LineBuf lb_walter, lb_usb;

static void feed_linebuf(LineBuf& lb, Stream& s) {
  if (lb.ready) return;                    // hold further bytes in HW/driver buffer
  while (s.available()) {
    char c = (char)s.read();
    if (c == '\n' || c == '\r') {
      if (lb.len > 0) { lb.buf[lb.len] = 0; lb.ready = true; return; }
    } else if (lb.len < sizeof(lb.buf) - 1) {
      lb.buf[lb.len++] = c;
    }
  }
}

// Called from scanner.cpp during blocking scan windows too.
void input_poll() {
  feed_linebuf(lb_walter, WALTER_SERIAL);
  feed_linebuf(lb_usb, Serial);
}

// ---- helpers ----

static void emit_ack(const char* cmd, bool ok, const char* msg) {
  out_printf("{\"t\":\"ack\",\"cmd\":\"%s\",\"ok\":%s", cmd, ok ? "true" : "false");
  if (msg && msg[0]) out_printf(",\"msg\":\"%s\"", msg);
  out_raw("}");
  out_end();
}

static void emit_stat() {
  float f, bw; uint8_t sf, cr;
  scan_get_home(&f, &bw, &sf, &cr);
  out_printf("{\"t\":\"stat\",\"ts\":%lu,\"ms\":%lu,\"up\":%lu,\"mode\":\"%s\",\"home\":%.3f",
             (unsigned long)time_epoch(), (unsigned long)millis(),
             (unsigned long)(millis() / 1000UL), scan_mode_name(), f);
  MonStats ms;
  if (scan_mode() == MODE_MONITOR && scan_get_monitor_stats(&ms, true)) {
    out_printf(",\"rssi\":{\"n\":%lu,\"avg\":%.1f,\"max\":%.1f,\"min\":%.1f,\"dc\":%.4f,\"th\":%.1f}",
               (unsigned long)ms.n, ms.avg, ms.mx, ms.mn, ms.dc, ms.th);
  }
  if (ina_ok()) {
    float v1, i1, v2, i2, v3, i3;
    bool a = ina_read(1, &v1, &i1), b = ina_read(2, &v2, &i2), c = ina_read(3, &v3, &i3);
    if (a && b && c) {
      out_printf(",\"pwr\":{\"vpanel\":%.2f,\"ipanel\":%.0f,\"vbat\":%.3f,\"ibat\":%.0f,\"vload\":%.3f,\"iload\":%.0f}",
                 v1, i1, v2, i2, v3, i3);
    }
  }
  out_raw("}");
  out_end();
}

static void emit_info() {
  float f, bw; uint8_t sf, cr;
  scan_get_home(&f, &bw, &sf, &cr);
  out_printf("{\"t\":\"info\",\"fw\":\"%s\",\"ver\":\"%s\",\"hw\":\"rak4630\",\"radio\":\"sx1262\","
             "\"build\":\"%s\",\"devid\":\"%08lx%08lx\",\"reset\":\"%s\",\"rtc\":%d,\"ina\":%d,"
             "\"radio_ok\":%d,\"boost\":%d,\"auto_min\":%lu",
             FW_NAME, FW_VERSION, __DATE__,
             (unsigned long)NRF_FICR->DEVICEID[1], (unsigned long)NRF_FICR->DEVICEID[0],
             s_reset_reason, rtc_ok() ? 1 : 0, ina_ok() ? 1 : 0,
             scan_radio_ok() ? 1 : 0, scan_get_boost() ? 1 : 0, (unsigned long)s_auto_min);
  out_printf(",\"home\":{\"f\":%.3f,\"bw\":%.1f,\"sf\":%u,\"cr\":%u}}", f, bw, sf, cr);
  out_end();
}

static void emit_help() {
  out_raw("# NoiseScope " FW_VERSION " — commands (NDJSON out, '#' lines are chatter)\n"
          "#   sweep [f0 f1 [step_khz [dwell_ms [passes]]]]   RSSI sweep; passes 0 = until stop\n"
          "#   hist  [f0 f1 [step_khz [samples [passes]]]]    spectral-scan histograms (33 x 4dB bins)\n"
          "#   dwell <f_mhz> [secs [win_ms]]                  time-domain stats on one frequency\n"
          "#   cad   [f_mhz [count]]                          LoRa preamble detection ratio\n"
          "#   stop | stat | info | help\n"
          "#   home <f_mhz> [bw_khz sf cr]                    set monitor channel\n"
          "#   boost on|off                                   RX boosted gain\n"
          "#   auto <minutes>                                 periodic background sweep (0 = off)\n"
          "#   time <epoch>                                   set clock (writes RTC when present)\n"
          "#   reboot | dfu [serial]                          restart / enter bootloader\n");
}

// ---- command dispatch ----

static void handle_command(char* line) {
  // tokenize (max 8 tokens)
  char* tok[8] = {0};
  int nt = 0;
  char* save = nullptr;
  for (char* t = strtok_r(line, " \t", &save); t && nt < 8; t = strtok_r(nullptr, " \t", &save)) {
    tok[nt++] = t;
  }
  if (nt == 0) return;
  for (char* p = tok[0]; *p; p++) *p = tolower(*p);
  const char* c = tok[0];

  if (!strcmp(c, "help")) { emit_help(); return; }
  if (!strcmp(c, "info")) { emit_info(); return; }
  if (!strcmp(c, "stat")) { emit_stat(); return; }

  if (!strcmp(c, "stop")) {
    scan_stop();
    emit_ack("stop", true, "monitor");
    return;
  }

  if (!strcmp(c, "sweep") || !strcmp(c, "hist")) {
    bool is_hist = !strcmp(c, "hist");
    float f0 = SWEEP_F0_MHZ, f1 = SWEEP_F1_MHZ, step = SWEEP_STEP_KHZ;
    long p4 = is_hist ? 2048 : SWEEP_DWELL_MS;   // samples / dwell_ms
    long passes = 1;
    if (nt >= 3) { f0 = atof(tok[1]); f1 = atof(tok[2]); }
    if (nt >= 4) step = atof(tok[3]);
    if (nt >= 5) p4 = atol(tok[4]);
    if (nt >= 6) passes = atol(tok[5]);
    scan_stop();
    bool ok = is_hist
      ? scan_start_hist(f0, f1, step, (uint16_t)p4, (int)passes)
      : scan_start_sweep(f0, f1, step, (uint16_t)p4, (int)passes);
    emit_ack(c, ok, ok ? "started" : "bad params or radio error");
    return;
  }

  if (!strcmp(c, "dwell")) {
    if (nt < 2) { emit_ack(c, false, "usage: dwell <f_mhz> [secs [win_ms]]"); return; }
    float f = atof(tok[1]);
    uint32_t secs = (nt >= 3) ? (uint32_t)atol(tok[2]) : 60;
    uint32_t win = (nt >= 4) ? (uint32_t)atol(tok[3]) : 1000;
    scan_stop();
    bool ok = scan_start_dwell(f, secs, win);
    emit_ack(c, ok, ok ? "started" : "bad params");
    return;
  }

  if (!strcmp(c, "cad")) {
    float fh, bw; uint8_t sf, cr;
    scan_get_home(&fh, &bw, &sf, &cr);
    float f = (nt >= 2) ? atof(tok[1]) : fh;
    uint16_t count = (nt >= 3) ? (uint16_t)atol(tok[2]) : 100;
    scan_stop();
    bool ok = scan_start_cad(f, count);
    emit_ack(c, ok, ok ? "started" : "bad params");
    return;
  }

  if (!strcmp(c, "home")) {
    if (nt < 2) { emit_ack(c, false, "usage: home <f_mhz> [bw_khz sf cr]"); return; }
    float fh, bw; uint8_t sf, cr;
    scan_get_home(&fh, &bw, &sf, &cr);
    float f = atof(tok[1]);
    if (nt >= 5) { bw = atof(tok[2]); sf = (uint8_t)atol(tok[3]); cr = (uint8_t)atol(tok[4]); }
    bool ok = scan_set_home(f, bw, sf, cr);
    emit_ack(c, ok, ok ? "retuned" : "bad params");
    return;
  }

  if (!strcmp(c, "boost")) {
    if (nt < 2) { emit_ack(c, false, "usage: boost on|off"); return; }
    bool on = !strcmp(tok[1], "on") || !strcmp(tok[1], "1");
    bool ok = scan_set_boost(on);
    emit_ack(c, ok, on ? "boosted gain on" : "boosted gain off");
    return;
  }

  if (!strcmp(c, "auto")) {
    if (nt < 2) { emit_ack(c, false, "usage: auto <minutes>"); return; }
    s_auto_min = (uint32_t)atol(tok[1]);
    s_auto_last_ms = millis();
    emit_ack(c, true, s_auto_min ? "auto-sweep on" : "auto-sweep off");
    return;
  }

  if (!strcmp(c, "time")) {
    if (nt < 2) { emit_ack(c, false, "usage: time <unix_epoch>"); return; }
    uint32_t t = (uint32_t)strtoul(tok[1], nullptr, 10);
    bool ok = (t > 1700000000UL) && time_set(t);
    emit_ack(c, ok, ok ? "clock set" : "bad epoch or rtc write failed");
    return;
  }

  if (!strcmp(c, "reboot")) {
    emit_ack(c, true, "rebooting");
    delay(50);
    NVIC_SystemReset();
  }

  if (!strcmp(c, "dfu")) {
    bool serial_only = (nt >= 2) && !strcmp(tok[1], "serial");
    emit_ack(c, true, serial_only ? "entering serial DFU" : "entering UF2 bootloader");
    delay(50);
    NRF_POWER->GPREGRET = serial_only ? 0x4e : 0x57;  // DFU_MAGIC_SERIAL_ONLY / DFU_MAGIC_UF2
    NVIC_SystemReset();
  }

  emit_ack(c, false, "unknown command, try: help");
}

static void check_commands() {
  if (lb_walter.ready) {
    handle_command(lb_walter.buf);
    lb_walter.len = 0; lb_walter.ready = false;
  }
  if (lb_usb.ready) {
    handle_command(lb_usb.buf);
    lb_usb.len = 0; lb_usb.ready = false;
  }
}

// ---- boot / main loop ----

static void capture_reset_reason() {
  uint32_t r = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS = r;   // clear (write 1 to clear)
  if (r & POWER_RESETREAS_DOG_Msk)           strcpy(s_reset_reason, "wdt");
  else if (r & POWER_RESETREAS_SREQ_Msk)     strcpy(s_reset_reason, "soft");
  else if (r & POWER_RESETREAS_LOCKUP_Msk)   strcpy(s_reset_reason, "lockup");
  else if (r & POWER_RESETREAS_RESETPIN_Msk) strcpy(s_reset_reason, "pin");
  else                                       strcpy(s_reset_reason, "por");
}

void setup() {
  capture_reset_reason();
  out_init();
  wdt_init();
  delay(100);            // let the 3V3 rail and TCXO settle after a power-kill cycle
  tele_init();
  bool radio_ok = scan_init();

  out_printf("{\"t\":\"boot\",\"fw\":\"%s\",\"ver\":\"%s\",\"hw\":\"rak4630\",\"radio\":\"sx1262\","
             "\"reset\":\"%s\",\"radio_ok\":%s,\"rtc\":%d,\"ina\":%d,\"ts\":%lu}",
             FW_NAME, FW_VERSION, s_reset_reason, radio_ok ? "true" : "false",
             rtc_ok() ? 1 : 0, ina_ok() ? 1 : 0, (unsigned long)time_epoch());
  out_end();

  s_stat_next_ms = millis() + STAT_INTERVAL_MS;
  s_auto_last_ms = millis();
}

void loop() {
  wdt_feed();
  out_service();
  input_poll();
  check_commands();
  scan_service();

  uint32_t now = millis();
  if ((int32_t)(now - s_stat_next_ms) >= 0) {
    emit_stat();
    s_stat_next_ms += STAT_INTERVAL_MS;
  }

  if (s_auto_min > 0 && scan_mode() == MODE_MONITOR &&
      now - s_auto_last_ms >= s_auto_min * 60000UL) {
    s_auto_last_ms = now;
    if (scan_start_sweep(SWEEP_F0_MHZ, SWEEP_F1_MHZ, SWEEP_STEP_KHZ, SWEEP_DWELL_MS, 1)) {
      out_line("{\"t\":\"ack\",\"cmd\":\"auto-sweep\",\"ok\":true}");
    }
  }
}
