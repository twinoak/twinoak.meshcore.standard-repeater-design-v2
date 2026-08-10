#include "scanner.h"
#include "config.h"
#include "out.h"
#include "telemetry.h"

#include <RadioLib.h>
#include <SPI.h>

// Semtech's spectral-scan RAM patch (origin: Lora-net/sx1302_hal, shipped with RadioLib)
#include <modules/SX126x/patches/SX126x_patch_scan.h>

static SX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

static bool s_radio_ok = false;
static volatile ScanMode s_mode = MODE_MONITOR;
static bool s_boost = (RX_BOOSTED_GAIN_DEFAULT != 0);
static uint32_t s_seq = 0;

// home channel (runtime-changeable copy of the build defaults)
static float   s_home_f  = HOME_FREQ_MHZ;
static float   s_home_bw = HOME_BW_KHZ;
static uint8_t s_home_sf = HOME_SF;
static uint8_t s_home_cr = HOME_CR;

// FSK receiver bandwidths the SX1262 supports (kHz)
static const float FSK_RBW[] = { 4.8f, 5.8f, 7.3f, 9.7f, 11.7f, 14.6f, 19.5f, 23.4f,
                                 29.3f, 39.0f, 46.9f, 58.6f, 78.2f, 93.8f, 117.3f,
                                 156.2f, 187.2f, 234.3f, 312.0f, 373.6f, 467.0f };

// ---------------- sweep / hist / dwell / cad state ----------------

struct SweepState {
  float f0, f1, step_khz, rbw;
  uint16_t dwell_ms, samples;   // samples used by hist mode
  uint16_t n_points, idx;
  int passes_left;              // -1 = infinite
  bool infinite;
  float acc[MAX_SWEEP_POINTS];
  int16_t mx10[MAX_SWEEP_POINTS];
  uint16_t n_per_point;
};
static SweepState sw;

struct DwellState {
  float f;
  uint32_t end_ms, win_ms, win_start_ms;
  uint32_t n, over;
  float sum, mx, mn, th;
  uint32_t next_sample_us;
};
static DwellState dw;

struct CadState {
  float f;
  uint16_t target, done, det;
};
static CadState cad;

// monitor accumulator
static struct {
  uint32_t n, over;
  float sum, mx, mn, th;
  uint32_t next_sample_us;
} mon;

// ---------------- low-level helpers ----------------

static void mon_reset_window() {
  // carry threshold over from the window that just ended
  if (mon.n > 0) mon.th = mon.mn + BURST_TH_DELTA_DB;
  mon.n = 0; mon.over = 0; mon.sum = 0;
  mon.mx = -200; mon.mn = 200;
}

static void apply_common() {
  radio.setCurrentLimit(SX126X_CURRENT_LIMIT);
  radio.setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
  radio.setRxBoostedGainMode(s_boost);
}

static int lora_begin(float f, float bw, uint8_t sf, uint8_t cr) {
  int st = radio.begin(f, bw, sf, cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                       10 /*never used, we don't TX*/, 16, SX126X_DIO3_TCXO_VOLTAGE, false);
  if (st == RADIOLIB_ERR_SPI_CMD_FAILED || st == RADIOLIB_ERR_SPI_CMD_INVALID) {
    st = radio.begin(f, bw, sf, cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 16, 0.0f, false);
  }
  if (st == RADIOLIB_ERR_NONE) apply_common();
  return st;
}

static int fsk_begin(float f, float rbw) {
  int st = radio.beginFSK(f, 4.8f, 5.0f, rbw, 10, 16, SX126X_DIO3_TCXO_VOLTAGE, false);
  if (st == RADIOLIB_ERR_SPI_CMD_FAILED || st == RADIOLIB_ERR_SPI_CMD_INVALID) {
    st = radio.beginFSK(f, 4.8f, 5.0f, rbw, 10, 16, 0.0f, false);
  }
  if (st == RADIOLIB_ERR_NONE) apply_common();
  return st;
}

static bool to_home_rx() {
  int st = lora_begin(s_home_f, s_home_bw, s_home_sf, s_home_cr);
  if (st != RADIOLIB_ERR_NONE) { out_line("{\"t\":\"err\",\"msg\":\"lora reinit failed (%d)\"}", st); return false; }
  st = radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) { out_line("{\"t\":\"err\",\"msg\":\"startReceive failed (%d)\"}", st); return false; }
  mon_reset_window();
  mon.th = -100;   // sane first-window default
  return true;
}

static float pick_rbw(float step_khz) {
  for (size_t i = 0; i < sizeof(FSK_RBW) / sizeof(FSK_RBW[0]); i++) {
    if (FSK_RBW[i] >= step_khz * 1.1f) return FSK_RBW[i];
  }
  return FSK_RBW[sizeof(FSK_RBW) / sizeof(FSK_RBW[0]) - 1];
}

