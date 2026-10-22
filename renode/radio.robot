*** Variables ***
${RADIO}                    sysbus.radio
# RAM the tests give each radio for its packet: no firmware runs.
${PACKET}                   0x20030000
# A data packet as Zephyr lays it out in RAM: header, length 3, payload.
${FRAME}                    0203a1b2c3

*** Keywords ***
Create Two Radios
    Reset Emulation
    Execute Command           include @${CURDIR}/Radio.cs
    Execute Command           emulation CreateBLEMedium "wireless"
    FOR    ${name}    IN    sender    receiver
        Execute Command           mach create "${name}"
        Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk.repl
        # No firmware: the processors stay halted while the tests drive the radios.
        Execute Command           cpu IsHalted true
        Execute Command           connector Connect ${RADIO} wireless
        Configure The Radio       ${name}    channel=0
    END
    Execute Command           emulation SetGlobalQuantum "0.00001"

Configure The Radio
    [Arguments]               ${machine}    ${channel}
    # Bluetooth LE packets as Zephyr's controller sets them: 1-byte S0 and 8-bit length fields,
    # 3-byte base address and little endian; the radio starts once ready and stops at the end.
    Execute Command           mach set "${machine}"
    Execute Command           ${RADIO} WriteDoubleWord 0x514 0x00000108
    Execute Command           ${RADIO} WriteDoubleWord 0x518 0x000300FF
    Execute Command           ${RADIO} WriteDoubleWord 0x51C 0x89BED600
    Execute Command           ${RADIO} WriteDoubleWord 0x524 0x0000008E
    Execute Command           ${RADIO} WriteDoubleWord 0x52C 0
    Execute Command           ${RADIO} WriteDoubleWord 0x530 1
    Execute Command           ${RADIO} WriteDoubleWord 0x504 ${PACKET}
    Execute Command           ${RADIO} WriteDoubleWord 0x200 3
    Tune                      ${machine}    ${channel}

Tune
    [Arguments]               ${machine}    ${channel}
    # Data channel n is at 2404 + 2n MHz; FREQUENCY counts MHz above 2400.
    ${offset}=                Evaluate    4 + 2 * int($channel)
    Execute Command           mach set "${machine}"
    Execute Command           ${RADIO} WriteDoubleWord 0x508 ${offset}

Send A Frame
    ${bytes}=                 Evaluate    "[" + ", ".join(hex(b) for b in bytes.fromhex($FRAME)) + "]"
    Execute Command           mach set "sender"
    Execute Command           sysbus WriteBytes ${bytes} ${PACKET}
    Execute Command           ${RADIO} WriteDoubleWord 0x000 1

Listen
    Execute Command           mach set "receiver"
    Execute Command           ${RADIO} WriteDoubleWord 0x004 1

Wait
    [Arguments]               ${seconds}
    Execute Command           emulation RunFor "${seconds}"

Received
    Execute Command           mach set "receiver"
    ${bytes}=                 Execute Command    sysbus ReadBytes ${PACKET} 5
    ${hex}=                   Evaluate    bytes(int(b, 16) for b in re.findall("0x[0-9A-F]+", $bytes)).hex()
    ...                       modules=re
    RETURN                    ${hex}

Loses A Frame It Missed
    [Arguments]               ${listen_after}    ${channel}
    # The frame goes out while the receiver isn't listening.
    Create Two Radios
    Send A Frame
    Wait                      ${listen_after}
    Tune                      receiver    ${channel}
    Listen
    Wait                      0.001
    ${received}=              Received
    Should Be Equal           ${received}    0000000000

*** Test Cases ***
Loses A Frame It Missed In An Earlier Event
    [Template]                Loses A Frame It Missed
    # A connection event is at least 7.5 ms after the last: a frame 5 ms old is from an earlier one.
    0.005                     0
    # The next event is on another channel.
    0.00015                   1

Hears A Frame That Arrives As It Turns Around
    # In one event the receiver may still be turning from its own packet to listening, 150 us, when
    # the other's arrives; the simulation has no air time, so it hears it as the real radio does.
    Create Two Radios
    Send A Frame
    Wait                      0.00015
    Listen
    Wait                      0.001
    ${received}=              Received
    Should Be Equal           ${received}    ${FRAME}
