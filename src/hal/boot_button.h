#pragma once

namespace boot_button {

// Configure the BOOT key as a pulled-up input.
void begin();

// True exactly once per debounced press. Call from loop().
bool pressed();

}  // namespace boot_button
