# medsec-oximeter

[![CI](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml/badge.svg)](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml)

A Bluetooth pulse oximeter built to the security expectations FDA sets for connected medical devices, together with the cybersecurity documentation a premarket submission needs: architecture views, threat model, risk assessment, security controls with test evidence, SBOM and a postmarket plan. The firmware runs on an nRF52840 under Zephyr and is tested in the Renode simulator.

> **This is a portfolio exercise, not a medical device.** It has not been cleared or approved by FDA or any other regulator. Don't use it on people or for any medical decision.

## Status

Phase 2 of 7, the baseline device, is complete. Once a second the firmware works out SpO2 and pulse rate from a MAX30101 pulse oximetry sensor's red and infrared light, sends them over Bluetooth LE to a hub, and raises alarms against limits the hub can set. The baseline is built without security controls on purpose, so the threat model starts from an honest "before" state.

Phase 3, the architecture views and threat model, is under way in [docs/security/](docs/security/): so far the [system description](docs/security/system.md), the [threat model](docs/security/threat-model.md) and the [updatability view](docs/security/updatability.md).

Phase 4, the security controls, is done but for the debug-port lock, which needs the board. A change of alarm limits must be signed, and the limits survive a reset; a security event log, tagged under a key of the oximeter's own, records what happened, and the backend can check it was given all of it (see [Alarms](#alarms)). MCUboot checks the firmware's signature at every boot, and updates arrive over Bluetooth from the paired gateway, which can't keep one: only the oximeter's own self-test does (see [Secure boot and updates](#secure-boot-and-updates)). The Bluetooth link is paired and encrypted, authenticated with keys only the backend can make, and a reset for a new patient forgets the gateway and the limits (see [Bluetooth](#bluetooth)).

Renode has no model of the MAX30101, so [renode/Max30101.cs](renode/Max30101.cs) adds one: the registers Zephyr's driver uses, the 32-sample FIFO filled at the configured rate, the interrupt line, and a synthetic pulse waveform whose light level, pulse depth and heart rate a test can set. Nor does it model the nRF52840's flash controller, which keeping anything in flash needs: [renode/Nvmc.cs](renode/Nvmc.cs) erases flash pages, and Zephyr's own flash driver tests check it. Nor the CCM, which encrypts and decrypts Bluetooth packets on an encrypted link: [renode/Ccm.cs](renode/Ccm.cs) adds it, checked against the Bluetooth specification's own sample data. And Renode's radio model hears a frame it missed whenever it next listens, late and even on another channel, which on an encrypted link breaks the packet sequence and drops the link: [renode/Radio.cs](renode/Radio.cs) is Renode's model with that fixed, and [renode/nrf52840dk.repl](renode/nrf52840dk.repl) Renode's description of the board, using it (both MIT, [renode/LICENSE-renode](renode/LICENSE-renode)).

## How it measures

The analysis lives in [core/](core/), plain C17 with no Zephyr, so the same code runs in the firmware and in the unit tests on a PC. It keeps the last four seconds of samples and, once a second:

- **Pulse rate**: compares the infrared signal with itself shifted by up to 2 seconds (autocorrelation) and takes the first shift where it repeats strongly. Taking the first rather than the strongest keeps a pulse from being read as half its rate.
- **SpO2**: oxygenated blood absorbs less red light than infrared, so the pulse shows weaker in red. The ratio of the two pulses, each against its own light level, gives SpO2 on a calibration line.
- **No reading** when the pulse is too weak (under a 0.1% perfusion index), has no steady rhythm, or is outside 30 to 240 bpm: the device shows nothing rather than a wrong number.

The calibration line, SpO2 = 110 − 25 R, is the textbook one. A real oximeter's comes from a clinical study against arterial blood samples (ISO 80601-2-61), so these readings are illustrative.

## Bluetooth

The oximeter advertises the Bluetooth SIG **Pulse Oximeter Service** and sends a PLX Continuous Measurement notification once a second: SpO2 and pulse rate as IEEE 11073 SFLOATs, with the standard's "not a number" when there's no reading. Only the gateway it's paired with can subscribe.

[hub/](hub/) stands in for the bedside gateway until the Raspberry Pi version: a Zephyr app for a second nRF52840 that finds the oximeter, subscribes and logs each reading. Everything it receives goes through a strict decoder in `core/`, which rejects reserved flags, lengths that don't match the flags, and values out of range. The gateway sets the link's timing, including 7.5 ms between connection events while it sends an update. The oximeter publishes its own preferences but never asks for them, since a request in the middle of an update would slow the upload threefold.

The link is encrypted and authenticated: LE Secure Connections only, at security level 4, which everything the oximeter offers requires except its device ID and its pairing data. Neither board has a display or keypad to confirm a pairing, so it's authenticated out of band. Before each pairing the oximeter makes fresh out-of-band data, LE Secure Connections' random value and confirmation, and seals it with AES-CCM under a key of its own, its pairing key, bound to its device ID ([common/src/pairing_seal.c](common/src/pairing_seal.c), [app/src/pairing.c](app/src/pairing.c)). The hub reads the sealed data, opens it with the pairing key it derives, and pairs ([hub/src/pairing.c](hub/src/pairing.c)). The confirmation proves the oximeter's public key to the hub, and pairing proves to the oximeter that the hub could open the seal: anyone who can't, such as a phone trying Just Works, is refused. In the real system the gateway would send the sealed data to the backend to open, and never hold a master. Both keep the bond, the oximeter in flash with its settings, so after a reset the hub reconnects with the stored key rather than pairing again. The oximeter keeps one bond: a new gateway's pairing, which needs the backend's sealed data like any other, takes the old one's place, and the old gateway can no longer secure the link.

Before the oximeter goes to the next patient, staff hold Button 1 for 5 s. It forgets its gateway and the clinician's limits, and starts again with the defaults ([app/src/new_patient.c](app/src/new_patient.c)), so a gateway must pair afresh, with the backend's sealed data, which the backend opens only for the next patient's gateway. A shorter press, as from a patient lying on it, does nothing. The hub, finding its bond refused, waits for that go-ahead, `pair` in its shell, before it pairs again ([hub/src/pairing.c](hub/src/pairing.c)). The security event log stays, and records the reset: it is the oximeter's own record, and a reset that erased it would let anyone holding the button erase the evidence too.

## Alarms

The oximeter alarms when SpO2 falls below its limit or the pulse rate leaves its range (by default under 90%, or under 50 or over 120 bpm), and raises a technical alarm when it finds no pulse. An alarm lights LED1 on the DK and is sent to the hub as a notification.

The limits are a characteristic in a second, vendor-specific service. The hub sets them from its shell, the way a clinician would at a central station: `limits 95 50 120`. The oximeter refuses settings a monitor wouldn't offer, such as an SpO2 limit of 100% or a low pulse limit above the high one, and keeps the last accepted ones.

Range checks keep out nonsense, not malice: a 50% SpO2 limit is a legitimate setting, and it also silences a hypoxia alarm. So a change has to be signed. The oximeter offers a random 16-byte challenge, replaced after every attempt and on every connection. A change is the limits followed by a tag over the challenge and the limits: HMAC-SHA256 cut to 128 bits ([core/src/limits_auth.c](core/src/limits_auth.c)). Only a holder of the key can make the tag, and each tag works once. The hub signs the way the monitoring backend will. Each oximeter has keys of its own, put into its UICR (a small area of non-volatile memory apart from the firmware) at the factory by [tools/provision.py](tools/provision.py): one checks limit changes, one tags its security log, and one seals its pairing data (see [Bluetooth](#bluetooth)). The backend derives the same keys from a master secret and the oximeter's device ID with HKDF-SHA256, a different label for each, so it keeps no per-device keys, and a key taken from one oximeter opens no other. The firmware holds no key, so every device runs the same image, and an oximeter that was never provisioned refuses every change. Here the hub stands in for the backend and holds its masters, a signing master for the limits and log keys and a pairing master for the pairing key. Its first build generates them into `build/signing-master.key` and `build/pairing-master.key`, which are never committed; in the real system they never leave the backend.

The oximeter watches itself too. If the sensor stops sending samples, from a fault or a loose connection, it raises the technical alarm within 1.5 s and sends "no reading" once a second, rather than falling silent. If the firmware itself stops, the nRF52840's watchdog resets it within 2 s ([app/src/supervisor.c](app/src/supervisor.c)).

It also keeps a security event log in flash: each start-up with the reset cause and the version it runs, each update marked for a test boot with its version, each central that connects, each pairing and each refused one with the reason, each limits change accepted or refused and why, and the sensor stopping and resuming ([core/src/security_log.c](core/src/security_log.c)). So the backend sees from the log alone whether an update took: one marked for a test boot and then a start-up of the old version means MCUboot refused it, or it failed its self-test. It holds the last 64 records, numbered so that a gap shows, and timed by boot and uptime, since the oximeter has no clock: the gateway adds the date when it reads them. Each record carries a MAC under the oximeter's log key, so the backend can tell a record the oximeter wrote from one changed or invented on the way, and with the sequence numbers, from one dropped. A gateway could still hold back the newest records, so the backend also asks the oximeter how far its log goes: it sends a fresh random challenge, and the oximeter answers with its newest record's number, tagged under the same key ([app/src/event_log.c](app/src/event_log.c)). The gateway can neither forge that answer nor pass off an old one, so any record the backend hasn't received shows. Nothing over the air can change or erase the log, and only the paired gateway can read it.

Accepted limits are kept in flash (Zephyr's settings, on NVS), so a reset doesn't quietly bring back the defaults. A change the oximeter can't save is refused rather than applied until the next reset, so the clinician knows it didn't take. One simplification a real monitor wouldn't make: there's no alarm delay to avoid nuisance alarms (IEC 60601-1-8).

## Secure boot and updates

MCUboot starts before the oximeter and runs it only if the image carries a valid signature from the release key: ECDSA P-256 over the image and its header, checked at every boot, not only after an update. The build makes the release key on its first run, in `build/release-key.pem`, which is never committed ([app/sysbuild.cmake](app/sysbuild.cmake)). Left to itself, sysbuild would sign with the development key that comes with MCUboot, whose private half anyone can download. A real product keeps its release key in an offline signing service. At start-up the oximeter logs the version MCUboot checked.

MCUboot checks signatures with its own copy of TinyCrypt, not the PSA Crypto the application uses. In MCUboot, PSA Crypto needs a random source, and the nRF52840's random number generator driver needs threads, which MCUboot leaves out, so it would fall back to Zephyr's test-only generator. Checking a signature needs neither randomness nor a secret, and with TinyCrypt the whole of MCUboot fits in 30 KB of its 48.

An update first runs as a test. MCUboot swaps it in but keeps the image before it, and the update marks itself as working only once it passes its self-test: the sensor answered, Bluetooth is up and the analysis gave a reading. Until then, the next reset, by the watchdog if the update hangs, swaps the previous image back. Each release also carries a security counter in its signed header ([app/prj.conf](app/prj.conf)). Only a release that fixes a vulnerability raises it, and MCUboot erases any update with a lower one, so a genuine but older release can't bring a fixed vulnerability back. Updates arrive over Bluetooth from the paired gateway, with MCUmgr's SMP protocol: the hub sends the image, marks it for a test boot and resets the oximeter ([hub/src/update.c](hub/src/update.c)). Only the oximeter's own self-test can keep an update: over SMP it refuses to confirm an image it isn't running, and refuses any change of image state while an update is on trial ([app/src/update_guard.c](app/src/update_guard.c)). Standard MCUmgr tools confirm an update from outside once it runs, and a gateway that did so could keep a broken update for good. SMP needs an authenticated link, so only the gateway the oximeter is paired with can send one, and the oximeter offers only what an update needs: image upload and state, and a reset.

Until the debug port is locked, which only real hardware can show, anyone with a debug probe can still replace MCUboot itself.

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

Each DK needs its keys provisioned once, before the debug port is locked: `python3 tools/provision.py --signing-master build/signing-master.key --pairing-master build/pairing-master.key --device-id <ID>` lists the UICR addresses and words to write with a debug probe. The ID is the one the hub logs as "signing for oximeter ...".

## Build and run

Needs `git`, `cmake`, `ninja`, Python 3 with `venv`, `dtc` and `gperf`. From the repository root, once:

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install west
west init -l app
west update --narrow -o=--depth=1          # Zephyr v4.4.2 and its modules, into deps/
pip install -r deps/zephyr/scripts/requirements-base.txt
pip install -r deps/bootloader/mcuboot/scripts/requirements.txt   # imgtool, which signs the oximeter
west sdk install -t arm-zephyr-eabi        # skip if a matching Zephyr SDK is installed
```

Then, in each new shell:

```bash
. .venv/bin/activate
west build --sysbuild -b nrf52840dk/nrf52840 -d build/oximeter app   # MCUboot and the signed oximeter
west build -b nrf52840dk/nrf52840 -d build/hub hub
python3 tools/provision.py --signing-master build/signing-master.key --pairing-master build/pairing-master.key \
    --device-id 0123456789abcdef \
    --renode build/provision.resc            # the factory's step, once: the oximeter's key into UICR
renode renode/oximeter.resc                # type `start`; each board's console opens in a window
```

In the hub's window, `limits <SpO2 low %> <pulse low bpm> <pulse high bpm>` sets the oximeter's alarm limits, `log` prints its security event log, `check` asks it how far the log goes and compares that with what `log` read, `update <bytes>` sends it the update staged in the hub's slot 1 partition (load one there with Renode's `sysbus LoadBinary`, as the tests do), `confirm` asks it to keep the image it runs for good, as standard MCUmgr tools do once an update runs, and `pair` pairs again with an oximeter reset for a new patient, standing in for the backend's go-ahead. In Renode's monitor, `mach set "oximeter"` then `sysbus.twi0.max30101 HeartRate 130` (or `RedPulse`, `InfraredPulse`, `RedLevel`, `InfraredLevel`) changes the simulated patient, and `sysbus.gpio0.sw0 Press`, then `Release` 5 s later, is the reset for a new patient (tap it once first: Renode misses the button's first press).

## Test

The code is C17 written to SEI CERT C. The host build of `core/` runs its unit tests under AddressSanitizer and UndefinedBehaviorSanitizer with strict conversion warnings, then two static analyzers check it: clang-tidy (its CERT C checks, the Clang static analyzer and bug-prone patterns, per [.clang-tidy](.clang-tidy)) and cppcheck.

```bash
cmake --workflow --preset host                 # unit tests, sanitized
clang-tidy -p build/host core/src/*.c
cppcheck --std=c11 --enable=warning,style,performance,portability -I core/include core/src
```

The unit tests cover the oximetry analysis, the PLX encoder and decoder, the alarm checks and limit settings, the checks on signed limit changes, the check that the sensor keeps sending samples, the security event log's records and numbering and the message that says how far it goes, and the check that a device key was provisioned. Code that needs Zephyr runs its tests as Zephyr programs on the PC (`native_sim`): the PSA Crypto binding that gives those checks HMAC-SHA-256 and tags the security log, against RFC 4231's test vectors; the hub's signer and its derivation of each oximeter's key (HKDF-SHA256, against RFC 5869's test vectors), against the oximeter's own checks, and its check of the oximeter's answer on how far its log goes, which refuses one replayed from an earlier challenge, one with the number changed, and another oximeter's; the storage that keeps the alarm limits and the security event log through a reset, on simulated flash; and the sealing of the oximeter's pairing data (AES-CCM), which opens only with the same key and device ID and never reuses a nonce:

```bash
west build -b native_sim/native/64 -d build/limits_crypto app/tests/limits_crypto
build/limits_crypto/zephyr/zephyr.exe
west build -b native_sim/native/64 -d build/limits_signer hub/tests/limits_signer
build/limits_signer/zephyr/zephyr.exe
west build -b native_sim/native/64 -d build/limits_store app/tests/limits_store
build/limits_store/zephyr/zephyr.exe -flash_in_ram
west build -b native_sim/native/64 -d build/event_log app/tests/event_log
build/event_log/zephyr/zephyr.exe -flash_in_ram
west build -b native_sim/native/64 -d build/pairing_seal common/tests/pairing_seal
build/pairing_seal/zephyr/zephyr.exe
```

The Robot Framework tests boot both boards in [Renode](https://renode.io) 1.17, joined by a simulated Bluetooth link, with the sensor model set to a patient (90 bpm, 94%). The first checks the hub pairs at security level 4 before any reading reaches it. The second resets the oximeter and checks the hub reconnects with the bond they both kept, without pairing again. The third and fourth reset it twice and check the hub subscribes afresh on every new link: it still hears the readings, and the oximeter still answers its update commands. The fifth has a second gateway pair while the first is out of range, and checks it replaces the bond, so the first can no longer secure the link. The sixth holds Button 1 for 5 s, the reset for a new patient, and checks the oximeter forgets the hub, whose bond no longer works; the seventh does it after the clinician set a limit and checks the oximeter starts with the defaults; the eighth presses it for a second and checks nothing is forgotten. The ninth checks the hub doesn't pair again by itself once the oximeter has forgotten it, only when the backend allows it, and then hears the readings. The tenth signs the firmware as release 1.1.0, stages it in the hub, and checks the hub updates the oximeter over Bluetooth: it starts 1.1.0, passes its self-test and keeps it, and readings carry on. The eleventh starts an update right after pairing and checks the link keeps the hub's fast interval until the oximeter restarts with it. The twelfth stops the sensor first, so the update can't pass its self-test, and checks the oximeter refuses when the hub asks it to keep the update anyway, as standard update tools do once an update runs: a reset brings 1.0.0 back. The thirteenth sends an update signed with MCUboot's published development key and checks MCUboot erases it, so 1.0.0 runs on. The fourteenth checks the oximeter refuses an update from a phone that isn't paired. The fifteenth runs the hub built with both masters of its own, a stranger, and checks it can't open the oximeter's pairing data, so it never pairs and hears nothing. The sixteenth puts Zephyr's Bluetooth shell in the hub's place, as a phone in range, and checks the oximeter refuses its Just Works pairing and every read of what it protects. The seventeenth checks the hub receives the patient's reading and the alarm LED stays off, then sets a 95% limit from the hub's shell and checks the oximeter accepts the signed change, alarms, and lights the LED. The eighteenth runs the hub built with a signing master of its own, as a gateway the backend let pair but won't sign for, and checks the oximeter refuses its change and keeps monitoring under the old limits. The nineteenth sets the limits, resets the oximeter, and checks it starts with them and alarms again. The twentieth stops the sensor and checks the technical alarm comes on within 2 s and the hub hears "no reading"; the twenty-first stops the processor and checks the watchdog brings the oximeter back, measuring under the clinician's limits. The next seven read the security event log from the hub: one finds the start-up, the hub's connection and its accepted change; one finds its pairing; one, after the phone has tried Just Works, finds that pairing refused and why; one, run with the gateway that can't sign, finds its change refused because the tag doesn't match; one, after an update, finds it marked for a test boot with its version; one finds the start-up of the new version that followed; and one finds the reset for a new patient. The twenty-ninth runs an oximeter the factory never provisioned and checks it reports having no key, and that nothing can pair with it or change it. The thirtieth checks the hub verifies every record of the log; the thirty-first, with the gateway that can't sign, that it can verify none. The thirty-second has the hub ask the oximeter how far its log goes, with a fresh challenge, and checks the answer shows the hub read it all; the last adds a record after the hub read the log, as a gateway holding one back would leave it, and checks the answer shows the one the hub hasn't read. Each test provisions the oximeter first, with the same tool the factory would use. They need `renode-test` on `PATH` with its Python packages (`pip install -r <renode>/tests/requirements.txt`), and write their report to the working directory, so run them from the build tree:

```bash
west build -b nrf52840dk/nrf52840 -d build/hub-other-key hub -- -DSIGNING_MASTER_FILE=$PWD/build/other-signing-master.key
west build -b nrf52840dk/nrf52840 -d build/hub-stranger hub -- -DSIGNING_MASTER_FILE=$PWD/build/other-signing-master.key \
    -DPAIRING_MASTER_FILE=$PWD/build/other-pairing-master.key
# The phone keeps no bonds: Renode's board for it has no flash controller.
west build -b nrf52840dk/nrf52840 -d build/phone deps/zephyr/tests/bluetooth/shell -- -DCONFIG_BT_SETTINGS=n
mkdir -p build/renode && cd build/renode && renode-test ../../renode/oximeter.robot
```

Six more boot the oximeter through MCUboot ([renode/boot.robot](renode/boot.robot)), signing their own images with MCUboot's imgtool. The first checks MCUboot starts the signed oximeter, which reports the version from its signed header; the second, that an image signed with MCUboot's published development key never runs; the third, that neither does one with a byte changed after signing. The fourth puts an update in the second slot, marked for a test boot, and checks it marks itself as working once it has a reading and still runs after a reset; the fifth stops the sensor first and checks the update never marks itself, so a reset brings the previous version back. The sixth offers an older release with a lower security counter and checks MCUboot erases it and starts the current one:

```bash
cd build/renode && renode-test ../../renode/boot.robot
```

Another runs Zephyr's own flash driver tests (`tests/drivers/flash/common`) on the oximeter's platform, which checks the flash controller model through the real driver:

```bash
west build -b nrf52840dk/nrf52840 -d build/flash-test deps/zephyr/tests/drivers/flash/common -- -DCONFIG_TEST_FORCE_STORAGE_PARTITION=y
cd build/renode && renode-test ../../renode/flash.robot
```

Another checks the CCM model against the Bluetooth Core specification's sample data (Vol 6, Part C): it encrypts and decrypts the specification's packets byte for byte, reports a packet it can't authenticate, passes empty packets through unencrypted, and signals the end of decryption with the interrupt Zephyr's controller waits for:

```bash
cd build/renode && renode-test ../../renode/ccm.robot
```

The last checks the radio model with two radios and no firmware: a frame sent while the other isn't listening is lost if it listens 5 ms later, or on another channel, and heard if it listens 150 µs later, still in the same exchange:

```bash
cd build/renode && renode-test ../../renode/radio.robot
```

## Layout

```
app/            the oximeter's Zephyr application: west.yml (the Zephyr pin), prj.conf, sysbuild.conf, sysbuild.cmake and sysbuild/mcuboot.conf (MCUboot and the release key), board overlay, src/, tests/
hub/            the stand-in gateway's Zephyr application, and tests for its signer
common/         what the two applications share: Bluetooth and crypto definitions, crypto settings
core/           the oximetry analysis, the PLX encoding, the alarm checks and signed limit changes: portable C17, no Zephyr
tests/          Unity tests for core/, run on the host
tools/          provision.py: the factory's step, an oximeter's limits key into UICR
renode/         Renode platform, scripts, the MAX30101, flash controller, CCM and radio models, and the Robot tests
docs/security/  the cybersecurity documentation for a premarket submission
deps/           Zephyr and its modules, fetched by west (git-ignored)
```

## License

MIT
