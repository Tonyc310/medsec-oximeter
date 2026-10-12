*** Variables ***
${OXIMETER_ELF}             ${CURDIR}/../build/oximeter/zephyr/zephyr.elf
${HUB_ELF}                  ${CURDIR}/../build/hub/zephyr/zephyr.elf
# The same hub built with a key of its own, as a gateway that wasn't given this oximeter's key.
${HUB_OTHER_KEY_ELF}        ${CURDIR}/../build/hub-other-key/zephyr/zephyr.elf
${SCRIPT}                   ${CURDIR}/oximeter.resc
${UART}                     sysbus.uart0
${SENSOR}                   sysbus.twi0.max30101
${ALARM_LED}                sysbus.gpio0.led0

*** Keywords ***
Start System
    [Arguments]               ${hub_elf}=${HUB_ELF}
    Execute Command           $oximeter_bin=@${OXIMETER_ELF}
    Execute Command           $hub_bin=@${hub_elf}
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
