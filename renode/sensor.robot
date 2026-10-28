*** Settings ***
Resource                    platform.resource

*** Variables ***
${SENSOR}                   sysbus.twi0.max30101
${FIFO_WRITE_POINTER}       0x04
${OVERFLOW_COUNTER}         0x05
${FIFO_READ_POINTER}        0x06
${FIFO_DATA}                0x07

*** Keywords ***
Write Sensor Register
    [Arguments]               ${register}    ${value}
    Execute Command           ${SENSOR} Write [${register}, ${value}]
    Execute Command           ${SENSOR} FinishTransmission

Read Sensor Registers
    [Arguments]               ${register}    ${count}
    Execute Command           ${SENSOR} Write [${register}]
    ${bytes}=                 Execute Command    ${SENSOR} Read ${count}
    Execute Command           ${SENSOR} FinishTransmission
    ${values}=                Evaluate    [int(b, 16) for b in re.findall("0x[0-9A-Fa-f]+", $bytes)]
    ...                       modules=re
    RETURN                    ${values}

Read Sensor Register
    [Arguments]               ${register}
    ${values}=                Read Sensor Registers    ${register}    1
    RETURN                    ${values}[0]

Start Sampling At 100 Per Second
    # As the oximeter sets it up: SpO2 mode, 100 samples a second, 411 us pulses, no averaging.
    Create The Platform
    Run A Loop From Flash
    Write Sensor Register     0x0A    0x07
    Write Sensor Register     0x09    0x03

Read Samples
    [Arguments]               ${count}
    # Red and infrared, 3 bytes each.
    FOR    ${i}    IN RANGE    ${count}
        Read Sensor Registers    ${FIFO_DATA}    6
    END

Should Count Lost Samples
    [Arguments]               ${seconds}    ${counter}    ${lost}
    Reset Emulation
    Start Sampling At 100 Per Second
    Execute Command           emulation RunFor "${seconds}"
    ${overflow}=              Read Sensor Register    ${OVERFLOW_COUNTER}
    Should Be Equal As Integers    ${overflow}    ${counter}
    ${total}=                 Execute Command    ${SENSOR} LostSamples
    Should Be Equal As Integers    ${total}    ${lost}

*** Test Cases ***
Points At The Samples Waiting
    Start Sampling At 100 Per Second
    Execute Command           emulation RunFor "0.105"
    Read Samples              3
    # Ten samples taken, three read: seven wait between the pointers.
    ${write}=                 Read Sensor Register    ${FIFO_WRITE_POINTER}
    ${read}=                  Read Sensor Register    ${FIFO_READ_POINTER}
    Should Be Equal As Integers    ${write}    10
    Should Be Equal As Integers    ${read}    3
    ${unread}=                Execute Command    ${SENSOR} Unread
    Should Be Equal As Integers    ${unread}    7

Counts The Samples A Full FIFO Loses
    [Template]                Should Count Lost Samples
    # The FIFO holds 32 samples, and its counter stops at 31; LostSamples, for tests, counts them
    # all.
    0.505    18    18
    1.005    31    68

Clears Its Count Of Lost Samples When One Is Read
    Start Sampling At 100 Per Second
    Execute Command           emulation RunFor "0.505"
    ${overflow}=              Read Sensor Register    ${OVERFLOW_COUNTER}
    Should Not Be Equal As Integers    ${overflow}    0
    Read Samples              1
    ${overflow}=              Read Sensor Register    ${OVERFLOW_COUNTER}
    Should Be Equal As Integers    ${overflow}    0
