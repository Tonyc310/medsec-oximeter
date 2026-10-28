# Security controls

The controls built against the [threat model](threat-model.md), grouped the way FDA's premarket guidance groups them (Appendix 1). For each one: what it must do, how it's built, the threats it addresses and the tests that verify it. The threat model stays the Phase 2 baseline, before any control. The test report (Phase 6) records each test's result, and the risk assessment rates what's left.

> Portfolio exercise, not a medical device.

## Summary

| ID | Control | Category | Threats | Status |
|---|---|---|---|---|
| C-01 | Pairing authenticated out of band | authentication | T-01, T-02, T-07, T-08, T-09 | built |
| C-02 | One gateway at a time | authentication | T-12, T-37 | built |
| C-03 | Signed alarm-limit changes | authentication | T-01, T-02, T-03, T-21, T-22 | built |
| C-04 | An authenticated link for everything but identification | authorization | T-01, T-05, T-07 | built |
| C-05 | Only the oximeter's self-test keeps an update | authorization | T-22 | built |
| C-06 | Keys of each oximeter's own | cryptography | T-01, T-15 | built |
| C-07 | Standard algorithms, checked against published vectors | cryptography | T-02, T-09, T-33 | built |
| C-08 | Secure boot | integrity | T-14, T-33 | built |
| C-09 | Nothing unsigned in an image | integrity | T-33 | built |
| C-10 | Strict decoding of what other devices send | integrity | T-16 | built |
| C-11 | Debug port locked | integrity | T-14, T-15, T-34 | planned, needs hardware |
| C-12 | Reset for a new patient | confidentiality | T-37 | built |
| C-13 | Security event log | logging | T-04, T-18 | built |
| C-14 | Log records tagged by the oximeter | logging | T-18, T-22 | built |
| C-15 | Proof the log read is complete | logging | T-22 | built |
| C-16 | Limits kept through a reset | resilience | T-06 | built |
| C-17 | Technical alarm when the sensor stops | resilience | T-17 | built |
| C-18 | Watchdog | resilience | T-17 | built |
| C-19 | The gateway notices a silent oximeter | resilience | T-10 | built at the gateway's stand-in |
| C-20 | Signed updates over Bluetooth, tried before they're kept | updates | T-32, T-33 | built |
| C-21 | No rollback to an older release | updates | T-34 | built |
| C-22 | SBOM and vulnerability monitoring | updates | T-36 | built |

Tests are named as their suites name them: Robot tests in [renode/oximeter.robot](../../renode/oximeter.robot) and [renode/boot.robot](../../renode/boot.robot), unit tests of `core/` in [tests/](../../tests/), Zephyr tests on `native_sim` in each application's `tests/`, and the fuzz targets in [fuzz/](../../fuzz/).

## Authentication

### C-01 Pairing authenticated out of band

**Requirement.** Only a gateway the backend authorizes can pair with the oximeter, and the link it gets is encrypted and protected against a man in the middle.

**Built.** LE Secure Connections only, at security level 4 (`CONFIG_BT_SMP_SC_ONLY`). Neither board can show or enter a passkey, so pairing is authenticated out of band: before each pairing the oximeter makes fresh out-of-band data and seals it with AES-CCM under its pairing key, bound to its device ID ([common/src/pairing_seal.c](../../common/src/pairing_seal.c), [app/src/pairing.c](../../app/src/pairing.c)). The gateway opens it with the key the backend derives for that device ([hub/src/pairing.c](../../hub/src/pairing.c)), which proves the oximeter's public key to the gateway. Anyone who can't open the seal can't pair, Just Works included. An oximeter that can't seal its data for the device ID it claims can't pair with a gateway either, which is what stops a fake oximeter (T-08).

