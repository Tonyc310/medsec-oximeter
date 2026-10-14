*** Settings ***
Library                     Process

*** Variables ***
${SCRIPT}                   ${CURDIR}/oximeter.resc
${SIGNED_IMAGE}             ${CURDIR}/../build/oximeter/app/zephyr/zephyr.signed.hex
${UNSIGNED_IMAGE}           ${CURDIR}/../build/oximeter/app/zephyr/zephyr.hex
${IMGTOOL}                  ${CURDIR}/../deps/bootloader/mcuboot/scripts/imgtool.py
# The development key that comes with MCUboot, and sysbuild's default: its private half is public.
${PUBLISHED_KEY}            ${CURDIR}/../deps/bootloader/mcuboot/root-ec-p256.pem
${TEST_IMAGES}              ${CURDIR}/../build/renode
# Checking signatures needs no device keys.
${UNPROVISIONED}            ${CURDIR}/unprovisioned.resc
${UART}                     sysbus.uart0

*** Keywords ***
Load The Oximeter
    [Arguments]               ${image}=${SIGNED_IMAGE}
    Execute Command           $oximeter_image=@${image}
    Execute Command           $provisioning=@${UNPROVISIONED}
    Execute Script            ${SCRIPT}
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter    timeout=5
    Set Test Variable         ${oximeter}

Sign The Oximeter
    [Arguments]               ${key}    ${output}
    # As the build signs it: room for the header MCUboot reads, the image slot's size, and the
    # flash's 4-byte writes.
    ${result}=                Run Process    python3    ${IMGTOOL}    sign    --version    1.0.0
    ...                       --header-size    0x200    --slot-size    483328    --align    4
    ...                       --key    ${key}    ${UNSIGNED_IMAGE}    ${output}
    Should Be Equal As Integers    ${result.rc}    0    ${result.stderr}

*** Test Cases ***
Boots The Signed Oximeter Through MCUboot
    Load The Oximeter
    Start Emulation
    Wait For Line On Uart     Booting MCUboot    testerId=${oximeter}
    # The version MCUboot checked, read back from the image's signed header.
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}

Refuses An Image Signed With MCUboot's Published Key
    Sign The Oximeter         ${PUBLISHED_KEY}    ${TEST_IMAGES}/published-key.hex
    Load The Oximeter         ${TEST_IMAGES}/published-key.hex
    Start Emulation
    Wait For Line On Uart     Unable to find bootable image    testerId=${oximeter}
    Should Not Be On Uart     medsec-oximeter    testerId=${oximeter}    timeout=3
