# Risk assessment

How exploitable each threat in the [threat model](threat-model.md) is and how badly it could harm a patient, before the controls in [controls.md](controls.md) and after them, and whether the risk that's left is acceptable. FDA's premarket guidance asks for the methods used to score risk before and after mitigation, the acceptance criteria, and how security risks pass to the safety risk assessment. This document gives them first, then the scores.

> Portfolio exercise, not a medical device.

## Method

**Exploitability, not probability.** FDA's guidance says the likelihood of a cyberattack can't be estimated from history or modelling the way a safety risk's can, so security risk is rated by exploitability: what an attacker needs to carry a threat out.

**CVSS v3.0, with MITRE's rubric.** Each threat gets a CVSS v3.0 base vector, its metrics chosen by answering the questions in MITRE's *Rubric for Applying CVSS to Medical Devices*, version 0.12.04. FDA's postmarket guidance names CVSS v3.0 as one way to rate exploitability, and FDA qualified the rubric in 2020 as a medical device development tool for use with CVSS v3.0. The qualification covers scoring vulnerabilities disclosed after a device is on the market, so scoring threats before market is outside it. The rubric is used anyway, so that a threat here and a vulnerability found in the field are scored the same way. Its scoring guidance applies throughout: where a question has more than one valid answer, or none is known, the metric takes its worst value, and each step of an attack chain is scored on its own prerequisites, not on what an earlier step gave the attacker. Every score is given with its vector, as the qualification requires, and with the rubric's answers in its extended-vector codes, so each metric traces to the questions that set it. [tools/cvss.py](../../tools/cvss.py) works out each score, exploitability level and verdict from the vector and severity, and CI checks that every row here agrees.

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

**Before and after controls.** *Before* is the device the threat model describes: the Phase 2 baseline, with no security controls. *After* counts only controls that are built, and says for each which metric it changes and why. A planned control, such as the debug-port lock (C-11), doesn't count until it's built. Threats against elements that aren't built yet, such as the backend, are scored against their design and marked so. A threat with no control keeps its score from before.

**To the safety risk assessment.** FDA's guidance treats security and safety risk management as separate but connected processes. A threat whose harm is a hazardous situation, such as an alarm that doesn't sound, a wrong reading shown or monitoring that stops unnoticed, passes to the safety risk assessment as one more cause of that hazardous situation, with its exploitability after controls, and ISO 14971's analysis takes it from there. This project has no safety risk file, so the assessment lists the threats that would pass, with the hazardous situation each would pass as.

**Unresolved anomalies.** Known defects left unfixed are assessed for their security impact and their CWE, as FDA's guidance asks: the oximeter's readings in noise ([test report](test-report.md#open-items)) and the undefined behavior fuzzing found in Zephyr's MCUmgr ([vulnerability assessment](vulnerabilities.md#found-by-fuzzing)).
