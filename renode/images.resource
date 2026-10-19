*** Settings ***
Documentation               Signing the oximeter's firmware with MCUboot's imgtool, as the build does,
...                         for the tests that boot or install images of their own.
Library                     Process

*** Variables ***
${UNSIGNED_IMAGE}           ${CURDIR}/../build/oximeter/app/zephyr/zephyr.hex
${IMGTOOL}                  ${CURDIR}/../deps/bootloader/mcuboot/scripts/imgtool.py
${RELEASE_KEY}              ${CURDIR}/../build/release-key.pem
${TEST_IMAGES}              ${CURDIR}/../build/renode

*** Keywords ***
Sign
    [Arguments]               ${key}    ${version}    ${output}    @{placement}
    # As the build signs it: room for the header MCUboot reads, and the flash's 4-byte writes.
    ${result}=                Run Process    python3    ${IMGTOOL}    sign    --version    ${version}
    ...                       --header-size    0x200    --align    4    --key    ${key}    @{placement}
    ...                       ${UNSIGNED_IMAGE}    ${output}
    Should Be Equal As Integers    ${result.rc}    0    ${result.stderr}

Sign The Oximeter
    [Arguments]               ${key}    ${output}
    Sign                      ${key}    1.0.0    ${output}    --slot-size    0x76000

Sign An Update
    [Arguments]               ${version}    ${security_counter}    ${output}
    # Where an upload leaves it: swap using offset keeps slot 1's first sector free, so the image
    # starts a sector in, at 0x83000, and the rest of the slot is its own. --pad adds the mark an
    # upload's test command writes, asking MCUboot for a test boot.
    Sign                      ${RELEASE_KEY}    ${version}    ${output}
    ...                       --security-counter    ${security_counter}
    ...                       --slot-size    0x75000    --pad    --hex-addr    0x83000

Sign A Release
    [Arguments]               ${version}    ${output}
    # A release as the build signs it, with app/prj.conf's security counter: what the backend sends.
    Sign                      ${RELEASE_KEY}    ${version}    ${output}
    ...                       --security-counter    1    --slot-size    0x76000
