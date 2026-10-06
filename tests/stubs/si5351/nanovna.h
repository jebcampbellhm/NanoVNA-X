#pragma once

#include "../nanovna.h"
#include "ch.h"

// Hardware-facing interfaces used by the production synthesizer driver.
#define AUDIO_CLOCK_REF 8000000U
#define IF_OFFSET 12000
#define DELAY_BAND_1_2 100
#define DELAY_BAND_3_4 200
#define DELAY_BANDCHANGE 5000
#define DELAY_CHANNEL_CHANGE 100
#define DELAY_RESET_PLL_BEFORE 0
#define DELAY_RESET_PLL_AFTER 4000

static inline uint32_t clamp_harmonic_threshold(uint32_t value) {
  return value < FREQUENCY_MIN ? FREQUENCY_MIN :
         value > FREQUENCY_MAX ? FREQUENCY_MAX : value;
}

void i2c_transfer(uint8_t address, const uint8_t* data, int length);
bool i2c_receive(uint8_t address, const uint8_t* tx, int tx_length,
                 uint8_t* rx, int rx_length);
void tlv320aic3204_set_gain(uint8_t left, uint8_t right);
systime_t chVTGetSystemTimeX(void);
void chThdSleepMicroseconds(uint32_t us);
