# Security labeling

The security information the oximeter's labeling gives its users, as FDA's premarket guidance recommends (section VI.A), written for them: patients and families at home, ward staff, and the hospital's biomedical and IT staff. In a product it would sit in the instructions for use and a security guide for the hospital. Where something depends on the gateway or the backend, which aren't built yet, it says so.

> Portfolio exercise, not a medical device.

## For patients and families

- The oximeter sends your readings only to its own gateway, the box beside your bed, and encrypts them on the way. Nothing else nearby can read them, or change when your alarm sounds.
- Its own light alarms whatever happens to the gateway or the internet.
- Keep it with you. Don't let anyone open it or plug anything into it, and if the case looks opened or damaged, stop using it and call your clinic.
- Your clinic chooses when a software update is installed, and tells you first. Monitoring pauses for under half a minute while it restarts.
- When you no longer need it, give it back to your clinic, which resets it before anyone else uses it.
- If you think something is wrong with it, call your clinic.

## For ward staff

- **Pairing.** Pair a gateway only with the oximeter on the bed that gateway serves. Readings from the wrong oximeter would be shown as another patient's.
- **Between patients.** Hold Button 1 for 5 s: the oximeter forgets its gateway and the alarm limits, and starts again with the defaults. A shorter press does nothing. If you forget, the last patient's gateway can still connect to it.
- **Alarm limits.** They change only through the central station, which signs each change; the oximeter refuses any other.
- **Updates.** Approve a time for each oximeter, and on a ward update a few beds at a time, so no two pauses in monitoring overlap.
- **A silent oximeter.** If the central station reports one, go to the bed: the oximeter's own alarm still works.

## For biomedical and IT staff

### What it connects to

```mermaid
flowchart LR
    oximeter["Oximeter"] -- "Bluetooth LE, encrypted, its one gateway" --- gateway["Bedside gateway"]
    gateway -- "the hospital network (design)" --- backend["Central station server"]
```

The oximeter has no network connection of its own: no IP address and no network ports. It talks only to the one gateway it's paired with, which must be within Bluetooth range. The gateway's network interfaces, and the endpoints it may reach, come with the gateway.

| Interface | Direction | Carries | Protection |
|---|---|---|---|
| Bluetooth LE advertising | out | that it's a pulse oximeter | anyone in range can see that one is there, but not follow it: its address changes at each start and every 15 minutes, and it doesn't advertise its name |
| Bluetooth LE, before pairing | in and out | its device ID and its sealed pairing data | nothing else is open; a device that doesn't pair is dropped after 30 s |
| Bluetooth LE, paired | in and out | readings, alarm state, signed alarm limits, the security log, firmware updates | LE Secure Connections at security level 4, with its one bonded gateway |
| Serial console | out | the firmware version, Bluetooth events and errors; no readings, alarms or limits | inside the enclosure |
| Debug port (SWD) | in and out | everything in memory | to be locked on every production device |
| Button 1 | in | the reset for a new patient, held 5 s | physical |
| Sensor bus (I2C) | in and out | the sensor's settings, and its light samples | inside the enclosure |

### As shipped

Each oximeter has its own keys, written into it at the factory: there are no passwords and no default credentials. It runs only firmware the manufacturer signed, and refuses any release older than the last one that fixed a vulnerability. Nothing on the oximeter changes its security. The only settings users can change are the alarm limits, through the central station, and the reset for a new patient.

### Updates

Only the manufacturer's signed releases install, and only through the hospital's server and the bedside gateway, at a time staff approve. An update runs on trial and is undone by itself if its self-test fails. Each start-up record in the security log carries the firmware version, so the central station can show which version each oximeter runs. The release notes say which vulnerabilities a release fixes; they and the security advisories are published where the [management plan](management-plan.md) says.

### Security events and the log

The oximeter records each start-up with its reset cause and firmware version, each central that connects or disconnects, each pairing and each refused one, each change of alarm limits accepted or refused, a change it couldn't save, an update marked for a trial, the sensor stopping and resuming, a missing device key, and each reset for a new patient.

Each record is 20 bytes, little-endian: a sequence number (4 bytes, from 1, never repeated, so a gap shows a lost record), the boot it happened in (2), milliseconds since that boot (4), the event code (1) and its detail (9). The oximeter has no clock: the gateway stamps the date when it reads the log. Each record carries a 16-byte tag, HMAC-SHA256 under the oximeter's own log key, which only the oximeter and the backend can make. The last 64 are kept in flash, the oldest overwritten first. The paired gateway reads them over Bluetooth; the backend checks each tag, and asks the oximeter for proof it has read them all. Exporting the log to the hospital's security monitoring comes with the backend.

When something unexpected happens, the oximeter keeps measuring and alarming: it refuses an unsigned or stale change of limits and an unauthenticated pairing, drops a device that doesn't pair, raises a technical alarm if the sensor stops, and restarts itself if its firmware hangs. Each of these is in the log.

### Backup and restore

The alarm limits are kept in flash through any restart, and change only by a signed command. After a reset for a new patient, the central station sends the new patient's limits again; the backend keeps each patient's limits.

### If you suspect a vulnerability or an attack

Report it to the manufacturer as [SECURITY.md](../../SECURITY.md) explains. Take an oximeter that may have been tampered with out of service, and keep it: its security log holds the evidence, and the reset for a new patient doesn't erase it. The [management plan](management-plan.md) says how reports are handled and how advisories reach you.

### Software bill of materials

Every release comes with a machine-readable SBOM in CycloneDX format, listing every software component in the oximeter's firmware and its bootloader, with versions, suppliers and the files compiled ([SBOM](sbom.md)). The [vulnerability assessment](vulnerabilities.md) says which known vulnerabilities affect them.

### End of support

The manufacturer provides security updates for at least five years after the last oximeter is sold, and announces the end of support twelve months ahead. An oximeter kept in service after that gets no more security fixes, and its cybersecurity risk can be expected to grow.

### Decommissioning

Between patients, the reset for a new patient forgets the gateway and the limits. It keeps the oximeter's keys, which belong to the device, and its security log, which holds the last gateway's address and the limit changes but no readings.

To take an oximeter out of service for good, erase it completely: the nRF52840's ERASEALL, through a debug probe, clears all of its flash, its UICR and its RAM, keys and log included, and works on a locked debug port. Or return it to the manufacturer. The backend then retires its device ID, so its keys are never derived again.

### Risks users manage

| Risk | What users do |
|---|---|
| A gateway paired with another bed's oximeter (T-12) | pair each gateway with its own bed's oximeter |
| The last patient's gateway still connecting (T-37) | reset the oximeter between patients |
| Anyone holding the oximeter reaching its console or debug port | keep it with the patient, and take it out of service if it's been opened |
| Many beds' monitoring pausing at once for updates | approve update times a few beds at a time |
| Monitoring stopping unnoticed when a link fails | go to the bed when the central station reports a silent oximeter |

A manufacturer would test each of these with real users, in usability testing, before relying on them.
