"""Tests for vulns.py, with a stand-in for NVD."""

import json
from pathlib import Path
from textwrap import dedent

import vulns

SBOM = {
    "bomFormat": "CycloneDX",
    "specVersion": "1.6",
    "components": [
        {
            "name": "zephyr",
            "version": "4.4.2",
            "cpe": "cpe:2.3:o:zephyrproject:zephyr:4.4.2:*:*:*:*:*:*:*",
        },
        {
            "name": "mbedtls-asn1",
            "version": "3.0.0",
            "cpe": "cpe:2.3:a:arm:mbed_tls:3.0.0:*:*:*:*:*:*:*",
        },
        {"name": "zcbor", "version": "0.9.1", "purl": "pkg:github/NordicSemiconductor/zcbor@0.9.1"},
    ],
}

# What NVD reports for each CPE.
FOUND = {
    "cpe:2.3:o:zephyrproject:zephyr:4.4.2:*:*:*:*:*:*:*": ["CVE-2026-1001"],
    "cpe:2.3:a:arm:mbed_tls:3.0.0:*:*:*:*:*:*:*": ["CVE-2022-2002", "CVE-2024-3003"],
}

DISPOSITIONS = dedent("""\
    # Vulnerability assessment

    | ID | Component | Disposition | Why |
    |---|---|---|---|
    | CVE-2026-1001 | zephyr | not affected: code not present | no Wi-Fi |
    | CVE-2022-2002 | mbedtls-asn1 | not affected: code not present | only ASN.1 is copied |
    | CVE-2024-3003 | mbedtls-asn1 | not affected: code not present | X.509 isn't copied |
    """)


def files(tmp_path: Path, dispositions: str) -> tuple[Path, Path]:
    """Write the SBOM and the assessment, and return their paths."""
    (tmp_path / "sbom.json").write_text(json.dumps(SBOM))
    (tmp_path / "vulnerabilities.md").write_text(dispositions)
    return tmp_path / "sbom.json", tmp_path / "vulnerabilities.md"


def nvd(cpe: str) -> list[str]:
    """Stand in for NVD: the CVEs FOUND lists for `cpe`."""
    return FOUND.get(cpe, [])


def test_passes_when_every_cve_found_has_a_disposition(tmp_path: Path) -> None:
    sbom, dispositions = files(tmp_path, DISPOSITIONS)

    assert vulns.undisposed(sbom, dispositions, nvd) == []


def test_names_a_cve_found_with_no_disposition(tmp_path: Path) -> None:
    sbom, dispositions = files(
        tmp_path, DISPOSITIONS.replace("| CVE-2024-3003 |", "| CVE-2024-9999 |")
    )

    assert vulns.undisposed(sbom, dispositions, nvd) == ["CVE-2024-3003 in mbedtls-asn1"]
