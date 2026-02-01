#pragma once

#include <Arduino.h>

class Radio {
public:
  // Initialize the Radio (APLL + GPIO)
  // pin: The GPIO pin to output the clock on (Must be a simplified I2S/CLOCK
  // pin or routed via Matrix) Note: APLL output usually goes to CLK_OUT1
  // (GPIO0), CLK_OUT2 (GPIO3), or CLK_OUT3 (GPIO1). However, on many devboards
  // GPIO0/1/3 are used. We will verify if we can route to other pins or if we
  // must use specific ones. For now, we will target CLK_OUT1 (GPIO0) - Careful
  // as it's boot pin. UPDATE: We can use ledcSetup for basic PWM but that's
  // jittery. We want LOW JITTER APLL. The APLL clock can be routed to GPIO 0,
  // 1, or 3 via the RTC clock out mux. On ESP32, GPIO 25/26 are DACs, but APLL
  // is different.

  // We will try using the `rtc_clk_apll_enable` and routing it to a pin.
  // Ideally we use a pin that is safe.

  static void begin();

  // Set frequency in Hz (double for precision)
  static void setFrequency(double freq);

  // Disable output
  static void disable();
};
