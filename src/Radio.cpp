#include "Radio.h"
#include "Config.h"
#include "driver/i2s.h"
#include "driver/periph_ctrl.h"
#include "driver/rtc_io.h"
#include "esp32-hal.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"

// Reference to internal function
extern "C" {
void rtc_clk_apll_enable(bool enable, uint32_t sdm0, uint32_t sdm1,
                         uint32_t sdm2, uint32_t o_div);
}

// Signal index for CLK_OUT1 (Output of APLL/RTC Mux) to GPIO Matrix
// This allows us to route the internal APLL clock to any pin.
#ifndef I2S0_CLKO_OUT_IDX
#define I2S0_CLKO_OUT_IDX 224
#endif

void Radio::begin() {
  // 1. Enable I2S0 Peripheral
  periph_module_enable(PERIPH_I2S0_MODULE);

  // 2. Configure Pin
  pinMode(RF_PIN, OUTPUT);

  // 3. Route I2S0_CLKO_OUT (MCLK) to our RF_PIN
  // We used index 224 which corresponds to CLK_OUT1.
  // If we enable APLL via rtc_clk_apll_enable, it drives CLK_OUT1.
  // By routing this signal to the GPIO Matrix, we can output it on RF_PIN.
  gpio_matrix_out(RF_PIN, I2S0_CLKO_OUT_IDX, false, false);

  // 4. Ensure I2S0 is configured to use APLL as clock source
  // This part is handled by rtc_clk_apll_enable?
  // No, rtc_clk_apll_enable configures the PLL itself.
  // We also need to tell I2S0 to use it?
  // Actually, if we use CLK_OUT1 signal (224), that comes directly from RTC
  // subsystem. So we *might* not even need I2S0 enabled if we just use the RTC
  // signal? BUT APLL is usually not running unless requested. Let's keep the
  // I2S0 configuration just in case it forces the path active.

  I2S0.clkm_conf.val = 0;
  I2S0.clkm_conf.clka_en = 1; // Use APLL
  I2S0.clkm_conf.clkm_div_num = 1;
  I2S0.clkm_conf.clkm_div_b = 0;
  I2S0.clkm_conf.clkm_div_a = 1;
}

void Radio::setFrequency(double freq) {
  if (freq < 100000) {
    Radio::disable();
    return;
  }

  uint32_t sdm0, sdm1, sdm2, o_div;
  long xtal_freq = 40000000;

  int min_o_div = 0;
  for (o_div = 0; o_div < 32; o_div++) {
    double f_apll_target = freq * 2.0 * (o_div + 2);
    if (f_apll_target >= 350000000 && f_apll_target <= 500000000) {
      break;
    }
  }
  if (o_div >= 32)
    o_div = 6;

  double total_factor = (freq * 2.0 * (o_div + 2)) / (double)xtal_freq;
  total_factor -= 4.0;
  if (total_factor < 0)
    total_factor = 0;

  sdm2 = (uint32_t)total_factor;
  double remainder1 = total_factor - sdm2;
  sdm1 = (uint32_t)(remainder1 * 256.0);
  double remainder2 = remainder1 - (sdm1 / 256.0);
  sdm0 = (uint32_t)(remainder2 * 65536.0);

  rtc_clk_apll_enable(true, sdm0, sdm1, sdm2, o_div);

  // Re-assert matrix routing (sometimes needed if pin mode resets)
  gpio_matrix_out(RF_PIN, I2S0_CLKO_OUT_IDX, false, false);
}

void Radio::disable() {
  rtc_clk_apll_enable(false, 0, 0, 0, 0);
  pinMode(RF_PIN, INPUT);
}
