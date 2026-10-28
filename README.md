# medsec-oximeter

[![CI](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml/badge.svg)](https://github.com/Tonyc310/medsec-oximeter/actions/workflows/ci.yml)

A Bluetooth pulse oximeter built to the security expectations FDA sets for connected medical devices, together with the cybersecurity documentation a premarket submission needs: architecture views, threat model, risk assessment, security controls with test evidence, SBOM and a postmarket plan. The firmware runs on an nRF52840 under Zephyr and is tested in the Renode simulator.

> **This is a portfolio exercise, not a medical device.** It has not been cleared or approved by FDA or any other regulator. Don't use it on people or for any medical decision.

## Status

Phase 2 of 7, the baseline device, is complete. Once a second the firmware works out SpO2 and pulse rate from a MAX30101 pulse oximetry sensor's red and infrared light, sends them over Bluetooth LE to a hub, and raises alarms against limits the hub can set. The baseline is built without security controls on purpose, so the threat model starts from an honest "before" state.

Phase 3, the architecture views and threat model, is under way in [docs/security/](docs/security/): so far the [system description](docs/security/system.md), the [threat model](docs/security/threat-model.md) and the [updatability view](docs/security/updatability.md).

Phase 4, the security controls, is done but for the debug-port lock, which needs the board. A change of alarm limits must be signed, and the limits survive a reset; a security event log, tagged under a key of the oximeter's own, records what happened, and the backend can check it was given all of it (see [Alarms](#alarms)). MCUboot checks the firmware's signature at every boot, and updates arrive over Bluetooth from the paired gateway, which can't keep one: only the oximeter's own self-test does (see [Secure boot and updates](#secure-boot-and-updates)). The Bluetooth link is paired and encrypted, authenticated with keys only the backend can make, and a reset for a new patient forgets the gateway and the limits (see [Bluetooth](#bluetooth)). [docs/security/controls.md](docs/security/controls.md) lists each control with the threats it addresses and the tests that verify it.

Phase 5, the SBOM and the vulnerability assessment, is done. Every build makes an SBOM of what it compiled ([docs/security/sbom.md](docs/security/sbom.md)), and every known vulnerability in its components has a written assessment of whether it affects the oximeter ([docs/security/vulnerabilities.md](docs/security/vulnerabilities.md)). A weekly check asks NVD for new CVEs and fails until each has one.

Phase 6, security testing, is done but for what needs the board: the debug-port lock and penetration testing by someone independent of the design. Fuzzing covers what other devices send, including the update protocol as a hostile gateway would send it; boot tests try forged, altered and older firmware; a check finds no key built into the firmware; and a table, checked on every push, traces each threat to its controls and their tests. [docs/security/test-report.md](docs/security/test-report.md) gives the result, the defects testing found, and what's open.

Renode has no model of the MAX30101, so [renode/Max30101.cs](renode/Max30101.cs) adds one: the registers Zephyr's driver and the firmware use, the 32-sample FIFO filled at the configured rate, with its pointers and its count of samples lost, the interrupt line, a synthetic pulse waveform whose light level, pulse depth and heart rate a test can set, and two faults a test can cause, a stopped sensor and a cut interrupt line. Nor does it model the nRF52840's flash controller, which keeping anything in flash needs: [renode/Nvmc.cs](renode/Nvmc.cs) erases flash pages, each taking the 85 ms the chip's specification gives with the processor halted, as on the chip, and Zephyr's own flash driver tests check it. Nor the CCM, which encrypts and decrypts Bluetooth packets on an encrypted link: [renode/Ccm.cs](renode/Ccm.cs) adds it, checked against the Bluetooth specification's own sample data. And Renode's radio model hears a frame it missed whenever it next listens, late and even on another channel, which on an encrypted link breaks the packet sequence and drops the link: [renode/Radio.cs](renode/Radio.cs) is Renode's model with that fixed, and [renode/nrf52840dk.repl](renode/nrf52840dk.repl) Renode's description of the board, using it (both MIT, [renode/LICENSE-renode](renode/LICENSE-renode)).

## How it measures

The analysis lives in [core/](core/), plain C17 with no Zephyr, so the same code runs in the firmware and in the unit tests on a PC. It keeps the last four seconds of samples and, once a second:

- **Pulse rate**: compares the infrared signal with itself shifted by up to 2 seconds (autocorrelation) and takes the first shift where it repeats strongly. Taking the first rather than the strongest keeps a pulse from being read as half its rate.
- **SpO2**: oxygenated blood absorbs less red light than infrared, so the pulse shows weaker in red. The ratio of the two pulses, each against its own light level, gives SpO2 on a calibration line.
- **No reading** when the pulse is too weak (under a 0.1% perfusion index), has no steady rhythm, or is outside 30 to 240 bpm: the device shows nothing rather than a wrong number.

The calibration line, SpO2 = 110 − 25 R, is the textbook one. A real oximeter's comes from a clinical study against arterial blood samples (ISO 80601-2-61), so these readings are illustrative.

## Bluetooth

The oximeter advertises the Bluetooth SIG **Pulse Oximeter Service** and sends a PLX Continuous Measurement notification once a second: SpO2 and pulse rate as IEEE 11073 SFLOATs, with the standard's "not a number" when there's no reading. Only the gateway it's paired with can subscribe.

[hub/](hub/) stands in for the bedside gateway until the Raspberry Pi version: a Zephyr app for a second nRF52840 that finds the oximeter, subscribes and logs each reading. Everything it receives goes through a strict decoder in `core/`, which rejects reserved flags, lengths that don't match the flags, and values out of range. The gateway sets the link's timing: 30 to 50 ms between connection events, 7.5 ms while it sends an update, and a supervision timeout of 420 ms, so it notices a silent oximeter within half a second. The oximeter publishes its own preferences but never asks for them, since a request in the middle of an update would slow the upload threefold.

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

MCUboot starts before the oximeter and runs it only if the image carries a valid signature from the release key: ECDSA P-256 over the image and its header, checked at every boot, not only after an update. Outside the signature, an image may carry only fields that can't be signed, such as the signature itself: MCUboot refuses one with anything else there ([app/sysbuild/mcuboot.conf](app/sysbuild/mcuboot.conf), CVE-2024-32883). The build makes the release key on its first run, in `build/release-key.pem`, which is never committed ([app/sysbuild.cmake](app/sysbuild.cmake)). Left to itself, sysbuild would sign with the development key that comes with MCUboot, whose private half anyone can download. A real product keeps its release key in an offline signing service. At start-up the oximeter logs the version MCUboot checked.

MCUboot checks signatures with its own copy of TinyCrypt, not the PSA Crypto the application uses. In MCUboot, PSA Crypto needs a random source, and the nRF52840's random number generator driver needs threads, which MCUboot leaves out, so it would fall back to Zephyr's test-only generator. Checking a signature needs neither randomness nor a secret, and with TinyCrypt the whole of MCUboot fits in 30 KB of its 48.

An update first runs as a test. MCUboot swaps it in but keeps the image before it, and the update marks itself as working only once it passes its self-test: the sensor answered, Bluetooth is up and the analysis gave a reading. Until then, the next reset, by the watchdog if the update hangs, swaps the previous image back. Each release also carries a security counter in its signed header ([app/prj.conf](app/prj.conf)). Only a release that fixes a vulnerability raises it, and MCUboot erases any update with a lower one, so a genuine but older release can't bring a fixed vulnerability back. Updates arrive over Bluetooth from the paired gateway, with MCUmgr's SMP protocol: the hub sends the image, marks it for a test boot and resets the oximeter ([hub/src/update.c](hub/src/update.c)). Only the oximeter's own self-test can keep an update: over SMP it refuses to confirm an image it isn't running, and refuses any change of image state while an update is on trial ([app/src/update_guard.c](app/src/update_guard.c)). Standard MCUmgr tools confirm an update from outside once it runs, and a gateway that did so could keep a broken update for good. SMP needs an authenticated link, so only the gateway the oximeter is paired with can send one, and the oximeter offers only what an update needs: image upload and state, and a reset. It keeps monitoring while the update uploads: each flash erase halts its processor for 85 ms, and at each interrupt it reads every sample the sensor holds, so none is lost ([app/src/main.c](app/src/main.c)). The swap and restart stop monitoring for about 15 s in Renode, so an update is installed at a time the clinician approves.

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

The code is C17 written to SEI CERT C and formatted by clang-format 23.1.2 ([.clang-format](.clang-format)). The host build of `core/` runs its unit tests under AddressSanitizer and UndefinedBehaviorSanitizer with strict conversion warnings, then two static analyzers check it: clang-tidy (its CERT C checks, the Clang static analyzer and bug-prone patterns, per [.clang-tidy](.clang-tidy)) and cppcheck. cppcheck also checks the firmware's code in `app/` and `common/`. It runs there without Zephyr's headers, whose devicetree macros it can't expand, so it doesn't follow Zephyr's types. CI runs the versions Ubuntu 24.04 gives, clang-tidy 18 and cppcheck 2.13.

```bash
git ls-files '*.[ch]' | xargs clang-format --dry-run --Werror
cmake --workflow --preset host                 # unit tests, sanitized
clang-tidy -p build/host core/src/*.c
cppcheck --std=c11 --enable=warning,style,performance,portability --inline-suppr \
    --error-exitcode=1 -q -I core/include -I common/include -I app/src core/src app/src common/src
```

libFuzzer gives the parsers of what other devices send any bytes at all, under the same sanitizers, and checks a property on every input. Whatever arrives as a limit change, the limits in force are the ones before or ones a monitor accepts, even with a MAC check that passes every tag, as a compromised backend key would. A reading the hub takes is in range, and one it refuses leaves the last reading as it was. A security log record the hub takes encodes back to the same bytes, so it shows the record the oximeter wrote. CI runs each target for a minute. The fuzz build needs clang:

```bash
CC=clang cmake --workflow --preset fuzz
for target in build/fuzz/fuzz/fuzz_*; do "$target" -max_total_time=60; done
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

The update protocol is fuzzed the same way, as a Zephyr program on the PC ([fuzz/smp](fuzz/smp)). libFuzzer sends any bytes at all as Bluetooth writes from the paired gateway, and MCUmgr handles them as the oximeter does, with its settings and its update guard, while an update runs on trial. Whatever arrives, the running image's slot never changes: a gateway can only stage an update in slot 1. The first cases are requests a gateway really sends, among them the confirm that standard update tools send once an update runs. UBSan stops at anything but two reports in Zephyr's MCUmgr, explained in [fuzz/smp/ubsan.supp](fuzz/smp/ubsan.supp). CI runs it for a minute; it needs clang:

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host/llvm west build -b native_sim/native/64 -d build/smp_fuzz fuzz/smp
python3 fuzz/smp/seeds.py build/smp_fuzz/seeds && mkdir -p build/smp_fuzz/corpus
UBSAN_OPTIONS=halt_on_error=1:suppressions=fuzz/smp/ubsan.supp build/smp_fuzz/zephyr/zephyr.exe \
    build/smp_fuzz/corpus build/smp_fuzz/seeds -dict=fuzz/smp/smp.dict -max_total_time=60
```

The Python tools follow PEP 8 and PEP 257, checked by ruff, and mypy checks their types strictly ([pyproject.toml](pyproject.toml)). The SBOM tool's tests check that it lists every component built into either image with its version and identifiers, and that it refuses one the curated list doesn't name. The vulnerability check's tests, with a stand-in for NVD, check that it passes when every CVE found has a disposition and names one that doesn't. The key check's tests check that it passes an image holding no key and names each key planted in one. The traceability tool's tests check that it builds a row for each threat, names each break in the chain from threat to control to test, and names a table that's out of date. The tool itself checks that every test the [controls](docs/security/controls.md) name exists, and that [docs/security/traceability.md](docs/security/traceability.md) is up to date; without `--check`, it rewrites the table:

```bash
ruff check tools fuzz && ruff format --check tools fuzz
mypy
pytest
python3 tools/traceability.py --check
```

The key check searches the built images for every secret the firmware must not hold: the backend's two masters, the release key's private half, and the keys provisioned into the oximeter the Renode tests run. MCUboot holds the release key's public half, which belongs there; the hub isn't checked, since it holds the masters by design:

```bash
python3 tools/find_keys.py --signing-master build/signing-master.key --pairing-master build/pairing-master.key \
    --release-key build/release-key.pem --device-id 0123456789abcdef \
    build/oximeter/app/zephyr/zephyr.signed.bin build/oximeter/mcuboot/zephyr/zephyr.bin
```

The Robot Framework tests boot both boards in [Renode](https://renode.io) 1.17, joined by a simulated Bluetooth link, with the sensor model set to a patient (90 bpm, 94%); in each, the hub hears readings only after pairing out of band at security level 4. The first resets the oximeter and checks the hub reconnects with the bond they both kept, without pairing again. The second and third reset it twice and check the hub subscribes afresh on every new link: it still hears the readings, and the oximeter still answers its update commands. The fourth has a second gateway pair while the first is out of range, and checks it replaces the bond, so the first can no longer secure the link. The fifth holds Button 1 for 5 s, the reset for a new patient, after the clinician set a limit, and checks the oximeter starts with the defaults; the sixth presses it for a second and checks nothing is forgotten. The seventh checks the reset leaves the hub's bond useless, and that the hub doesn't pair again by itself, only when the backend allows it, and then hears the readings. The eighth signs the firmware as release 1.1.0, stages it in the hub, starts the update right after pairing, and checks the link keeps the hub's fast interval until the oximeter restarts with it. The ninth leaves the release before it where the update goes, as every update after the first finds it, so the upload has to erase it, each page halting the processor for 85 ms, and checks the sensor loses no sample; the tenth checks monitoring resumes within 30 s of the hub's reset command. The eleventh stops the sensor first, so the update can't pass its self-test, and checks the oximeter starts 1.1.0 but refuses when the hub asks it to keep it anyway, as standard update tools do once an update runs: a reset brings 1.0.0 back. The twelfth puts Zephyr's Bluetooth shell in the hub's place, as a phone in range, and checks the oximeter refuses it the readings, the security log and the start of an update; the thirteenth checks it refuses the phone's Just Works pairing. The fourteenth sets a 95% limit from the hub's shell and checks the oximeter accepts the signed change, alarms, and lights the LED. The fifteenth runs the hub built with a signing master of its own, as a gateway the backend let pair but won't sign for, and checks the oximeter refuses its change and keeps monitoring under the old limits. The sixteenth sets the limits, resets the oximeter, and checks it starts with them and alarms again. The seventeenth keeps the sensor's interrupt from the firmware for 200 ms, as a flash erase or a key computation holding it up would, and checks the next interrupt reads every sample waiting; the eighteenth keeps it away for 400 ms, longer than the sensor's FIFO lasts, and checks the oximeter starts a new reading rather than one across the samples lost. The nineteenth stops the sensor and checks the technical alarm comes on within 2 s and the hub hears "no reading"; the twentieth stops the oximeter's processor early in a link and checks the hub notices the silence within two seconds; the twenty-first stops the processor and checks the watchdog brings the oximeter back, measuring again. The next six read the security event log from the hub: one finds the start-up with the version running, the hub's connection and its accepted change; one finds its pairing; one, after the phone has tried Just Works, finds that pairing refused and why; one, run with the gateway that can't sign, finds its change refused because the tag doesn't match; one, after an update, finds it marked for a test boot with its version; and one finds the reset for a new patient. The twenty-eighth runs an oximeter the factory never provisioned and checks it reports having no key, and that nothing can pair with it. The twenty-ninth checks the hub verifies every record of the log; the thirtieth, with the gateway that can't sign, that it can verify none. The last has the hub ask the oximeter how far its log goes, with a fresh challenge, and checks the answer shows the hub read it all, and, with a record added after the hub read the log, as a gateway holding one back would leave it, that it shows the one the hub hasn't read. Each test provisions the oximeter first, with the same tool the factory would use. They need `renode-test` on `PATH` with its Python packages (`pip install -r <renode>/tests/requirements.txt`), and write their report to the working directory, so run them from the build tree:

```bash
west build -b nrf52840dk/nrf52840 -d build/hub-other-key hub -- -DSIGNING_MASTER_FILE=$PWD/build/other-signing-master.key
# The phone keeps no bonds: Renode's board for it has no flash controller.
west build -b nrf52840dk/nrf52840 -d build/phone deps/zephyr/tests/bluetooth/shell -- -DCONFIG_BT_SETTINGS=n
mkdir -p build/renode && cd build/renode && renode-test ../../renode/oximeter.robot
```

Five more boot the oximeter through MCUboot ([renode/boot.robot](renode/boot.robot)), signing their own images with MCUboot's imgtool. The first checks MCUboot never starts an image the release key didn't sign: one signed with MCUboot's published development key, or one with a byte changed after signing. The second puts an update in the second slot, marked for a test boot, and checks it marks itself as working once it has a reading and still runs after a reset; the third stops the sensor first and checks the update never marks itself, so a reset brings the previous version back. The fourth offers an older release with a lower security counter and checks MCUboot erases it and starts the current one. The last offers an update with its hash but no signature, and a genuine update given a higher security counter outside its signature; MCUboot erases both and starts the current release:

```bash
cd build/renode && renode-test ../../renode/boot.robot
```

Two more check the flash controller model ([renode/flash.robot](renode/flash.robot)): one runs Zephyr's own flash driver tests (`tests/drivers/flash/common`) on the oximeter's platform, through the real driver; the other checks a page erase takes the 85 ms the nRF52840's specification gives, with the processor halted meanwhile, as the chip halts one that runs from flash:

```bash
west build -b nrf52840dk/nrf52840 -d build/flash-test deps/zephyr/tests/drivers/flash/common -- -DCONFIG_TEST_FORCE_STORAGE_PARTITION=y
cd build/renode && renode-test ../../renode/flash.robot
```

Another checks the CCM model against the Bluetooth Core specification's sample data (Vol 6, Part C): it encrypts and decrypts the specification's packets byte for byte, reports a packet it can't authenticate, passes empty packets through unencrypted, and signals the end of decryption with the interrupt Zephyr's controller waits for:

```bash
cd build/renode && renode-test ../../renode/ccm.robot
```

Another checks the MAX30101 model with no firmware: its FIFO's pointers show the samples waiting, it counts the samples a full FIFO loses, up to 31 as the chip does, and reading a sample clears that count:

```bash
cd build/renode && renode-test ../../renode/sensor.robot
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
fuzz/           libFuzzer targets for core/'s parsers, and in smp/ for the update protocol on native_sim
tools/          provision.py, the factory's step that puts an oximeter's keys into UICR; sbom.py, the SBOM of a build; vulns.py, the vulnerability check; find_keys.py, the check that no key is built in; traceability.py, the table from threat to test; tests/
renode/         Renode platform, scripts, the MAX30101, flash controller, CCM and radio models, and the Robot tests
docs/security/  the cybersecurity documentation for a premarket submission
deps/           Zephyr and its modules, fetched by west (git-ignored)
```

## License

MIT
