# medsec-oximeter

[![CI](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml/badge.svg)](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml)

A Bluetooth pulse oximeter built to the security expectations FDA sets for connected medical devices, together with the cybersecurity documentation a premarket submission needs: architecture views, threat model, risk assessment, security controls with test evidence, SBOM and a postmarket plan. The firmware runs on an nRF52840 under Zephyr and is tested in the Renode simulator.

> **This is a portfolio exercise, not a medical device.** It has not been cleared or approved by FDA or any other regulator. Don't use it on people or for any medical decision.

## Status

Phase 2 of 7, the baseline device, is complete. Once a second the firmware works out SpO2 and pulse rate from a MAX30101 pulse oximetry sensor's red and infrared light, sends them over Bluetooth LE to a hub, and raises alarms against limits the hub can set. The baseline is built without security controls on purpose, so the threat model starts from an honest "before" state.

Phase 3, the architecture views and threat model, is under way in [docs/security/](docs/security/): so far the [system description](docs/security/system.md), the [threat model](docs/security/threat-model.md) and the [updatability view](docs/security/updatability.md).

Phase 4, the security controls, has started with the threat model's central case: a change of alarm limits must now be signed (see [Alarms](#alarms)).

Renode has no model of the MAX30101, so [renode/Max30101.cs](renode/Max30101.cs) adds one: the registers Zephyr's driver uses, the 32-sample FIFO filled at the configured rate, the interrupt line, and a synthetic pulse waveform whose light level, pulse depth and heart rate a test can set. Nor does it model the nRF52840's flash controller, which keeping anything in flash needs: [renode/Nvmc.cs](renode/Nvmc.cs) erases flash pages, and Zephyr's own flash driver tests check it.

## How it measures

The analysis lives in [core/](core/), plain C17 with no Zephyr, so the same code runs in the firmware and in the unit tests on a PC. It keeps the last four seconds of samples and, once a second:

- **Pulse rate**: compares the infrared signal with itself shifted by up to 2 seconds (autocorrelation) and takes the first shift where it repeats strongly. Taking the first rather than the strongest keeps a pulse from being read as half its rate.
- **SpO2**: oxygenated blood absorbs less red light than infrared, so the pulse shows weaker in red. The ratio of the two pulses, each against its own light level, gives SpO2 on a calibration line.
- **No reading** when the pulse is too weak (under a 0.1% perfusion index), has no steady rhythm, or is outside 30 to 240 bpm: the device shows nothing rather than a wrong number.

The calibration line, SpO2 = 110 − 25 R, is the textbook one. A real oximeter's comes from a clinical study against arterial blood samples (ISO 80601-2-61), so these readings are illustrative.

## Bluetooth

The oximeter advertises the Bluetooth SIG **Pulse Oximeter Service** and sends a PLX Continuous Measurement notification once a second: SpO2 and pulse rate as IEEE 11073 SFLOATs, with the standard's "not a number" when there's no reading. Any central that knows the service can read it.

[hub/](hub/) stands in for the bedside gateway until the Raspberry Pi version: a Zephyr app for a second nRF52840 that finds the oximeter, subscribes and logs each reading. Everything it receives goes through a strict decoder in `core/`, which rejects reserved flags, lengths that don't match the flags, and values out of range.

The baseline link has no pairing or encryption, so anyone in range can connect and listen.

## Alarms

The oximeter alarms when SpO2 falls below its limit or the pulse rate leaves its range (by default under 90%, or under 50 or over 120 bpm), and raises a technical alarm when it finds no pulse. An alarm lights LED1 on the DK and is sent to the hub as a notification.

The limits are a characteristic in a second, vendor-specific service. The hub sets them from its shell, the way a clinician would at a central station: `limits 95 50 120`. The oximeter refuses settings a monitor wouldn't offer, such as an SpO2 limit of 100% or a low pulse limit above the high one, and keeps the last accepted ones.

Range checks keep out nonsense, not malice: a 50% SpO2 limit is a legitimate setting, and it also silences a hypoxia alarm. So a change has to be signed. The oximeter offers a random 16-byte challenge, replaced after every attempt and on every connection. A change is the limits followed by a tag over the challenge and the limits: HMAC-SHA256 cut to 128 bits ([core/src/limits_auth.c](core/src/limits_auth.c)). Only a holder of the key can make the tag, and each tag works once. The hub signs the way the monitoring backend will. Until devices get keys of their own, a later Phase 4 control, both builds share a key that the first build generates into `build/limits.key`, which is never committed.

The oximeter watches itself too. If the sensor stops sending samples, from a fault or a loose connection, it raises the technical alarm within 1.5 s and sends "no reading" once a second, rather than falling silent. If the firmware itself stops, the nRF52840's watchdog resets it within 2 s ([app/src/supervisor.c](app/src/supervisor.c)).

