#pragma once
#include <Arduino.h>

// NDJSON output helper: every report line goes to the Walter UART always,
// and to USB CDC when a host is attached. Human-readable text is prefixed
// with "# " so line-parsers can skip anything that doesn't start with '{'.

void out_init();
void out_service();                       // refresh USB-attached state (call from loop)
void out_raw(const char* s);              // write a fragment (no newline handling)
void out_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void out_end();                           // terminate the current line ('\n')
void out_line(const char* fmt, ...) __attribute__((format(printf, 1, 2))); // fragment + '\n'
