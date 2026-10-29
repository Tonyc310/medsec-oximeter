# Risk assessment

How exploitable each threat in the [threat model](threat-model.md) is and how badly it could harm a patient, before the controls in [controls.md](controls.md) and after them, and whether the risk that's left is acceptable. FDA's premarket guidance asks for the methods used to score risk before and after mitigation, the acceptance criteria, and how security risks pass to the safety risk assessment. This document gives them first, then the scores.

> Portfolio exercise, not a medical device.

## Method

**Exploitability, not probability.** FDA's guidance says the likelihood of a cyberattack can't be estimated from history or modelling the way a safety risk's can, so security risk is rated by exploitability: what an attacker needs to carry a threat out.

**CVSS v3.0, with MITRE's rubric.** Each threat gets a CVSS v3.0 base vector, its metrics chosen by answering the questions in MITRE's *Rubric for Applying CVSS to Medical Devices*, version 0.12.04. FDA's postmarket guidance names CVSS v3.0 as one way to rate exploitability, and FDA qualified the rubric in 2020 as a medical device development tool for use with CVSS v3.0. The qualification covers scoring vulnerabilities disclosed after a device is on the market, so scoring threats before market is outside it. The rubric is used anyway, so that a threat here and a vulnerability found in the field are scored the same way. Its scoring guidance applies throughout: where a question has more than one valid answer, or none is known, the metric takes its worst value, and each step of an attack chain is scored on its own prerequisites, not on what an earlier step gave the attacker. Every score is given with its vector, as the qualification requires, and with the rubric's answers in its extended-vector codes, so each metric traces to the questions that set it. [tools/cvss.py](../../tools/cvss.py) works out each vector from the rubric's answers, by its decision flows, and each score, exploitability level and verdict from the vector and severity; CI checks that every row here agrees.

Only the base metrics are scored. The temporal metrics, such as exploit code maturity, can only lower a score, so leaving them out takes the worst case FDA's guidance allows before market: a working exploit is assumed to exist.

**Exploitability level.** A base score mixes how easy an attack is with what it does to the device. The acceptance criteria need the first alone, which is CVSS's exploitability subscore, 8.22 × AV × AC × PR × UI, from 0.1 to 3.9:

| Level | Subscore | Typically |
|---|---|---|
| High | 2.5 or more | anyone in radio range or on the network, needing nothing else: AV:A/AC:L/PR:N/UI:N is 2.8 |
| Medium | 1.5 to 2.4 | in radio range, with one hurdle: special conditions (AC:H, 1.6), a paired device's privileges (PR:L, 2.1) or someone's action (UI:R, 2.1) |
| Low | under 1.5 | physical access to the device (AV:P/AC:L/PR:N/UI:N is 0.9), high privileges, or two hurdles |

An attack only gets easier over a device's life: FDA's guidance expects that once a tester has exploited a vulnerability, attackers' ability to exploit it grows. So a threat rated Low or Medium only because an attack is hard today (AC:H) is reviewed with every [vulnerability assessment](vulnerabilities.md).

**Severity of patient harm.** The five levels FDA's postmarket guidance gives, from ISO 14971, and a sixth for threats that harm no patient's health:

| Severity | Harm |
|---|---|
| Catastrophic | results in patient death |
| Critical | results in permanent impairment or life-threatening injury |
| Serious | results in injury or impairment requiring professional medical intervention |
| Minor | results in temporary injury or impairment not requiring professional medical intervention |
| Negligible | inconvenience or temporary discomfort |
| None | no harm to health: the threat discloses data or hides other attacks, and that harm is stated instead |

Each threat takes the worst harm it could credibly cause in either use environment, a hospital ward or the patient's home ([system description](system.md)).

**Acceptance criteria.** A risk is *controlled* (acceptable) or *uncontrolled* (unacceptable), a binary decision as FDA's guidance asks, from the threat's exploitability level and severity:

| Exploitability | None | Negligible | Minor | Serious | Critical | Catastrophic |
|---|---|---|---|---|---|---|
| High | controlled | controlled | controlled | **uncontrolled** | **uncontrolled** | **uncontrolled** |
| Medium | controlled | controlled | controlled | controlled | **uncontrolled** | **uncontrolled** |
| Low | controlled | controlled | controlled | controlled | controlled | controlled |

