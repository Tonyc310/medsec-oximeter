#!/usr/bin/env python3
"""Check every known vulnerability in the SBOM's components has a written disposition.

NVD is asked for the CVEs that affect each component with a CPE, at its version. Each must have a
row in docs/security/vulnerabilities.md, saying whether it affects the oximeter and why; the check
fails, naming them, for any that don't. Components with no CPE aren't in NVD: their advisories are
reviewed by hand, on the same page.
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
from collections.abc import Callable
from pathlib import Path

NVD = "https://services.nvd.nist.gov/rest/json/cves/2.0"
ROW = re.compile(r"^\|\s*(CVE-\d+-\d+)\s*\|\s*([\w.-]+)\s*\|")


def disposed(assessment: Path) -> set[tuple[str, str]]:
    """Return each (vulnerability, component) the assessment's table has a row for."""
    rows = (ROW.match(line) for line in assessment.read_text().splitlines())
    return {(row.group(1), row.group(2)) for row in rows if row}


def undisposed(sbom: Path, assessment: Path, cves: Callable[[str], list[str]]) -> list[str]:
    """Return each CVE `cves` finds for an SBOM component that the assessment doesn't cover."""
    covered = disposed(assessment)
    return [
        f"{cve} in {component['name']}"
        for component in json.loads(sbom.read_text())["components"]
        if "cpe" in component
        for cve in cves(component["cpe"])
        if (cve, component["name"]) not in covered
    ]


def nvd(cpe: str) -> list[str]:
    """Return the CVEs NVD says affect `cpe`, matched against each CVE's affected versions."""
    key = os.environ.get("NVD_API_KEY")
    found: list[str] = []
    start = 0
    while True:
        query = urllib.parse.urlencode(
            {"virtualMatchString": cpe, "startIndex": start, "resultsPerPage": 2000}
        )
        request = urllib.request.Request(f"{NVD}?{query}", headers={"apiKey": key} if key else {})
        with urllib.request.urlopen(request, timeout=60) as response:
            page = json.load(response)
        found += [item["cve"]["id"] for item in page["vulnerabilities"]]
        start += page["resultsPerPage"]
        # Without an API key, NVD allows five requests in 30 seconds.
        time.sleep(1 if key else 6)
        if start >= page["totalResults"]:
            return found


def main() -> None:
    """Check the SBOM against NVD and the assessment."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sbom", type=Path, required=True, help="the CycloneDX SBOM")
    parser.add_argument(
        "--assessment", type=Path, required=True, help="the vulnerability assessment"
    )
    args = parser.parse_args()

    missing = undisposed(args.sbom, args.assessment, nvd)
    for vulnerability in missing:
        print(f"no disposition: {vulnerability}")
    if missing:
        sys.exit(1)
    print("every known vulnerability has a disposition")


if __name__ == "__main__":
    main()
