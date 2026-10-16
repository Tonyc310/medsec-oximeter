*** Variables ***
${CCM}                      sysbus.ccm
# RAM the tests give the CCM for its configuration and packets: no firmware runs.
${CONFIGURATION}            0x20030000
${INPUT}                    0x20030100
${OUTPUT}                   0x20030200
# The Bluetooth Core specification's sample data (Vol 6, Part C, 1): the session key, most
# significant byte first as the CCM reads it, and the IV.
${SESSION_KEY}              99ad1b5226a37e3e058e3b8e27c2c666
${IV}                       24abdcbabebaafde

*** Keywords ***
Create The Platform
    Execute Command           include @${CURDIR}/Max30101.cs
    Execute Command           include @${CURDIR}/Nvmc.cs
    Execute Command           include @${CURDIR}/Ccm.cs
    Execute Command           mach create
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-oximeter.repl

Write Hex
    [Arguments]               ${address}    ${hex}
    ${bytes}=                 Evaluate    "[" + ", ".join(hex(b) for b in bytes.fromhex($hex)) + "]"
    Execute Command           sysbus WriteBytes ${bytes} ${address}

Read Hex
    [Arguments]               ${address}    ${count}
    ${bytes}=                 Execute Command    sysbus ReadBytes ${address} ${count}
    ${hex}=                   Evaluate    bytes(int(b, 16) for b in re.findall("0x[0-9A-F]+", $bytes)).hex()
    ...                       modules=re
    RETURN                    ${hex}

Configure The Link
    [Arguments]               ${packet_counter}    ${direction}
    # What CNFPTR points at: the key, the 39-bit packet counter in 8 bytes, the direction bit (1 for
    # packets from the central), and the IV.
    ${counter}=               Evaluate    int($packet_counter).to_bytes(8, "little").hex()
    Write Hex                 ${CONFIGURATION}    ${SESSION_KEY}${counter}0${direction}${IV}
    Execute Command           ${CCM} WriteDoubleWord 0x508 ${CONFIGURATION}
    Execute Command           ${CCM} WriteDoubleWord 0x50C ${INPUT}
    Execute Command           ${CCM} WriteDoubleWord 0x510 ${OUTPUT}
    Execute Command           ${CCM} WriteDoubleWord 0x500 2

Encrypt
    [Arguments]               ${packet}
    # As Zephyr sends a packet: 8-bit length field, and the shortcut from key-stream generation to
    # encryption, so one task does both.
    Write Hex                 ${INPUT}    ${packet}
    Execute Command           ${CCM} WriteDoubleWord 0x504 0x01000000
    Execute Command           ${CCM} WriteDoubleWord 0x200 1
    Execute Command           ${CCM} WriteDoubleWord 0x000 1

Decrypt
    [Arguments]               ${packet}
    # As Zephyr receives a packet: the key stream when it sets up the radio, then decryption when
    # the radio hears the packet's access address.
    Write Hex                 ${INPUT}    ${packet}
    Execute Command           ${CCM} WriteDoubleWord 0x504 0x01000001
    Execute Command           ${CCM} WriteDoubleWord 0x200 0
    Execute Command           ${CCM} WriteDoubleWord 0x000 1
    Execute Command           ${CCM} WriteDoubleWord 0x004 1

MIC Should Be
    [Arguments]               ${valid}
    ${status}=                Execute Command    ${CCM} ReadDoubleWord 0x400
    Should Be Equal As Integers    ${status.strip()}    ${valid}

Decrypting Should Fail The MIC
    [Arguments]               ${packet}
    Configure The Link        0    1
    Decrypt                   ${packet}
    MIC Should Be             0

Should Leave An Empty Packet As It Is
    [Arguments]               ${operation}
    Configure The Link        1    0
    Run Keyword               ${operation}    010000
    ${packet}=                Read Hex    ${OUTPUT}    3
    Should Be Equal           ${packet}    010000

*** Test Cases ***
Encrypts A Packet As The Bluetooth Specification Does
    Create The Platform
    # The specification's data packet 2: the peripheral's second packet, so packet counter 1. In
    # RAM a packet is its header, its length and a spare byte (S1), then the payload.
    Configure The Link        1    0
    Encrypt                   061b00170037363534333231304142434445464748494a4b4c4d4e4f5051
    ${packet}=                Read Hex    ${OUTPUT}    34
    # The same header, 4 bytes longer for the MIC that follows the encrypted payload.
    Should Be Equal           ${packet}    061f00f38881e7bd94c9c369b9a66846dd4786aa8c39ce540d0dae3adcdf89b96088

Decrypts A Packet And Accepts Its MIC
    Create The Platform
    # The specification's LL_START_ENC_RSP from the central: the first packet after encryption
    # starts, so packet counter 0.
    Configure The Link        0    1
    Decrypt                   0f05009fcda7f448
    ${packet}=                Read Hex    ${OUTPUT}    4
    Should Be Equal           ${packet}    0f010006
    MIC Should Be             1

Reports A Packet It Can't Authenticate
    [Setup]                   Create The Platform
    [Template]                Decrypting Should Fail The MIC
    # The same packet with one bit of the encrypted payload flipped, by interference or an attacker.
    0f05009ecda7f448
    # Too short to hold a MIC.
    0f02009fcd

Passes An Empty Packet Through Unencrypted
    # Bluetooth sends empty packets, which keep the connection going, without encryption or a MIC.
    [Setup]                   Create The Platform
    [Template]                Should Leave An Empty Packet As It Is
    Encrypt
    Decrypt

Signals The End Of Decryption With An Interrupt
    # Zephyr's controller sleeps until this interrupt, to check the MIC of the packet it received.
    Create The Platform
    Configure The Link        0    1
    Execute Command           ${CCM} WriteDoubleWord 0x304 0x2
    Decrypt                   0f05009fcda7f448
    ${ended}=                 Execute Command    ${CCM} ReadDoubleWord 0x104
    Should Be Equal As Integers    ${ended.strip()}    1
    ${interrupt}=             Execute Command    ${CCM} IRQ IsSet
    Should Contain            ${interrupt}    True
