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
NanoVNA-X RF settling constants are retained and need confirmation on physical
hardware; the startup and timer handling fixes below do not change those values.

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

## Startup and timing corrections

Saved configuration is restored before generator initialization, so both the
selected RF profile and crystal calibration apply on the first boot-time setup.
Board peripherals are initialized once. Codec clocks still start before slave
I2S, with the existing 200 ms codec startup delay.

Capture and PLL-lock waits use unsigned elapsed time, including across the
32-bit, 100 kHz system timer rollover (approximately 11.93 hours). Capture timing
is published while DMA interrupts are masked. The sweep-start delay applies to
the first frequency point only; band-change discard cycles remain unchanged.

ADC startup now waits for calibration and a fresh ready flag, with a 10 ms
timeout that halts for watchdog recovery instead of proceeding with an unready
ADC. Both targets allow 1 ms after calibration; F303 also allows 1 ms for its
internal regulator before calibration. These conservative startup-only delays
cover the requirements in STM32 [RM0091](https://www.st.com/resource/en/reference_manual/DM00031936-.pdf)
and [RM0316](https://www.st.com/resource/en/reference_manual/DM00043574.pdf).
This ADC handles touch/battery measurements, not the RF codec samples.

`make test` includes a fake-clock harness executing the production capture,
PLL-wait and sweep-setup functions. It checks ordinary and rollover timing,
first-point delays, preserved discard cycles, and ADC readiness/timeout paths.
The synthesizer test also starts with a saved ZeeTK profile. Startup ordering
is checked separately; these tests do not emulate analog settling or the full
STM32 ADC peripheral.

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
- **Remaining startup comparisons:** ADC readiness and initialization ordering
  are addressed above. NanoVNA-X already initializes its display late and
  starts codec clocks before slave I2S. Any further vendor LCD/codec delay
  changes should follow cold-start tests on both boards.

Sources: [1.2.43 to 1.2.44](https://github.com/hugen79/NanoVNA-H/compare/1.2.43...1.2.44)
and [1.2.44 to 1.2.50](https://github.com/hugen79/NanoVNA-H/compare/1.2.44...1.2.50).

With WSL GCC 14.2.1, the previous default F072 build exceeded flash by 536 bytes;
use the supported LTO option above. H4 uses the default build options.
