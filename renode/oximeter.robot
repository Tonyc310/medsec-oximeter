*** Variables ***
${ELF}          ${CURDIR}/../build/nrf52840dk/zephyr/zephyr.elf
${SCRIPT}       ${CURDIR}/oximeter.resc
${UART}         sysbus.uart0
${SENSOR}       sysbus.twi0.max30101

*** Keywords ***
Start Board
    Execute Command           $bin=@${ELF}
    Execute Script            ${SCRIPT}
    Create Terminal Tester    ${UART}

*** Test Cases ***
Reports SpO2 And Pulse Rate
    Start Board
    # 94% on the calibration line SpO2 = 110 - 25 R needs R of about 0.64: the red pulse, against
    # its light level, about 0.64 times the infrared one (2000 on 130000). 1083 on 110000 is that.
    Execute Command           ${SENSOR} HeartRate 90
    Execute Command           ${SENSOR} RedPulse 1083
    Start Emulation
    Wait For Line On Uart     SpO2 94%, pulse 90 bpm
