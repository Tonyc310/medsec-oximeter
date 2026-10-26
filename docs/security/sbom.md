# Software bill of materials

Every software component in the oximeter's firmware, with its version, supplier, identifiers, level of support and end of support, as FDA's premarket guidance asks of an SBOM. The SBOM itself is a CycloneDX 1.6 file that every build makes from what that build compiled; this page explains it and lists what it holds.

> Portfolio exercise, not a medical device.

## How it's made

The firmware is two images: the oximeter application and MCUboot, the bootloader that checks it. For each, Zephyr's `west spdx` lists the source files the build compiled, header files included, grouped by the Zephyr module they come from, in SPDX 2.3. [components.yaml](components.yaml) gives each component its release version, supplier, CPE or PURL, support and advisories, and [tools/sbom.py](../../tools/sbom.py) joins the two into `medsec-oximeter.cdx.json`. It stops if the build compiled anything from a module the list doesn't name, so nothing reaches the firmware unaccounted for. CI makes the SBOM on every build and keeps it, with the SPDX documents, as the build's `sbom` artifact.

Every build of the oximeter is ready for it: `app/sysbuild.cmake` places the query CMake needs before each image is configured, and both images write the build metadata `west spdx` reads. To make it locally:

```bash
west build --sysbuild -b nrf52840dk/nrf52840 -d build/oximeter app
west spdx --analyze-includes -d build/oximeter/app
west spdx --analyze-includes -d build/oximeter/mcuboot
python3 tools/sbom.py --components docs/security/components.yaml --version-file app/VERSION \
    --image app=build/oximeter/app/spdx --image mcuboot=build/oximeter/mcuboot/spdx \
    --output build/medsec-oximeter.cdx.json
```

## Components

| Component | Version | Supplier | In | Identifier | Support |
|---|---|---|---|---|---|
| Zephyr RTOS | 4.4.2 | Zephyr Project | both images | `cpe:2.3:o:zephyrproject:zephyr:4.4.2` | regular release, until 2027-04-12 |
| Mbed TLS | 4.1.0 | Trusted Firmware | application | `cpe:2.3:a:arm:mbed_tls:4.1.0` | long-term support, until March 2029 |
| TF-PSA-Crypto | 1.1.0 | Trusted Firmware | application | `cpe:2.3:a:trustedfirmware:tf-psa-crypto:1.1.0` | long-term support, until March 2029 |
| MCUboot | 2.4.0 | MCUboot project | both images | `pkg:github/mcu-tools/mcuboot@v2.4.0` | maintained, no published end |
| TinyCrypt | 0.2.8 | Intel | MCUboot | `pkg:github/intel/tinycrypt@v0.2.8` | no longer maintained |
| Mbed TLS ASN.1 parser, MCUboot's copy | 3.0.0 | Trusted Firmware | MCUboot | `cpe:2.3:a:arm:mbed_tls:3.0.0` | Mbed TLS 3.0 is out of support |
| nrfx | 4.2.0 | Nordic Semiconductor | both images | `pkg:github/NordicSemiconductor/nrfx@v4.2.0` | maintained, no published end |
| CMSIS-Core | 6.1.1 | Arm | both images | `pkg:github/ARM-software/CMSIS_6@v6.2.0-dev` | maintained, no published end |
| zcbor | 0.9.1 | Nordic Semiconductor | application | `pkg:github/NordicSemiconductor/zcbor@0.9.1` | maintained, no published end |
| picolibc | 1.8.10 | picolibc project | both images | `pkg:github/picolibc/picolibc@1.8.10` | maintained, no published end |
| libgcc | 14.3.0 | GNU Project | both images | `cpe:2.3:a:gnu:gcc:14.3.0` | until GCC 14's last point release |

The application in the table is also this project's own code (`app/`, `core/`, `common/`), the SBOM's top-level component.

## What the build says, and what it doesn't

- **Versions.** The west manifest pins commits of Zephyr's copies of each project, not releases, so `west spdx` reports commit hashes. The SBOM keeps them, as each component's source, and takes the release version from the project's own files: its version header, release notes or `VERSION`.
- **Identifiers.** `west spdx` gives a CPE, the name vulnerability databases match on, only to Mbed TLS and TF-PSA-Crypto, and TF-PSA-Crypto's (`arm:tf-psa-crypto`) is one NVD doesn't use: the list corrects it. MCUboot, TinyCrypt, nrfx, CMSIS, zcbor and picolibc have no CPE in NVD at all, so no database lookup can find their vulnerabilities. They're checked against their own projects' advisories instead.
- **Compiled is not linked.** `west spdx` lists what each image compiled. Zephyr compiles all of Mbed TLS, its TLS and X.509 code included, but the oximeter calls none of it, and none is linked: `zephyr.elf` has no `mbedtls_ssl` or `mbedtls_x509` symbol. Only the PSA Crypto side, TF-PSA-Crypto, runs.
- **The toolchain.** The C library (picolibc) and the compiler's runtime (libgcc) are linked from the Zephyr SDK 1.0.1 toolchain, which `west spdx` doesn't follow; the list names them for both images.

## Support

- **Zephyr 4.4** is a regular release, supported until 12 April 2027; the long-term support release is 3.7, until July 2029. Before that date the oximeter has to move to a supported release, a decision for the postmarket plan (Phase 7).
- **TinyCrypt** is no longer maintained by Intel. MCUboot keeps its own copy and uses it only to check the firmware's ECDSA P-256 signature. MCUboot can't be updated in this design, so a flaw in it would need physical access to every device to fix: residual risk, as for the release key ([updatability view](updatability.md)).
- **MCUboot's copy of the Mbed TLS ASN.1 parser** dates from Mbed TLS 3.0.0 (2021). MCUboot uses it to read its own public key and the signature in an image's metadata, so it parses part of an update before the signature is checked. Its known vulnerabilities are assessed one by one in the vulnerability assessment.
- **Mbed TLS 4.1 and TF-PSA-Crypto 1.1** are long-term support releases, supported until March 2029.
