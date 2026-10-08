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
Reads Red And Infrared Light
    Start Board
    # With the heartbeat switched off every sample is the steady level, so each second's mean is exact.
    Execute Command           ${SENSOR} RedPulse 0
    Execute Command           ${SENSOR} InfraredPulse 0
    Start Emulation
    Wait For Line On Uart     ppg: red 110000, ir 130000
