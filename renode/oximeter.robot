*** Settings ***
Library                     Process

*** Variables ***
${OXIMETER_ELF}             ${CURDIR}/../build/oximeter/zephyr/zephyr.elf
${HUB_ELF}                  ${CURDIR}/../build/hub/zephyr/zephyr.elf
# The same hub built with a master of its own, as a gateway that can't make this oximeter's key.
${HUB_OTHER_KEY_ELF}        ${CURDIR}/../build/hub-other-key/zephyr/zephyr.elf
${SCRIPT}                   ${CURDIR}/oximeter.resc
${MASTER}                   ${CURDIR}/../build/limits-master.key
# The simulated oximeter's ID: the FICR DEVICEID values in nrf52840dk-oximeter.repl.
${DEVICE_ID}                0123456789abcdef
${PROVISIONED}              ${CURDIR}/../build/renode/provision.resc
${UNPROVISIONED}            ${CURDIR}/unprovisioned.resc
${UART}                     sysbus.uart0
${SENSOR}                   sysbus.twi0.max30101
${ALARM_LED}                sysbus.gpio0.led0

*** Keywords ***
Start System
    [Arguments]               ${hub_elf}=${HUB_ELF}    ${provisioning}=${PROVISIONED}
    # The factory derives the oximeter's key from the master and its ID, as the hub does.
    ${result}=                Run Process    python3    ${CURDIR}/../tools/provision.py
    ...                       --master    ${MASTER}    --device-id    ${DEVICE_ID}    --renode    ${PROVISIONED}
    Should Be Equal As Integers    ${result.rc}    0    ${result.stderr}
    Execute Command           $oximeter_bin=@${OXIMETER_ELF}
    Execute Command           $hub_bin=@${hub_elf}
    Execute Command           $provisioning=@${provisioning}
    Execute Script            ${SCRIPT}
    ${hub}=                   Create Terminal Tester    ${UART}    machine=hub
    Set Test Variable         ${hub}

Monitor A Patient At 94 Percent
    # 94% on the calibration line SpO2 = 110 - 25 R needs R of about 0.64: the red pulse, against
    # its light level, about 0.64 times the infrared one (2000 on 130000). 1083 on 110000 is that.
    Execute Command           mach set "oximeter"
    Execute Command           ${SENSOR} HeartRate 90
    Execute Command           ${SENSOR} RedPulse 1083
    Create LED Tester         ${ALARM_LED}    machine=oximeter    defaultTimeout=0.5
    Start Emulation

    # The analysis promises the pulse rate to within 1 bpm.
    Wait For Line On Uart     hub: SpO2 94%, pulse (89|90|91) bpm    testerId=${hub}    treatAsRegex=true    timeout=10
    # 94% is above the default 90% limit.
    Assert LED State          false

*** Test Cases ***
Sends Readings To The Hub And Alarms On Limits It Signs
    Start System
    Monitor A Patient At 94 Percent

    # A clinician raises the SpO2 limit above the patient's reading; the hub signs the change over
    # the oximeter's challenge.
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}
    Wait For Line On Uart     ALARM from oximeter: SpO2 low    testerId=${hub}
    Assert LED State          true

Refuses Limits From A Hub Without Its Key
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent

    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter rejected the alarm limits (ATT error 0x13)    testerId=${hub}
    # Monitoring goes on under the old limits: the next reading raises no alarm.
    Wait For Line On Uart     hub: SpO2 94%    testerId=${hub}
    Assert LED State          false

Keeps The Clinician's Limits Through A Reset
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}
    Assert LED State          true

    # A reset, from a flat battery or someone pulling it (threat T-06), must not quietly bring back
    # the defaults: the oximeter starts with the clinician's limits, and nothing is sent again.
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Execute Command           mach set "oximeter"
    Execute Command           machine Reset
    Wait For Line On Uart     alarm limits: SpO2 below 95%    testerId=${oximeter}
    Wait For Line On Uart     ALARM: SpO2 low    testerId=${oximeter}    timeout=10
    Assert LED State          true

Raises The Technical Alarm When The Sensor Stops
    Start System
    Monitor A Patient At 94 Percent

    # Without samples there's nothing to measure: the oximeter must say so, not fall silent. The
    # LED times it, as log lines can trail by up to a second.
    Execute Command           ${SENSOR} Stopped true
    Assert LED State          true    timeout=2
    Wait For Line On Uart     ALARM from oximeter: no pulse found    testerId=${hub}
    Wait For Line On Uart     hub: no reading    testerId=${hub}

Recovers From A Hang Through The Watchdog
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    # The processor stops, as in a hang: the watchdog resets it, and it measures again under the
    # clinician's limits.
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Execute Command           mach set "oximeter"
    Execute Command           cpu IsHalted true
    Wait For Line On Uart     alarm limits: SpO2 below 95%    testerId=${oximeter}    timeout=5
    Wait For Line On Uart     SpO2 94%, pulse (89|90|91) bpm    testerId=${oximeter}    treatAsRegex=true    timeout=10
    Assert LED State          true

Logs The Security Events The Hub Reads
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#1 boot 1 \\+[0-9.]+ s: started    testerId=${hub}    treatAsRegex=true
    Wait For Line On Uart     \#2 boot 1 \\+[0-9.]+ s: connected: C0:00:AA:BB:CC:DD \\(random\\)    testerId=${hub}    treatAsRegex=true
    Wait For Line On Uart     \#3 boot 1 \\+[0-9.]+ s: limits accepted: SpO2 below 95%, pulse below 50 or above 120 bpm    testerId=${hub}    treatAsRegex=true

Logs A Refused Change From A Hub Without Its Key
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter rejected the alarm limits (ATT error 0x13)    testerId=${hub}

    # Anyone in range can read the log until pairing, the stranger included.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#3 boot 1 \\+[0-9.]+ s: limits refused: tag doesn't match    testerId=${hub}    treatAsRegex=true

Refuses Every Change Without A Device Key
    Start System              provisioning=${UNPROVISIONED}
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter rejected the alarm limits (ATT error 0x13)    testerId=${hub}
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#2 boot 1 \\+[0-9.]+ s: no device key provisioned    testerId=${hub}    treatAsRegex=true
    Assert LED State          false

Verifies Every Record Of The Log
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    # Each record carries a MAC under the oximeter's log key, which the backend can derive.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     3 security records, 3 verified    testerId=${hub}

Can't Verify The Log Without The Master
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent

    # A gateway without the master can read the log but can neither vouch for it nor forge it.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     [0-9]+ security records, 0 verified    testerId=${hub}    treatAsRegex=true
