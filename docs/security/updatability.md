# Updatability and patchability view

How a software fix reaches the oximeter and the bedside gateway, end to end: from the manufacturer's build, through servers and networks the manufacturer doesn't control, to the device. FDA's premarket guidance asks for this view in section V.B.2, with the level of detail in its Appendix 2, and recommends that the update path be protected end to end because it crosses technology the manufacturer doesn't control.

> Portfolio exercise, not a medical device.

## Today

The last two hops are built and tested in simulation: the hub, standing in for the gateway, uploads a signed image to the oximeter over Bluetooth, and MCUboot checks it before it runs (hops 4 and 5, below). The hops from the manufacturer to the gateway, the backend and the Raspberry Pi gateway are planned. Until the debug port is locked, which only real hardware can show, anyone with a debug probe can still replace MCUboot itself (T-14).

Each part below says whether it's done or planned. The [security controls](controls.md) it relies on are C-05, C-08, C-09, C-11, C-20 and C-21.

## Design in one paragraph

The manufacturer signs every release. The signature is checked only on the device that runs it: by MCUboot on the oximeter, and by the update client on the gateway. The servers, networks and the gateway in between carry images but can't change them undetected, so the path needs no trust in them for integrity; they matter for availability and for deciding who gets which update when. A device that receives a broken or unbootable image goes back to the image it had.

## The oximeter update path

```mermaid
sequenceDiagram
    participant S as E-6 Build and signing
    participant U as E-5 Update server
    participant B as E-3 Backend
    participant G as E-2 Gateway
    participant A as E-1 Oximeter app
    participant M as E-1 MCUboot

    S->>S: build, sign with the release key (DS-5)
    S->>U: signed image
    B->>U: fetch image and release notes
    Note over B: clinician (home) or biomedical staff (ward)<br/>approve the device and a time
    B->>G: image, over the gateway's authenticated connection
    G->>A: upload to slot 1 over Bluetooth (SMP), in chunks
    Note over A: measuring and alarming continue during the upload
    G->>A: mark slot 1 for a test boot, reset
    A->>M: reset
    M->>M: check signature and security counter of slot 1
    M->>M: swap slots 0 and 1, boot the new image
    M->>A: start
    A->>A: self-test passes: mark the image OK
    G->>A: read the security log
    G->>B: the log: update marked for test, then the version that started
```

| Hop | From, to | Carries | Protocol | Protection | Status |
|---|---|---|---|---|---|
| 1 | E-6 to E-5 | signed image | HTTPS, TLS 1.3 | the image's signature; the release pipeline authenticates to the server | planned |
| 2 | E-5 to E-3 | signed image, version, security counter, target hardware | HTTPS, TLS 1.3 | server certificate; at home over the internet, on a ward the hospital's server fetches it | planned |
| 3 | E-3 to E-2 | signed image | HTTPS, TLS 1.3, both sides authenticated by certificate (I-6) | the gateway only accepts updates from its own backend | planned |
| 4 | E-2 to E-1 | signed image, upload and test commands | MCUmgr SMP over Bluetooth LE: service `8d53dc1d-1db7-4cd3-868b-8a527460aa84`, characteristic `da2e7828-fbce-4e01-ae9e-261174997c48` (I-8) | the SMP characteristic requires an encrypted, authenticated link. Only a gateway the backend authorizes can pair: pairing needs the oximeter's sealed out-of-band data, which only the backend can open | done |
| 5 | slot 1 to slot 0 | the image | MCUboot, on the device | signature and security counter checked before the image is used | done |

Hop 4 relies on the Bluetooth pairing that Phase 4 adds for the readings and the alarm limits, now built: LE Secure Connections only, at security level 4, authenticated with out-of-band data the oximeter seals under its pairing key (README, Bluetooth). Both sides keep the bond, and the oximeter keeps only one: a replacement gateway's pairing takes the old one's place. Zephyr's SMP transport already defaults to requiring an authenticated link once pairing is enabled (`CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN`). Without it, anyone in radio range could upload; the signature check would still stop them installing anything the manufacturer didn't sign. A test checks that a phone that isn't paired can't. The oximeter offers only what an update needs over SMP (image upload and state, and a reset), and the hub, standing in for the gateway, sends an update, marks it for a test boot and resets the oximeter into it. Keeping the update is left to the oximeter's self-test alone (below).

## Verification on the oximeter

