"""Tests for traceability.py, on a small repository made up for each test."""

from pathlib import Path
from textwrap import dedent

import pytest
import traceability

THREATS = dedent("""\
    | ID | STRIDE | Threat | Harm to the patient |
    |---|---|---|---|
    | T-01 | S | A stranger writes the alarm limits | the alarm is silenced |
    | T-02 | T | Limits are changed in transit | as T-01 |
    | T-03 | D | Jamming | no remote alarms |
    | T-04 | D | The gateway goes offline | as T-03 |
    | T-05 | I | The gateway's storage is read | privacy harm |
    | T-06 | D | The backend goes down | as T-03 |
    """)

VERIFIED_BY = (
    '"Refuses Unsigned Limits"; `test_rejects_a_bad_tag` (tests/test_auth.c); '
    "the two tests of [tests/test_alarm.c](../../tests/test_alarm.c); `fuzz_limits`; "
    "the update fuzzer ([fuzz/smp](../../fuzz/smp))."
)

CONTROLS = dedent(f"""\
    | ID | Control | Category | Threats | Status |
    |---|---|---|---|---|
    | C-01 | Signed limits | authentication | T-01, T-02 | built |
    | C-02 | Debug port locked | integrity | T-02 | planned, needs hardware |

    ### C-01 Signed limits

    **Verified by.** {VERIFIED_BY}

    ### C-02 Debug port locked

    **Verified by.** Nothing yet: on hardware.

    ## Threats without a control yet

    | Threat | Why |
    |---|---|
    | T-03 Jamming | It can't be prevented |
    | T-04 to T-06 The gateway's and backend's own threats | Not built yet |
    """)

SUITES = {
    "renode/oximeter.robot": "*** Test Cases ***\nRefuses Unsigned Limits\n    Start System\n",
    "tests/test_auth.c": "static void test_rejects_a_bad_tag(void)\n{\n}\n",
    "tests/test_alarm.c": "static void test_alarms_below(void)\n{\n}\n"
    "static void test_alarms_above(void)\n{\n}\n",
    "fuzz/fuzz_limits.c": "",
    "fuzz/smp/CMakeLists.txt": "",
}

GAP = "[gap](controls.md#threats-without-a-control-yet)"
TABLE = "".join(
    [
        "| Threat | Controls | Tests |\n",
        "|---|---|---|\n",
        "| T-01 A stranger writes the alarm limits | [C-01](controls.md#c-01-signed-limits)"
        " | 1 Renode, 3 unit, 2 fuzz |\n",
        "| T-02 Limits are changed in transit | [C-01](controls.md#c-01-signed-limits),"
        " [C-02](controls.md#c-02-debug-port-locked) (planned) | 1 Renode, 3 unit, 2 fuzz |\n",
        f"| T-03 Jamming | {GAP} | none |\n",
        f"| T-04 The gateway goes offline | {GAP} | none |\n",
        f"| T-05 The gateway's storage is read | {GAP} | none |\n",
        f"| T-06 The backend goes down | {GAP} | none |\n",
    ]
)


def repository(tmp_path: Path, controls: str = CONTROLS) -> Path:
    """Write the documents and test suites under `tmp_path`, and return it."""
    files = {
        "docs/security/threat-model.md": THREATS,
        "docs/security/controls.md": controls,
        **SUITES,
    }
    for name, text in files.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text)
    return tmp_path


def test_builds_a_row_for_each_threat(tmp_path: Path) -> None:
    assert traceability.trace(repository(tmp_path)) == (TABLE, [])


@pytest.mark.parametrize(
    ("change", "old", "new", "named"),
    [
        (
            "a Renode test that doesn't exist",
            '"Refuses Unsigned Limits"',
            '"Refuses Unsigned Limit"',
            'C-01 names "Refuses Unsigned Limit", which doesn\'t exist',
        ),
        (
            "a unit test that doesn't exist",
            "`test_rejects_a_bad_tag`",
            "`test_rejects_a_bad_mac`",
            "C-01 names `test_rejects_a_bad_mac`, which doesn't exist",
        ),
        (
            "a fuzz target that doesn't exist",
            "`fuzz_limits`",
            "`fuzz_limit`",
            "C-01 names `fuzz_limit`, which doesn't exist",
        ),
        (
            "a linked file that doesn't exist",
            "[fuzz/smp](../../fuzz/smp)",
            "[fuzz/l2cap](../../fuzz/l2cap)",
            "C-01 names fuzz/l2cap, which doesn't exist",
        ),
        (
            "a wrong count of a file's tests",
            "the two tests of",
            "the three tests of",
            "C-01 says tests/test_alarm.c has three tests; it has 2",
        ),
        (
            "a threat the model doesn't have",
            "| T-01, T-02 | built |",
            "| T-01, T-09 | built |",
            "C-01 lists T-09, which the threat model doesn't have",
        ),
        (
            "a built control with no test",
            "| T-02 | planned, needs hardware |",
            "| T-02 | built |",
            "C-02 is built but names no test",
        ),
        (
            "a threat with no control",
            "| T-03 Jamming | It can't be prevented |\n",
            "",
            "T-03 has no control and isn't among the threats without one",
        ),
    ],
)
def test_names_each_break_in_the_chain(
    tmp_path: Path, change: str, old: str, new: str, named: str
) -> None:
    assert old in CONTROLS, change
    root = repository(tmp_path, CONTROLS.replace(old, new))

    assert traceability.trace(root)[1] == [named]


def test_check_names_a_stale_table(tmp_path: Path) -> None:
    root = repository(tmp_path)
    (root / traceability.TABLE_FILE).write_text(
        traceability.document(TABLE.replace("2 fuzz", "1 fuzz"))
    )

    assert traceability.check(root) == [
        f"{traceability.TABLE_FILE} is out of date: run tools/traceability.py"
    ]
