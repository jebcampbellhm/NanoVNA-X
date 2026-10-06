// Exercise the production driver through a fake I2C register bank.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nanovna.h"
#include "driver/si5351.h"

config_t config = {
    ._xtal_freq = XTALFREQ,
    ._harmonic_freq_threshold = FREQUENCY_THRESHOLD,
};
static uint8_t registers[256];
static uint8_t left_gain, right_gain;
static unsigned writes;

void i2c_transfer(uint8_t address, const uint8_t* data, int length) {
  assert(address == 0x60 && length >= 2);
  assert(data[0] + length - 1 <= (int)sizeof(registers));
  memcpy(registers + data[0], data + 1, length - 1);
  writes++;
}

bool i2c_receive(uint8_t address, const uint8_t* tx, int tx_length,
                 uint8_t* rx, int rx_length) {
  assert(address == 0x60 && tx_length == 1);
  memcpy(rx, registers + tx[0], rx_length);
  return true;
}

void tlv320aic3204_set_gain(uint8_t left, uint8_t right) {
  left_gain = left;
  right_gain = right;
}
systime_t chVTGetSystemTimeX(void) { return 0; }
void chThdSleepMilliseconds(uint32_t ms) { (void)ms; }
void chThdSleepMicroseconds(uint32_t us) { (void)us; }

// Decode the programmed PLL/MS ratio, independently of the driver's encoder.
static double ratio(unsigned base) {
  const uint8_t* r = registers + base;
  unsigned p1 = ((r[2] & 3) << 16) | (r[3] << 8) | r[4];
  unsigned p2 = ((r[5] & 15) << 16) | (r[6] << 8) | r[7];
  unsigned p3 = ((r[5] & 240) << 12) | (r[0] << 8) | r[1];
  assert(p3 != 0);
  return (p1 + 512.0 + (double)p2 / p3) / 128.0;
}

static double output_frequency(unsigned channel) {
  unsigned base = SI5351_REG_42_MULTISYNTH0 + 8 * channel;
  unsigned pll = registers[16 + channel] & SI5351_CLK_PLL_SELECT_B ?
                 SI5351_REG_PLL_B : SI5351_REG_PLL_A;
  double divider = (registers[base + 2] & SI5351_DIVBY4) == SI5351_DIVBY4 ?
                   4.0 : ratio(base);
  unsigned rdiv = 1U << ((registers[base + 2] >> 4) & 7);
  return XTALFREQ * ratio(pll) / divider / rdiv;
}