| What | Design | Why | Threat | Status |
|---|---|---|---|---|
| Signature | ECDSA P-256 with SHA-256 over the image and its header, checked by MCUboot; the public key is built into MCUboot | a NIST-approved algorithm (FIPS 186-5) with 128-bit security, supported by MCUboot at the commit Zephyr v4.4.2 pins | T-33 | done |
| Check on every boot | the running image's signature is checked at each boot, not only after an update (`BOOT_VALIDATE_SLOT0`, MCUboot's default) | an image altered in flash some other way still won't run | T-14 | done |
| Rollback | each image carries a security counter in its signed metadata. MCUboot refuses an update whose counter is below the running image's. A release that fixes a vulnerability raises the counter, which blocks every image before it (`MCUBOOT_DOWNGRADE_PREVENTION_SECURITY_COUNTER`) | a genuine but older image would reopen a fixed vulnerability | T-34 | done |
| Who keeps an update | only the oximeter's own self-test. Over SMP it refuses to confirm an image it isn't running (`CONFIG_MCUMGR_GRP_IMG_ALLOW_CONFIRM_NON_ACTIVE_SLOT=n`), and refuses any change of image state while an update is on trial (`app/src/update_guard.c`) | standard MCUmgr tools confirm an update from outside once it runs. A gateway that did, compromised or not, would keep a broken image for good and defeat the revert | T-22 | done |
| Debug port | locked at startup (`CONFIG_NRF_APPROTECT_LOCK`) | MCUboot's own documentation notes that its rollback check can be bypassed by writing flash through a debug port. Signature, rollback and debug lock only work together | T-14, T-15 | planned, needs hardware |
| Flash layout | the nRF52840 DK's standard partitions: MCUboot 48 KB, two image slots of 472 KB, storage 32 KB | MCUboot takes 30 KB and the image 167 KB today. FDA asks for room for future patches, and each slot has almost three times what's needed now | T-32 | done |

The release key never leaves the build environment (DS-5): an attacker with it can sign anything (T-35). In this project the build makes one per checkout, in `build/release-key.pem`, never committed, so no two checkouts share a key. A product keeps its release key in an offline signing service, which only the release process can use. MCUboot's own development key, sysbuild's default, is never used: its private half is published with MCUboot, and a test checks that MCUboot never starts an image signed with it. MCUboot checks an update against the same key before it swaps it in, and erases one the release key didn't sign. The key MCUboot trusts is built into MCUboot, which can't be updated in this design, so replacing a compromised key needs physical access to every device. That limitation goes into the risk assessment.

## Interrupted and failed updates

FDA asks what happens when an update is interrupted, at each phase of the process. MCUboot's swap mode is chosen for this: it keeps the previous image until the new one has proved it works. Of its swap methods, swap using offset is the one MCUboot recommends for new products, and it writes less flash than swap using move.

| What happens | Result | Status |
|---|---|---|
| The Bluetooth link drops during the upload | slot 1 holds a partial image that is never booted. The running image is untouched, and the gateway starts the upload again on the next connection | planned |
| The image fails its signature or rollback check | MCUboot erases it from slot 1 and boots the current image. The security log shows the update marked for a test boot with its version, then a start-up of the version before (`app/src/update_log.c`), so the backend sees it didn't take. It doesn't say why: only MCUboot's console does | done |
| Power is lost during the swap | MCUboot records the swap's progress in the slots' trailers and finishes it after the next reset. MCUboot's own simulator tests this, cutting power at each flash write of a swap in turn, and its CI runs it with swap using offset; Renode tests here would add nothing to that | done, MCUboot's tests |
| The new image boots but doesn't pass its self-test, or hangs | it never marks itself OK, and won't let the gateway mark it. A watchdog (T-17) resets a hung image. At the next reset MCUboot swaps the previous image back | done |
| The new image passes its self-test | it marks itself OK and the swap becomes permanent | done |

The self-test that confirms an image checks that the sensor answers with its part ID, the analysis runs, and the Bluetooth stack is up. The first reading shows all three, since the sensor starts only after Bluetooth. It doesn't wait for the gateway to connect, because a gateway outage shouldn't undo a good update.

**Monitoring during an update.** The upload runs while the oximeter keeps measuring and alarming. Flash work stalls the processor: the nRF52840 halts a processor that runs from flash for each page erase, 85 ms, and Zephyr fits each erase between radio events. The sensor's 32-sample FIFO holds 320 ms at 100 samples a second, so a stall leaves samples waiting, and at each interrupt the firmware reads every sample waiting. Every update after the first finds the release before it where the new one goes, so the upload has to erase it first; a test does that in Renode, each erase timed, and checks the sensor loses no sample. Phase 6 found that the firmware read one sample an interrupt, so the samples a stall left were never caught up: in Renode, pairing alone filled the FIFO, and such an upload lost 43% of the samples. The swap and restart do stop monitoring: 14.9 s in Renode, from the gateway's reset command to the first reading after it, and about 5 s more on the chip, whose flash writes (41 µs a word) Renode doesn't time. Monitoring must resume within 30 s, and a test checks it. So an update is installed only at a time the clinician approves, and the gateway reports "paused for update" to the backend. That way the gap is announced, not silent like T-10 and T-23. Both figures are to be measured again on the board.

