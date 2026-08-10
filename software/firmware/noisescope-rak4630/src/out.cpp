#include "out.h"
#include "config.h"
#include <stdarg.h>

static bool usb_ok = false;

void out_init() {
  WALTER_SERIAL.begin(WALTER_BAUD);
  Serial.begin(115200);   // USB CDC; non-blocking if no host
}

void out_service() {
  usb_ok = (bool)Serial;
}

void out_raw(const char* s) {
  WALTER_SERIAL.write(s);
  if (usb_ok) Serial.write(s);
}

void out_printf(const char* fmt, ...) {
  char buf[224];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out_raw(buf);
}

void out_end() {
  out_raw("\n");
}

void out_line(const char* fmt, ...) {
  char buf[224];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out_raw(buf);
  out_raw("\n");
}
