// Minimal Arduino shim for the host-side BOOT key test.
//
// This directory is only on the include path when a test is compiled with
// -Itools/host; PlatformIO never sees it. It deliberately implements only
// the four symbols boot_button.cpp uses.
#pragma once
#include <stdint.h>
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
void pinMode(int pin, int mode);
int digitalRead(int pin);
uint32_t millis();
