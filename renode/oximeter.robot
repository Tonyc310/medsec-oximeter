*** Variables ***
${OXIMETER_ELF}     ${CURDIR}/../build/oximeter/zephyr/zephyr.elf
${HUB_ELF}          ${CURDIR}/../build/hub/zephyr/zephyr.elf
${SCRIPT}           ${CURDIR}/oximeter.resc
${UART}             sysbus.uart0
${SENSOR}           sysbus.twi0.max30101

*** Keywords ***
Start System
    Execute Command           $oximeter_bin=@${OXIMETER_ELF}
    Execute Command           $hub_bin=@${HUB_ELF}
    Execute Script            ${SCRIPT}
    ${hub}=                   Create Terminal Tester    ${UART}    machine=hub
    Set Test Variable         ${hub}

*** Test Cases ***
Sends SpO2 And Pulse Rate To The Hub
    Start System
    # 94% on the calibration line SpO2 = 110 - 25 R needs R of about 0.64: the red pulse, against
    # its light level, about 0.64 times the infrared one (2000 on 130000). 1083 on 110000 is that.
    Execute Command           mach set "oximeter"
    Execute Command           ${SENSOR} HeartRate 90
    Execute Command           ${SENSOR} RedPulse 1083
    Start Emulation
    # The analysis promises the pulse rate to within 1 bpm.
    Wait For Line On Uart     hub: SpO2 94%, pulse (89|90|91) bpm    testerId=${hub}    treatAsRegex=true    timeout=10