static void retune_rx(float f_mhz) {
  radio.standby();
  radio.setFrequency(f_mhz);
  radio.startReceive();
}

static void emit_meta() {
  out_printf("\"seq\":%lu,\"ts\":%lu,\"ms\":%lu",
             (unsigned long)++s_seq, (unsigned long)time_epoch(), (unsigned long)millis());
}

// Sample RSSI for dur_ms while keeping serial RX drained. Returns avg/max via args.
static void sample_window(uint16_t dur_ms, float* avg, float* mx_out, uint16_t* n_out) {
  uint32_t t_end = micros() + (uint32_t)dur_ms * 1000UL;
  uint32_t t_next = micros() + 500;   // let AGC settle before first sample
  float sum = 0, mx = -200;
  uint16_t n = 0;
  while ((int32_t)(t_end - micros()) > 0) {
    if ((int32_t)(t_next - micros()) <= 0) {
      float v = radio.getRSSI(false);
      sum += v;
      if (v > mx) mx = v;
      n++;
      t_next += 200;   // ~5 kHz sampling inside the window
    }
    input_poll();
  }
  *avg = (n > 0) ? sum / n : -200;
  *mx_out = mx;
  *n_out = n;
}

// ---------------- init ----------------

bool scan_init() {
  SPI.setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  SPI.begin();
  s_radio_ok = to_home_rx();
  s_mode = MODE_MONITOR;
  return s_radio_ok;
}

bool scan_radio_ok() { return s_radio_ok; }
ScanMode scan_mode() { return s_mode; }

const char* scan_mode_name() {
  switch (s_mode) {
    case MODE_SWEEP: return "sweep";
    case MODE_HIST:  return "hist";
    case MODE_DWELL: return "dwell";
    case MODE_CAD:   return "cad";
    default:         return "monitor";
  }
}

// ---------------- mode starters ----------------

static bool sweep_common_setup(float f0, float f1, float step_khz, int passes) {
  if (!s_radio_ok) return false;
  if (f1 <= f0 || f0 < 150.0f || f1 > 960.0f) return false;
  if (step_khz < 1.0f || step_khz > 1000.0f) return false;
  uint32_t n = (uint32_t)((f1 - f0) * 1000.0f / step_khz) + 1;
  if (n < 2 || n > MAX_SWEEP_POINTS) return false;
  sw.f0 = f0; sw.f1 = f1; sw.step_khz = step_khz;
  sw.n_points = n; sw.idx = 0;
  sw.infinite = (passes == 0);
  sw.passes_left = sw.infinite ? -1 : passes;
  sw.rbw = pick_rbw(step_khz);
  for (uint32_t i = 0; i < n; i++) { sw.acc[i] = 0; sw.mx10[i] = -2000; }
  return true;
}

bool scan_start_sweep(float f0, float f1, float step_khz, uint16_t dwell_ms, int passes) {
  if (dwell_ms < 5 || dwell_ms > 500) return false;
  if (!sweep_common_setup(f0, f1, step_khz, passes)) return false;
  sw.dwell_ms = dwell_ms;
  int st = fsk_begin(f0, sw.rbw);
  if (st != RADIOLIB_ERR_NONE) {
    out_line("{\"t\":\"err\",\"msg\":\"fsk init failed (%d)\"}", st);
    to_home_rx();
    return false;
  }
  s_mode = MODE_SWEEP;
  return true;
}

bool scan_start_hist(float f0, float f1, float step_khz, uint16_t samples, int passes) {
  if (samples < 256) samples = 256;
  if (!sweep_common_setup(f0, f1, step_khz, passes)) return false;
  sw.samples = samples;
  int st = fsk_begin(f0, sw.rbw);
  if (st == RADIOLIB_ERR_NONE) {
    st = radio.uploadPatch(sx126x_patch_scan, sizeof(sx126x_patch_scan));
  }
  if (st != RADIOLIB_ERR_NONE) {
    out_line("{\"t\":\"err\",\"msg\":\"hist init/patch failed (%d)\"}", st);
    radio.reset();
    to_home_rx();
    return false;
  }
  s_mode = MODE_HIST;
  return true;
}

bool scan_start_dwell(float f_mhz, uint32_t secs, uint32_t win_ms) {
  if (!s_radio_ok) return false;
  if (f_mhz < 150.0f || f_mhz > 960.0f) return false;
  if (secs < 1) secs = 1;
  if (secs > 86400) secs = 86400;
  if (win_ms < 100) win_ms = 100;
  if (win_ms > 60000) win_ms = 60000;
  dw.f = f_mhz;
  dw.end_ms = millis() + secs * 1000UL;
  dw.win_ms = win_ms;
  dw.win_start_ms = millis();
  dw.n = 0; dw.over = 0; dw.sum = 0; dw.mx = -200; dw.mn = 200; dw.th = -100;
  dw.next_sample_us = micros();
  retune_rx(f_mhz);            // stays in LoRa mode: RBW = the home channel filter
  s_mode = MODE_DWELL;
  return true;
}