**Verified by.** "Pairs Again Once The Backend Allows It", "Refuses A Phone That Pairs With Just Works", and every end-to-end test, since the hub hears readings only over a link it paired at level 4; `test_opens_what_it_sealed`, `test_refuses_what_it_cannot_authenticate`, `test_seals_with_a_fresh_nonce_each_time` (common/tests/pairing_seal). No test yet plays a fake oximeter.

### C-02 One gateway at a time

**Requirement.** The oximeter is bonded to one gateway. Another takes its place only by pairing with the backend's sealed data, and the old one is then shut out.

**Built.** One bond (`CONFIG_BT_MAX_PAIRED=1`), kept in flash; a new pairing overwrites it. The bond survives a reset, so the gateway reconnects without pairing again.

**Verified by.** "Keeps Its Pairing Through A Reset", "Lets A New Gateway's Pairing Replace The Old One".

**Limits.** On a ward this keeps a gateway on its own patient only if the backend opens each oximeter's seal for that patient's gateway alone (T-12). The hub, standing in for gateway and backend, holds the pairing master and can open any oximeter's seal.

### C-03 Signed alarm-limit changes

**Requirement.** The alarm limits change only on a command the backend signed for this oximeter and this moment: no other device, no gateway and no replayed command can change them.

**Built.** The oximeter offers a random 16-byte challenge, replaced after every attempt and on every connection. A command is the limits and an HMAC-SHA256 tag, cut to 128 bits, over the challenge and the limits, under the oximeter's limits key ([core/src/limits_auth.c](../../core/src/limits_auth.c), [app/src/limits_crypto.c](../../app/src/limits_crypto.c)). The tag is checked in constant time by PSA Crypto. Only the backend holds the key's master, so a gateway, or someone at it, relays commands but can't make one (T-21, T-22).

**Verified by.** "Alarms On Limits The Hub Signs", "Refuses Limits From A Hub Without Its Key"; the ten tests of [tests/test_limits_auth.c](../../tests/test_limits_auth.c), among them `test_never_accepts_the_same_command_twice`, `test_rejects_a_tag_made_for_another_challenge` and `test_rejects_every_command_when_no_new_challenge_can_be_made`; `test_signs_commands_the_oximeter_accepts`, `test_commands_signed_with_another_key_are_refused` (hub/tests/limits_signer); the fuzz target `fuzz_limits_command`.

## Authorization

### C-04 An authenticated link for everything but identification

**Requirement.** Readings, alarm state, limits, the security log and updates are open only over an encrypted, authenticated link. A device that hasn't paired can read only what it needs to pair.

**Built.** Every characteristic needs security level 4, through `MEDSEC_PERM_READ_L4` and `MEDSEC_PERM_WRITE_L4` ([app/src/alarm_service.c](../../app/src/alarm_service.c), [app/src/plx_service.c](../../app/src/plx_service.c)), except the device ID and the sealed pairing data, which a gateway reads to pair. MCUmgr's SMP characteristic requires an authenticated link once pairing is on (`CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN`).

**Verified by.** "Refuses A Phone That Isn't Paired", which tries to read the readings service and the security log, and to start an upload.

### C-05 Only the oximeter's self-test keeps an update

**Requirement.** A gateway can stage an update and ask for a test boot, but can't make an update permanent: only the update's own self-test can.

**Built.** Over SMP, the oximeter refuses to confirm an image it isn't running (`CONFIG_MCUMGR_GRP_IMG_ALLOW_CONFIRM_NON_ACTIVE_SLOT=n`), and refuses any change of image state while an update is on trial ([app/src/update_guard.c](../../app/src/update_guard.c)). Standard MCUmgr tools confirm an update once it runs; a gateway that did would keep a broken update for good.

**Verified by.** "Keeps An Update On Trial Whatever The Gateway Says"; the update-protocol fuzzer ([fuzz/smp](../../fuzz/smp)), which checks that no request changes the running image's slot while an update is on trial.

## Cryptography

### C-06 Keys of each oximeter's own

**Requirement.** Each oximeter has its own keys, one per purpose. The firmware holds none, and a key taken from one oximeter opens no other.

