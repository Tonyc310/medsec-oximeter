#!/usr/bin/env python3
"""Score each threat in the risk assessment from its CVSS v3.0 vector and severity.

Reads each table in docs/security/risk-assessment.md that has Vector and Severity columns, and
fills in each row's base score, its exploitability subscore and level, and whether the risk is
controlled under the method's acceptance criteria. Each row's vector must be the one its threat's
answers to MITRE's rubric give, in the same section. Fails, naming the row, for a vector, answers
or a severity it can't read or that disagree. With --check, it also fails for any row that isn't
up to date.
"""

import argparse
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RISK_FILE = "docs/security/risk-assessment.md"
PREFIX = "CVSS:3.0/"

# The base metrics' weights, from the CVSS v3.0 specification. Privileges count for more when an
# attack changes scope, since they're then privileges over something else.
ATTACK_VECTOR = {"N": 0.85, "A": 0.62, "L": 0.55, "P": 0.2}
ATTACK_COMPLEXITY = {"L": 0.77, "H": 0.44}
PRIVILEGES = {"U": {"N": 0.85, "L": 0.62, "H": 0.27}, "C": {"N": 0.85, "L": 0.68, "H": 0.5}}
USER_INTERACTION = {"N": 0.85, "R": 0.62}
IMPACT = {"H": 0.56, "L": 0.22, "N": 0.0}
BASE_METRICS = {
    "AV": tuple(ATTACK_VECTOR),
    "AC": tuple(ATTACK_COMPLEXITY),
    "PR": tuple(PRIVILEGES["U"]),
    "UI": tuple(USER_INTERACTION),
    "S": tuple(PRIVILEGES),
    "C": tuple(IMPACT),
    "I": tuple(IMPACT),
    "A": tuple(IMPACT),
}

# The rubric's questions, by their extended-vector codes, and the answers each allows. Whether
# PHI can be read has no Low: the rubric treats a privacy breach as binary.
YES_NO = ("Y", "N", "U")
SKIPPABLE = (*YES_NO, "NA")
CONFIDENTIALITY = ("XCP", "XCD", "XCT", "XCW", "XCS", "XCO")
INTEGRITY = ("XIP", "XID", "XIT", "XIW", "XIS", "XIO")
AVAILABILITY = ("XAP", "XAD", "XAT", "XAW", "XAS", "XAO")
SUMMARIES = (("XCH", "XCL"), ("XIH", "XIL"), ("XAH", "XAL"))
QUESTIONS = {
    "XAVN": YES_NO,
    **dict.fromkeys(("XAVT", "XAVW", "XAVR", "XAVP"), SKIPPABLE),
    "XACL": YES_NO,
    "XPRL": YES_NO,
    **dict.fromkeys(("XPRZ", "XPRS"), SKIPPABLE),
    "XUI": YES_NO,
    "XS": YES_NO,
    **dict.fromkeys(CONFIDENTIALITY + INTEGRITY + AVAILABILITY, ("H", "L", "N", "U")),
    "XCP": ("H", "N", "U"),
    "XCPM": SKIPPABLE,
    **dict.fromkeys(("XCH", "XIH", "XAH"), ("Y", "N")),
    **dict.fromkeys(("XCL", "XIL", "XAL"), ("Y", "N", "NA")),
}
RUBRIC_ANSWERS = re.compile(r"^Rubric answers: `([^`]+)`")
THREAT_NOTE = re.compile(r"^\*\*(T-\d+)\*\*")
THREAT = re.compile(r"T-\d+")

SEVERITIES = ("None", "Negligible", "Minor", "Serious", "Critical", "Catastrophic")
# The acceptance criteria: the least severe harm each exploitability level makes uncontrolled.
UNCONTROLLED_FROM = {"High": "Serious", "Medium": "Critical", "Low": None}


class VectorError(ValueError):
    """A vector that isn't a CVSS v3.0 base vector."""


class AnswerError(ValueError):
    """Answers to the rubric that are malformed, or that don't give the vector they're with."""


def parse(vector: str) -> dict[str, str]:
    """Return the value of each base metric in `vector`, by its abbreviation."""
    if not vector.startswith(PREFIX):
        raise VectorError(f"doesn't start with {PREFIX}")
    metrics: dict[str, str] = {}
    for part in vector.removeprefix(PREFIX).split("/"):
        name, _, value = part.partition(":")
        if name in metrics:
            raise VectorError(f"gives {name} twice")
        if name not in BASE_METRICS:
            raise VectorError(f"has {part}, which isn't a base metric")
        if value not in BASE_METRICS[name]:
            raise VectorError(f"has {part}, which CVSS v3.0 doesn't define")
        metrics[name] = value
    missing = [name for name in BASE_METRICS if name not in metrics]
    if missing:
        raise VectorError(f"has no {missing[0]}")
    return metrics


