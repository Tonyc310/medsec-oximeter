#!/usr/bin/env python3
"""Trace each threat to the controls built against it and the tests that verify them.

Reads the threat model, the controls (each one's threats from the summary table, its tests from
its "Verified by" line) and the test suites, and writes docs/security/traceability.md. Fails,
naming each, for any break in the chain: a test named that doesn't exist, a threat neither under
a control nor among those without one, a built control that names no test. With --check, it also
fails when the committed table is out of date.
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = "docs/security"
THREAT_MODEL = f"{DOCS}/threat-model.md"
CONTROLS = f"{DOCS}/controls.md"
TABLE_FILE = f"{DOCS}/traceability.md"
WITHOUT_A_CONTROL = "Threats without a control yet"

KINDS = ("Renode", "unit", "fuzz")
NUMBERS = {
    word: number
    for number, word in enumerate(
        "one two three four five six seven eight nine ten eleven twelve".split(), start=1
    )
}

# How a "Verified by" line names its tests: Robot tests in quotes, unit tests and fuzz targets in
# backquotes, all of a file's tests by their count, and fuzzers by a link to their folder.
RENODE_TEST = re.compile(r'"([^"]+)"')
NAMED_TEST = re.compile(r"`((test|fuzz)_\w+)`")
FILE_OF_TESTS = re.compile(r"[Tt]he (\w+) tests of \[[^\]]+\]\(([^)]+)\)")
LINK = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
# A Unity test, a ztest, or a pytest test.
UNIT_TEST = re.compile(
    r"^(?:static void (test_\w+)\(void\)|ZTEST\(\w+, (test_\w+)\)|def (test_\w+)\()", re.M
)

HEADER = """\
# Traceability

Each threat in the [threat model](threat-model.md), the [controls](controls.md) built against it,
and how many tests verify them, by kind: Robot tests in Renode, unit tests on the host or
`native_sim`, and fuzz targets. The controls name each test. A gap links to why a threat has no
control yet, or only part of one.

Made by [tools/traceability.py](../../tools/traceability.py), which CI runs to check that every
test named exists and that this table is up to date.

"""


@dataclass
class Control:
    """A control: the threats it addresses, whether it's built, and its tests by kind."""

    ident: str
    threats: list[str]
    built: bool
    heading: str = ""
    tests: dict[str, set[str]] = field(default_factory=lambda: {kind: set() for kind in KINDS})


def unit_tests(path: Path) -> list[str]:
    """Return the names of the tests defined in `path`."""
    return [next(name for name in match if name) for match in UNIT_TEST.findall(path.read_text())]


def renode_tests(path: Path) -> list[str]:
    """Return the names of the test cases in the Robot file at `path`."""
    section = path.read_text().partition("*** Test Cases ***")[2].partition("\n***")[0]
    return [line for line in section.splitlines() if line and line[0] not in " \t#"]


def suites(root: Path) -> dict[str, set[str]]:
    """Return the name of every test in the repository, by kind."""
    unit = [
        *root.glob("tests/*.c"),
        *root.glob("*/tests/*/src/*.c"),
        *root.glob("tools/tests/*.py"),
    ]
    return {
        "Renode": {name for path in root.glob("renode/*.robot") for name in renode_tests(path)},
        "unit": {name for path in unit for name in unit_tests(path)},
        "fuzz": {path.stem for path in root.glob("fuzz/fuzz_*.c")},
    }


def threats(root: Path) -> dict[str, str]:
    """Return each threat in the threat model, by ID."""
    found = {}
    for line in (root / THREAT_MODEL).read_text().splitlines():
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) > 3 and re.fullmatch(r"T-\d+", cells[1]):
            found[cells[1]] = cells[3]
    return found


def threat_ids(text: str) -> list[str]:
    """Return the threat IDs in `text`, with each range such as "T-23 to T-31" spelled out."""
    ids = re.findall(r"T-\d+", text)
    for first, last in re.findall(r"T-(\d+) to T-(\d+)", text):
        ids += [f"T-{number:02}" for number in range(int(first) + 1, int(last))]
    return ids


def anchor(heading: str) -> str:
    """Return the link GitHub gives a heading."""
    return "#" + re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")