Two rules override the table. A vulnerability in a component that's listed in CISA's Known Exploited Vulnerabilities catalog is uncontrolled whatever its level, since FDA's guidance says those should be designed out. And an uncontrolled risk needs a control, or a stated reason why the device can't have one; it's never accepted as it stands.

**Before and after controls.** *Before* is the device the threat model describes: the Phase 2 baseline, with no security controls. *After* counts only controls that are built, and says for each which metric it changes and why. A planned control, such as the debug-port lock (C-11), doesn't count until it's built. Threats against elements that aren't built yet, such as the backend, are scored against their design and marked so. A threat with no control keeps its score from before. Where the controls mean a threat now needs a key they keep secret, what's left is the way to that key, so the threat is scored for that way, as the rubric directs scoring the underlying vulnerability. An oximeter's own keys can be read out with the device in hand until the debug-port lock is built; the release key is on no device, so the way to it is T-35's; and the backend's masters are the backend's own threats.

**To the safety risk assessment.** FDA's guidance treats security and safety risk management as separate but connected processes. A threat whose harm is a hazardous situation, such as an alarm that doesn't sound, a wrong reading shown or monitoring that stops unnoticed, passes to the safety risk assessment as one more cause of that hazardous situation, with its exploitability after controls, and ISO 14971's analysis takes it from there. This project has no safety risk file, so the assessment lists the threats that would pass, with the hazardous situation each would pass as.

