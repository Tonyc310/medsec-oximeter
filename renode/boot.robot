*** Settings ***
Resource                    images.resource

*** Variables ***
${SCRIPT}                   ${CURDIR}/oximeter.resc
${SIGNED_IMAGE}             ${CURDIR}/../build/oximeter/app/zephyr/zephyr.signed.hex
# The development key that comes with MCUboot, and sysbuild's default: its private half is public.
${PUBLISHED_KEY}            ${CURDIR}/../deps/bootloader/mcuboot/root-ec-p256.pem
# In the oximeter's program: its slot starts at 0xC000, after MCUboot, with a 0x200-byte header.
${PROGRAM_BYTE}             0xD000
# Checking signatures needs no device keys.
${UNPROVISIONED}            ${CURDIR}/unprovisioned.resc
${UART}                     sysbus.uart0
${SENSOR}                   sysbus.twi0.max30101

*** Keywords ***
Load The Oximeter
    [Arguments]               ${image}=${SIGNED_IMAGE}
    Execute Command           $oximeter_image=@${image}
    Execute Command           $provisioning=@${UNPROVISIONED}
    Execute Script            ${SCRIPT}
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter    timeout=5
    Set Test Variable         ${oximeter}

Load The Update
    [Arguments]               ${update}
    Execute Command           mach set "oximeter"
    Execute Command           sysbus LoadHEX @${update}

Reset The Oximeter
    Execute Command           mach set "oximeter"
    Execute Command           machine Reset

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

Refuses An Image Changed After Signing
    Load The Oximeter
    # One byte of the program flipped, as by someone rewriting the flash.
    Execute Command           mach set "oximeter"
    ${byte}=                  Execute Command    sysbus ReadByte ${PROGRAM_BYTE}
    ${flipped}=               Evaluate    int($byte.strip(), 16) ^ 0xFF
    Execute Command           sysbus WriteByte ${PROGRAM_BYTE} ${flipped}
    Start Emulation
    Wait For Line On Uart     Unable to find bootable image    testerId=${oximeter}
    Should Not Be On Uart     medsec-oximeter    testerId=${oximeter}    timeout=3

Keeps An Update That Passes Its Self-Test
    Sign An Update            1.1.0    1    ${TEST_IMAGES}/update.hex
    Load The Oximeter
    Load The Update           ${TEST_IMAGES}/update.hex
    Start Emulation
    Wait For Line On Uart     medsec-oximeter 1.1.0+0 started    testerId=${oximeter}
    Wait For Line On Uart     image marked as working    testerId=${oximeter}    timeout=10
    Reset The Oximeter
    Wait For Line On Uart     medsec-oximeter 1.1.0+0 started    testerId=${oximeter}

Reverts An Update That Fails Its Self-Test
    Sign An Update            1.1.0    1    ${TEST_IMAGES}/update.hex
    Load The Oximeter
    Load The Update           ${TEST_IMAGES}/update.hex
    # The sensor answers with its part ID but sends no samples, so the analysis never runs.
    Execute Command           ${SENSOR} Stopped true
    Start Emulation
    Wait For Line On Uart     medsec-oximeter 1.1.0+0 started    testerId=${oximeter}
    # Longer than a working sensor takes to give the first reading.
    Should Not Be On Uart     image marked as working    testerId=${oximeter}    timeout=10
    Reset The Oximeter
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}

Refuses An Older Release
    # A genuine release from before a security fix, with the lower counter it shipped with.
    Sign An Update            0.9.0    0    ${TEST_IMAGES}/older.hex
    Load The Oximeter
    Load The Update           ${TEST_IMAGES}/older.hex
    Start Emulation
    Wait For Line On Uart     erased due to downgrade prevention    testerId=${oximeter}
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}
