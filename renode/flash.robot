*** Settings ***
Resource                    platform.resource

*** Variables ***
${FLASH_TEST_ELF}           ${CURDIR}/../build/flash-test/zephyr/zephyr.elf
${NVMC}                     sysbus.nvmc
# A page well clear of the program the erase test runs at the start of flash.
${PAGE}                     0x80000

*** Keywords ***
Should Be Erasing
    [Arguments]               ${erasing}
    ${halted}=                Execute Command    cpu IsHalted
    Should Be Equal           ${halted.strip()}    ${erasing}
    ${ready}=                 Execute Command    ${NVMC} ReadDoubleWord 0x400
    Should Be Equal As Integers    ${ready}    ${{0 if $erasing == "True" else 1}}

*** Test Cases ***
Passes Zephyr's Flash Driver Tests
    # Zephyr's own suite (tests/drivers/flash/common) checks the flash controller model, through
    # the real driver, on the oximeter's platform.
    Create The Platform
    Execute Command           sysbus LoadELF @${FLASH_TEST_ELF}
    Create Terminal Tester    sysbus.uart0
    Start Emulation
    Wait For Line On Uart     PROJECT EXECUTION SUCCESSFUL    timeout=30

Halts The Processor While It Erases A Page
    Create The Platform
    Run A Loop From Flash
    Execute Command           sysbus WriteDoubleWord ${PAGE} 0x12345678
    Execute Command           ${NVMC} WriteDoubleWord 0x504 2
    Execute Command           ${NVMC} WriteDoubleWord 0x508 ${PAGE}
    # tERASEPAGE, 85 ms, in the nRF52840 product specification's NVMC electrical specification.
    Execute Command           emulation RunFor "0.084"
    Should Be Erasing         True
    Execute Command           emulation RunFor "0.002"
    Should Be Erasing         False
