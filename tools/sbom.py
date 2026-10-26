#!/usr/bin/env python3
"""Write the oximeter's SBOM, in CycloneDX 1.6, from what its build compiled.

west spdx lists each firmware image's source files by Zephyr module, with commit hashes for
versions and few identifiers. docs/security/components.yaml gives each component its release
version, supplier, CPE or PURL, and support; this joins the two, and refuses a module the build used
that the list doesn't name, so nothing goes into the firmware unaccounted for.
"""

import argparse
import json
import sys
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import yaml


class UnlistedComponentError(Exception):
    """A module the build compiled that components.yaml doesn't list."""


def read_spdx(path: Path) -> dict[str, tuple[str, list[str]]]:
    """Return each package's download location and files, from a west spdx document."""
    packages: dict[str, tuple[str, list[str]]] = {}
    name = ""
    for line in path.read_text().splitlines():
        key, _, value = line.partition(": ")
        if key == "PackageName":
            name = value
            packages[name] = ("", [])
        elif key == "PackageDownloadLocation" and name:
            packages[name] = (value, packages[name][1])
        elif key == "FileName" and name:
            packages[name][1].append(value.removeprefix("./"))
    return packages


def owner(components: list[dict[str, Any]], module: str, path: str) -> dict[str, Any] | None:
    """Return the component a module's file belongs to: the one with the longest matching path."""
    matches = [
        c for c in components if c.get("module") == module and path.startswith(c.get("path", ""))
    ]
    return max(matches, key=lambda c: len(c.get("path", "")), default=None)


def built(components: list[dict[str, Any]], document: Path) -> Iterator[tuple[str, str]]:
    """Yield the name and source of each component an image compiled, as often as it has files."""
    for package, (location, files) in read_spdx(document).items():
        module = package.removesuffix("-sources")
        for path in files:
            component = owner(components, module, path)
            if component is None:
                raise UnlistedComponentError(
                    f"{document} compiles {path} from {module}, which components.yaml doesn't list"
                )
            yield component["name"], location


def entry(component: dict[str, Any], images: list[str], source: str) -> dict[str, Any]:
    """Return a component as CycloneDX lists it."""
    listed: dict[str, Any] = {
        "type": component.get("type", "library"),
        "bom-ref": component["name"],
        "name": component["name"],
        "version": component["version"],
        "supplier": {"name": component["supplier"]},
    }
    for key in ("cpe", "purl"):
        if key in component:
            listed[key] = component[key]
    references = [{"type": "vcs", "url": source}] if source else []
    if "advisories" in component:
        references.append({"type": "advisories", "url": component["advisories"]})
    if references:
        listed["externalReferences"] = references
    listed["properties"] = [{"name": "medsec:images", "value": ", ".join(images)}] + [
        {"name": f"medsec:{key}", "value": str(component[key])}
        for key in ("support", "end-of-support")
        if key in component
    ]
    return listed


def make(components_file: Path, version: str, images: dict[str, Path]) -> dict[str, Any]:
    """Return the SBOM for the firmware images, each given by its west spdx directory."""
    catalog = yaml.safe_load(components_file.read_text())
    components: list[dict[str, Any]] = catalog["components"]
    holders: dict[str, list[str]] = {}
    sources: dict[str, str] = {}
    for image, directory in images.items():
        for name, location in built(components, directory / "zephyr.spdx"):
            if image not in holders.setdefault(name, []):
                holders[name].append(image)
            sources.setdefault(name, location)
    # The toolchain's own libraries, which west spdx doesn't follow, name their images instead.
    for component in components:
        for image in component.get("images", []):
            if image in images and image not in holders.setdefault(component["name"], []):
                holders[component["name"]].append(image)
    listed = [
        entry(c, holders[c["name"]], sources.get(c["name"], ""))
        for c in components
        if holders.get(c["name"])
    ]
    product = catalog["product"]
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "version": 1,
        "metadata": {
            "component": {
                "type": "firmware",
                "bom-ref": product,
                "name": product,
                "version": version,
            }
        },
        "components": sorted(listed, key=lambda c: c["name"]),
        "dependencies": [{"ref": product, "dependsOn": sorted(c["bom-ref"] for c in listed)}],
    }


def main() -> None:
    """Write the SBOM for the images named on the command line."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--components", type=Path, required=True, help="the curated component list")
    parser.add_argument(
        "--version-file", type=Path, required=True, help="the app's VERSION, for the firmware's"
    )
    parser.add_argument(
        "--image",
        action="append",
        required=True,
        metavar="NAME=DIR",
        help="an image and its west spdx directory; repeat for each image",
    )
    parser.add_argument("--output", type=Path, required=True, help="where to write the SBOM")
    args = parser.parse_args()

    # Zephyr's VERSION file: "VERSION_MAJOR = 1" and so on.
    fields = {
        key.strip(): value.strip()
        for key, _, value in (
            line.partition("=") for line in args.version_file.read_text().splitlines()
        )
    }
    version = ".".join(fields[key] for key in ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"))
    images = {name: Path(directory) for name, directory in (i.split("=", 1) for i in args.image)}
    try:
        bom = make(args.components, version, images)
    except UnlistedComponentError as error:
        sys.exit(str(error))
    args.output.write_text(json.dumps(bom, indent=2) + "\n")


if __name__ == "__main__":
    main()
