*** Variables ***
${FLASH_TEST_ELF}           ${CURDIR}/../build/flash-test/zephyr/zephyr.elf

*** Test Cases ***
Passes Zephyr's Flash Driver Tests
    # Zephyr's own suite (tests/drivers/flash/common) checks the flash controller model, through
    # the real driver, on the oximeter's platform.
    Execute Command           include @${CURDIR}/Max30101.cs
    Execute Command           include @${CURDIR}/Nvmc.cs
    Execute Command           include @${CURDIR}/Ccm.cs
    Execute Command           mach create
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-oximeter.repl
    Execute Command           sysbus LoadELF @${FLASH_TEST_ELF}
    Create Terminal Tester    sysbus.uart0
    Start Emulation
    Wait For Line On Uart     PROJECT EXECUTION SUCCESSFUL    timeout=30
