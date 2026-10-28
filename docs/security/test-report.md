# Test report

How the oximeter's cybersecurity was tested, in the four kinds of testing FDA's premarket guidance names: security requirements, threat mitigation, vulnerability testing and penetration testing. Then the defects testing found, and what's still open.

> Portfolio exercise, not a medical device.

## Result

Every test passed in CI [run 47](https://github.com/Tonyc310/medsec-oximeter/actions/runs/40729609491) at commit [7d80453](https://github.com/Tonyc310/medsec-oximeter/commit/7d80453), on 28 October 2026. CI fails on any failed test, sanitizer report, fuzzer finding, static analysis finding, formatting difference, key found in an image or break in the traceability chain, so a passing run means everything below passed. Each test's name is its expected result: the behavior it checks.

| Kind | Where | Tests | How |
|---|---|---|---|
| End to end | [renode/oximeter.robot](../../renode/oximeter.robot) | 31 | both boards in Renode, joined by a simulated Bluetooth link |
| Secure boot | [renode/boot.robot](../../renode/boot.robot) | 5 | the oximeter booting through MCUboot in Renode |
| Simulation models | `flash.robot`, `ccm.robot`, `radio.robot`, `sensor.robot` in [renode/](../../renode/) | 12 | the models this project adds to Renode or fixes, against their specifications or Zephyr's own driver tests |
| Unit, host | [tests/](../../tests/) | 56 | `core/` under AddressSanitizer and UndefinedBehaviorSanitizer |
| Unit, `native_sim` | `tests/` in [app/](../../app/tests/), [hub/](../../hub/tests/), [common/](../../common/tests/) | 28 | code that needs Zephyr, run as a program on the PC |
| Fuzzing | [fuzz/](../../fuzz/) | 4 targets | libFuzzer, a minute each, under the same sanitizers |
| Tools | [tools/tests/](../../tools/tests/) | 21 | pytest |

## Environment

The firmware under test is the build CI makes: the oximeter and MCUboot for the nRF52840 DK, on Zephyr 4.4.2 with the SDK its manifest names. It runs in Renode 1.17, with this project's models of the MAX30101 sensor, the flash controller's timing and the CCM, and a fix to Renode's radio. The hub stands in for the gateway and the backend; Zephyr's Bluetooth shell plays a phone. Nothing has run on hardware yet.

What the simulation can't show:

- **Physical attacks**: on the debug port (I-4) and the sensor bus (I-1), and fault injection such as IN133 ([vulnerability assessment](vulnerabilities.md#the-chip)).
- **Real timing and radio**: Renode's radio has no interference, loss or range, flash takes the datasheet's times, and the sensor's waveform is synthetic.
- **The models' own errors**: each is written from its chip's specification and tested against it or Zephyr's drivers. A wrong model can hide a firmware defect or invent one; the radio did ([Defects](#defects-testing-found)).

## Security requirements

[controls.md](controls.md) gives each control's requirement and names the tests that verify it. Every built control names at least one test, and every test named exists: the traceability check fails CI otherwise. The one control not yet built, the debug-port lock (C-11), has no test; it needs the DK.

The unit tests check both sides of the ranges the code enforces, such as the alarm limits' bounds and the pulse range, with the gaps under [Open items](#open-items).

## Threat mitigation

[traceability.md](traceability.md) traces each threat in the [threat model](threat-model.md) to the controls built against it and the number of tests that verify them, by kind. CI checks it's up to date on every push.

The end-to-end and boot tests play each attacker the simulation can:

- **A device in range that isn't the gateway** (T-01, T-05, T-07): a phone that never paired is refused the readings, the security log and an update ("Refuses A Phone That Isn't Paired"), and one that tries Just Works can't pair ("Refuses A Phone That Pairs With Just Works", "Logs A Refused Pairing").
- **A gateway the backend let pair but won't sign for** (T-21, T-22): its limits are refused and logged, and it can't verify the log ("Refuses Limits From A Hub Without Its Key", "Logs A Refused Change From A Hub Without Its Key", "Can't Verify The Log Without The Signing Master").
- **A gateway that holds back log records, or keeps an update that failed** (T-22): "Shows Whether The Hub Read The Whole Log", "Keeps An Update On Trial Whatever The Gateway Says".
- **Forged, altered and old firmware** (T-33, T-34): an image signed with MCUboot's published key, one changed after signing, an unsigned update and one given a security counter outside its signature (CVE-2024-32883) never run, and an older release is erased ("Never Starts An Image The Release Key Didn't Sign", "Erases An Update With Anything The Release Key Didn't Sign", "Refuses An Older Release").
- **Another gateway, and a device passed to a new patient** (T-12, T-37): "Lets A New Gateway's Pairing Replace The Old One", "Pairs Again Once The Backend Allows It".
- **An oximeter the factory never provisioned**: "Can't Be Paired Without A Device Key".

No test covers the threats without a control (T-11, T-13, T-19, T-20, T-23 to T-31, T-35), nor T-08: C-01 addresses a fake oximeter, but no test plays one.

## Vulnerability testing

**Malformed and unexpected input.** libFuzzer gives any bytes at all to the parsers of what other devices send, and checks a property on every input: three targets on `core/`, for limit changes, readings and log records, and one on the update protocol as a hostile paired gateway would send it, where the running image's slot must never change ([fuzz/smp](../../fuzz/smp)). Fuzzing found undefined behavior at two places in Zephyr's MCUmgr, neither of which a gateway can steer; the [vulnerability assessment](vulnerabilities.md#found-by-fuzzing) explains both.

**Abuse cases.** The attackers under [Threat mitigation](#threat-mitigation).

**Static analysis.** clang-tidy checks `core/` with its CERT C checks, the Clang static analyzer and bug-prone patterns. cppcheck checks `core/`, `app/` and `common/`, the last two without Zephyr's headers, so it doesn't follow Zephyr's types. Every build treats warnings as errors, and the host build adds strict conversion warnings. Neither analyzer checks `hub/`, which stands in for the backend, or Zephyr itself.

**Dynamic analysis.** The unit tests and fuzzers run under AddressSanitizer and UndefinedBehaviorSanitizer. The firmware runs without sanitizers in Renode.

**Hardcoded credentials.** [tools/find_keys.py](../../tools/find_keys.py) searches the oximeter's and MCUboot's images on every build for the backend's masters, the release key's private half and a provisioned oximeter's keys, and finds none. The oximeter has no passwords or default credentials: its keys are its own, written at the factory (C-06), and one never provisioned can't be paired. MCUboot's published development key is never trusted (C-08).

**Software composition.** The [SBOM](sbom.md) lists what each build compiled, and [tools/vulns.py](../../tools/vulns.py) checks it against NVD: each CVE found has a disposition in the [vulnerability assessment](vulnerabilities.md). That analyzes the source the build compiled, not the binaries. The assessment was made on 26 October 2026 with the check run locally; CI's weekly run of it starts on 2 November 2026.

**Attack surface.** Of the oximeter's interfaces in the [system description](system.md), Bluetooth (I-2, I-3) and the update path over it (I-8) are tested end to end. The console (I-5) has no control yet (T-20), and attacks on the sensor bus (I-1) and the debug port (I-4) need hardware.

## Penetration testing

None yet. The developer wrote every test here, knowing the design; FDA asks for penetration testing by testers independent of it. It waits for the DK, since attacks on the radio, the debug port and the sensor bus need hardware, as does an attacker's own Bluetooth device, such as an ESP32. Vulnerability chaining and closed-box scanning belong with it.

## Defects testing found

| Defect | Found by | Fixed in |
|---|---|---|
| A pulse over 240 bpm read as half its rate, where it should give no reading | testing both sides of the pulse range, 240 and 241 bpm | [713a2c1](https://github.com/Tonyc310/medsec-oximeter/commit/713a2c1) |
| From the third link on, the hub lost its subscriptions: no readings, alarms or update replies | resetting the oximeter twice ("Hears Readings After Every Reconnection") | [fd762fc](https://github.com/Tonyc310/medsec-oximeter/commit/fd762fc) |
| The oximeter's own request for slower link timing undid the hub's fast interval, so an upload took about three times as long | "Keeps The Fast Interval Through An Update" | [16c843f](https://github.com/Tonyc310/medsec-oximeter/commit/16c843f) |
| The hub took up to 4 s to notice a silent oximeter, beyond the 2 s C-19 requires | "Notices A Silent Oximeter Within Two Seconds" | [7d80cc8](https://github.com/Tonyc310/medsec-oximeter/commit/7d80cc8) |
| The firmware read one sensor sample per interrupt, so any delay left samples waiting for good. Once the sensor's FIFO had filled, every flash erase lost samples: 43% of them during an update | flash erases timed as the chip times them, with the sensor model's FIFO pointers | [5cdfa5e](https://github.com/Tonyc310/medsec-oximeter/commit/5cdfa5e) |
| A reading that spanned samples the sensor lost would be skewed | the same | [2bcd209](https://github.com/Tonyc310/medsec-oximeter/commit/2bcd209) |

Two more were in the test equipment:

| Defect | Found by | Fixed in |
|---|---|---|
| Renode's radio delivered frames the receiver had missed, up to 97 ms late and on other channels: links failed their MIC checks and both Bluetooth controllers trapped on an assertion | end-to-end tests failing intermittently | [a9d4c2b](https://github.com/Tonyc310/medsec-oximeter/commit/a9d4c2b) |
| Two of the CCM model's tests couldn't fail | auditing the tests, with faults planted in the model | [dc83cd3](https://github.com/Tonyc310/medsec-oximeter/commit/dc83cd3) |

## Open items

- **On the DK**: the debug-port lock (C-11) and what IN133 leaves open on the board's revision; the timing the simulation only models, such as flash stalls, link timing and the gap in monitoring after an update; and penetration testing.
- **A second update with a real gateway**: the hub gives up on a request after 5 s, and in one probe it timed out while the oximeter erased the old update's slot. The tests never have.
- **Tests not yet written**: a fake oximeter (T-08); a failed save of the alarm limits (C-16); and boundary cases no unit test checks today, each shown by a planted bug that every unit test passes: the high byte of the pulse limits, a device key erased but for its last byte, and the accepting side of the oximetry analysis's perfusion and periodicity thresholds.