def verified_by(
    ident: str, line: str, root: Path, known: dict[str, set[str]]
) -> tuple[dict[str, set[str]], list[str]]:
    """Return the tests a "Verified by" line names, by kind, and each one that doesn't exist."""
    tests: dict[str, set[str]] = {kind: set() for kind in KINDS}
    breaks: list[str] = []
    for word, link in FILE_OF_TESTS.findall(line):
        path = Path(os.path.normpath(root / DOCS / link))
        if path.is_file():
            names = unit_tests(path)
            tests["unit"].update(names)
            if NUMBERS.get(word) != len(names):
                where = path.relative_to(root)
                breaks.append(f"{ident} says {where} has {word} tests; it has {len(names)}")
    for link in LINK.findall(line):
        path = Path(os.path.normpath(root / DOCS / link))
        where = path.relative_to(root)
        if not path.exists():
            breaks.append(f"{ident} names {where}, which doesn't exist")
        elif where.parts[0] == "fuzz":
            tests["fuzz"].add(str(where))
    for name in RENODE_TEST.findall(line):
        if name not in known["Renode"]:
            breaks.append(f'{ident} names "{name}", which doesn\'t exist')
        tests["Renode"].add(name)
    for name, prefix in NAMED_TEST.findall(line):
        kind = "unit" if prefix == "test" else "fuzz"
        if name not in known[kind]:
            breaks.append(f"{ident} names `{name}`, which doesn't exist")
        tests[kind].add(name)
    return tests, breaks


def controls(
    root: Path, known: dict[str, set[str]]
) -> tuple[dict[str, Control], list[str], list[str]]:
    """Return each control by ID, the threats listed as without one, and each break found."""
    text = (root / CONTROLS).read_text()
    found: dict[str, Control] = {}
    breaks: list[str] = []
    for line in text.splitlines():
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) == 7 and re.fullmatch(r"C-\d+", cells[1]):
            found[cells[1]] = Control(cells[1], threat_ids(cells[4]), cells[5].startswith("built"))
    without: list[str] = []
    for section in re.split(r"^(?=#{2,3} )", text, flags=re.M):
        heading, _, body = section.partition("\n")
        title = heading.lstrip("# ")
        ident = title.split(" ")[0]
        if heading.startswith("### ") and ident in found:
            line = next(
                (line for line in body.splitlines() if line.startswith("**Verified by.**")), ""
            )
            found[ident].heading = title
            found[ident].tests, named = verified_by(ident, line, root, known)
            breaks += named
        elif title == WITHOUT_A_CONTROL:
            without += [
                tid
                for row in body.splitlines()
                if row.startswith("| T-")
                for tid in threat_ids(row.split("|")[1])
            ]
    return found, without, breaks


def trace(root: Path) -> tuple[str, list[str]]:
    """Return the traceability table, and each break in the chain from threat to test."""
    model = threats(root)
    found, without, breaks = controls(root, suites(root))
    for ident, control in found.items():
        breaks += [
            f"{ident} lists {tid}, which the threat model doesn't have"
            for tid in control.threats
            if tid not in model
        ]
        if control.built and not any(control.tests.values()):
            breaks.append(f"{ident} is built but names no test")

    rows = ["| Threat | Controls | Tests |", "|---|---|---|"]
    for tid, description in model.items():
        against = [control for control in found.values() if tid in control.threats]
        if not against and tid not in without:
            breaks.append(f"{tid} has no control and isn't among the threats without one")
        links = [
            f"[{control.ident}](controls.md{anchor(control.heading)})"
            + ("" if control.built else " (planned)")
            for control in against
        ]
        if tid in without:
            links.append(f"[gap](controls.md{anchor(WITHOUT_A_CONTROL)})")
        built = [control for control in against if control.built]
        counts = [
            f"{len(names)} {kind}"
            for kind in KINDS
            if (names := set().union(*(control.tests[kind] for control in built)))
        ]
        rows.append(f"| {tid} {description} | {', '.join(links)} | {', '.join(counts) or 'none'} |")
    return "\n".join(rows) + "\n", breaks


def document(table: str) -> str:
    """Return the traceability document that holds `table`."""
    return HEADER + table


def check(root: Path) -> list[str]:
    """Return each break in the chain, and whether the committed table is out of date."""
    table, breaks = trace(root)
    if not breaks and (root / TABLE_FILE).read_text() != document(table):
        breaks.append(f"{TABLE_FILE} is out of date: run tools/traceability.py")
    return breaks


def main() -> None:
    """Write the traceability table, or with --check, check the committed one."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true", help="check the table instead of writing it"
    )
    args = parser.parse_args()

    if args.check:
        breaks = check(ROOT)
    else:
        table, breaks = trace(ROOT)
        if not breaks:
            (ROOT / TABLE_FILE).write_text(document(table))
    for problem in breaks:
        print(problem)
    if breaks:
        sys.exit(1)


if __name__ == "__main__":
    main()
