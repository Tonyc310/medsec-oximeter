"""Tests for cvss.py: CVSS v3.0 scores, the method's levels and verdicts, and the risk tables."""

from pathlib import Path
from textwrap import dedent

import cvss
import pytest

HEAD = dedent("""\
    # Risk assessment

    | Threat | Vector | Score | Exploitability | Severity | Risk |
    |---|---|---|---|---|---|
    """)
T_01 = "| T-01 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:H/A:N` |"
T_05 = "| T-05 | `CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:L/I:N/A:N` |"

TABLE = HEAD + T_01 + " | | Catastrophic | |\n" + T_05 + " | | None | |\n"
FILLED = (
    HEAD
    + T_01
    + " 6.5 | 2.8 High | Catastrophic | **uncontrolled** |\n"
    + T_05
    + " 4.3 | 2.8 High | None | controlled |\n"
)


# Examples from FIRST's CVSS v3.0 examples document, chosen so every weight is used, and two from
# NVD where FIRST has none: high privileges with a changed scope, and a score at the cap.
@pytest.mark.parametrize(
    ("example", "vector", "score"),
    [
        ("phpMyAdmin XSS, CVE-2013-1937", "AV:N/AC:L/PR:N/UI:R/S:C/C:L/I:L/A:N", 6.1),
        ("MySQL SQL injection, CVE-2013-0375", "AV:N/AC:L/PR:L/UI:N/S:C/C:L/I:L/A:N", 6.4),
        ("Cisco IOS command execution, CVE-2012-0384", "AV:N/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H", 8.8),
        ("Apache Tomcat XML parser, CVE-2009-0783", "AV:L/AC:L/PR:H/UI:N/S:U/C:L/I:L/A:L", 4.2),
        ("stored XSS by an administrator, NVD", "AV:N/AC:L/PR:H/UI:R/S:C/C:L/I:L/A:N", 4.8),
        ("SSLv3 POODLE, CVE-2014-3566", "AV:N/AC:H/PR:N/UI:R/S:U/C:L/I:N/A:N", 3.1),
        ("Sophos login screen bypass, CVE-2014-2005", "AV:P/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H", 6.8),
        ("Juniper proxy ARP DoS, CVE-2013-6014", "AV:A/AC:L/PR:N/UI:N/S:C/C:H/I:N/A:H", 9.3),
        ("Log4Shell, CVE-2021-44228, NVD", "AV:N/AC:L/PR:N/UI:N/S:C/C:H/I:H/A:H", 10.0),
    ],
)
def test_scores_firsts_worked_examples(example: str, vector: str, score: float) -> None:
    assert cvss.base_score(f"CVSS:3.0/{vector}") == score, example


# The vector that MITRE's rubric calculator scores 1.7 (its issue #2).
def test_scores_no_impact_as_zero() -> None:
    assert cvss.base_score("CVSS:3.0/AV:N/AC:H/PR:L/UI:N/S:U/C:N/I:N/A:N") == 0.0


# The specification's own examples.
@pytest.mark.parametrize(("value", "rounded"), [(4.02, 4.1), (4.00, 4.0)])
def test_rounds_up_to_one_decimal(value: float, rounded: float) -> None:
    assert cvss.round_up(value) == rounded


def test_gives_the_exploitability_subscore() -> None:
    assert cvss.exploitability("CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:H/A:N") == 2.8


@pytest.mark.parametrize(
    ("subscore", "level"), [(2.5, "High"), (2.4, "Medium"), (1.5, "Medium"), (1.4, "Low")]
)
def test_rates_exploitability_by_its_subscore(subscore: float, level: str) -> None:
    assert cvss.level(subscore) == level


@pytest.mark.parametrize(
    ("level", "severity", "risk"),
    [
        ("High", "Minor", "controlled"),
        ("High", "Serious", "uncontrolled"),
        ("Medium", "Serious", "controlled"),
        ("Medium", "Critical", "uncontrolled"),
        ("Low", "Catastrophic", "controlled"),
        ("High", "None", "controlled"),
    ],
)
def test_decides_the_edges_of_the_acceptance_matrix(level: str, severity: str, risk: str) -> None:
    assert cvss.risk(level, severity) == risk


@pytest.mark.parametrize(
    ("problem", "vector", "message"),
    [
        (
            "another version",
            "CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H",
            "doesn't start with CVSS:3.0/",
        ),
        ("a missing metric", "CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H", "has no A"),
        (
            "a metric twice",
            "CVSS:3.0/AV:N/AV:A/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H",
            "gives AV twice",
        ),
        (
            "an unknown value",
            "CVSS:3.0/AV:X/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H",
            "has AV:X, which CVSS v3.0 doesn't define",
        ),
        (
            "a temporal metric",
            "CVSS:3.0/AV:N/AC:L/PR:N/UI:N/S:U/C:H/I:H/A:H/E:F",
            "has E:F, which isn't a base metric",
        ),
    ],
)
def test_refuses_a_malformed_vector(problem: str, vector: str, message: str) -> None:
    with pytest.raises(cvss.VectorError, match=f"^{message}$"):
        cvss.base_score(vector)


def test_reads_metrics_in_any_order() -> None:
    assert cvss.base_score("CVSS:3.0/A:N/I:L/C:L/S:C/UI:R/PR:N/AC:L/AV:N") == 6.1


def test_fills_each_rows_computed_columns() -> None:
    assert cvss.fill(TABLE) == (FILLED, [])


def test_check_names_a_stale_row(tmp_path: Path) -> None:
    (tmp_path / cvss.RISK_FILE).parent.mkdir(parents=True)
    (tmp_path / cvss.RISK_FILE).write_text(FILLED.replace("| 6.5 |", "| 7.5 |"))

    assert cvss.check(tmp_path) == ["T-01 is out of date: run tools/cvss.py"]


@pytest.mark.parametrize(
    ("problem", "old", "new", "named"),
    [
        (
            "a bad vector",
            "CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:H/A:N",
            "CVSS:3.0/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:H",
            "T-01: has no A",
        ),
        (
            "an unknown severity",
            "| Catastrophic |",
            "| Severe |",
            "T-01: Severe isn't a severity the method defines",
        ),
    ],
)
def test_names_a_row_it_can_not_score(problem: str, old: str, new: str, named: str) -> None:
    assert old in TABLE, problem

    assert cvss.fill(TABLE.replace(old, new))[1] == [named]
