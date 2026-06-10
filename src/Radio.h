#pragma once

#include <Arduino.h>

// RF generator: ESP32 APLL -> I2S0 clock dividers -> BCK output, routed to
// RF_PIN through the GPIO matrix (I2S0O_BCK_OUT works on any output-capable
// GPIO, unlike CLK_OUT1/2/3 which are fixed to GPIO 0/1/3).
//
// The APLL's smallest tuning step is ~19 Hz at a 14 MHz output — far coarser
// than the 1.46 Hz WSPR / 6.25 Hz FT8 tone spacing. A background task dithers
// the APLL fractional divider between two adjacent codes (software
// fractional-N) so the average frequency lands exactly on the requested tone.
class Radio {
public:
  // Configure I2S0 as a free-running clock source and start the tuning task.
  static void begin();

  // Set output frequency in Hz (corrected by FREQ_CORRECTION_PPM).
  static void setFrequency(double freq);

  // Stop RF output (powers down the APLL, floats the pin).
  static void disable();
};
