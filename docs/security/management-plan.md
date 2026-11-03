# Cybersecurity management plan

How vulnerabilities found after release are watched for, assessed, fixed, disclosed and told to users: the plan FDA's premarket guidance asks for (section VI.B), and the one section 524B(b)(1) of the FD&C Act requires of a cyber device, "to monitor, identify, and address, as appropriate, in a reasonable time, postmarket cybersecurity vulnerabilities and exploits, including coordinated vulnerability disclosure and related procedures". Each part says what's in place today and what's planned.

> Portfolio exercise, not a medical device.

## Who does what

| Role | Responsible for |
|---|---|
| Product security lead | this plan; watching for vulnerabilities; assessing each one; deciding the timeline; coordinating disclosure |
| Firmware engineering | reproducing a vulnerability, fixing it, and the tests that show the fix |
| Release manager | building and signing releases, with a second person approving each signature, and raising the security counter for a vulnerability fix |
| Clinical safety | the severity of patient harm in each assessment, and what passes to the safety risk assessment |
| Regulatory affairs | reports to FDA under 21 CFR part 806, and whether a fix needs a premarket submission |
| Customer support | notices to hospitals, clinics and patients |

In this project one developer holds every role.

## Watching for vulnerabilities

| Source | How | How often | Status |
|---|---|---|---|
| NVD, for each component in the [SBOM](sbom.md) that has a CPE | [tools/vulns.py](../../tools/vulns.py) fails until each CVE has an assessment ([vulnerability assessment](vulnerabilities.md)) | every Monday in CI, and on demand | in place |
| The components' own advisories: Zephyr's, MCUboot's, TF-PSA-Crypto and Mbed TLS's, Nordic's notices for the nRF52840 | read by hand, with NVD's text search for the components without a CPE | weekly, with the NVD check | by hand; last done 26 October 2026 |
| CISA's Known Exploited Vulnerabilities catalog | each CVE assessed is looked up | weekly | by hand, last on 28 October 2026; to be automated in the weekly check |
| Researchers, users, hospitals and suppliers | coordinated disclosure, below | as reported | in place once private reporting is turned on |
| The project's own testing | CI on every change, and the periodic testing below | every change | in place |
| An information sharing and analysis organization | membership of one that shares medical device vulnerabilities, such as Health-ISAC (FDA's postmarket guidance names its predecessor, NH-ISAC) | ongoing | planned |

GitHub turns off a public repository's scheduled workflows after 60 days without activity, so the release manager checks every month that the Monday check has run.

## Assessing a vulnerability

1. **Reproduce it.** The whole system runs in Renode with no hardware, so a reported flaw can be tried against the release in the field and against the next.
2. **Find what it affects.** Each release's SBOM lists the files each image compiled, so whether the vulnerable code is in the image is a lookup, as in the [vulnerability assessment](vulnerabilities.md).
3. **Score it** with the [risk assessment](risk-assessment.md)'s method: CVSS v3.0 with MITRE's rubric for medical devices, the severity of patient harm, and the acceptance criteria. A vulnerability listed in CISA's catalog is uncontrolled whatever its score.
4. **Record it** in the vulnerability assessment, and look again at every threat the risk assessment rates Low or Medium only because an attack is hard today: a published vulnerability can make it easier.

## Timelines

| Risk | Users told | Fix delivered | Basis |
|---|---|---|---|
| Uncontrolled, or the vulnerability is in CISA's catalog | as soon as possible, no later than 30 days after learning of it: the vulnerability and its impact, interim compensating controls, and the plan to fix it | as soon as possible, no later than 60 days after learning of it, out of the regular cycle, validated and installable | FDA's postmarket guidance (section VII.B); section 524B(b)(2)(B) |
| Controlled | in the release notes | in the next regular release, every three months | section 524B(b)(2)(A) |
| A component reaching its end of support | in the release notes | before the support ends | |

Sixty days is enough because building and testing a fix is automated: CI builds both images, makes their SBOM and runs every test in Renode on each change. The slower steps are the signing ceremony and the hospitals' and clinics' approval of when each device is updated. Zephyr 4.4, the version in use, is supported until 12 April 2027 ([SBOM](sbom.md#support)): the firmware moves to a supported release before then, and Zephyr's advisories already name flaws in 4.4.2 fixed only in 4.5.0.

## Delivering a fix

**The process.** A fix goes through the same review and CI as any change, plus a test that fails without it. The release manager builds the release, signs it with the release key, a second person approving, and raises the security counter when the release fixes a vulnerability, so no older release can be installed again (C-21). The release ships with its SBOM and an updated vulnerability assessment. The [updatability view](updatability.md) follows the image from the manufacturer to the oximeter; the hops before the gateway are design.

**How fast it reaches devices.** In Renode, a gateway uploads an image of about 220 KB in about 20 s while the oximeter keeps measuring, and the oximeter is monitoring again within 30 s of the gateway's reset command: 14.9 s in Renode, and about 5 s more expected on the chip ([updatability view](updatability.md#interrupted-and-failed-updates)). Each gateway updates its own oximeter, so a ward's devices update in parallel, but only a few beds at a time, so their pauses don't overlap ([multi-patient harm view](multi-patient.md#monitoring-stopping-together)). Each oximeter's security log records the version it starts (C-13), so the backend knows which devices have the fix.

**What can't be fixed in the field.** MCUboot, with its copies of TinyCrypt and of Mbed TLS's ASN.1 parser, can't be updated, nor can the release key it trusts or the keys written into each oximeter at the factory. A vulnerability in any of them is fixed only with each device in hand.

## Coordinated vulnerability disclosure

**Reporting.** Anyone can report a vulnerability privately, through the repository's security advisories, as [SECURITY.md](../../SECURITY.md) explains; a public issue is the wrong place.

**What happens next.**

1. The report is acknowledged within five working days.
2. It's assessed as above, and the reporter is told the score and the planned timeline.
3. The fix is made and delivered on the timeline above, and the reporter may check it first.
4. A security advisory is published once the fix is available, or after 90 days if it isn't, unless the reporter agrees to wait: what's affected, the versions fixed, the CVSS vector with the rubric's answers, and the compensating controls. GitHub assigns a CVE, as a CVE numbering authority. The reporter is credited, if they wish.
5. Where the vulnerability is in a third-party component, it's reported to that project through its own security process, and disclosed on that project's terms.

Vulnerabilities the manufacturer finds itself follow the same steps, from the second on. Two found by fuzzing, undefined behavior in Zephyr's MCUmgr that a gateway can't steer ([vulnerability assessment](vulnerabilities.md#found-by-fuzzing)), are still to be reported to the Zephyr project.

## Telling users

Each advisory is also sent to the hospitals and clinics that use the device, with the content FDA's postmarket guidance asks for at the least: the vulnerability and its impact, that a fix is underway, the compensating controls, and when the fix will come. Patients at home hear from their clinic. Every release's notes list the vulnerabilities it fixes, and the labeling says where advisories and release notes are published.

## Periodic security testing

| When | What | Status |
|---|---|---|
| Every change | unit tests under sanitizers, fuzzing of what other devices send, static analysis, the attacker tests in Renode, the check that no key is built in, and the traceability check ([test report](test-report.md)) | in place |
| Every Monday | the vulnerability check against NVD | in place |
| Every release | all of the above on the release build, on the development kit as well as in Renode | Renode in place; the kit planned |
| Every year, and after any significant change | penetration testing by testers independent of the design; the threat model and the risk assessment reviewed | planned |

## End of support

Security updates continue for at least five years after the last device is sold, and the end of support is announced twelve months ahead. A device kept in service after that gets no more fixes, and its cybersecurity risk can be expected to grow: the labeling says so, and says how to take the device out of service.
