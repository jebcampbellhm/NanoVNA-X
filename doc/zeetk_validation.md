# ZeeTK hardware support (issue #10)

Adds a selectable ZeeTK RF profile for NanoVNA-H rev3.7 and NanoVNA-H4 rev4.4
using NE602A mixers and the SJWCH5351 signal generator. Earlier hardware
profiles and their saved configuration numbers remain unchanged. Profile
changes invalidate the synthesizer cache; harmonic band changes now update
drive strength even when the multisynth divider stays the same.

The profile's PLL multipliers, 32 kHz / 130 MHz boundaries, harmonic ratios,
drive strengths and codec gains come from [hugen79/NanoVNA-H 1.2.50](https://github.com/hugen79/NanoVNA-H/blob/816495e/si5351.c).
NanoVNA-X already supports the 600 Hz minimum and a configurable approximately
300 MHz harmonic threshold, so those global defaults are unchanged. Existing
NanoVNA-X sweep timing is retained and needs confirmation on physical hardware.

## Build and automated checks

Run in WSL Ubuntu, with GNU Make, host GCC and the Arm GNU toolchain installed:

```sh
make test
make clean TARGET=F303
make TARGET=F303
# Preserve build/H4.{elf,bin,hex} before cleaning for the other board.
make clean TARGET=F072
make TARGET=F072 USE_LTO=yes
```

The synthesizer test decodes actual I2C register writes to check RF and audio
frequencies, drive currents and ADC gain around band boundaries. It also
switches from a longer legacy table at 2.7 GHz to catch stale cached indices.

## Bench validation still required

1. On the matching ZK board, choose `System -> Device -> More -> Mode -> ZeeTK`,
   save configuration, power-cycle, and confirm the selection persists.
2. Select automatic calibration power and collect a fresh open/short/load/thru
   calibration. Do not reuse calibration collected under another RF profile.
3. Check a 50-ohm load and a through connection over the intended frequency
   range. Inspect both upward and downward sweeps around 32 kHz, 130 MHz,
   the configured harmonic threshold, 588 MHz, and the higher harmonic changes.
4. Check repeated sweeps for settling discontinuities and compare against the
   vendor's 1.2.50 ZK firmware on the same board. Host tests and compilation
   cannot establish RF accuracy or validate the 2.7 GHz upper range on this board.

## Deferred upstream changes (discuss before porting)

Audit date: 2026-10-06. RF settings match 1.2.50; these items are deferred:

- **Sweep timing:** upstream 1.2.50 H4 delays are 100/200 us for fixed-PLL/higher
  bands, 3000 us for band changes, 200 us for channel changes, and 2000 us at
  sweep start. H uses 5000/500 us for band changes/sweep start. NanoVNA-X adds
  capture cycles and PLL-lock handling; compare effective settling and bench
  results before adopting these constants.
- **Synthesizer arithmetic:** 1.2.50 scales PLL/multisynth calculations before
  fractional approximation and keeps cached frequency in unscaled Hz. Review
  precision and low-frequency cache behavior separately across all profiles.
- **NanoVNA-H startup:** 1.2.44 changes F072 ADC initialization (including
  clearing ADRDY before enabling), LCD startup order, and codec startup delay.
  NanoVNA-X already contains some related changes. Compare initialization
  paths before porting; this is separate from H4 ZeeTK RF settings.

Sources: [1.2.43 to 1.2.44](https://github.com/hugen79/NanoVNA-H/compare/1.2.43...1.2.44)
and [1.2.44 to 1.2.50](https://github.com/hugen79/NanoVNA-H/compare/1.2.44...1.2.50).

With WSL GCC 14.2.1, the previous default F072 build exceeded flash by 536 bytes;
use the supported LTO option above. H4 uses the default build options.