## The gateway's own updates

The gateway runs Linux on a Raspberry Pi and updates itself with A/B root partitions:

| What | Design | Status |
|---|---|---|
| Format | a signed update bundle (RAUC, or SWUpdate: both are packaged by Buildroot), signed with a separate gateway release key | planned |
| Install | written to the root partition that isn't running, with the running system untouched | planned |
| Switch | the Pi's firmware boots the new partition once ("tryboot"); the system marks it good only after its health checks pass, or falls back on the next boot | planned |
| Rollback | a version check at install refuses bundles older than the installed one | planned |
| Settings and pairings | kept on a separate data partition both systems can read, so an update doesn't unpair the oximeter | planned |

## Who can start an update

| Step | Who | Control |
|---|---|---|
| Sign a release | the manufacturer's release process only | the release key's access controls (Phase 4); two people approve a release |
| Choose which devices get it, and when | home: the clinic; ward: the hospital's biomedical staff | backend accounts and roles (T-27, T-28) |
| Upload to the oximeter | the gateway it's bonded to | the authenticated SMP link (hop 4) |
| Install it | MCUboot | the signature and the security counter |

A compromised server or gateway can withhold or delay updates, a denial of service that the backend can detect from the versions devices report. It can't install firmware the manufacturer didn't sign (T-22, T-33).

## Third-party software

| Component | Where | How a fix ships |
|---|---|---|
| Zephyr, pinned to v4.4.2 in `app/west.yml` | oximeter | move the pin to the patch release with the fix, rebuild, ship through the path above |
| Zephyr's Bluetooth controller (`CONFIG_BT_LL_SW_SPLIT`) | oximeter | built into the same image, so controller fixes ship with the firmware: no separate radio firmware to update |
| MCUboot | oximeter | MCUboot can't replace itself in this design. A bootloader fix needs physical access, so MCUboot is kept minimal and its configuration fixed. Its vulnerabilities are assessed like any other component's ([vulnerability assessment](vulnerabilities.md)) |
| TinyCrypt, MCUboot's own copy | oximeter, inside MCUboot | built into MCUboot, so a fix needs physical access, as MCUboot's does. It checks signatures instead of PSA Crypto, which the app uses: in MCUboot, PSA Crypto needs a random source, and with no threads for the nRF52840's random number generator driver it falls back to Zephyr's test-only generator. The original TinyCrypt project is no longer maintained, so MCUboot's copy is watched like MCUboot itself. Checking a signature handles no secret, so timing leaks don't apply to it |
| Linux, Buildroot packages | gateway | Buildroot LTS patch releases, shipped as gateway bundles |

FDA also asks manufacturers to keep everything needed to build and test a patch long after release. Here the build is fully pinned: the west manifest, the Zephyr SDK version and the CI configuration. The whole system also runs in Renode with no hardware, so a fix can be tested end to end on any machine years from now. The [SBOM](sbom.md) lists every component, CI checks it against NVD every week ([vulnerability assessment](vulnerabilities.md)), and the postmarket plan in Phase 7 covers how fast fixes ship. Security fixes ship as their own releases, not waiting for feature releases.

## Threats this view addresses

| Threat | Phase 2 baseline | Now |
|---|---|---|
| T-14 Malicious firmware through the debug port | exploitable | MCUboot checks every boot (C-08); the debug-port lock (C-11) is planned |
| T-17 A hang stops monitoring | exploitable | a watchdog resets the oximeter, and an update that hangs is reverted (C-18, C-20) |
| T-32 No field updates | exploitable | updates over Bluetooth from the paired gateway (C-20); the hops before the gateway are planned |
| T-33 Forged image | not yet built | signature checked on the device (C-08, C-09) |
| T-34 Rollback to a vulnerable image | not yet built | security counter (C-21), complete once the debug port is locked |
| T-35 Stolen release key | not yet built | one key per checkout, never committed; the offline signing service is planned, and the single-key limitation above is residual risk |
| T-36 Vulnerable third-party component | partly mitigated | everything above the bootloader can be updated; an SBOM from every build and a weekly vulnerability check (C-22) |