static void check_frequency(uint32_t frequency, unsigned harmonic,
                            unsigned offset_harmonic, unsigned gain, unsigned drive, unsigned lo_drive) {
  si5351_set_frequency(frequency, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  assert(fabs(output_frequency(1) * harmonic - frequency) < 50.0);
  assert(fabs(output_frequency(0) * offset_harmonic - (frequency + IF_OFFSET)) < 50.0);
  assert(fabs(output_frequency(2) - AUDIO_CLOCK_REF) < 5.0);
  assert(left_gain == gain && right_gain == gain);
  assert((registers[16] & 3) == lo_drive);
  assert((registers[17] & 3) == drive);
}

int main(void) {
  // Runtime restores config before initializing the generator on cold boot.
  config._band_mode = SI5351_BAND_ZEETK;
  si5351_init();
  check_frequency(500000000, 3, 5, 5, 2, 2);
  si5351_set_band_mode(SI5351_BAND_ZEETK);
  static const struct {
    uint32_t frequency;
    unsigned harmonic, offset_harmonic, gain, drive, lo_drive;
  } cases[] = {
      {600, 1, 1, 0, 0, 0},
      {31999, 1, 1, 0, 0, 0},
      {32000, 1, 1, 0, 2, 1},
      {130000000, 1, 1, 0, 2, 1},
      {130000001, 1, 1, 0, 2, 1},
      {FREQUENCY_THRESHOLD, 1, 1, 0, 2, 1},
      {FREQUENCY_THRESHOLD + 1, 3, 5, 5, 2, 2},
      {588000000, 3, 5, 5, 2, 2},
      {588000001, 3, 5, 5, 2, 2},
      {3 * FREQUENCY_THRESHOLD, 3, 5, 5, 2, 2},
      {3 * FREQUENCY_THRESHOLD + 1, 5, 7, 5, 2, 2},
      {5 * FREQUENCY_THRESHOLD, 5, 7, 5, 2, 2},
      // Same divider, but both outputs must switch from 6 to 8 mA here.
      {5 * FREQUENCY_THRESHOLD + 1, 7, 9, 40, 3, 3},
      {7 * FREQUENCY_THRESHOLD, 7, 9, 40, 3, 3},
      {7 * FREQUENCY_THRESHOLD + 1, 9, 11, 50, 3, 3},
      {FREQUENCY_MAX, 9, 11, 50, 3, 3},
  };
  // Drive register encodings above: 0=2 mA, 1=4 mA, 2=6 mA, 3=8 mA.
  // Exercise upward and downward transitions, including asymmetric RF/LO drive.
  for (unsigned pass = 0; pass < 2; pass++) {
    for (unsigned i = 0; i < ARRAY_COUNT(cases); i++) {
      unsigned n = pass == 0 ? i : ARRAY_COUNT(cases) - 1 - i;
      check_frequency(cases[n].frequency, cases[n].harmonic, cases[n].offset_harmonic,
                      cases[n].gain, cases[n].drive, cases[n].lo_drive);
      if (cases[n].frequency == 130000000)
        assert(fabs(ratio(SI5351_REG_PLL_A) - 40.0) < 0.0001);
      if (cases[n].frequency == 130000001)
        assert((registers[SI5351_REG_42_MULTISYNTH0 + 2] & SI5351_DIVBY4) == SI5351_DIVBY4);
    }
  }

  // Switching from an older, longer band table must not reuse its cached index.
  si5351_set_band_mode(SI5351_BAND_SI5351);
  si5351_set_frequency(FREQUENCY_MAX, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  unsigned before = writes;
  si5351_set_band_mode(SI5351_BAND_ZEETK);
  check_frequency(FREQUENCY_MAX, 9, 11, 50, 3, 3);
  assert(writes > before);

  // At the same fundamental frequency, changing profiles must update gain/drive.
  si5351_set_band_mode(SI5351_BAND_SWC5351);
  si5351_set_frequency(200000000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  assert(left_gain == 0);
  si5351_set_band_mode(SI5351_BAND_ZEETK);
  check_frequency(200000000, 1, 1, 0, 2, 1);

  si5351_set_band_mode(SI5351_BAND_SWC5351);
  si5351_set_frequency(500000000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  assert(left_gain != 5);
  si5351_set_band_mode(SI5351_BAND_ZEETK);
  check_frequency(500000000, 3, 5, 5, 2, 2);

  // Invalid saved modes fall back to the original profile without indexing past it.
  si5351_set_band_mode(UINT16_MAX);
  si5351_set_frequency(50000000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
  assert(left_gain == 0 && right_gain == 0);
  assert(fabs(output_frequency(1) - 50000000) < 50.0);
  for (unsigned mode = 0; mode < SI5351_BAND_COUNT; ++mode) {
    si5351_set_band_mode(mode);
    si5351_set_frequency(64000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
    assert(si5351_get_frequency() == 64000);
    si5351_set_frequency(1024000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
    assert(fabs(output_frequency(1) - 1024000) < 1.0);
    si5351_set_frequency(64000, SI5351_CLK_DRIVE_STRENGTH_AUTO);
    si5351_set_power(SI5351_CLK_DRIVE_STRENGTH_8MA);
    assert(si5351_get_frequency() == 64000);
    assert(fabs(output_frequency(1) - 64000) < 1.0);
    const uint32_t thresholds[] = {600, 100000000, FREQUENCY_THRESHOLD, FREQUENCY_MAX};
    for (unsigned i = 0; i < ARRAY_COUNT(thresholds); ++i) {
      config._harmonic_freq_threshold = thresholds[i];
      si5351_set_band_mode(mode);
      assert(si5351_get_harmonic_lvl(UINT32_MAX) < 16);
      si5351_set_frequency(FREQUENCY_MAX, SI5351_CLK_DRIVE_STRENGTH_AUTO);
      if (thresholds[i] <= 100000000)
        assert(fabs(output_frequency(1) * 11 - FREQUENCY_MAX) < 50.0);
    }
    config._harmonic_freq_threshold = FREQUENCY_THRESHOLD;
  }
  puts("si5351: ZeeTK frequencies, gains, drive and profile switching passed");
  return 0;
}