def parse_answers(text: str) -> dict[str, str]:
    """Return the rubric's answers in an extended vector, by question code."""
    answers: dict[str, str] = {}
    for part in text.split("/"):
        code, _, value = part.partition(":")
        if code in answers:
            raise AnswerError(f"gives {code} twice")
        if code not in QUESTIONS:
            raise AnswerError(f"has {part}, which the rubric doesn't ask")
        if value not in QUESTIONS[code]:
            raise AnswerError(f"has {part}, which the rubric doesn't allow")
        answers[code] = value
    missing = [code for code in QUESTIONS if code not in answers]
    if missing:
        raise AnswerError(f"has no {missing[0]}")
    return answers


# Each metric from its questions, by the rubric's decision flows. An unknown answer takes the
# branch with the higher score, as the rubric directs, and a question the flow doesn't reach is
# ignored, whatever its answer.


def attack_vector(answers: dict[str, str]) -> str:
    """Return the Attack Vector the answers give."""
    if answers["XAVN"] == "N":
        return "P" if answers["XAVP"] == "Y" else "L"
    if answers["XAVN"] == "U" or answers["XAVT"] != "N":
        return "N"
    return "L" if answers["XAVW"] == "Y" and answers["XAVR"] == "Y" else "A"


def attack_complexity(answers: dict[str, str]) -> str:
    """Return the Attack Complexity the answers give."""
    return "H" if answers["XACL"] == "N" else "L"


def privileges_required(answers: dict[str, str]) -> str:
    """Return the Privileges Required the answers give."""
    if answers["XPRL"] == "Y" and answers["XPRZ"] == "Y":
        return {"Y": "H", "N": "L"}.get(answers["XPRS"], "N")
    return "N"


def user_interaction(answers: dict[str, str]) -> str:
    """Return the User Interaction the answers give."""
    return "R" if answers["XUI"] == "Y" else "N"


def scope(answers: dict[str, str]) -> str:
    """Return the Scope the answers give."""
    return "U" if answers["XS"] == "N" else "C"


def impact(answers: dict[str, str], questions: tuple[str, ...]) -> str:
    """Return the impact that the answers to one impact's questions give."""
    given = [answers[question] for question in questions]
    if "H" in given or "U" in given:
        return "H"
    return "L" if "L" in given else "N"


def impacts(answers: dict[str, str]) -> tuple[str, str, str]:
    """Return the Confidentiality, Integrity and Availability impacts the answers give."""
    return (
        impact(answers, CONFIDENTIALITY),
        impact(answers, INTEGRITY),
        impact(answers, AVAILABILITY),
    )


def summaries(answers: dict[str, str]) -> dict[str, str]:
    """Return the answers to the rubric's summary questions that the other answers give."""
    expected = {}
    for (high, low), value in zip(SUMMARIES, impacts(answers), strict=True):
        expected[high] = "Y" if value == "H" else "N"
        expected[low] = "NA" if value == "H" else ("Y" if value == "L" else "N")
    return expected


def vector_from(answers: dict[str, str]) -> str:
    """Return the CVSS v3.0 base vector the answers give."""
    confidentiality, integrity, availability = impacts(answers)
    return (
        f"{PREFIX}AV:{attack_vector(answers)}/AC:{attack_complexity(answers)}"
        f"/PR:{privileges_required(answers)}/UI:{user_interaction(answers)}/S:{scope(answers)}"
        f"/C:{confidentiality}/I:{integrity}/A:{availability}"
    )


def agree(vector: str, text: str) -> None:
    """Raise AnswerError unless the answers in `text` are consistent and give `vector`."""
    answers = parse_answers(text)
    for code, value in summaries(answers).items():
        if answers[code] != value:
            raise AnswerError(f"{code} should be {value}")
    stated = parse(vector)
    derived = parse(vector_from(answers))
    differ = [f"{name}:{value}" for name, value in derived.items() if stated[name] != value]
    if differ:
        raise AnswerError(f"its answers give {', '.join(differ)}")


def round_up(value: float) -> float:
    """Return the smallest number with one decimal place that is at least `value`."""
    # No base vector scores within float error above a tenth, the case CVSS v3.1's integer
    # rounding guards against, so the plain ceiling is exact.
    return math.ceil(value * 10) / 10


