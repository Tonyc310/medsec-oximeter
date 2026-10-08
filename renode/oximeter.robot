*** Variables ***
${ELF}          ${CURDIR}/../build/nrf52840dk/zephyr/zephyr.elf
${SCRIPT}       ${CURDIR}/oximeter.resc
${UART}         sysbus.uart0

*** Test Cases ***
Boots
    Execute Command           $bin=@${ELF}
    Execute Script            ${SCRIPT}
    Create Terminal Tester    ${UART}
    Start Emulation
    Wait For Line On Uart     medsec-oximeter started
