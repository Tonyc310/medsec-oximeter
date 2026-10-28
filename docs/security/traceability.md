# Traceability

Each threat in the [threat model](threat-model.md), the [controls](controls.md) built against it,
and how many tests verify them, by kind: Robot tests in Renode, unit tests on the host or
`native_sim`, and fuzz targets. The controls name each test. A gap links to why a threat has no
control yet, or only part of one.

Made by [tools/traceability.py](../../tools/traceability.py), which CI runs to check that every
test named exists and that this table is up to date.

| Threat | Controls | Tests |
|---|---|---|
| T-01 A device that isn't the patient's gateway connects and writes alarm limits, such as an SpO2 limit of 50% (AS-2) | [C-01](controls.md#c-01-pairing-authenticated-out-of-band), [C-03](controls.md#c-03-signed-alarm-limit-changes), [C-04](controls.md#c-04-an-authenticated-link-for-everything-but-identification), [C-06](controls.md#c-06-keys-of-each-oximeters-own) | 6 Renode, 21 unit, 1 fuzz |
| T-02 A man in the middle between gateway and oximeter changes limits in transit | [C-01](controls.md#c-01-pairing-authenticated-out-of-band), [C-03](controls.md#c-03-signed-alarm-limit-changes), [C-07](controls.md#c-07-standard-algorithms-checked-against-published-vectors) | 4 Renode, 20 unit, 1 fuzz |
| T-03 A captured limits write is replayed later, restoring settings a clinician has since changed | [C-03](controls.md#c-03-signed-alarm-limit-changes) | 2 Renode, 12 unit, 1 fuzz |
| T-04 Limits change with no record of who changed them, from where, or when | [C-13](controls.md#c-13-security-event-log) | 6 Renode, 11 unit |
| T-05 Anyone in range reads a patient's alarm limits | [C-04](controls.md#c-04-an-authenticated-link-for-everything-but-identification) | 1 Renode |
| T-06 Limits live only in RAM, so any reset, including an attacker pulling the battery, silently restores the defaults | [C-16](controls.md#c-16-limits-kept-through-a-reset) | 1 Renode, 3 unit |
| T-07 Anyone in range subscribes to, or sniffs, a patient's readings (AS-1) | [C-01](controls.md#c-01-pairing-authenticated-out-of-band), [C-04](controls.md#c-04-an-authenticated-link-for-everything-but-identification) | 3 Renode, 3 unit |
| T-08 A fake oximeter advertises the Pulse Oximeter Service and the gateway connects to it instead of the patient's | [C-01](controls.md#c-01-pairing-authenticated-out-of-band) | 2 Renode, 3 unit |
| T-09 A man in the middle alters readings or alarm state in transit | [C-01](controls.md#c-01-pairing-authenticated-out-of-band), [C-07](controls.md#c-07-standard-algorithms-checked-against-published-vectors) | 2 Renode, 8 unit |
| T-10 Radio jamming stops readings and alarms reaching the gateway | [C-19](controls.md#c-19-the-gateway-notices-a-silent-oximeter), [gap](controls.md#threats-without-a-control-yet) | 1 Renode |
| T-11 An attacker's device connects first and holds the oximeter's only connection | [gap](controls.md#threats-without-a-control-yet) | none |
| T-12 On a ward, a gateway connects to another bed's oximeter, by accident or by an attacker's doing (AS-5) | [C-02](controls.md#c-02-one-gateway-at-a-time) | 2 Renode |
| T-13 The oximeter's fixed address and name let anyone in range detect and follow it | [gap](controls.md#threats-without-a-control-yet) | none |
| T-14 Malicious firmware is written through the debug port | [C-08](controls.md#c-08-secure-boot), [C-11](controls.md#c-11-debug-port-locked) (planned), [gap](controls.md#threats-without-a-control-yet) | 2 Renode |
| T-15 Firmware and memory are read out through the debug port | [C-06](controls.md#c-06-keys-of-each-oximeters-own), [C-11](controls.md#c-11-debug-port-locked) (planned), [gap](controls.md#threats-without-a-control-yet) | 1 Renode, 6 unit |
| T-16 A crafted Bluetooth packet exploits a bug in the firmware's handlers or the Bluetooth stack, corrupting memory or running code | [C-10](controls.md#c-10-strict-decoding-of-what-other-devices-send) | 6 unit, 4 fuzz |
| T-17 A crash or hang stops measurement and alarms | [C-17](controls.md#c-17-technical-alarm-when-the-sensor-stops), [C-18](controls.md#c-18-watchdog) | 2 Renode, 5 unit |
| T-18 The oximeter records no security events: connections, limit changes, rejected writes | [C-13](controls.md#c-13-security-event-log), [C-14](controls.md#c-14-log-records-tagged-by-the-oximeter) | 8 Renode, 15 unit |
| T-19 The sensor is replaced, or false signals are injected on its bus | [gap](controls.md#threats-without-a-control-yet) | none |
| T-20 The console port logs readings and limit changes in plain text | [gap](controls.md#threats-without-a-control-yet) | none |
| T-21 Someone at the gateway changes alarm limits through its local interfaces | [C-03](controls.md#c-03-signed-alarm-limit-changes) | 2 Renode, 12 unit, 1 fuzz |
| T-22 A compromised gateway alters, drops or invents what it relays, in both directions | [C-03](controls.md#c-03-signed-alarm-limit-changes), [C-05](controls.md#c-05-only-the-oximeters-self-test-keeps-an-update), [C-14](controls.md#c-14-log-records-tagged-by-the-oximeter), [C-15](controls.md#c-15-proof-the-log-read-is-complete), [gap](controls.md#threats-without-a-control-yet) | 6 Renode, 20 unit, 2 fuzz |
| T-23 The gateway goes offline: power, network, crash | [gap](controls.md#threats-without-a-control-yet) | none |
| T-24 Health data is read from the gateway's storage or its network traffic | [gap](controls.md#threats-without-a-control-yet) | none |
| T-25 A fake backend sends gateways limits or updates; a fake gateway sends the backend readings for a patient | [gap](controls.md#threats-without-a-control-yet) | none |
| T-26 Traffic between gateway and backend is altered or read in transit; at home it crosses the internet | [gap](controls.md#threats-without-a-control-yet) | none |
| T-27 A stolen clinician or nurse login changes patients' limits | [gap](controls.md#threats-without-a-control-yet) | none |
| T-28 A compromised backend changes limits, suppresses alarms or alters records across all patients | [gap](controls.md#threats-without-a-control-yet) | none |
| T-29 The backend's database is breached | [gap](controls.md#threats-without-a-control-yet) | none |
| T-30 Limit changes and alarm acknowledgements aren't attributed to a person | [gap](controls.md#threats-without-a-control-yet) | none |
| T-31 The backend goes down | [gap](controls.md#threats-without-a-control-yet) | none |
| T-32 There's no way to update the oximeter in the field, so a vulnerability can't be fixed without physical access to every device (FDA's updatability objective) | [C-20](controls.md#c-20-signed-updates-over-bluetooth-tried-before-theyre-kept) | 11 Renode, 2 unit |
| T-33 A forged firmware image is pushed through the update path | [C-07](controls.md#c-07-standard-algorithms-checked-against-published-vectors), [C-08](controls.md#c-08-secure-boot), [C-09](controls.md#c-09-nothing-unsigned-in-an-image), [C-20](controls.md#c-20-signed-updates-over-bluetooth-tried-before-theyre-kept) | 13 Renode, 7 unit |
| T-34 An older, genuinely signed image with a known vulnerability is installed again (rollback) | [C-11](controls.md#c-11-debug-port-locked) (planned), [C-21](controls.md#c-21-no-rollback-to-an-older-release) | 1 Renode |
| T-35 The signing key is stolen or misused, so the attacker's firmware passes as the manufacturer's | [gap](controls.md#threats-without-a-control-yet) | none |
| T-36 A third-party component (Zephyr, the Bluetooth controller, the gateway's Linux packages) has a vulnerability or a planted flaw, as in SweynTooth (2020) | [C-22](controls.md#c-22-sbom-and-vulnerability-monitoring) | 4 unit |
| T-37 A device passed to a new patient, or discarded, still holds the last patient's pairings, keys or settings | [C-02](controls.md#c-02-one-gateway-at-a-time), [C-12](controls.md#c-12-reset-for-a-new-patient) | 6 Renode, 1 unit |