**Built.** Three 32-byte keys, for limit changes, the security log and the pairing data, written into the nRF52840's UICR at the factory ([tools/provision.py](../../tools/provision.py)). The backend derives the same keys from a master secret and the device ID with HKDF-SHA256, a different label for each, so it stores no per-device keys. Every device runs the same image. An oximeter that was never provisioned refuses every pairing and every change ([core/src/device_key.c](../../core/src/device_key.c)).

**Verified by.** "Can't Be Paired Without A Device Key"; `test_accepts_a_provisioned_key`, `test_refuses_an_erased_or_zeroed_key` (tests/test_device_key.c); `test_derives_rfc5869_outputs`, `test_gives_each_device_its_own_key`, `test_derives_a_different_key_for_the_log`, `test_signs_with_the_key_derived_for_that_device` (hub/tests/limits_signer).

**Limits.** Until the debug port is locked (C-11), anyone holding an oximeter can read its keys out. They open that oximeter only.

### C-07 Standard algorithms, checked against published vectors

**Requirement.** Only standard, current algorithms, from maintained libraries, each checked against published test vectors.

**Built.** HMAC-SHA256 for limit changes and log records, and AES-CCM for the pairing seal, through PSA Crypto (TF-PSA-Crypto 1.1, long-term support); HKDF-SHA256 for key derivation; LE Secure Connections (P-256 ECDH, AES-CCM) for the link; ECDSA P-256 with SHA-256 for firmware signatures, in MCUboot. The challenges and nonces come from PSA Crypto, which takes them from Zephyr's cryptographically secure random source, backed by the nRF52840's hardware random number generator (`CONFIG_MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG`).

**Verified by.** `test_accepts_rfc4231_tags_cut_to_128_bits`, `test_rejects_a_tag_with_one_bit_changed`, `test_tags_log_records_as_rfc4231_does`, `test_returns_fresh_random_bytes` (app/tests/limits_crypto); `test_derives_rfc5869_outputs` (hub/tests/limits_signer); the pairing seal's tests (C-01). MCUboot's signature check is verified by C-08's tests.

## Code, data and execution integrity

### C-08 Secure boot

**Requirement.** The oximeter runs only firmware the manufacturer signed, checked at every boot.

**Built.** MCUboot checks an ECDSA P-256 signature over the image and its header at every boot, not only after an update (`BOOT_VALIDATE_SLOT0`), with the public half of the release key built into it. The build makes the release key on its first run and never commits it ([app/sysbuild.cmake](../../app/sysbuild.cmake)), so MCUboot's published development key, sysbuild's default, is never trusted.

**Verified by.** "Never Starts An Image The Release Key Didn't Sign", with an image signed with MCUboot's published key and one changed after signing; "Erases An Update With Anything The Release Key Didn't Sign", with an unsigned update.

**Limits.** Through the debug port, until it's locked (C-11), anyone can replace MCUboot itself. The release key is one per checkout here; a product keeps it in an offline signing service (T-35), and since MCUboot can't be updated, replacing a compromised key needs every device in hand.

### C-09 Nothing unsigned in an image

**Requirement.** Nothing outside an image's signature changes what MCUboot does with it.

**Built.** Outside the signature, an image may carry only fields that can't be signed, such as the signature itself: MCUboot refuses an image with any other field there (`CONFIG_MCUBOOT_USE_TLV_ALLOW_LIST`, [app/sysbuild/mcuboot.conf](../../app/sysbuild/mcuboot.conf)), as CVE-2024-32883 requires.

**Verified by.** "Erases An Update With Anything The Release Key Didn't Sign", with an update given a security counter outside its signature.

### C-10 Strict decoding of what other devices send

**Requirement.** Every parser of bytes from another device refuses anything malformed, without reading or writing out of bounds, and leaves the state as it was.