Accepted limits are kept in flash (Zephyr's settings, on NVS), so a reset doesn't quietly bring back the defaults. A change the oximeter can't save is refused rather than applied until the next reset, so the clinician knows it didn't take. One simplification a real monitor wouldn't make: there's no alarm delay to avoid nuisance alarms (IEC 60601-1-8).

## Hardware

An nRF52840 DK and a MAX30101 (or MAX30102) breakout, wired to the DK's Arduino header. The hub runs on a second nRF52840 board, such as another DK or the nRF52840 Dongle; nRF Connect on a phone also shows the readings. Not yet tested on real boards.

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
west build -b nrf52840dk/nrf52840 -d build/oximeter app
west build -b nrf52840dk/nrf52840 -d build/hub hub
renode renode/oximeter.resc                # type `start`; each board's console opens in a window
```

In the hub's window, `limits <SpO2 low %> <pulse low bpm> <pulse high bpm>` sets the oximeter's alarm limits. In Renode's monitor, `mach set "oximeter"` then `sysbus.twi0.max30101 HeartRate 130` (or `RedPulse`, `InfraredPulse`, `RedLevel`, `InfraredLevel`) changes the simulated patient.

## Test

The code is C17 written to SEI CERT C. The host build of `core/` runs its unit tests under AddressSanitizer and UndefinedBehaviorSanitizer with strict conversion warnings, then two static analyzers check it: clang-tidy (its CERT C checks, the Clang static analyzer and bug-prone patterns, per [.clang-tidy](.clang-tidy)) and cppcheck.

```bash
cmake --workflow --preset host                 # unit tests, sanitized
clang-tidy -p build/host core/src/*.c
cppcheck --std=c11 --enable=warning,style,performance,portability -I core/include core/src
```

The unit tests cover the oximetry analysis, the PLX encoder and decoder, the alarm checks and limit settings, the checks on signed limit changes, and the check that the sensor keeps sending samples. Code that needs Zephyr runs its tests as Zephyr programs on the PC (`native_sim`): the PSA Crypto binding that gives those checks HMAC-SHA-256, against RFC 4231's test vectors; the hub's signer, against the oximeter's own checks; and the storage that keeps the alarm limits through a reset, on simulated flash:

```bash
west build -b native_sim/native/64 -d build/limits_crypto app/tests/limits_crypto
build/limits_crypto/zephyr/zephyr.exe
west build -b native_sim/native/64 -d build/limits_signer hub/tests/limits_signer
build/limits_signer/zephyr/zephyr.exe
west build -b native_sim/native/64 -d build/limits_store app/tests/limits_store
build/limits_store/zephyr/zephyr.exe -flash_in_ram
```

The Robot Framework tests boot both boards in [Renode](https://renode.io) 1.17, joined by a simulated Bluetooth link, with the sensor model set to a patient (90 bpm, 94%). The first checks the hub receives that reading and the alarm LED stays off, then sets a 95% limit from the hub's shell and checks the oximeter accepts the signed change, alarms, and lights the LED. The second runs the same hub built with a key of its own, as a gateway that wasn't given this oximeter's key, and checks the oximeter refuses its change and keeps monitoring under the old limits. The third sets the limits, resets the oximeter, and checks it starts with them and alarms again. The fourth stops the sensor and checks the technical alarm comes on within 2 s and the hub hears "no reading"; the fifth stops the processor and checks the watchdog brings the oximeter back, measuring under the clinician's limits. They need `renode-test` on `PATH` with its Python packages (`pip install -r <renode>/tests/requirements.txt`), and write their report to the working directory, so run them from the build tree:

```bash
west build -b nrf52840dk/nrf52840 -d build/hub-other-key hub -- -DLIMITS_KEY_FILE=$PWD/build/other-limits.key
mkdir -p build/renode && cd build/renode && renode-test ../../renode/oximeter.robot
```

Another runs Zephyr's own flash driver tests (`tests/drivers/flash/common`) on the oximeter's platform, which checks the flash controller model through the real driver:

```bash
west build -b nrf52840dk/nrf52840 -d build/flash-test deps/zephyr/tests/drivers/flash/common -- -DCONFIG_TEST_FORCE_STORAGE_PARTITION=y
cd build/renode && renode-test ../../renode/flash.robot
```

## Layout

```
app/            the oximeter's Zephyr application: west.yml (the Zephyr pin), prj.conf, board overlay, src/, tests/
hub/            the stand-in gateway's Zephyr application, and tests for its signer
common/         what the two applications share: Bluetooth and crypto definitions, crypto settings
core/           the oximetry analysis, the PLX encoding, the alarm checks and signed limit changes: portable C17, no Zephyr
tests/          Unity tests for core/, run on the host
renode/         Renode platform, scripts, the MAX30101 and flash controller models, and the Robot tests
docs/security/  the cybersecurity documentation for a premarket submission
deps/           Zephyr and its modules, fetched by west (git-ignored)
```

## License

MIT