bool scan_start_cad(float f_mhz, uint16_t count) {
  if (!s_radio_ok) return false;
  if (f_mhz < 150.0f || f_mhz > 960.0f) return false;
  if (count < 1) count = 1;
  if (count > 1000) count = 1000;
  cad.f = f_mhz; cad.target = count; cad.done = 0; cad.det = 0;
  radio.standby();
  radio.setFrequency(f_mhz);
  s_mode = MODE_CAD;
  return true;
}

void scan_stop() {
  if (s_mode == MODE_HIST) {
    radio.spectralScanAbort();
    radio.reset();             // purge the RAM patch before going back to LoRa
  }
  if (s_mode != MODE_MONITOR) {
    to_home_rx();
    s_mode = MODE_MONITOR;
  }
}

// ---------------- runtime config ----------------

bool scan_set_home(float f_mhz, float bw_khz, uint8_t sf, uint8_t cr) {
  if (f_mhz < 150.0f || f_mhz > 960.0f) return false;
  if (sf < 5 || sf > 12 || cr < 5 || cr > 8) return false;
  s_home_f = f_mhz; s_home_bw = bw_khz; s_home_sf = sf; s_home_cr = cr;
  if (s_mode == MODE_MONITOR) return to_home_rx();
  return true;
}

bool scan_set_boost(bool on) {
  s_boost = on;
  return radio.setRxBoostedGainMode(on) == RADIOLIB_ERR_NONE;
}

bool scan_get_boost() { return s_boost; }

void scan_get_home(float* f, float* bw, uint8_t* sf, uint8_t* cr) {
  *f = s_home_f; *bw = s_home_bw; *sf = s_home_sf; *cr = s_home_cr;
}

bool scan_get_monitor_stats(MonStats* s, bool reset) {
  if (mon.n == 0) return false;
  s->n = mon.n;
  s->avg = mon.sum / mon.n;
  s->mx = mon.mx;
  s->mn = mon.mn;
  s->dc = (float)mon.over / (float)mon.n;
  s->th = mon.th;
  if (reset) mon_reset_window();
  return true;
}

// ---------------- per-mode service ----------------

static void sweep_emit_pass() {
  out_printf("{\"t\":\"sweep\",");
  emit_meta();
  out_printf(",\"f0\":%.3f,\"f1\":%.3f,\"step\":%.1f,\"rbw\":%.1f,\"dwell\":%u,\"n\":%u,\"unit\":\"dBm\",\"avg\":[",
             sw.f0, sw.f1, sw.step_khz, sw.rbw, sw.dwell_ms, sw.n_points);
  for (uint16_t i = 0; i < sw.n_points; i++) {
    out_printf(i ? ",%.1f" : "%.1f", sw.acc[i]);
  }
  out_printf("],\"max\":[");
  for (uint16_t i = 0; i < sw.n_points; i++) {
    out_printf(i ? ",%.1f" : "%.1f", sw.mx10[i] / 10.0f);
  }
  out_raw("]}");
  out_end();
}

static void pass_finished(const char* what) {
  sw.idx = 0;
  if (!sw.infinite) {
    sw.passes_left--;
    if (sw.passes_left <= 0) {
      out_printf("{\"t\":\"done\",\"cmd\":\"%s\",", what);
      emit_meta();
      out_raw("}");
      out_end();
      if (s_mode == MODE_HIST) { radio.reset(); }
      to_home_rx();
      s_mode = MODE_MONITOR;
      return;
    }
  }
  if (s_mode == MODE_SWEEP) {
    for (uint16_t i = 0; i < sw.n_points; i++) { sw.acc[i] = 0; sw.mx10[i] = -2000; }
  }
}

static void service_sweep() {
  float f = sw.f0 + (sw.idx * sw.step_khz) / 1000.0f;
  radio.standby();
  radio.setFrequency(f);
  radio.startReceive();
  float avg, mx; uint16_t n;
  sample_window(sw.dwell_ms, &avg, &mx, &n);
  sw.acc[sw.idx] = avg;
  sw.mx10[sw.idx] = (int16_t)(mx * 10.0f);
  sw.idx++;
  if (sw.idx >= sw.n_points) {
    sweep_emit_pass();
    pass_finished("sweep");
  }
}