**Built.** The decoders in `core/` check length, flags and range before taking anything: limits ([core/src/alarm.c](../../core/src/alarm.c)), limit commands ([core/src/limits_auth.c](../../core/src/limits_auth.c)), readings at the hub ([core/src/plx.c](../../core/src/plx.c)) and log records ([core/src/security_log.c](../../core/src/security_log.c)). Requests over SMP go to Zephyr's MCUmgr.

**Verified by.** The unit tests' refusals, such as `test_rejects_limits_of_the_wrong_length`, `test_keeps_the_limits_in_force_when_it_rejects_new_ones`, `test_rejects_a_length_that_does_not_match_its_flags`, `test_rejects_reserved_flags`, `test_keeps_the_last_reading_when_it_rejects_a_measurement`, `test_ignores_malformed_records_found_in_flash`; the fuzz targets `fuzz_limits_command`, `fuzz_plx_decode` and `fuzz_security_log`, and the update-protocol fuzzer, each under AddressSanitizer and UndefinedBehaviorSanitizer.

**Limits.** Zephyr's Bluetooth stack below the characteristics is not fuzzed here (T-16).

### C-11 Debug port locked

**Requirement.** The debug port is closed on every production device, so its flash can be neither read nor written.

**Planned.** `CONFIG_NRF_APPROTECT_LOCK`, which needs a real board to show (the [updatability view](updatability.md)). Nordic's notice IN133 says fault injection can reopen the port on nRF52840 builds up to Dx0 ([vulnerability assessment](vulnerabilities.md#the-chip)), so on those the lock doesn't stand alone.

**Verified by.** Nothing yet: Phase 6's debug-port test, on hardware.

## Confidentiality

Readings, limits and the log travel only over the encrypted link of C-01 and C-04.

### C-12 Reset for a new patient

**Requirement.** Before an oximeter goes to the next patient, staff can make it forget its gateway and the clinician's limits, and nothing else can. The old gateway can't connect again.

**Built.** Holding Button 1 for 5 s deletes the bond and the stored limits, and the oximeter starts again with the defaults ([app/src/new_patient.c](../../app/src/new_patient.c)); a shorter press, as from a patient lying on it, does nothing. A gateway must then pair afresh with the backend's sealed data, and the hub waits for the backend's go-ahead before it does ([hub/src/pairing.c](../../hub/src/pairing.c)). The security log stays and records the reset (C-13).

**Verified by.** "Goes Back To The Default Limits For A New Patient", "Ignores A Short Press", "Pairs Again Once The Backend Allows It", "Logs The Reset For A New Patient"; `test_forgets_the_limits_when_cleared` (app/tests/limits_store).

## Event detection and logging

### C-13 Security event log

**Requirement.** The oximeter records its security events where the backend can read them: start-ups, connections, pairings, limit changes and updates, with outcomes and reasons, in order and with gaps showing.

**Built.** Each start-up with the reset cause and the version running, each update marked for a test boot with its version, each central that connects, each pairing and each refused one with the reason, each limit change accepted or refused and why, the sensor stopping and resuming, and the reset for a new patient ([core/src/security_log.c](../../core/src/security_log.c), [app/src/event_log.c](../../app/src/event_log.c)). The last 64 records are kept in flash, numbered from 1 and timed by boot and uptime, since the oximeter has no clock. Nothing over the air can change or erase them, and only the paired gateway can read them (C-04).

**Verified by.** "Logs The Security Events The Hub Reads", "Logs The Pairing", "Logs A Refused Pairing", "Logs A Refused Change From A Hub Without Its Key", "Logs An Update Marked For Test", "Logs The Reset For A New Patient"; the ten tests of [tests/test_security_log.c](../../tests/test_security_log.c); `test_records_survive_a_restart` (app/tests/event_log).

**Limits.** The log records which central changed the limits, not which person (T-04); naming the person is the backend's job (T-30).

### C-14 Log records tagged by the oximeter

**Requirement.** The backend can tell a record the oximeter wrote from one changed or invented on the way.

