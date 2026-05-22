#ifndef RG15_H
#define RG15_H

#include <Arduino.h>

bool readRG15Line(char *out, size_t maxLen, uint32_t timeoutMs);

#endif