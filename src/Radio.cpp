#include "Radio.h"
#include "Config.h"

#include "driver/periph_ctrl.h"
#include "rom/gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2s_struct.h"
#include "soc/rtc.h"

// APLL VCO: f_vco = XTAL * (4 + sdm2 + sdm1/256 + sdm0/65536),
// valid between 350 and 500 MHz.
// RF pin:   f_rf = f_vco / (2 * (o_div + 2)) / CLKM_DIV / BCK_DIV
static const double XTAL_HZ = 40000000.0;
static const double APLL_VCO_MIN = 350000000.0;
static const double APLL_VCO_MAX = 500000000.0;
static const uint32_t CLKM_DIV = 2; // I2S master clock divider (min 2)
static const uint32_t BCK_DIV = 2;  // I2S bit clock divider (min 2)

// One SDM LSB is XTAL/65536 at the VCO, i.e. ~19 Hz at a 14 MHz output. The
// tuning task runs every DITHER_PERIOD_MS and dithers between two adjacent
// SDM codes so the average frequency hits the requested tone. The resulting
// FM sidebands sit at the dither rate (+/-1 kHz, roughly -40 dBc), far
// outside the WSPR/FT8 decode bandwidth.
static const uint32_t DITHER_PERIOD_MS = 1;

struct TuneState {
  bool enabled;
  uint32_t sdmWord; // integer SDM code: (sdm2 << 16) | (sdm1 << 8) | sdm0
  double sdmFrac;   // fractional SDM code, realized by dithering
  uint32_t oDiv;
};

static portMUX_TYPE tuneMux = portMUX_INITIALIZER_UNLOCKED;
static TuneState tuneState = {false, 0, 0.0, 0};

static void applyApll(uint32_t sdmWord, uint32_t oDiv) {
  rtc_clk_apll_enable(true, sdmWord & 0xFF, (sdmWord >> 8) & 0xFF,
                      (sdmWord >> 16) & 0x3F, oDiv);
}

// All APLL register access happens here, so writes never interleave.
static void tuningTask(void *) {
  bool outputOn = false;
  uint32_t lastWord = 0;
  double acc = 0.0;

  for (;;) {
    portENTER_CRITICAL(&tuneMux);
    TuneState s = tuneState;
    portEXIT_CRITICAL(&tuneMux);

    if (!s.enabled) {
      if (outputOn) {
        rtc_clk_apll_enable(false, 0, 0, 0, 0);
        pinMode(RF_PIN, INPUT);
        outputOn = false;
      }
    } else {
      // First-order sigma-delta: average output = sdmWord + sdmFrac
      acc += s.sdmFrac;
      uint32_t word = s.sdmWord;
      if (acc >= 1.0) {
        acc -= 1.0;
        word++;
      }

      if (!outputOn || word != lastWord) {
        applyApll(word, s.oDiv);
        lastWord = word;
      }
      if (!outputOn) {
        pinMode(RF_PIN, OUTPUT);
        gpio_matrix_out(RF_PIN, I2S0O_BCK_OUT_IDX, false, false);
        outputOn = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(DITHER_PERIOD_MS));
  }
}

void Radio::begin() {
  periph_module_enable(PERIPH_I2S0_MODULE);

  I2S0.conf.tx_reset = 1;
  I2S0.conf.tx_reset = 0;

  // Free-running master clock from the APLL. No DMA or sample data is
  // needed: BCK toggles as long as TX is started.
  I2S0.clkm_conf.val = 0;
  I2S0.clkm_conf.clkm_div_a = 1;
  I2S0.clkm_conf.clkm_div_b = 0;
  I2S0.clkm_conf.clkm_div_num = CLKM_DIV;
  I2S0.clkm_conf.clka_en = 1; // clock source = APLL
  I2S0.clkm_conf.clk_en = 1;
  I2S0.sample_rate_conf.tx_bck_div_num = BCK_DIV;
  I2S0.conf.tx_start = 1;

  xTaskCreatePinnedToCore(tuningTask, "radio_tune", 4096, nullptr, 10, nullptr,
                          1);
}

void Radio::setFrequency(double freq) {
  freq *= 1.0 + FREQ_CORRECTION_PPM / 1e6;

  uint32_t oDiv;
  double vco = 0.0;
  for (oDiv = 0; oDiv < 32; oDiv++) {
    vco = freq * CLKM_DIV * BCK_DIV * 2.0 * (oDiv + 2);
    if (vco >= APLL_VCO_MIN && vco <= APLL_VCO_MAX)
      break;
  }

  double sdm = (vco / XTAL_HZ - 4.0) * 65536.0;
  if (oDiv >= 32 || sdm < 0) { // frequency out of reach
    disable();
    return;
  }

  portENTER_CRITICAL(&tuneMux);
  tuneState.enabled = true;
  tuneState.sdmWord = (uint32_t)sdm;
  tuneState.sdmFrac = sdm - tuneState.sdmWord;
  tuneState.oDiv = oDiv;
  portEXIT_CRITICAL(&tuneMux);
}

void Radio::disable() {
  portENTER_CRITICAL(&tuneMux);
  tuneState.enabled = false;
  portEXIT_CRITICAL(&tuneMux);
}
