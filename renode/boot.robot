*** Settings ***
Resource                    images.resource

*** Variables ***
${SCRIPT}                   ${CURDIR}/oximeter.resc
${SIGNED_IMAGE}             ${CURDIR}/../build/oximeter/app/zephyr/zephyr.signed.hex
# In the oximeter's program: its slot starts at 0xC000, after MCUboot, with a 0x200-byte header.
${PROGRAM_BYTE}             0xD000
# Where an update's header lands in slot 1 (images.resource).
${UPDATE_HEADER}            ${0x83000}
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
    # At 85 ms a page, MCUboot takes about 10 s to swap an update in or erase one it refuses.
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter    timeout=30
    Set Test Variable         ${oximeter}

Load The Update
    [Arguments]               ${update}
    Execute Command           mach set "oximeter"
    Execute Command           sysbus LoadHEX @${update}

Reset The Oximeter
    Execute Command           mach set "oximeter"
    Execute Command           machine Reset

Read From Flash
    [Arguments]               ${size}    ${address}
    ${value}=                 Execute Command    sysbus Read${size} ${address}
    ${number}=                Evaluate    int($value.strip(), 16)
    RETURN                    ${number}

Add A Security Counter Outside The Signature
    [Arguments]               ${counter}
    # After the program come the fields the signature covers, then the ones it doesn't: a magic
    # number (0x6907) and their total size, then each field's type and length (LE16 each) and value.
    Execute Command           mach set "oximeter"
    ${header_size}=           Read From Flash    Word    ${UPDATE_HEADER + 8}
    ${signed_size}=           Read From Flash    Word    ${UPDATE_HEADER + 10}
    ${program_size}=          Read From Flash    DoubleWord    ${UPDATE_HEADER + 12}
    ${unsigned}=              Evaluate    ${UPDATE_HEADER} + ${header_size} + ${program_size} + ${signed_size}
    ${magic}=                 Read From Flash    Word    ${unsigned}
    Should Be Equal As Integers    ${magic}    0x6907
    ${total}=                 Read From Flash    Word    ${unsigned + 2}
    ${field}=                 Evaluate    ${unsigned} + ${total}
    # IMAGE_TLV_SEC_CNT, as imgtool writes it in the signed fields.
    Execute Command           sysbus WriteWord ${field} 0x50
    Execute Command           sysbus WriteWord ${field + 2} 4
    Execute Command           sysbus WriteDoubleWord ${field + 4} ${counter}
    Execute Command           sysbus WriteWord ${unsigned + 2} ${total + 8}

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

Refuses An Unsigned Update
    # Hashed, so MCUboot can tell it's intact, but signed by no one.
    Sign An Update            1.1.0    1    ${TEST_IMAGES}/unsigned-update.hex    key=${EMPTY}
    Load The Oximeter
    Load The Update           ${TEST_IMAGES}/unsigned-update.hex
    Start Emulation
    Wait For Line On Uart     Image in the secondary slot is not valid    testerId=${oximeter}
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}

Refuses An Update With A Field Added Outside Its Signature
    # A genuine update, given a higher security counter where the signature doesn't reach, as
    # CVE-2024-32883 describes.
    Sign An Update            1.1.0    1    ${TEST_IMAGES}/update.hex
    Load The Oximeter
    Load The Update           ${TEST_IMAGES}/update.hex
    Add A Security Counter Outside The Signature    99
    Start Emulation
    Wait For Line On Uart     Image in the secondary slot is not valid    testerId=${oximeter}
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}
