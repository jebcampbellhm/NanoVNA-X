# ZeeTK hardware support (issue #10)

Adds a selectable ZeeTK RF profile for NanoVNA-H rev3.7 and NanoVNA-H4 rev4.4
using NE602A mixers and the SJWCH5351 signal generator. Earlier hardware
profiles and their saved configuration numbers remain unchanged. Profile
changes invalidate the synthesizer cache; harmonic band changes now update
drive strength even when the multisynth divider stays the same.

The profile's PLL multipliers, 32 kHz / 145 MHz boundaries, harmonic ratios,
drive strengths and codec gains come from [hugen79/NanoVNA-H 1.2.43](https://github.com/hugen79/NanoVNA-H/blob/939a55b5f2ac968f21641a0a9b287d74b4c649e5/si5351.c).
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
make TARGET=F072
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
   range. Inspect both upward and downward sweeps around 32 kHz, 145 MHz,
   the configured harmonic threshold, 588 MHz, and the higher harmonic changes.
4. Check repeated sweeps for settling discontinuities and compare against the
   vendor's 1.2.43 ZK firmware on the same board. Host tests and compilation
   cannot establish RF accuracy or validate the 2.7 GHz upper range on this board.
