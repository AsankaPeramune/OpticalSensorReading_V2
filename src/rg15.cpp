#include <Arduino.h>
#include "rg15.h"

// Function implementation
bool readRG15Line(char *out, size_t maxLen, uint32_t timeoutMs) {
  size_t idx = 0;
  unsigned long t0 = millis();

  while (millis() - t0 < timeoutMs) {
    while (Serial1.available()) {
      char c = (char)Serial1.read();

      if (c == '\n') {
        out[idx] = '\0';
        return idx > 0;
      }

      if (c != '\r' && idx < maxLen - 1) {
        out[idx++] = c;
      }
    }
  }

  out[idx] = '\0';
  return false;
}