def subscore(metrics: dict[str, str]) -> float:
    """Return the exploitability subscore of a vector's metrics, unrounded."""
    return (
        8.22
        * ATTACK_VECTOR[metrics["AV"]]
        * ATTACK_COMPLEXITY[metrics["AC"]]
        * PRIVILEGES[metrics["S"]][metrics["PR"]]
        * USER_INTERACTION[metrics["UI"]]
    )


def base_score(vector: str) -> float:
    """Return the CVSS v3.0 base score of `vector`, from 0.0 to 10.0."""
    metrics = parse(vector)
    unchanged = metrics["S"] == "U"
    combined = 1 - (
        (1 - IMPACT[metrics["C"]]) * (1 - IMPACT[metrics["I"]]) * (1 - IMPACT[metrics["A"]])
    )
    if unchanged:
        impact = 6.42 * combined
    else:
        impact = 7.52 * (combined - 0.029) - 3.25 * (combined - 0.02) ** 15
    if impact <= 0:
        return 0.0
    total = impact + subscore(metrics)
    return round_up(min(total if unchanged else 1.08 * total, 10.0))


def exploitability(vector: str) -> float:
    """Return the exploitability subscore of `vector`, to one decimal place."""
    return round(subscore(parse(vector)), 1)


def level(points: float) -> str:
    """Return the method's exploitability level for an exploitability subscore."""
    if points >= 2.5:
        return "High"
    if points >= 1.5:
        return "Medium"
    return "Low"


def risk(rating: str, severity: str) -> str:
    """Return whether a risk is controlled or uncontrolled under the acceptance criteria."""
    first = UNCONTROLLED_FROM[rating]
    if first is not None and SEVERITIES.index(severity) >= SEVERITIES.index(first):
        return "uncontrolled"
    return "controlled"


def cells(line: str) -> list[str]:
    """Return the cells of a Markdown table row."""
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def rubric_answers(lines: list[str]) -> dict[tuple[str, str], str]:
    """Return each threat's rubric answers, by the section they're in and the threat's ID."""
    found = {}
    section = threat = ""
    for line in lines:
        if line.startswith("## "):
            section = line
        elif note := THREAT_NOTE.match(line):
            threat = note.group(1)
        elif answers := RUBRIC_ANSWERS.match(line):
            found[(section, threat)] = answers.group(1)
    return found


def scored(columns: list[str], row: list[str], answers: str | None) -> str:
    """Return a risk table row with its score, exploitability and risk filled in."""
    record = dict(zip(columns, row, strict=True))
    vector = record["Vector"].strip("`")
    severity = record["Severity"]
    if severity not in SEVERITIES:
        raise ValueError(f"{severity} isn't a severity the method defines")
    if answers is None:
        raise AnswerError("has no rubric answers")
    agree(vector, answers)
    points = exploitability(vector)
    record["Score"] = f"{base_score(vector):.1f}"
    record["Exploitability"] = f"{points:.1f} {level(points)}"
    verdict = risk(level(points), severity)
    record["Risk"] = f"**{verdict}**" if verdict == "uncontrolled" else verdict
    return "| " + " | ".join(record[column] for column in columns) + " |"


def fill(text: str) -> tuple[str, list[str]]:
    """Return `text` with each risk row filled in, and each row that can't be scored."""
    lines = text.split("\n")
    answers = rubric_answers(lines)
    problems = []
    section = ""
    columns: list[str] = []
    for number, line in enumerate(lines):
        if line.startswith("## "):
            section = line
        if not line.startswith("|"):
            columns = []
            continue
        row = cells(line)
        if "Vector" in row and "Severity" in row:
            columns = row
        elif columns and not set(line) <= set("|-: "):
            threat = THREAT.match(row[0])
            try:
                lines[number] = scored(
                    columns, row, answers.get((section, threat.group() if threat else row[0]))
                )
            except ValueError as error:
                problems.append(f"{row[0]}: {error}")
    return "\n".join(lines), problems


def check(root: Path) -> list[str]:
    """Return each row that can't be scored, and each that isn't up to date."""
    text = (root / RISK_FILE).read_text()
    filled, problems = fill(text)
    return problems + [
        f"{cells(old)[0]} is out of date: run tools/cvss.py"
        for old, new in zip(text.split("\n"), filled.split("\n"), strict=True)
        if old != new
    ]


def main() -> None:
    """Fill in the risk tables, or with --check, check the committed ones."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true", help="check the tables instead of filling them in"
    )
    args = parser.parse_args()

    if args.check:
        problems = check(ROOT)
    else:
        text, problems = fill((ROOT / RISK_FILE).read_text())
        if not problems:
            (ROOT / RISK_FILE).write_text(text)
    for problem in problems:
        print(problem)
    if problems:
        sys.exit(1)


if __name__ == "__main__":
    main()
