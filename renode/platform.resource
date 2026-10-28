*** Keywords ***
Create The Platform
    # The oximeter's board and models, for tests of the models themselves.
    Execute Command           include @${CURDIR}/Max30101.cs
    Execute Command           include @${CURDIR}/Nvmc.cs
    Execute Command           include @${CURDIR}/Ccm.cs
    Execute Command           include @${CURDIR}/Radio.cs
    Execute Command           mach create
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-oximeter.repl

Run A Loop From Flash
    # For tests that let time pass: a CPU with nothing to run would fault. Firmware runs from flash,
    # which is why an erase stalls it: a vector table, then `b .`.
    Execute Command           sysbus WriteDoubleWord 0x0 0x20001000
    Execute Command           sysbus WriteDoubleWord 0x4 0x101
    Execute Command           sysbus WriteWord 0x100 0xE7FE
    Execute Command           cpu SP 0x20001000
    Execute Command           cpu PC 0x100
