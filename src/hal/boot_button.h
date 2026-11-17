#pragma once

namespace boot_button {

// Configure the BOOT key as a pulled-up input.
void begin();

enum class Event : unsigned char {
  None,
  Short,  // one accent step
  Long,   // switch between boost gauge and G-meter
};

// Poll the key once per loop. A long press is reported at the threshold
// (while the key is still held); its release does not also report a short
// press.
Event poll();

}  // namespace boot_button
