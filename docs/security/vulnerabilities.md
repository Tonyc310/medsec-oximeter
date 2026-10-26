# Vulnerability assessment

Every known vulnerability in the oximeter's software components and in the chip they run on, and whether it affects this device, as FDA's premarket guidance asks. The software components are the ones in the [SBOM](sbom.md). A check against NVD keeps the assessment complete as new CVEs are published.

> Portfolio exercise, not a medical device.

## How it's checked

[tools/vulns.py](../../tools/vulns.py) asks NVD for the CVEs that affect each SBOM component with a CPE, at the version the build uses. It fails for any CVE without a row in the tables below, and names it. CI runs it every Monday on a fresh build, and on demand, so a CVE published after a release fails the check until it's assessed. With the SBOM from a build ([how it's made](sbom.md#how-its-made)):

```bash
python3 tools/vulns.py --sbom build/medsec-oximeter.cdx.json \
    --assessment docs/security/vulnerabilities.md
```

Without an API key, NVD answers five requests in 30 seconds, so the check takes about half a minute. An `NVD_API_KEY` in the environment speeds it up; CI passes one from the repository's secrets when it's set.

NVD matches by version range. It reports CVEs that the version in use has already fixed, and CVEs in parts of a component the build doesn't compile. Each row says which, in the analysis terms of CycloneDX, the SBOM's format:

- **resolved**: fixed in the version in use.
- **not affected: code not present**: neither image compiles the vulnerable code. Either its file isn't in the SBOM's list of compiled files, or an option that's off leaves it out of a file that is.
- **not affected: requires environment**: the vulnerability exists only on another architecture.

Assessed on 26 October 2026. None of these CVEs is in CISA's Known Exploited Vulnerabilities catalog (version 2026.10.23).

## Zephyr 4.4.2

Neither image builds networking (`CONFIG_NETWORKING` is off), USB, Wi-Fi, file systems, Bluetooth Mesh or user mode (`CONFIG_USERSPACE` is off). The oximeter is a Bluetooth peripheral with no GATT client.

| ID | Component | Disposition | Why |
|---|---|---|---|
| CVE-2026-2411 | zephyr | resolved | Notifications checked link security against the attribute passed in, so a notification sent through a characteristic's declaration skipped the value's permissions. The oximeter notifies through declarations, the case this concerns. Fixed in 4.4.2. |
| CVE-2026-7007 | zephyr | not affected: code not present | ext2 file system |
| CVE-2026-10634 | zephyr | not affected: code not present | TCP, in the network stack |
| CVE-2026-10636 | zephyr | not affected: code not present | IGMP, in the network stack |
| CVE-2026-10637 | zephyr | not affected: code not present | IPv6 MLD, in the network stack |
| CVE-2026-10638 | zephyr | not affected: code not present | ICMPv6, in the network stack |
| CVE-2026-10639 | zephyr | not affected: code not present | ICMPv4, in the network stack |
| CVE-2026-10640 | zephyr | not affected: code not present | IPv6 neighbor discovery, in the network stack |
| CVE-2026-10642 | zephyr | not affected: code not present | Arm PL011 UART driver; the nRF52840 has none |
| CVE-2026-10647 | zephyr | not affected: code not present | USB CDC-NCM device class |
| CVE-2026-10663 | zephyr | not affected: code not present | USB host stack |
| CVE-2026-10664 | zephyr | not affected: code not present | nRF70 Wi-Fi driver |
| CVE-2026-10669 | zephyr | not affected: code not present | Xtensa MPU; the nRF52840 is an Arm Cortex-M4 |
| CVE-2026-10670 | zephyr | not affected: code not present | The `k_thread_name_copy` system call's check. `kernel/thread.c` is built, but compiles that check only with `CONFIG_USERSPACE`. |
| CVE-2026-10671 | zephyr | not affected: code not present | The `k_pipe_init` system call's check. `kernel/pipe.c` is built, but compiles that check only with `CONFIG_USERSPACE`. Also fixed in 4.4.2. |
| CVE-2026-10673 | zephyr | not affected: code not present | ADIN2111 Ethernet driver |
| CVE-2026-10681 | zephyr | not affected: code not present | User mode's dynamic kernel objects |
| CVE-2026-10682 | zephyr | not affected: code not present | The `log_filter_set` system call's check. `subsys/logging/log_mgmt.c` is built, but compiles that check only with `CONFIG_USERSPACE`. Also fixed in 4.4.2. |
| CVE-2026-10683 | zephyr | not affected: code not present | Synopsys DesignWare I2C driver; the nRF52840 has none |
| CVE-2026-10685 | zephyr | not affected: code not present | The GATT client's subscription handling. `subsys/bluetooth/host/gatt.c` is built, but compiles the client only with `CONFIG_BT_GATT_CLIENT`, off on the oximeter. Also fixed in 4.4.2. |
| CVE-2026-10686 | zephyr | not affected: code not present | IPv6 forwarding, in the network stack |
| CVE-2026-10773 | zephyr | not affected: code not present | DHCPv4 client, in the network stack |
| CVE-2026-10774 | zephyr | not affected: code not present | Bluetooth Mesh |
| CVE-2026-10848 | zephyr | not affected: code not present | OCPP client |
| CVE-2026-10849 | zephyr | not affected: code not present | hawkBit update client |
| CVE-2026-11368 | zephyr | resolved | The ATT layer used a channel after a peer disconnected with a PDU in flight. The oximeter builds this code. Fixed in 4.4.2. |

## Mbed TLS ASN.1 parser, MCUboot's copy (3.0.0)

NVD can only name Mbed TLS 3.0.0 as a whole. MCUboot's copy is two of its files, `asn1parse.c` and `platform_util.c`, with their headers; MCUboot uses them to read its public key and an image's signature. None of these CVEs is in either file.

| ID | Component | Disposition | Why |
|---|---|---|---|
| CVE-2021-43666 | mbedtls-asn1 | not affected: code not present | PKCS#12 key derivation (`pkcs12.c`) |
| CVE-2021-45451 | mbedtls-asn1 | not affected: code not present | PSA AEAD nonce generation (`psa_crypto.c`) |
| CVE-2023-52353 | mbedtls-asn1 | not affected: code not present | TLS session reset (`ssl_tls.c`) |
| CVE-2025-47917 | mbedtls-asn1 | not affected: code not present | X.509 name parsing (`x509_create.c`) |
| CVE-2025-48965 | mbedtls-asn1 | not affected: code not present | The ASN.1 writer (`asn1write.c`), not the parser |
| CVE-2025-52496 | mbedtls-asn1 | not affected: code not present | AES-NI detection (`aesni.c`), on x86 only |
| CVE-2025-52497 | mbedtls-asn1 | not affected: code not present | PEM parsing (`pem.c`, `pkparse.c`) |
| CVE-2025-54764 | mbedtls-asn1 | not affected: code not present | Timing of RSA's modular inversion and GCD (`bignum.c`) |
| CVE-2025-59438 | mbedtls-asn1 | not affected: code not present | Timing of padding errors in PSA's CBC decryption (`psa_crypto_cipher.c`) |
| CVE-2025-66442 | mbedtls-asn1 | not affected: code not present | Clang optimizations breaking constant time in RSA and CBC/ECB padding checks. MCUboot has neither and is built with GCC. |
| CVE-2026-25835 | mbedtls-asn1 | not affected: code not present | PSA random generator cloned with the process (`psa_crypto.c`) |
| CVE-2026-34871 | mbedtls-asn1 | not affected: code not present | Entropy on Linux falling back to `/dev/urandom` (`entropy_poll.c`) |
| CVE-2026-34872 | mbedtls-asn1 | not affected: code not present | PSA's finite-field Diffie-Hellman peer key checks (`psa_crypto_ffdh.c`) |
| CVE-2026-34877 | mbedtls-asn1 | not affected: code not present | Serialized TLS contexts and sessions (`ssl_tls.c`) |

## libgcc 14.3.0

| ID | Component | Disposition | Why |
|---|---|---|---|
| CVE-2023-4039 | libgcc | not affected: requires environment | GCC's stack protector misses overflows of variable-length arrays on AArch64. The nRF52840 is a 32-bit Cortex-M4, and neither image uses the stack protector. The CVE is disputed. |

## Components without a CPE

NVD can't be asked about these by name and version, so they're checked by hand: each project's own advisories, and NVD's text search for its name. Checked on 26 October 2026.

| Component | Version | Found | Assessment |
|---|---|---|---|
| MCUboot | 2.4.0 | CVE-2021-3399, CVE-2021-3890 and CVE-2024-32883 in its advisories; CVE-2026-14296 in NVD | The first two affect versions before 1.10. CVE-2024-32883 accepted protected TLVs in an image's unprotected area: 2.4.0 accepts there only the TLVs on its allow list (`CONFIG_MCUBOOT_USE_TLV_ALLOW_LIST`, on), and measured boot, whose boot record it concerned, is off. CVE-2026-14296 needs Direct XIP and a second core: the oximeter uses swap-using-offset on a single core. |
| TinyCrypt | 0.2.8 | none | Intel no longer maintains it, so none will be published ([support](sbom.md#support)). |
| nrfx | 4.2.0 | none | Nordic's advisories name no nrfx version. |
| CMSIS-Core | 6.1.1 | none | |
| zcbor | 0.9.1 | none | |
| picolibc | 1.8.10 | none | |

## The chip

Nordic's [security advisories](https://docs.nordicsemi.com/r/bundle/struct_sa/page/struct/sa.html) name the nRF52840 in four.

| ID | Concerns | Assessment |
|---|---|---|
| IN133 | Fault injection that reopens a locked debug port, on nRF52840 builds up to Dx0 | Open. It bears on the debug-port lock, which Phase 4 builds on real hardware: on those builds the lock alone doesn't keep the firmware and keys from someone holding the device. |
| SA-2025-380 | Voltage fault injection on the CryptoCell CC310's AES engine | Not affected: neither image uses the CryptoCell. |
| SA-2024-375 | Physical attacks on Matter's device attestation key | Not affected: the oximeter doesn't run Matter. |
| SA-2023-234 | Unauthorized Thread network key updates | Not affected: the oximeter doesn't run Thread. |

## Found by fuzzing

Fuzzing the update protocol ([fuzz/smp](../../fuzz/smp)) under UndefinedBehaviorSanitizer finds undefined behavior at two places in Zephyr 4.4.2's MCUmgr, both still on Zephyr's main branch. Neither has a CVE.

| Where | What | Assessment |
|---|---|---|
| `zcbor_map_decode_bulk`, in `subsys/mgmt/mcumgr/util/src/zcbor_bulk.c` | Calls each value's decoder through a function pointer of another type (CERT EXP37-C). | Not affected. The call is the same whatever a request's bytes, and on Arm the types agree where it matters: each decoder takes two pointers and returns a bool. |
| `ERASED_VAL_32`, in `img_mgmt_priv.h`, used by `img_mgmt_read_info` and `img_mgmt_flash_check_empty_inner` | Shifts the flash's erased byte, 0xFF promoted to `int`, left by 24 (CERT INT34-C). | Not affected. The byte is the flash's, not a gateway's, and GCC computes 0xFFFFFFFF as intended. |

The fuzzer stops at any other undefined behavior; it lets these two pass ([fuzz/smp/ubsan.supp](../../fuzz/smp/ubsan.supp)).

## Limits

- **NVD lags.** Zephyr publishes advisories on GitHub before NVD analyzes them. This page covers what NVD lists, and the weekly check fails as NVD adds more. At the time of writing, Zephyr's advisories name flaws in 4.4.2 that are fixed only in 4.5.0, which isn't released yet, and NVD lists only a few of them so far.
- **Compiled is per file.** The SBOM lists the files each image compiled. Where a vulnerable function sits in a compiled file behind an option, the row names the option, checked against the build's configuration and the source.
- **Scheduled runs stop.** GitHub disables a public repository's scheduled workflows after 60 days with no activity, which ends the weekly check until it's turned back on.
