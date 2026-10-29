# Security documentation

The cybersecurity documentation FDA's premarket guidance (February 2026 edition) asks for, written for this project as a manufacturer would write it. Each document separates what's built from what's planned, and every claim about a security control points to the code or test behind it.

> Portfolio exercise, not a medical device.

| Document | Covers | Submission item | Status |
|---|---|---|---|
| [System description](system.md) | intended use, use environments, elements, interfaces, assets, adversaries, trust boundaries, global system view | architecture: global system view; the threat model's inputs | draft |
| [Threat model](threat-model.md) | data flows, and STRIDE threats for every element and flow | threat model | draft |
| Attack trees | the top threats in depth | threat model | Phase 3 |
| [Updatability and patchability view](updatability.md) | the update path end to end, verification on each device, interrupted and failed updates, third-party fixes | architecture views | draft |
| Other architecture views | multi-patient harm, security use cases | architecture views | Phase 3 |
| [Risk assessment](risk-assessment.md) | each threat's exploitability (MITRE's CVSS rubric for medical devices) and severity of patient harm, before and after controls | cybersecurity risk assessment | done for the system as built; the gateway's and backend's threats scored again when they're built |
| [Security controls](controls.md) | each control's requirement, implementation, threats and tests, by FDA's categories | security architecture | done but for the debug-port lock |
| [SBOM](sbom.md) | every software component, its version, supplier, identifiers and support, made from each build | SBOM | done |
| [Vulnerability assessment](vulnerabilities.md) | each component's known vulnerabilities and what they mean for this device, checked against NVD every week | vulnerability assessment | done |
| [Test report](test-report.md) | how each kind of testing FDA names was done, the result, the defects found and what's open | cybersecurity testing | done but for hardware and penetration testing |
| Management plan and labeling | postmarket vulnerability handling; what users are told | management plan; labeling | Phase 7 |
| [Traceability](traceability.md) | threat → control → tests, checked on every push | traceability | done for the controls built |

## IDs

Anything another document refers to has an ID: **E** elements, **I** interfaces, **AS** assets, **ADV** adversaries, **Z** trust zones, **TB** trust boundaries, **UC** use cases. Later documents add **T** threats and **C** controls. Tests go by their names.