**Unresolved anomalies.** Known defects left unfixed are assessed for their security impact and their CWE, as FDA's guidance asks: the oximeter's readings in noise ([test report](test-report.md#open-items)) and the undefined behavior fuzzing found in Zephyr's MCUmgr ([vulnerability assessment](vulnerabilities.md#found-by-fuzzing)).

## Before controls

The device the [threat model](threat-model.md) describes: the Phase 2 baseline, with no security controls. Each threat's notes give the reasons for the metrics and the severity that aren't obvious from the threat itself, then the rubric's answers. Q5.1 of the Attack Vector questions has no codes in the rubric, so its answer is given in words. A threat marked *design* is against an element that isn't built yet, and is scored against its design.

### The alarm limits

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-01 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-02 | `CVSS:3.0/AV:A/AC:H/PR:N/UI:N/S:C/C:H/I:H/A:N` | 8.0 | 1.6 Medium | Catastrophic | **uncontrolled** |
| T-03 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-04 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:L/A:N` | 4.3 | 2.8 High | None | controlled |
| T-05 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 6.5 | 2.8 High | None | controlled |
| T-06 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 5.3 | 0.9 Low | Serious | controlled |

**T-01**, a device that isn't the gateway writes the limits. *Attack vector*: Bluetooth LE isn't an IP protocol, and the oximeter's radio reaches tens of metres, further with a directional antenna (ADV-1). The rubric rates a wireless link as Local only when its range is about 10 feet or less; it names Bluetooth LE as an example, but the worst case holds when answers conflict, so Adjacent. *Privileges*: the baseline has no authorization model, so every central is the same. *Scope*: the central station's alarms, the system's essential performance, depend on the oximeter's alarm state, which the rubric counts as a changed scope. *Integrity*: the limits are the monitoring function's settings. *Severity*: a falling SpO2 raises no alarm at the bedside or at the central station, and at home the patient may be alone, so death is credible.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-02**, a man in the middle changes limits in transit. *Attack complexity*: the rubric rates a man in the middle as High. *User interaction*: the attacker waits for a clinician's change, but that's the system's normal use, not a victim's action; FIRST scores its own man-in-the-middle example, CVE-2014-0224, the same way. *Confidentiality*: it reads the limits it changes, as T-05. The rest as T-01.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:N/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-03**, a captured write is replayed. Without authentication, a replayed write needs nothing a fresh one doesn't, so it scores as T-01. The threat differs from T-01 only once writes are signed.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-04**, limits change with no record. The oximeter's side: a change through the air leaves no record of who made it. *Integrity*: the missing record is workflow data, not monitoring data. *Severity*: none to health; the harm is that T-01 to T-03 go unnoticed. The backend's records are T-30's.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:L/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:Y/XAH:N/XAL:N`

**T-05**, anyone in range reads the limits. *Confidentiality*: limits are settings, not readings, so Low as monitoring data; but a lowered limit can reveal a condition, such as a COPD patient's, and an attacker in range can see who wears the device, so the worst case makes them health data about a known person. *Severity*: none to health; the harm is to privacy.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-06**, a reset restores the default limits. *Attack vector*: the attacker pulls the battery, through a human-user interface (Q5.1). *Scope and integrity*: as T-01. *Availability*: the restart interrupts readings for seconds, which the rubric counts as no impact. *Severity*: the defaults still alarm below 90% and outside 50 to 120 bpm, so a patient whose clinician set tighter limits is alarmed later, not never.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

### Readings and alarms over Bluetooth

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-07 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 6.5 | 2.8 High | None | controlled |
| T-08 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-09 | `CVSS:3.0/AV:A/AC:H/PR:N/UI:N/S:C/C:H/I:H/A:H` | 8.3 | 1.6 Medium | Catastrophic | **uncontrolled** |
| T-10 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-11 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-12 | `CVSS:3.0/AV:A/AC:H/PR:N/UI:N/S:C/C:H/I:H/A:H` | 8.3 | 1.6 Medium | Catastrophic | **uncontrolled** |
| T-13 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 6.5 | 2.8 High | None | controlled |

**T-07**, anyone in range subscribes to or sniffs the readings. *Confidentiality*: readings of a person the attacker can see. *Severity*: none to health; the harm is to privacy.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-08**, a fake oximeter takes the real one's place. *Attack complexity*: an attacker advertising fast is found first whenever the gateway scans, at start-up or after any disconnection. *Scope*: the vulnerable component is the gateway, which connects to anything advertising the service, and the central station's alarms depend on what it relays. *Impact*: the fake's readings replace the patient's, which no longer reach clinicians, and the fake receives any limits the gateway sends, as T-05. *Severity*: false normal readings hide a decline, as T-01.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-09**, a man in the middle alters readings or alarms. *Attack complexity*: a man in the middle, as T-02. *Impact*: it reads what it relays, as T-07, and can change or drop it. *Severity*: a suppressed alarm, as T-01.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:N/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-10**, jamming. *Attack vector*: a jammer needn't use the protocol at all; answered as an attack over the wireless channel, which gives Adjacent rather than Local, the worst case. *Severity*: at baseline the gateway raises no alarm when readings stop, so the central station is silent and only the oximeter's LED alarms; at home the patient may be asleep and alone.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-11**, an attacker's device holds the oximeter's only connection. *Attack complexity*: the oximeter advertises whenever it isn't connected, and an attacker's central can connect at its first advertisement. *Severity*: as T-10.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-12**, a gateway connects to another bed's oximeter. *Attack complexity*: the other bed's oximeter is connected to its own gateway, so an attacker must first break both links and then win the race to reconnect. *Integrity*: readings recorded against the wrong patient are health data made to refer to another person (Q1.I High). *Severity*: the wrong patient is treated, and the right one's decline goes unseen.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:N/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:H/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-13**, the oximeter is detected and followed. *Confidentiality*: that a known person uses a medical device, and where they go. *Severity*: none to health.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

### The oximeter

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-14 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 7.6 | 0.9 Low | Catastrophic | controlled |
| T-15 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 4.6 | 0.9 Low | None | controlled |
| T-16 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-17 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-18 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:L/A:N` | 4.3 | 2.8 High | None | controlled |
| T-19 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:H` | 7.3 | 0.9 Low | Catastrophic | controlled |
| T-20 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 4.6 | 0.9 Low | None | controlled |

**T-14**, malicious firmware through the debug port. *Attack vector*: physical; the debug port is inside the case, not directly accessible (Q5.1). *Impact*: the attacker's firmware can do anything the oximeter can. *Severity*: as T-01, persisting through resets. Needing the device in hand keeps exploitability Low, which the acceptance criteria accept.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-15**, firmware and memory read out. *Attack vector*: as T-14. *Confidentiality*: the readings in memory. At baseline the oximeter holds no key (AS-7), so nothing read out helps attack other devices. *Severity*: none to health.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-16**, a crafted packet exploits a bug. Scored for a bug that exists and, by the rubric's rule that the attacker has full knowledge, is known. *Impact*: code running on the oximeter can do anything it can.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-17**, a crash or hang stops monitoring. Scored for a crash an attacker in range can trigger at will. *Severity*: with no watchdog and no alarm at the gateway, monitoring stops silently until someone notices, as T-10.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-18**, no security events are recorded: as T-04, for every attack on the device.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:L/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:Y/XAH:N/XAL:N`

**T-19**, a fake sensor, or false signals on its bus. *Attack vector*: the case must be opened, so not directly accessible (Q5.1). *Impact*: what the sensor reports is what the oximeter measures, so false readings or none. *Severity*: as T-08.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-20**, the console logs readings and limit changes. *Attack vector*: a serial adapter plugged into the console's port, a human-user interface (Q5.1). *Severity*: none to health.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

### The bedside gateway

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-21 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 5.3 | 0.9 Low | Catastrophic | controlled |
| T-22 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-23 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 8.6 | 3.9 High | Catastrophic | **uncontrolled** |
| T-24 (design) | `CVSS:3.0/AV:N/AC:H/PR:N/UI:N/S:U/C:H/I:N/A:N` | 5.9 | 2.2 Medium | None | controlled |

**T-21**, someone at the gateway sets limits. *Attack vector*: the hub's shell, on its serial port, a human-user interface (Q5.1). *Scope*: the vulnerable component is the gateway and the impacted one the oximeter, as in the rubric's example of a programmer that reprograms a pump. *Severity*: as T-01.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-22**, a compromised gateway. Scored as the step after the compromise, as the rubric directs for attack chains: the attacker runs code on the gateway with its privileges. *Privileges*: the gateway's software runs with system-level privileges. *Scope*: what it relays reaches the oximeter and the backend. *Severity*: as T-01, for the gateway's patient.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-23**, the gateway goes offline. *Attack vector*: over IP, which the rubric rates Network even inside a home or hospital network. *Privileges*: the gateway has users, but stopping it needs none. *Severity*: as T-10.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-24**, health data read from the gateway. *Attack complexity*: reading another device's traffic on a switched or encrypted network takes a man in the middle; reading its storage takes the device in hand, which scores lower. *Severity*: none to health.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:N/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

### The network path and the backend

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-25 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 10.0 | 3.9 High | Catastrophic | **uncontrolled** |
| T-26 (design) | `CVSS:3.0/AV:N/AC:H/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.0 | 2.2 Medium | Catastrophic | **uncontrolled** |
| T-27 (design) | `CVSS:3.0/AV:N/AC:L/PR:L/UI:N/S:C/C:H/I:H/A:N` | 9.6 | 3.1 High | Catastrophic | **uncontrolled** |
| T-28 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-29 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 7.5 | 3.9 High | None | controlled |
| T-30 (design) | `CVSS:3.0/AV:N/AC:L/PR:L/UI:N/S:U/C:N/I:L/A:N` | 4.3 | 2.8 High | None | controlled |
| T-31 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 8.6 | 3.9 High | Catastrophic | **uncontrolled** |

**T-25**, a fake backend or a fake gateway. Without authentication between them, a fake gateway needs nothing but the backend's address, the worse of the two directions. *Integrity*: invented readings recorded against a real patient (Q1.I High). *Severity*: forged limits, as T-01.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:H/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-26**, traffic altered or read in transit. *Attack complexity*: a man in the middle. *Confidentiality*: a hospital network carries many gateways' traffic, so 500 patients or more (Q1.1.C). *Severity*: as T-09, for many patients.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:N/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-27**, a stolen clinician login. Scored as the step after the theft: the attacker holds a clinician's account. *Privileges*: a clinician's, not an administrator's. *Scope*: the backend reaches the oximeters' limits, as T-21's programmer does. *Severity*: as T-01, for every patient the account reaches.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:N/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-28**, a compromised backend: as T-22, for every patient. *Integrity*: records can be made to refer to another patient (Q1.I High).

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:H/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-29**, the backend's database is breached. *Severity*: none to health, though every patient's health data is disclosed: a privacy harm on the scale that brings breach notification (Q1.1.C), which this assessment of patient harm doesn't weigh.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-30**, changes aren't attributed: as T-04, at the backend. *Privileges*: whoever acts unattributed holds an account.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:N/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:L/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:Y/XAH:N/XAL:N`

**T-31**, the backend goes down. *Severity*: no remote alarms for any patient, as T-10.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

### Updates, signing and the supply chain

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-33 (design) | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-34 (design) | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-35 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-36 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-37 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:N` | 0.0 | 0.9 Low | None | controlled |

T-32, that the oximeter can't be updated in the field, isn't scored: it isn't an attack itself, but the reason the others can't be fixed. It raises the exploitability of T-16 and T-36 over the device's life, which the method's lifecycle rule covers, and the [updatability view](updatability.md) answers it.

**T-33**, a forged image through the update path. The baseline has no update path, so scored against the design without its controls: the oximeter takes any image over Bluetooth, which scores higher than the same image pushed by a man in the middle on the network. *Severity*: as T-14, on every device.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-34**, an old image installed again. Without signing, an old image needs nothing a forged one doesn't, so it scores as T-33. The threat differs only once images are signed.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-35**, the signing key is stolen or misused. Scored as the step after the theft, as T-22: someone holds the key on the build system. *Confidentiality*: firmware every device runs reaches every patient (Q1.1.C). *Severity*: as T-33.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-36**, a flaw in a third-party component: scored as T-16, which it can be.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-37**, a device passed on with the last patient's data. At baseline nothing outlives a reset (T-06), so there's nothing to find: no impact, and a score of 0. The threat appears with the controls that keep things in flash, and is scored after them.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:N/XAL:N`

## After controls

With the controls in [controls.md](controls.md) that are built. Each note says which control changed which metric; a threat no built control stands against keeps its score from before.

### The alarm limits

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-01 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 5.3 | 0.9 Low | Catastrophic | controlled |
| T-02 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:N` | 7.3 | 0.9 Low | Catastrophic | controlled |
| T-03 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 5.3 | 0.9 Low | Catastrophic | controlled |
| T-04 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:N/I:L/A:N` | 2.4 | 0.9 Low | None | controlled |
| T-05 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 4.6 | 0.9 Low | None | controlled |
| T-06 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:H` | 7.3 | 0.9 Low | Catastrophic | controlled |

**T-01**, C-01 and C-04 keep an unpaired device off the link, and C-03 refuses limits without a tag under this oximeter's limits key (C-06), fresh for its challenge. What's left is the key, which can be read out through the open debug port with the device in hand: *attack vector* Physical, not directly accessible (Q5.1). With the key, the rest could be done over the air later. The backend's master is T-28's.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-02**, C-01's out-of-band pairing stops a man in the middle who lacks the pairing key, and C-03's tag covers the limits: as T-01, the keys are what's left.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-03**, C-03 replaces the challenge after every attempt, so a captured command never verifies again: what's left is making a fresh one, as T-01.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-04**, C-13 records every change over the air with the central that made it, C-14 tags each record and C-15 shows one held back. A change with no record now needs flash written through the debug port. A record names a central, not a person; that's T-30's.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:L/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:Y/XAH:N/XAL:N`

**T-05**, C-04 opens the limits only to the paired gateway's level-4 link: as T-01, the pairing key is what's left.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-06**, C-16 keeps the limits through a reset. What's left is C-12's reset for a new patient: anyone holding the device can hold Button 1 for 5 s, a human-user interface (Q5.1), which restores the defaults and shuts the gateway out until the backend allows a new pairing. *Severity*: the gateway notices the silence (C-19) but raises no alarm yet, so as T-10, worse than before; needing the device in hand keeps it Low.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

### Readings and alarms over Bluetooth

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-07 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 4.6 | 0.9 Low | None | controlled |
| T-08 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 7.6 | 0.9 Low | Catastrophic | controlled |
| T-09 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 7.6 | 0.9 Low | Catastrophic | controlled |
| T-10 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-11 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-12 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 7.6 | 0.9 Low | Catastrophic | controlled |
| T-13 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 6.5 | 2.8 High | None | controlled |

**T-07**, C-04 opens the readings only to the paired gateway, over C-01's encrypted link: as T-05.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-08**, C-01: a gateway pairs only with data sealed under the oximeter's own pairing key, so a fake needs a real oximeter's key, read out through its debug port, as T-01. No test plays a fake oximeter yet ([test report](test-report.md#open-items)).

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-09**, C-01's link is encrypted and authenticated, so a man in the middle needs its keys: as T-01.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-10**, No control yet: C-19 notices the silence within half a second, but raises no alarm. As before.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-11**, No control yet. As before.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-12**, C-01 and C-02: a gateway pairs only with an oximeter whose seal the backend opens for it, so another bed's needs that oximeter's pairing key, as T-08. Staff pairing or resetting the wrong device is a use error, for the safety risk assessment.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:H/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-13**, No control yet. As before.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

### The oximeter

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-14 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 7.6 | 0.9 Low | Catastrophic | controlled |
| T-15 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:N/A:N` | 5.3 | 0.9 Low | None | controlled |
| T-16 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-17 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 7.4 | 2.8 High | Catastrophic | **uncontrolled** |
| T-18 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:N/I:L/A:N` | 2.4 | 0.9 Low | None | controlled |
| T-19 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:H` | 7.3 | 0.9 Low | Catastrophic | controlled |
| T-20 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 4.6 | 0.9 Low | None | controlled |

**T-14**, No control yet: C-08 checks the firmware at every boot, but through the debug port MCUboot itself can be replaced. As before.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-15**, No control yet, and there's more to read: the oximeter now holds its keys (C-06) and keeps its log and limits in flash. *Scope*: the keys let the attacker pass as this oximeter to its gateway and backend. Using them is scored under the threats they serve, T-01 to T-12.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:H/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-16**, C-10's parsers are fuzzed, and C-04 leaves an unpaired device only the device ID and the pairing data. But Zephyr's Bluetooth stack beneath them is reached before pairing, and a bug there scores as before: CVSS rates what a known bug allows, not how likely one is. C-08 keeps it from persisting, and C-22 and C-20 find and fix the known ones.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-17**, C-18 restarts a hung oximeter within 2 s, and C-16 keeps its limits through the restart, but a crash an attacker can trigger repeats at will, so monitoring stays down. As before.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-18**, As T-04.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:L/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:Y/XAH:N/XAL:N`

**T-19**, No control yet. As before.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-20**, No control yet. As before.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

### The bedside gateway

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-21 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:N/I:H/A:N` | 5.3 | 0.9 Low | Catastrophic | controlled |
| T-22 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-23 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 8.6 | 3.9 High | Catastrophic | **uncontrolled** |
| T-24 (design) | `CVSS:3.0/AV:N/AC:H/PR:N/UI:N/S:U/C:H/I:N/A:N` | 5.9 | 2.2 Medium | None | controlled |

**T-21**, C-03: the gateway relays limit commands but can't make one, since only the backend holds the master. What's left is the oximeter's limits key, as T-01. The hub here stands in for the backend too and holds its masters, so its shell can still set limits; a real gateway holds none.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-22**, C-03, C-05, C-14 and C-15 stop a compromised gateway forging limits, keeping an update, and editing or holding back the log. Readings and alarms aren't signed end to end, so it can still alter or drop them: the vector is unchanged.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-23**, The gateway isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

**T-24**, The gateway isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:N/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

### The network path and the backend

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-25 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 10.0 | 3.9 High | Catastrophic | **uncontrolled** |
| T-26 (design) | `CVSS:3.0/AV:N/AC:H/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.0 | 2.2 Medium | Catastrophic | **uncontrolled** |
| T-27 (design) | `CVSS:3.0/AV:N/AC:L/PR:L/UI:N/S:C/C:H/I:H/A:N` | 9.6 | 3.1 High | Catastrophic | **uncontrolled** |
| T-28 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-29 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 7.5 | 3.9 High | None | controlled |
| T-30 (design) | `CVSS:3.0/AV:N/AC:L/PR:L/UI:N/S:U/C:N/I:L/A:N` | 4.3 | 2.8 High | None | controlled |
| T-31 (design) | `CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:C/C:N/I:N/A:H` | 8.6 | 3.9 High | Catastrophic | **uncontrolled** |

**T-25**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:H/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-26**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:N/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-27**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:N/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:H/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:N/XAL:N`

**T-28**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:H/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-29**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

**T-30**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:N/XUI:N/XS:N/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:L/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:Y/XAH:N/XAL:N`

**T-31**, The backend isn't built yet, so nothing stands against this: as before.

Rubric answers: `XAVN:Y/XAVT:Y/XAVW:NA/XAVR:NA/XAVP:NA/XACL:Y/XPRL:Y/XPRZ:N/XPRS:NA/XUI:N/XS:Y/XCP:N/XCPM:NA/XIP:N/XAP:N/XCD:N/XID:N/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:N/XCL:N/XIH:N/XIL:N/XAH:Y/XAL:NA`

### Updates, signing and the supply chain

| Threat | Vector | Score | Exploitability | Severity | Risk |
|---|---|---|---|---|---|
| T-33 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-34 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 7.6 | 0.9 Low | Catastrophic | controlled |
| T-35 (design) | `CVSS:3.0/AV:L/AC:L/PR:H/UI:N/S:C/C:H/I:H/A:H` | 8.2 | 1.5 Medium | Catastrophic | **uncontrolled** |
| T-36 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H` | 9.6 | 2.8 High | Catastrophic | **uncontrolled** |
| T-37 | `CVSS:3.0/AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:N/A:N` | 4.6 | 0.9 Low | None | controlled |

T-32 is answered by C-20: the oximeter can now be updated over Bluetooth.

**T-33**, C-08, C-09 and C-20: the oximeter runs and keeps only an image signed with the release key, which is on no device (the key check). A forged image needs that key, so this takes T-35's way to it, and its score.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-34**, C-21: MCUboot erases an update whose security counter is below the running image's. What's left is the debug port, through which, as MCUboot's documentation notes, the check can be bypassed.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-35**, No control yet: one release key per checkout, never committed, and the offline signing service is planned. As before.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:N/XACL:Y/XPRL:Y/XPRZ:Y/XPRS:Y/XUI:N/XS:Y/XCP:H/XCPM:Y/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-36**, C-22 finds the known vulnerabilities in every component each week, and C-20 delivers the fix. Until it's delivered, a known one scores as T-16.

Rubric answers: `XAVN:Y/XAVT:N/XAVW:Y/XAVR:N/XAVP:NA/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:Y/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:H/XID:H/XAD:H/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:Y/XIL:NA/XAH:Y/XAL:NA`

**T-37**, C-12's reset deletes the bond and the limits, so the old gateway can't connect again. The security log stays, as evidence, and holds the last patient's limit changes, which anyone holding the device can read through the debug port. Staff forgetting the reset is a use error, for the safety risk assessment.

Rubric answers: `XAVN:N/XAVT:NA/XAVW:NA/XAVR:NA/XAVP:Y/XACL:Y/XPRL:N/XPRZ:NA/XPRS:NA/XUI:N/XS:N/XCP:H/XCPM:N/XIP:N/XAP:N/XCD:L/XID:N/XAD:N/XCT:N/XIT:N/XAT:N/XCW:N/XIW:N/XAW:N/XCS:N/XIS:N/XAS:N/XCO:N/XIO:N/XAO:N/XCH:Y/XCL:NA/XIH:N/XIL:N/XAH:N/XAL:N`

## Risk that's left

With the controls built, 22 of the 36 threats scored are controlled and 14 aren't: five on the oximeter as built, and nine against the gateway and the backend, which aren't built yet. None of the components' known vulnerabilities ([vulnerability assessment](vulnerabilities.md)) is in CISA's Known Exploited Vulnerabilities catalog, as of its 28 October 2026 edition, so that rule adds none.

Each uncontrolled risk needs a control, or a reason it can't have one:

| Threats | What they need |
|---|---|
| T-10, a jammed link | It can't be prevented. The gateway and the backend need a technical alarm within 2 s of readings stopping, so the loss isn't silent; C-19 already notices it. With that alarm built, the severity is rated again. |
| T-11, a held connection | The oximeter should drop a central that doesn't reach security level 4 within a few seconds, and keep a connection free for its bonded gateway; and the alarm of T-10. |
| T-16 and T-36, a flaw in the Bluetooth stack or another component | No control rules out a bug in third-party code, so this can't be designed out. Against it: fuzzing of the oximeter's own parsers (C-10), the weekly check for known vulnerabilities (C-22), signed updates to deliver a fix (C-20), and secure boot, so an exploit doesn't survive a restart (C-08). Still to do: fuzz the Bluetooth protocols Zephyr handles before pairing, and turn off those the oximeter doesn't use. |
| T-17, a crash an attacker repeats | As T-16, and the alarm of T-10, so the restarts are seen. |
| T-22, a compromised gateway alters readings | Readings tagged by the oximeter, as its log records are (C-14), so the backend can tell an altered one; and the alarm of T-23 for those dropped. |
| T-23 and T-31, the gateway or the backend down | A technical alarm at the backend when a gateway goes quiet, and at the gateway when the backend does; a backend run with redundancy. |
| T-25 and T-26, a fake backend or gateway, or traffic altered in transit | Mutual authentication and encryption between gateway and backend, such as TLS with a certificate on each side. |
| T-27, a stolen clinician login | Multi-factor sign-in, and each clinician reaching only their own patients. |
| T-28, a compromised backend | The masters held in a hardware security module, so a compromised server can ask for signatures but can't take the keys; least privilege and monitoring. |
| T-33 and T-35, a stolen release key | The offline signing service, with access controlled and logged. MCUboot can't be updated, so a release key compromised after devices ship can be replaced only with every device in hand. |

**Conclusion.** The residual risk isn't acceptable yet: fourteen risks are uncontrolled. On the oximeter, T-11 needs a firmware change, and T-10 and T-17 the technical alarm that comes with the gateway and the backend. T-16 and T-36 can't be designed out; they rest on the reasons above and on monitoring and updates after release. The other nine come with the gateway and the backend, and are scored again when those are built. No risk needs the debug-port lock (C-11) to be controlled under these acceptance criteria, but most of the remote threats now come down to reading the oximeter's keys out with it in hand, and the lock is what closes that.

## To the safety risk assessment

Security and safety meet at three hazardous situations: an alarm that doesn't sound at the limits the clinician set, a reading shown that's wrong or another patient's, and monitoring that stops with no alarm at the central station. Each threat that can cause one passes to the safety risk assessment as one of its causes, with its exploitability after controls. That assessment isn't part of this project.

| Threat | Hazardous situations it can cause | Exploitability after controls |
|---|---|---|
| T-01, T-02, T-03, T-21 | alarm not at the clinician's limits | Low |
| T-06 | alarm not at the clinician's limits; silent loss of monitoring | Low |
| T-08, T-12 | wrong reading | Low |
| T-09, T-19 | wrong reading; silent loss of monitoring | Low |
| T-14, T-34 | all three | Low |
| T-10, T-11, T-17 | silent loss of monitoring | High |
| T-16, T-36 | all three | High |
| T-22, T-26 | wrong reading; silent loss of monitoring | Medium, design |
| T-23, T-31 | silent loss of monitoring | High, design |
| T-25 | alarm not at the clinician's limits; wrong reading | High, design |
| T-27 | alarm not at the clinician's limits | High, design |
| T-28, T-33, T-35 | all three | Medium, design |

Two use errors turned up here, for the safety risk assessment too: staff pairing a gateway with the wrong bed's oximeter (T-12), and staff forgetting the reset before the next patient (T-37), after which the old gateway still connects.

## Unresolved anomalies

Known defects left unfixed, each assessed for what an attacker could make of it, with its CWE, as FDA's guidance asks.

| Anomaly | CWE | Security impact |
|---|---|---|
| In noise, the pulse rate reads 3 to 5 bpm high, and once half the rate, before the rhythm check refuses the signal ([test report](test-report.md#open-items)) | CWE-682, incorrect calculation | Noise reaches the analysis only through the sensor: light, movement or signals on its bus, all with the device in hand, as T-19. Nothing over the air adds noise. Its wrong readings are for the safety risk assessment. |
| Zephyr's MCUmgr calls decoders through a function pointer of another type ([vulnerability assessment](vulnerabilities.md#found-by-fuzzing)) | CWE-686, function call with incorrect argument type | None: the call is the same whatever a request holds, and only the paired gateway reaches MCUmgr (C-04). |
| Zephyr's MCUmgr shifts the flash's erased byte into the sign bit of an `int` (the same) | CWE-190, integer overflow | None: the byte is the flash's, not a gateway's. |
| A second update can time out at the gateway's stand-in while the oximeter erases the old update's slot; seen once in a probe, never in the tests ([test report](test-report.md#open-items)) | none fits: a timing fault between two devices | Only the paired gateway sends updates, and a failed one leaves the running image in place (C-20). A fix arrives later, which lengthens the time T-36 stays open. |
