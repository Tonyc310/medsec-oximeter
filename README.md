# medsec-oximeter

[![CI](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml/badge.svg)](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml)

A Bluetooth pulse oximeter built to the security expectations FDA sets for connected medical devices, together with the cybersecurity documentation a premarket submission needs: architecture views, threat model, risk assessment, security controls with test evidence, SBOM and a postmarket plan. The firmware runs on an nRF52840 under Zephyr and is tested in the Renode simulator.

> **This is a portfolio exercise, not a medical device.** It has not been cleared or approved by FDA or any other regulator. Don't use it on people or for any medical decision.

## Status

Phase 2 of 7, the baseline device. Once a second the firmware reports SpO2 and pulse rate from a MAX30101 pulse oximetry sensor's red and infrared light; BLE comes next. The baseline is built without security controls on purpose, so the threat model starts from an honest "before" state.

Renode has no model of the MAX30101, so [renode/Max30101.cs](renode/Max30101.cs) adds one: the registers Zephyr's driver uses, the 32-sample FIFO filled at the configured rate, the interrupt line, and a synthetic pulse waveform whose light level, pulse depth and heart rate a test can set.

## How it measures

The analysis lives in [core/](core/), plain C17 with no Zephyr, so the same code runs in the firmware and in the unit tests on a PC. It keeps the last four seconds of samples and, once a second:

- **Pulse rate**: compares the infrared signal with itself shifted by 0.25 to 2 seconds (autocorrelation, covering 240 down to 30 bpm) and takes the first shift where it repeats strongly. Taking the first rather than the strongest keeps a pulse from being read as half its rate.
- **SpO2**: oxygenated blood absorbs less red light than infrared, so the pulse shows weaker in red. The ratio of the two pulses, each against its own light level, gives SpO2 on a calibration line.
- **No reading** when the pulse is too weak (under a 0.1% perfusion index) or has no steady rhythm: the device shows nothing rather than a wrong number.

The calibration line, SpO2 = 110 − 25 R, is the textbook one. A real oximeter's comes from a clinical study against arterial blood samples (ISO 80601-2-61), so these readings are illustrative.

## Hardware

An nRF52840 DK and a MAX30101 (or MAX30102) breakout, wired to the DK's Arduino header. Not yet tested on the real board.

| Breakout | nRF52840 DK |
|---|---|
| SDA | SDA / D14 (P0.26) |
| SCL | SCL / D15 (P0.27) |
| INT | D2 (P1.03) |
| GND | GND |
| VIN | 5V, if the breakout has its own regulators (most do) |

The DK's GPIO run at 3.0 V: check that the breakout pulls SDA, SCL and INT up to no more than that.

## Build and run

Needs `git`, `cmake`, `ninja`, Python 3 with `venv`, `dtc` and `gperf`. From the repository root, once:

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install west
west init -l app
west update --narrow -o=--depth=1          # Zephyr v4.4.2 and its modules, into deps/
pip install -r deps/zephyr/scripts/requirements-base.txt
west sdk install -t arm-zephyr-eabi        # skip if a matching Zephyr SDK is installed
```

Then, in each new shell:

```bash
. .venv/bin/activate
west build -b nrf52840dk/nrf52840 -d build/nrf52840dk app
renode renode/oximeter.resc                # type `start`; the console opens in its own window
```

## Test

The code is C17 written to SEI CERT C. The host build of `core/` runs its unit tests under AddressSanitizer and UndefinedBehaviorSanitizer with strict conversion warnings, then two static analyzers check it: clang-tidy (its CERT C checks, the Clang static analyzer and bug-prone patterns, per [.clang-tidy](.clang-tidy)) and cppcheck.

```bash
cmake --workflow --preset host                 # unit tests, sanitized
clang-tidy -p build/host core/src/*.c
cppcheck --std=c11 --enable=warning,style,performance,portability -I core/include core/src
```

The Robot Framework test boots the firmware in [Renode](https://renode.io) 1.17 with the sensor model set to a patient (90 bpm, 94%) and checks the firmware reports that. It needs `renode-test` on `PATH` with its Python packages (`pip install -r <renode>/tests/requirements.txt`). It writes its report to the working directory, so run it from the build tree:

```bash
mkdir -p build/renode && cd build/renode && renode-test ../../renode/oximeter.robot
```

## Layout

```
app/       Zephyr application: west.yml (the Zephyr pin), prj.conf, board overlay, src/
core/      the oximetry analysis: portable C17, no Zephyr
tests/     Unity tests for core/, run on the host
renode/    Renode platform, scripts, the MAX30101 model and the Robot tests
deps/      Zephyr and its modules, fetched by west (git-ignored)
```

## License

MIT
