#include "telemetry.h"
#include "config.h"
#include <Wire.h>

static bool s_rtc = false;
static bool s_ina = false;
static uint32_t s_epoch_base = 0;    // epoch at s_millis_base (0 = never set)
static uint32_t s_millis_base = 0;

static bool i2c_probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ---- RV-3028-C7: 32-bit UNIX time counter at regs 0x1B..0x1E (LSB first) ----

static bool rtc_read_unix(uint32_t* t) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)0x1B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)RTC_ADDR, (uint8_t)4) != 4) return false;
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= ((uint32_t)Wire.read()) << (8 * i);
  *t = v;
  return true;
}

static bool rtc_write_unix(uint32_t t) {
  // App-note recommended: write twice to dodge a counter tick mid-write
  for (int pass = 0; pass < 2; pass++) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write((uint8_t)0x1B);
    for (int i = 0; i < 4; i++) Wire.write((uint8_t)((t >> (8 * i)) & 0xFF));
    if (Wire.endTransmission() != 0) return false;
  }
  return true;
}

bool tele_init() {
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);
  s_rtc = i2c_probe(RTC_ADDR);
  s_ina = i2c_probe(INA_ADDR);
  if (s_rtc) {
    uint32_t t;
    if (rtc_read_unix(&t) && t > 1700000000UL) {   // sanity: after Nov 2023 = "has been set"
      s_epoch_base = t;
      s_millis_base = millis();
    }
  }
  return s_rtc || s_ina;
}

bool rtc_ok() { return s_rtc; }
bool ina_ok() { return s_ina; }

uint32_t time_epoch() {
  if (s_epoch_base == 0) return 0;
  return s_epoch_base + (millis() - s_millis_base) / 1000UL;
}

bool time_set(uint32_t epoch) {
  s_epoch_base = epoch;
  s_millis_base = millis();
  if (s_rtc) return rtc_write_unix(epoch);
  return true;
}

// ---- INA3221: 16-bit BE registers; shunt LSB 40 uV, bus LSB 8 mV (both >>3) ----

static bool ina_reg(uint8_t reg, int16_t* out) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)INA_ADDR, (uint8_t)2) != 2) return false;
  *out = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

bool ina_read(uint8_t ch, float* bus_v, float* cur_ma) {
  if (!s_ina || ch < 1 || ch > 3) return false;
  int16_t shunt_raw, bus_raw;
  uint8_t base = 0x01 + (ch - 1) * 2;          // shunt reg; bus = base+1
  if (!ina_reg(base, &shunt_raw)) return false;
  if (!ina_reg(base + 1, &bus_raw)) return false;
  float shunt_uv = (shunt_raw >> 3) * 40.0f;
  *bus_v  = (bus_raw >> 3) * 0.008f;
  *cur_ma = shunt_uv / (float)INA_SHUNT_MILLIOHM;
  return true;
}
