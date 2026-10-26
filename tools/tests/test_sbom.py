"""Tests for sbom.py, on west spdx documents and a component list made for them."""

from pathlib import Path
from textwrap import dedent

import pytest
import sbom

COMPONENTS = dedent("""\
    product: medsec-oximeter
    components:
      - name: zephyr
        type: operating-system
        module: zephyr
        version: 4.4.2
        supplier: Zephyr Project
        cpe: cpe:2.3:o:zephyrproject:zephyr:4.4.2:*:*:*:*:*:*:*
        purl: pkg:github/zephyrproject-rtos/zephyr@v4.4.2
      - name: mcuboot
        module: mcuboot
        version: 2.4.0
        supplier: MCUboot project
        purl: pkg:github/mcu-tools/mcuboot@v2.4.0
      - name: tinycrypt
        module: mcuboot
        path: ext/tinycrypt/
        version: 0.2.8
        supplier: Intel
        purl: pkg:github/intel/tinycrypt@v0.2.8
      - name: picolibc
        images: [app, mcuboot]
        version: 1.8.10
        supplier: picolibc project
        purl: pkg:github/picolibc/picolibc@1.8.10
    """)


def spdx(*packages: tuple[str, str, list[str]]) -> str:
    """Return a zephyr.spdx document as west spdx writes it, with these packages and files."""
    text = "SPDXVersion: SPDX-2.3\nDocumentName: zephyr-sources\n"
    for name, location, files in packages:
        text += f"\nPackageName: {name}\nPackageDownloadLocation: {location}\n"
        text += "".join(f"\nFileName: ./{path}\nSPDXID: SPDXRef-File\n" for path in files)
    return text


ZEPHYR = (
    "zephyr-sources",
    "git+https://github.com/zephyrproject-rtos/zephyr@dccb0959",
    ["kernel/sched.c"],
)


def image(root: Path, name: str, document: str) -> Path:
    """Write an image's spdx directory and return it."""
    directory = root / name / "spdx"
    directory.mkdir(parents=True)
    (directory / "zephyr.spdx").write_text(document)
    return directory


def build(tmp_path: Path, mcuboot_files: list[str]) -> dict[str, object]:
    """Return the SBOM for an app image of Zephyr alone and an MCUboot image with these files."""
    (tmp_path / "components.yaml").write_text(COMPONENTS)
    images = {
        "app": image(tmp_path, "app", spdx(ZEPHYR)),
        "mcuboot": image(
            tmp_path,
            "mcuboot",
            spdx(
                ZEPHYR,
                (
                    "mcuboot-sources",
                    "git+https://github.com/zephyrproject-rtos/mcuboot@6d3b3d2c",
                    mcuboot_files,
                ),
            ),
        ),
    }
    return sbom.make(tmp_path / "components.yaml", "1.0.0", images)


def test_lists_every_component_built_into_either_image_with_its_version_and_identifiers(
    tmp_path: Path,
) -> None:
    bom = build(tmp_path, ["boot/bootutil/src/loader.c", "ext/tinycrypt/lib/source/ecc.c"])
    listed = {c["name"]: c for c in bom["components"]}  # type: ignore[attr-defined]

    assert sorted(listed) == ["mcuboot", "picolibc", "tinycrypt", "zephyr"]
    zephyr = listed["zephyr"]
    assert zephyr["version"] == "4.4.2"
    assert zephyr["cpe"] == "cpe:2.3:o:zephyrproject:zephyr:4.4.2:*:*:*:*:*:*:*"
    assert zephyr["purl"] == "pkg:github/zephyrproject-rtos/zephyr@v4.4.2"
    assert {
        "type": "vcs",
        "url": "git+https://github.com/zephyrproject-rtos/zephyr@dccb0959",
    } in zephyr["externalReferences"]
    images = {name: c["properties"][0] for name, c in listed.items()}
    assert images["zephyr"] == {"name": "medsec:images", "value": "app, mcuboot"}
    assert images["tinycrypt"] == {"name": "medsec:images", "value": "mcuboot"}
    assert images["picolibc"] == {"name": "medsec:images", "value": "app, mcuboot"}


def test_refuses_a_built_component_with_no_curated_entry(tmp_path: Path) -> None:
    (tmp_path / "components.yaml").write_text(COMPONENTS)
    images = {
        "app": image(
            tmp_path,
            "app",
            spdx(
                ZEPHYR,
                (
                    "zcbor-sources",
                    "git+https://github.com/zephyrproject-rtos/zcbor@9164bd18",
                    ["src/zcbor_decode.c"],
                ),
            ),
        )
    }

    with pytest.raises(sbom.UnlistedComponentError, match="zcbor"):
        sbom.make(tmp_path / "components.yaml", "1.0.0", images)