static void service_hist() {
  float f = sw.f0 + (sw.idx * sw.step_khz) / 1000.0f;
  radio.standby();
  radio.setFrequency(f);
  int st = radio.spectralScanStart(sw.samples);
  if (st != RADIOLIB_ERR_NONE) {
    out_line("{\"t\":\"err\",\"msg\":\"scan start failed (%d) at %.3f\"}", st, f);
    scan_stop();
    return;
  }
  // 8.2 us per sample + generous margin
  uint32_t t_out = millis() + (sw.samples / 100) + 100;
  bool done = false;
  while ((int32_t)(t_out - millis()) > 0) {
    if (radio.spectralScanGetStatus() == RADIOLIB_ERR_NONE) { done = true; break; }
    input_poll();
    yield();
  }
  if (!done) {
    radio.spectralScanAbort();
    out_line("{\"t\":\"err\",\"msg\":\"scan timeout at %.3f\"}", f);
    scan_stop();
    return;
  }
  uint16_t bins[RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE];
  st = radio.spectralScanGetResult(bins);
  if (st != RADIOLIB_ERR_NONE) {
    out_line("{\"t\":\"err\",\"msg\":\"scan result failed (%d) at %.3f\"}", st, f);
    scan_stop();
    return;
  }
  out_printf("{\"t\":\"hist\",");
  emit_meta();
  out_printf(",\"f\":%.3f,\"rbw\":%.1f,\"n\":%u,\"b0\":-11,\"bw_db\":4,\"bins\":[", f, sw.rbw, sw.samples);
  for (int i = 0; i < HIST_BINS; i++) {
    out_printf(i ? ",%u" : "%u", bins[i]);
  }
  out_raw("]}");
  out_end();
  sw.idx++;
  if (sw.idx >= sw.n_points) pass_finished("hist");
}

static void service_dwell() {
  uint32_t now_us = micros();
  if ((int32_t)(dw.next_sample_us - now_us) <= 0) {
    float v = radio.getRSSI(false);
    dw.n++; dw.sum += v;
    if (v > dw.mx) dw.mx = v;
    if (v < dw.mn) dw.mn = v;
    if (v > dw.th) dw.over++;
    dw.next_sample_us += MON_SAMPLE_US;
  }
  uint32_t now = millis();
  if (now - dw.win_start_ms >= dw.win_ms && dw.n > 0) {
    out_printf("{\"t\":\"dwell\",");
    emit_meta();
    out_printf(",\"f\":%.3f,\"win\":%lu,\"n\":%lu,\"avg\":%.1f,\"max\":%.1f,\"min\":%.1f,\"dc\":%.4f,\"th\":%.1f}",
               dw.f, (unsigned long)dw.win_ms, (unsigned long)dw.n,
               dw.sum / dw.n, dw.mx, dw.mn, (float)dw.over / dw.n, dw.th);
    out_end();
    dw.th = dw.mn + BURST_TH_DELTA_DB;
    dw.n = 0; dw.over = 0; dw.sum = 0; dw.mx = -200; dw.mn = 200;
    dw.win_start_ms = now;
  }
  if ((int32_t)(dw.end_ms - now) <= 0) {
    out_printf("{\"t\":\"done\",\"cmd\":\"dwell\",");
    emit_meta();
    out_raw("}");
    out_end();
    to_home_rx();
    s_mode = MODE_MONITOR;
  }
}

static void service_cad() {
  for (int i = 0; i < 4 && cad.done < cad.target; i++) {
    int16_t st = radio.scanChannel();
    if (st == RADIOLIB_LORA_DETECTED) cad.det++;
    cad.done++;
    input_poll();
  }
  if (cad.done >= cad.target) {
    out_printf("{\"t\":\"cad\",");
    emit_meta();
    out_printf(",\"f\":%.3f,\"sf\":%u,\"bw\":%.1f,\"n\":%u,\"det\":%u}",
               cad.f, s_home_sf, s_home_bw, cad.target, cad.det);
    out_end();
    to_home_rx();
    s_mode = MODE_MONITOR;
  }
}

static void service_monitor() {
  uint32_t now_us = micros();
  if ((int32_t)(mon.next_sample_us - now_us) <= 0) {
    float v = radio.getRSSI(false);
    mon.n++; mon.sum += v;
    if (v > mon.mx) mon.mx = v;
    if (v < mon.mn) mon.mn = v;
    if (v > mon.th) mon.over++;
    mon.next_sample_us += MON_SAMPLE_US;
    // resync if we fell behind (e.g. after a long serial write)
    if ((int32_t)(now_us - mon.next_sample_us) > 100000) mon.next_sample_us = now_us;
  }
}

void scan_service() {
  if (!s_radio_ok) return;
  switch (s_mode) {
    case MODE_SWEEP: service_sweep(); break;
    case MODE_HIST:  service_hist();  break;
    case MODE_DWELL: service_dwell(); break;
    case MODE_CAD:   service_cad();   break;
    default:         service_monitor(); break;
  }
}