**Built.** Each record carries an HMAC-SHA256 tag under the oximeter's log key, which only the oximeter and the backend can derive (C-06).

**Verified by.** "Verifies Every Record Of The Log", "Can't Verify The Log Without The Signing Master"; `test_keeps_each_record_tagged_through_a_restart` (app/tests/event_log); `test_verifies_a_record_tagged_with_the_devices_log_key`, `test_refuses_a_record_with_one_byte_changed`, `test_refuses_a_record_tagged_with_another_devices_key` (hub/tests/limits_signer).

### C-15 Proof the log read is complete

**Requirement.** A gateway can't hold back the newest records unnoticed.

**Built.** The backend sends a fresh random challenge; the oximeter answers with its newest record's number, tagged with the challenge under the log key ([app/src/event_log.c](../../app/src/event_log.c)). The gateway can neither forge that answer nor pass off an old one.

**Verified by.** "Shows Whether The Hub Read The Whole Log"; `test_names_the_newest_record`, `test_encodes_the_answer_to_a_challenge_as_the_backend_checks_it` (tests/test_security_log.c); `test_accepts_the_oximeters_answer_to_its_challenge`, `test_refuses_an_answer_it_didnt_ask_for` (hub/tests/limits_signer).

## Resiliency and recovery

### C-16 Limits kept through a reset

**Requirement.** The clinician's limits survive a reset; a change that can't be kept isn't applied.

**Built.** Accepted limits are stored in flash with Zephyr's settings, on NVS ([app/src/limits_store.c](../../app/src/limits_store.c)). A change the oximeter can't save is refused and logged, rather than applied until the next reset.

**Verified by.** "Keeps The Clinician's Limits Through A Reset"; `test_starts_with_the_defaults_when_nothing_is_stored`, `test_restores_the_last_saved_limits`, `test_keeps_the_defaults_when_the_stored_value_is_not_valid` (app/tests/limits_store). No test yet makes a save fail.

### C-17 Technical alarm when the sensor stops

**Requirement.** If the sensor stops sending samples, the oximeter raises a technical alarm within 2 s rather than falling silent.

**Built.** The supervisor raises the technical alarm 1.5 s after the last sample, lights the alarm LED and sends "no reading" once a second ([core/src/supervision.c](../../core/src/supervision.c), [app/src/supervisor.c](../../app/src/supervisor.c)).

**Verified by.** "Raises The Technical Alarm When The Sensor Stops"; the five tests of [tests/test_supervision.c](../../tests/test_supervision.c).

### C-18 Watchdog

**Requirement.** If the firmware stops, the oximeter restarts within 2 s and monitors again under the clinician's limits.

**Built.** The nRF52840's watchdog, fed by the supervisor on its own lowest-priority thread, so a hang anywhere stops the feeding ([app/src/supervisor.c](../../app/src/supervisor.c)). A new update that hangs is reverted at that reset (C-20).

**Verified by.** "Recovers From A Hang Through The Watchdog".

### C-19 The gateway notices a silent oximeter

**Requirement.** The gateway notices within 2 s when readings stop arriving, so a jammed or lost link can be raised as an alarm rather than going quiet.

**Built at the gateway's stand-in.** The hub sets a 420 ms supervision timeout from the first connection, so it notices a silent oximeter within half a second, and logs it ([hub/src/main.c](../../hub/src/main.c)).

**Verified by.** "Notices A Silent Oximeter Within Two Seconds".

**Limits.** Noticing isn't alarming: the technical alarm at the gateway and the backend comes with them (T-10, T-23). Nothing stops a device connecting first and holding the oximeter's only connection (T-11).

## Firmware and software updates

### C-20 Signed updates over Bluetooth, tried before they're kept

**Requirement.** A fix reaches the oximeter in the field, through its paired gateway, and an update that doesn't work is undone without anyone touching the device. The oximeter loses no sample while an update uploads, and monitors again within 30 s of restarting into it.

**Built.** The gateway uploads a signed image with MCUmgr's SMP protocol over the authenticated link (C-04), marks it for a test boot and resets the oximeter ([hub/src/update.c](../../hub/src/update.c)). MCUboot checks it (C-08, C-21) and swaps it in, keeping the image before it. The update marks itself working only once its self-test passes; until then the next reset, by the watchdog if it hangs, swaps the previous image back. The security log shows the update marked for a test and the version that then started, so the backend sees whether it took. Each flash erase halts the processor for 85 ms, so at each interrupt the firmware reads every sample waiting in the sensor, and starts a new reading if the sensor ever lost some ([app/src/main.c](../../app/src/main.c), [core/src/sensor_fifo.c](../../core/src/sensor_fifo.c)). The [updatability view](updatability.md) has the whole path.

**Verified by.** "Keeps An Update On Trial Whatever The Gateway Says", "Keeps The Fast Interval Through An Update", "Takes Update Commands After Every Reconnection", "Keeps An Update That Passes Its Self-Test", "Reverts An Update That Fails Its Self-Test", "Logs An Update Marked For Test", "Logs The Security Events The Hub Reads" (the start record carries the running version), "Loses No Sample During An Update", "Resumes Monitoring Soon After An Update", "Reads Every Sample Waiting In The Sensor", "Starts A New Reading After The Sensor Loses Samples"; the two tests of [tests/test_sensor_fifo.c](../../tests/test_sensor_fifo.c).

**Limits.** Only the last hop, gateway to oximeter, is built; the hops from the manufacturer to the gateway are planned. The monitoring gap is measured in Renode, which times flash erases but not writes: about 5 s more on the chip.

### C-21 No rollback to an older release

**Requirement.** A genuine but older release can't bring a fixed vulnerability back.

**Built.** Each release carries a security counter in its signed header ([app/prj.conf](../../app/prj.conf)), raised by releases that fix a vulnerability; MCUboot erases an update whose counter is below the running image's ([app/sysbuild/mcuboot.conf](../../app/sysbuild/mcuboot.conf)).

**Verified by.** "Refuses An Older Release".

**Limits.** MCUboot's own documentation notes that the debug port can bypass the check, until it's locked (C-11).

### C-22 SBOM and vulnerability monitoring

**Requirement.** Every software component of the firmware is known, and every known vulnerability in one has a written assessment, as new ones are published.

**Built.** Every build makes a CycloneDX SBOM of what it compiled ([sbom.md](sbom.md)); every week CI asks NVD for the CVEs in its components and fails until each has a disposition ([vulnerabilities.md](vulnerabilities.md)).

**Verified by.** The SBOM tool's and the vulnerability check's tests ([tools/tests/](../../tools/tests/)).

## Threats without a control yet

| Threat | Why |
|---|---|
| T-10 Jamming | It can't be prevented; C-19 notices it, and the alarm it should raise comes with the gateway and the backend |
| T-11 A device holds the only connection | The oximeter allows one connection and stops advertising while connected |
| T-13 Tracking by address and name | The oximeter keeps a fixed address and names itself in its scan response; Bluetooth privacy (resolvable private addresses) is off |
| T-14, T-15 Firmware written or read through the debug port | C-08 stops altered firmware running, but the debug port stays open until C-11 |
| T-19 A replaced sensor or injected signals | The firmware checks only the sensor's part number |
| T-20 Health data on the console | The console logs readings and limit changes in plain text |
| T-22 A compromised gateway alters readings | C-03, C-05, C-14 and C-15 stop it forging limits, keeping updates and editing the log, but readings aren't signed end to end |
| T-23 to T-31 The gateway's and backend's own threats | The Raspberry Pi gateway and the backend aren't built yet |
| T-35 A stolen release key | One key per checkout, never committed; the offline signing service is planned, and a key MCUboot trusts can't be replaced in the field |
