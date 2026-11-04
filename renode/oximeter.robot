*** Settings ***
Library                     OperatingSystem
Library                     Process
Resource                    images.resource

*** Variables ***
${OXIMETER_IMAGE}           ${CURDIR}/../build/oximeter/app/zephyr/zephyr.signed.hex
${HUB_ELF}                  ${CURDIR}/../build/hub/zephyr/zephyr.elf
# The same hub built with a signing master of its own, as a gateway the backend let pair but won't
# sign for.
${HUB_OTHER_KEY_ELF}        ${CURDIR}/../build/hub-other-key/zephyr/zephyr.elf
# A hub built with a pairing master of its own: on the hub's board, a device with the gateway's
# address but without the keys to pair.
${HUB_OTHER_PAIRING_ELF}    ${CURDIR}/../build/hub-other-pairing/zephyr/zephyr.elf
# Zephyr's Bluetooth shell (tests/bluetooth/shell) in the hub's place: a phone in range, trying
# what any central can.
${PHONE_ELF}                ${CURDIR}/../build/phone/zephyr/zephyr.elf
# A fake oximeter (renode/fake_oximeter): the real one's characteristics and device ID, without its
# pairing key.
${FAKE_OXIMETER_ELF}        ${CURDIR}/../build/fake-oximeter/zephyr/zephyr.elf
${SCRIPT}                   ${CURDIR}/oximeter.resc
${SIGNING_MASTER}           ${CURDIR}/../build/signing-master.key
${PAIRING_MASTER}           ${CURDIR}/../build/pairing-master.key
# The simulated oximeter's ID: the FICR DEVICEID values in nrf52840dk-oximeter.repl.
${DEVICE_ID}                0123456789abcdef
${PROVISIONED}              ${CURDIR}/../build/renode/provision.resc
${UNPROVISIONED}            ${CURDIR}/unprovisioned.resc
${UART}                     sysbus.uart0
# The hub's slot 1 partition, where it keeps the image the backend sent (hub/src/update.c).
${STAGING}                  0x82000
${SENSOR}                   sysbus.twi0.max30101
${ALARM_LED}                sysbus.gpio0.led0
# Where the oximeter takes an update: slot 1, a sector in, as MCUboot's swap using offset wants it.
${UPDATE_AREA}              0x83000
${LAST_RELEASE}             ${CURDIR}/../build/oximeter/app/zephyr/zephyr.signed.bin
# The longest monitoring may stop for an update, from the gateway's reset command to the first
# reading after it: the gateway reports the pause, at a time the clinician approved.
${MONITORING_GAP}           30
# How long a central may stay connected without pairing: Bluetooth's own pairing timeout.
${PAIRING_TIMEOUT}          ${30}

*** Keywords ***
Start System
    [Arguments]               ${hub_elf}=${HUB_ELF}    ${provisioning}=${PROVISIONED}
    # The factory derives the oximeter's keys from the masters and its ID, as the hub does.
    ${result}=                Run Process    python3    ${CURDIR}/../tools/provision.py
    ...                       --signing-master    ${SIGNING_MASTER}    --pairing-master    ${PAIRING_MASTER}
    ...                       --device-id    ${DEVICE_ID}    --renode    ${PROVISIONED}
    Should Be Equal As Integers    ${result.rc}    0    ${result.stderr}
    Execute Command           $oximeter_image=@${OXIMETER_IMAGE}
    Execute Command           $hub_bin=@${hub_elf}
    Execute Command           $provisioning=@${provisioning}
    Execute Script            ${SCRIPT}
    ${hub}=                   Create Terminal Tester    ${UART}    machine=hub
    Set Test Variable         ${hub}

Start System With An Update
    [Arguments]               ${image}
    # The backend's image, staged in the hub's flash for its update command.
    Start System
    Execute Command           mach set "hub"
    Execute Command           sysbus LoadBinary @${image} ${STAGING}
    ${update_size}=           Get File Size    ${image}
    Set Test Variable         ${update_size}
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Set Test Variable         ${oximeter}

Leave The Last Release In Slot 1
    # Every update after the first finds the release before it where the new one goes.
    Execute Command           mach set "oximeter"
    Execute Command           sysbus LoadBinary @${LAST_RELEASE} ${UPDATE_AREA}

Virtual Seconds
    ${info}=                  Execute Command    emulation GetTimeSourceInfo
    ${time}=                  Evaluate    re.search("Elapsed Virtual Time: ([0-9]+):([0-9]+):([0-9.]+)", $info)
    ...                       modules=re
    RETURN                    ${{3600 * int($time[1]) + 60 * int($time[2]) + float($time[3])}}

Hold Button 1
    # Renode's GPIOTE misses a pin's first edge after Zephyr sets up its interrupt, so a tap first
    # shows it the button is up.
    Execute Command           mach set "oximeter"
    Execute Command           sysbus.gpio0.sw0 PressAndRelease
    Execute Command           sysbus.gpio0.sw0 Press

Release Button 1
    Execute Command           mach set "oximeter"
    Execute Command           sysbus.gpio0.sw0 Release

Reset The Oximeter For A New Patient
    # As staff do before the oximeter goes to the next patient: Button 1 held for 5 s.
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Set Test Variable         ${oximeter}
    Hold Button 1
    Wait For Line On Uart     reset for a new patient    testerId=${oximeter}    timeout=10
    Release Button 1

Reset The Oximeter
    Execute Command           mach set "oximeter"
    Execute Command           machine Reset

Start A Phone In The Hub's Place
    # Zephyr's Bluetooth shell.
    Start System              ${PHONE_ELF}
    Start Emulation
    Wait For Line On Uart     to initialize the stack    testerId=${hub}
    Write Line To Uart        bt init    testerId=${hub}
    Wait For Line On Uart     Bluetooth initialized    testerId=${hub}

Start A Fake Oximeter In The Oximeter's Place
    Start System
    Execute Command           mach set "oximeter"
    Execute Command           connector Disconnect sysbus.radio wireless
    # On the hub's board, which has the CCM its pairing needs, with an address of its own: every
    # board here has the same one in FICR.
    Execute Command           mach create "fake oximeter"
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-hub.repl
    Execute Command           sysbus Tag <0x100000a4, 0x100000a7> "DEVICEADDR[0]" 0x0F0E0D0C
    Execute Command           connector Connect sysbus.radio wireless
    Execute Command           sysbus LoadELF @${FAKE_OXIMETER_ELF}
    Start Emulation

Oximeter's Address
    [Arguments]               ${phone}
    # As a phone scanning sees it: in simulation, the oximeter is the only device advertising.
    Write Line To Uart        bt scan on    testerId=${phone}
    ${seen}=                  Wait For Line On Uart    [[]DEVICE[]]: .* AD evt type 0,    testerId=${phone}
    ...                       treatAsRegex=true    timeout=10
    Write Line To Uart        bt scan off    testerId=${phone}
    ${address}=               Evaluate    re.search(r"[[]DEVICE[]]: ([0-9A-F:]+) ", str($seen)).group(1)
    ...                       modules=re
    RETURN                    ${address}

Connect From
    [Arguments]               ${phone}
    ${address}=               Oximeter's Address    ${phone}
    Write Line To Uart        bt connect ${address} random    testerId=${phone}

Connect A Phone
    Start A Phone In The Hub's Place
    Connect From              ${hub}
    # Paused there, so a test can time what follows from the connection.
    Wait For Line On Uart     Connected:    testerId=${hub}    timeout=10    pauseEmulation=true

Add A Phone
    [Arguments]               ${name}    ${identity}=${EMPTY}
    # A third board running Zephyr's Bluetooth shell, beside the hub.
    Execute Command           pause
    Execute Command           mach create "${name}"
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-hub.repl
    # Renode reads FICR as zero, so phones would share their identity root and, from the same
    # simulated random numbers, their private addresses: a second phone needs its own.
    IF    $identity
        Execute Command           sysbus Tag <0x100000a4, 0x100000a7> "DEVICEADDR[0]" ${identity}
        Execute Command           sysbus Tag <0x100000c0, 0x100000cf> "IR" ${identity}
    END
    Execute Command           connector Connect sysbus.radio wireless
    Execute Command           sysbus LoadELF @${PHONE_ELF}
    ${phone}=                 Create Terminal Tester    ${UART}    machine=${name}
    Execute Command           start
    Wait For Line On Uart     to initialize the stack    testerId=${phone}
    Write Line To Uart        bt init    testerId=${phone}
    Wait For Line On Uart     Bluetooth initialized    testerId=${phone}
    RETURN                    ${phone}

Add A Device Using The Hub's Address
    [Arguments]               ${name}
    # It connects, reads the oximeter's sealed pairing data, can't open it, and stays.
    Execute Command           pause
    Execute Command           mach create "${name}"
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-hub.repl
    Execute Command           connector Connect sysbus.radio wireless
    Execute Command           sysbus LoadELF @${HUB_OTHER_PAIRING_ELF}
    Execute Command           start

Take The Hub Out Of Range
    Execute Command           pause
    Execute Command           mach set "hub"
    Execute Command           connector Disconnect sysbus.radio wireless
    Execute Command           start

Bring The Hub Back In Range
    Execute Command           pause
    Execute Command           mach set "hub"
    Execute Command           connector Connect sysbus.radio wireless
    Execute Command           start

A Stranger Holds A Connection
    # The hub pairs, then goes out of range, and a phone connects and stays without pairing.
    Start System
    Monitor A Patient At 94 Percent
    Take The Hub Out Of Range
    ${phone}=                 Add A Phone    phone
    Connect From              ${phone}
    Wait For Line On Uart     Connected:    testerId=${phone}    timeout=10
    RETURN                    ${phone}

Hub Reading Time
    [Arguments]               ${pause}=false
    # When the hub heard the reading, from its log's timestamp: its log lines can trail by a second.
    ${line}=                  Wait For Line On Uart    hub: SpO2    testerId=${hub}    timeout=10
    ...                       pauseEmulation=${pause}
    ${time}=                  Evaluate
    ...                       re.search(r"[[]([0-9]+):([0-9]+):([0-9]+)[.]([0-9]+),([0-9]+)[]] .*hub: SpO2", str($line))
    ...                       modules=re
    RETURN                    ${{3600 * int($time[1]) + 60 * int($time[2]) + int($time[3]) + int($time[4]) / 1e3 + int($time[5]) / 1e6}}

Milliseconds Stamped On
    [Arguments]               ${line}
    # The uptime on a line of Zephyr's log, to the millisecond, as k_uptime_get counts it.
    ${time}=                  Evaluate    re.search(r"[[]([0-9]+):([0-9]+):([0-9]+)[.]([0-9]+),", str($line))
    ...                       modules=re
    RETURN                    ${{(3600 * int($time[1]) + 60 * int($time[2]) + int($time[3])) * 1000 + int($time[4])}}

Value Handle Of
    [Arguments]               ${uuid}
    # The shell reads 16-bit UUIDs only, so a 128-bit characteristic is used by its value's handle,
    # the one after its declaration's.
    Write Line To Uart        gatt discover-characteristic    testerId=${hub}
    ${found}=                 Wait For Line On Uart    Characteristic ${uuid} found: handle ([0-9a-f]+)
    ...                       testerId=${hub}    treatAsRegex=true    timeout=10
    ${handle}=                Evaluate    hex(int(re.search("handle ([0-9a-f]+)", str($found)).group(1), 16) + 1)
    ...                       modules=re
    RETURN                    ${handle}

The Log Check Should Show
    [Arguments]               ${held_back}    ${answer}
    Reset Emulation
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     security records,    testerId=${hub}
    # Records written after the log was read stand for ones a gateway held back.
    FOR    ${record}    IN RANGE    ${held_back}
        Write Line To Uart        limits 95 50 120    testerId=${hub}
        Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}
    END
    # The backend's fresh challenge, answered under the oximeter's log key.
    Write Line To Uart        check    testerId=${hub}
    Wait For Line On Uart     ${answer}    testerId=${hub}

Send The Update
    Write Line To Uart        update ${update_size}    testerId=${hub}
    Wait For Line On Uart     the oximeter is restarting with the update    testerId=${hub}    timeout=120

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
Keeps Its Pairing Through A Reset
    Start System
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Monitor A Patient At 94 Percent
    Wait For Line On Uart     pairing: paired    testerId=${oximeter}
    Execute Command           mach set "oximeter"
    Execute Command           machine Reset
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}
    # Both kept the bond, so the hub reconnects at level 4 with the stored key, without pairing
    # again: the oximeter never says it paired.
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=10
    Should Not Be On Uart     pairing: paired    testerId=${oximeter}    timeout=1

Hears Readings After Every Reconnection
    Start System
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Monitor A Patient At 94 Percent
    # Twice: the hub must subscribe on every new link, the third as much as the second.
    FOR    ${reset}    IN RANGE    2
        Reset The Oximeter
        Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=10
        Wait For Line On Uart     hub: SpO2 94%    testerId=${hub}    timeout=10
    END

Takes Update Commands After Every Reconnection
    Start System
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Monitor A Patient At 94 Percent
    FOR    ${reset}    IN RANGE    2
        Reset The Oximeter
        Wait For Line On Uart     ready to update the oximeter    testerId=${hub}    timeout=10
    END
    # 1.0.0 passed its self-test long ago, so the oximeter agrees to keep it.
    Write Line To Uart        confirm    testerId=${hub}
    Wait For Line On Uart     the oximeter keeps the image it runs for good    testerId=${hub}    timeout=10

Lets A New Gateway's Pairing Replace The Old One
    Start System
    Monitor A Patient At 94 Percent
    # The first hub goes out of range, and a second, with an address of its own, sets the oximeter
    # up: the backend authorizes it, as it would a replacement gateway.
    Execute Command           pause
    Execute Command           mach set "hub"
    Execute Command           connector Disconnect sysbus.radio wireless
    Execute Command           mach create "second hub"
    Execute Command           machine LoadPlatformDescription @${CURDIR}/nrf52840dk-hub.repl
    Execute Command           sysbus Tag <0x100000a4, 0x100000a7> "DEVICEADDR[0]" 0x11223344
    Execute Command           connector Connect sysbus.radio wireless
    Execute Command           sysbus LoadELF @${HUB_ELF}
    ${second_hub}=            Create Terminal Tester    ${UART}    machine=second hub
    Execute Command           start
    # Pairing ends once the oximeter has sent its keys, after the link is encrypted.
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${second_hub}    timeout=15
    Wait For Line On Uart     pairing: paired with the oximeter    testerId=${second_hub}
    # Back in range, the first hub tries its old bond, which the oximeter no longer has.
    Execute Command           mach set "second hub"
    Execute Command           connector Disconnect sysbus.radio wireless
    Execute Command           mach set "hub"
    Execute Command           connector Connect sysbus.radio wireless
    Wait For Line On Uart     using the oximeter's bond    testerId=${hub}    timeout=15
    Wait For Line On Uart     not paired with the oximeter    testerId=${hub}    timeout=10

Goes Back To The Default Limits For A New Patient
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}
    # The last patient's limits are no one else's: the next starts with the defaults.
    Reset The Oximeter For A New Patient
    # At 94% the last patient's 95% limit would light the alarm at the first reading, 4 s after the
    # start, as would a sensor that stopped: a dark LED shows the defaults are back, and readings on.
    Assert And Hold Led State    false    2    8

Ignores A Short Press
    Start System
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Monitor A Patient At 94 Percent
    # A second's press, as from a patient lying on it: nothing is forgotten.
    Hold Button 1
    Should Not Be On Uart     reset for a new patient    testerId=${oximeter}    timeout=1
    Release Button 1
    Should Not Be On Uart     reset for a new patient    testerId=${oximeter}    timeout=6

Pairs Again Once The Backend Allows It
    Start System
    Monitor A Patient At 94 Percent
    Reset The Oximeter For A New Patient
    # The hub doesn't pair again by itself: in the real system the backend decides which gateway
    # serves the next patient, and opens the oximeter's sealed data only for that one.
    Wait For Line On Uart     the oximeter has forgotten this hub    testerId=${hub}    timeout=20
    Should Not Be On Uart     pairing: paired with the oximeter    testerId=${hub}    timeout=3
    Write Line To Uart        pair    testerId=${hub}
    Wait For Line On Uart     pairing: paired with the oximeter    testerId=${hub}    timeout=20
    Wait For Line On Uart     hub: SpO2 94%    testerId=${hub}    timeout=10

Keeps The Fast Interval Through An Update
    Sign A Release            1.1.0    ${TEST_IMAGES}/release-1.1.0.bin
    Start System With An Update    ${TEST_IMAGES}/release-1.1.0.bin
    Start Emulation
    Wait For Line On Uart     ready to update the oximeter    testerId=${hub}    timeout=10
    # Sooner than 5 s into the link, when Zephyr would have the oximeter ask for its own timing.
    Write Line To Uart        update ${update_size}    testerId=${hub}
    Wait For Line On Uart     link interval 7.50 ms    testerId=${hub}    timeout=120
    # The hub's log waits for the upload to end: the next timing line would come before this one.
    ${next}=                  Wait For Line On Uart    (link interval|the oximeter is restarting with the update)
    ...                       testerId=${hub}    treatAsRegex=true    timeout=120
    ${next}=                  Convert To String    ${next}
    Should Contain            ${next}    the oximeter is restarting with the update

Loses No Sample During An Update
    Sign A Release            1.1.0    ${TEST_IMAGES}/release-1.1.0.bin
    Start System With An Update    ${TEST_IMAGES}/release-1.1.0.bin
    Leave The Last Release In Slot 1
    Monitor A Patient At 94 Percent
    ${before}=                Execute Command    ${SENSOR} LostSamples
    Write Line To Uart        update ${update_size}    testerId=${hub}
    # Up to the restart into the update, when monitoring stops anyway.
    Wait For Line On Uart     Starting swap using offset algorithm    testerId=${oximeter}    timeout=120
    ${after}=                 Execute Command    ${SENSOR} LostSamples
    Should Be Equal As Integers    ${after}    ${before}

Resumes Monitoring Soon After An Update
    Sign A Release            1.1.0    ${TEST_IMAGES}/release-1.1.0.bin
    Start System With An Update    ${TEST_IMAGES}/release-1.1.0.bin
    Monitor A Patient At 94 Percent
    Send The Update
    ${restarted}=             Virtual Seconds
    Wait For Line On Uart     connected to a pulse oximeter    testerId=${hub}    timeout=${MONITORING_GAP}
    Wait For Line On Uart     hub: SpO2 94%    testerId=${hub}    timeout=${MONITORING_GAP}
    ${resumed}=               Virtual Seconds
    Should Be True            ${resumed} - ${restarted} < ${MONITORING_GAP}

Keeps An Update On Trial Whatever The Gateway Says
    Sign A Release            1.1.0    ${TEST_IMAGES}/release-1.1.0.bin
    Start System With An Update    ${TEST_IMAGES}/release-1.1.0.bin
    Monitor A Patient At 94 Percent
    # The sensor stops, so the update can never pass its self-test.
    Execute Command           ${SENSOR} Stopped true
    Send The Update
    Wait For Line On Uart     medsec-oximeter 1.1.0+0 started    testerId=${oximeter}    timeout=30
    Wait For Line On Uart     ready to update the oximeter    testerId=${hub}    timeout=30
    # As standard update tools do after the reboot: ask the oximeter to keep the update for good.
    Write Line To Uart        confirm    testerId=${hub}
    Wait For Line On Uart     the oximeter refused to keep the update    testerId=${hub}    timeout=10
    # No self-test passed, so the next reset brings the old version back.
    Reset The Oximeter
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}    timeout=30

Refuses A Phone That Isn't Paired
    Connect A Phone
    # Insufficient authentication for everything but the device ID and the sealed pairing data:
    # the readings service, the security log, and the start of an upload (an SMP write to the image
    # group's upload command), refused before the oximeter reads it.
    Write Line To Uart        gatt read-uuid 2a60    testerId=${hub}
    Wait For Line On Uart     Read complete: err 0x05    testerId=${hub}    timeout=10
    ${log}=                   Value Handle Of    2f6a0005-0f8a-4e3c-9d2b-6c1e4a7b9d10
    Write Line To Uart        gatt read ${log}    testerId=${hub}
    Wait For Line On Uart     Read complete: err 0x05    testerId=${hub}    timeout=10
    ${smp}=                   Value Handle Of    da2e7828-fbce-4e01-ae9e-261174997c48
    Write Line To Uart        gatt write ${smp} 0 0200000000010001    testerId=${hub}
    Wait For Line On Uart     Write complete: err 0x05    testerId=${hub}    timeout=10

Refuses A Phone That Pairs With Just Works
    Connect A Phone
    # Unauthenticated pairing, as a phone without the oximeter's sealed data would try.
    Write Line To Uart        bt security 2    testerId=${hub}
    Wait For Line On Uart     Security failed:    testerId=${hub}    timeout=10

Lets Its Gateway Back While A Stranger Holds A Connection
    ${phone}=                 A Stranger Holds A Connection
    Bring The Hub Back In Range
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=10
    Wait For Line On Uart     hub: SpO2 94%    testerId=${hub}    timeout=10
    # The phone still has its connection: the hub took the other.
    Should Not Be On Uart     Disconnected:    testerId=${phone}    timeout=0.1

Turns Away A Second Stranger
    A Stranger Holds A Connection
    ${second}=                Add A Phone    second phone    identity=0x11223344
    Connect From              ${second}
    # The oximeter never answers its connection request, so its controller gives up on the link.
    Wait For Line On Uart     Disconnected: .* \\(reason 0x3e\\)    testerId=${second}    treatAsRegex=true    timeout=10

Takes No Other Central While Its Gateway Is Linked
    Start System
    Monitor A Patient At 94 Percent
    ${phone}=                 Add A Phone    phone
    # The oximeter doesn't even advertise.
    Write Line To Uart        bt scan on    testerId=${phone}
    Should Not Be On Uart     [[]DEVICE[]]:    testerId=${phone}    treatAsRegex=true    timeout=5

Drops A Central That Doesn't Pair In Time
    Connect A Phone
    ${connected}=             Virtual Seconds
    Wait For Line On Uart     Disconnected: .* \\(reason 0x05\\)    testerId=${hub}    treatAsRegex=true
    ...                       timeout=${PAIRING_TIMEOUT + 5}    pauseEmulation=true
    ${dropped}=               Virtual Seconds
    # Dropped at the first connection event past the deadline.
    Should Be True            ${PAIRING_TIMEOUT} <= ${dropped} - ${connected} < ${PAIRING_TIMEOUT} + 1

Drops Two Strangers Each At Its Own Deadline
    A Stranger Holds A Connection
    # 5 s later, a second stranger with the gateway's address, which the controller lets in.
    Execute Command           pause
    Execute Command           emulation RunFor "5"
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Add A Device Using The Hub's Address    impostor
    ${connected}=             Wait For Line On Uart    central connected    testerId=${oximeter}    timeout=10
    # The phone's deadline comes first.
    Wait For Line On Uart     central didn't pair in time    testerId=${oximeter}    timeout=${PAIRING_TIMEOUT}
    ${dropped}=               Wait For Line On Uart    central didn't pair in time    testerId=${oximeter}
    ...                       timeout=10
    # By the oximeter's own clock: its log lines reach the UART up to a second after it logs them.
    ${from}=                  Milliseconds Stamped On    ${connected}
    ${to}=                    Milliseconds Stamped On    ${dropped}
    Should Be True            ${PAIRING_TIMEOUT} * 1000 <= ${to} - ${from} < (${PAIRING_TIMEOUT} + 1) * 1000

Keeps Its Gateway Linked Past The Pairing Timeout
    Start System
    Monitor A Patient At 94 Percent
    # The hub connected a few seconds before its first reading, and paired.
    Should Not Be On Uart     hub: disconnected    testerId=${hub}    timeout=${PAIRING_TIMEOUT}

Keeps Running When A Central Leaves Mid-Pairing
    Start A Phone In The Hub's Place
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    ${address}=               Oximeter's Address    ${hub}
    # The phone claims the oximeter's out-of-band data, so the oximeter takes its pairing request
    # and waits for the rest of the pairing.
    Write Line To Uart        bt oob-remote ${address} random 01 02    testerId=${hub}
    Write Line To Uart        bt connect ${address} random    testerId=${hub}
    Wait For Line On Uart     Connected:    testerId=${hub}    timeout=10    pauseEmulation=true
    Write Line To Uart        bt security 2    testerId=${hub}    waitForEcho=false
    Execute Command           emulation RunFor "0.2"
    Take The Hub Out Of Range
    Wait For Line On Uart     central disconnected    testerId=${oximeter}    timeout=10
    # The watchdog would reset an oximeter stuck after the link, and it would start again.
    Should Not Be On Uart     started    testerId=${oximeter}    timeout=5

Alarms On Limits The Hub Signs
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
    Assert LED State          false    timeout=2
    Assert LED State          true    timeout=8

Reads Every Sample Waiting In The Sensor
    Start System
    Monitor A Patient At 94 Percent
    # 200 ms without an interrupt, as when a flash erase or a key computation holds the firmware
    # up: 20 samples wait in the sensor, and the next interrupt reads them all.
    Execute Command           pause
    Execute Command           ${SENSOR} InterruptCut true
    Execute Command           emulation RunFor "0.2"
    Execute Command           ${SENSOR} InterruptCut false
    Execute Command           emulation RunFor "0.05"
    ${waiting}=               Execute Command    ${SENSOR} Unread
    ${waiting}=               Convert To Integer    ${waiting.strip()}
    Should Be True            ${waiting} <= 1

Starts A New Reading After The Sensor Loses Samples
    Start System
    Monitor A Patient At 94 Percent
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    # Paused at a reading the hub logged; those it heard and hasn't logged yet come next.
    ${previous}=              Hub Reading Time    pause=true
    # 400 ms without an interrupt, longer than the sensor's FIFO lasts (320 ms): it loses samples,
    # and a reading across the gap would be skewed.
    Execute Command           ${SENSOR} InterruptCut true
    Execute Command           emulation RunFor "0.4"
    Execute Command           ${SENSOR} InterruptCut false
    Start Emulation
    Wait For Line On Uart     the sensor lost samples; starting a new reading    testerId=${oximeter}    timeout=2
    # Readings come a second apart. The first after the gap waits for four seconds of samples; one
    # across the gap would come 1.4 s after the last.
    FOR    ${i}    IN RANGE    8
        ${reading}=           Hub Reading Time
        ${gap}=               Evaluate    ${reading} - ${previous}
        IF    ${gap} > 1.2    BREAK
        ${previous}=          Set Variable    ${reading}
    END
    Should Be True            ${gap} >= 4

Raises The Technical Alarm When The Sensor Stops
    Start System
    Monitor A Patient At 94 Percent

    # Without samples there's nothing to measure: the oximeter must say so, not fall silent. The
    # LED times it, as log lines can trail by up to a second.
    Execute Command           ${SENSOR} Stopped true
    Assert LED State          true    timeout=2
    Wait For Line On Uart     ALARM from oximeter: no pulse found    testerId=${hub}
    Wait For Line On Uart     hub: no reading    testerId=${hub}

Notices A Silent Oximeter Within Two Seconds
    Start System
    Start Emulation
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=10
    # Silent sooner than 5 s into the link, before the oximeter would ask for its own timing.
    Execute Command           mach set "oximeter"
    Execute Command           cpu IsHalted true
    # 420 ms of silence ends the link; the hub's log can trail it by up to a second.
    Wait For Line On Uart     hub: disconnected (reason 0x08)    testerId=${hub}    timeout=2

Recovers From A Hang Through The Watchdog
    Start System
    Monitor A Patient At 94 Percent
    # The processor stops, as in a hang: the watchdog resets it, and it measures again.
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Execute Command           mach set "oximeter"
    Execute Command           cpu IsHalted true
    Wait For Line On Uart     medsec-oximeter 1.0.0+0 started    testerId=${oximeter}    timeout=5
    Wait For Line On Uart     connected to a pulse oximeter    testerId=${hub}    timeout=10
    Wait For Line On Uart     hub: SpO2 94%, pulse (89|90|91) bpm    testerId=${hub}    treatAsRegex=true    timeout=10

Logs The Security Events The Hub Reads
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#1 boot 1 \\+[0-9.]+ s: started 1\\.0\\.0 \\(    testerId=${hub}    treatAsRegex=true
    Wait For Line On Uart     \#2 boot 1 \\+[0-9.]+ s: connected: C0:00:AA:BB:CC:DD \\(random\\)    testerId=${hub}    treatAsRegex=true
    Wait For Line On Uart     \#4 boot 1 \\+[0-9.]+ s: limits accepted: SpO2 below 95%, pulse below 50 or above 120 bpm    testerId=${hub}    treatAsRegex=true

Logs The Pairing
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#3 boot 1 \\+[0-9.]+ s: paired: C0:00:AA:BB:CC:DD \\(random\\)    testerId=${hub}    treatAsRegex=true

Logs A Refused Pairing
    Connect A Phone
    Write Line To Uart        bt security 2    testerId=${hub}
    Wait For Line On Uart     Security failed:    testerId=${hub}    timeout=10
    # The hub takes the phone's place, pairs and reads the log.
    Execute Command           pause
    Execute Command           mach set "hub"
    Execute Command           sysbus LoadELF @${HUB_ELF}
    Execute Command           machine Reset
    Execute Command           start
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=20
    Write Line To Uart        log    testerId=${hub}
    # The phone connects from a private address of its own.
    Wait For Line On Uart     pairing refused: authentication requirements: [0-9A-F:]{17} \\(random\\)    testerId=${hub}    treatAsRegex=true

Logs A Refused Change From A Hub Without Its Key
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter rejected the alarm limits (ATT error 0x13)    testerId=${hub}

    # It holds the pairing master, so it pairs and reads the log as any gateway would.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#4 boot 1 \\+[0-9.]+ s: limits refused: tag doesn't match    testerId=${hub}    treatAsRegex=true

Logs An Update Marked For Test
    Sign A Release            1.1.0    ${TEST_IMAGES}/release-1.1.0.bin
    Start System With An Update    ${TEST_IMAGES}/release-1.1.0.bin
    Monitor A Patient At 94 Percent
    Send The Update
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=30
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     boot 1 \\+[0-9.]+ s: update marked for test: 1\\.1\\.0\\+0    testerId=${hub}    treatAsRegex=true

Logs The Reset For A New Patient
    Start System
    Monitor A Patient At 94 Percent
    Reset The Oximeter For A New Patient
    Wait For Line On Uart     the oximeter has forgotten this hub    testerId=${hub}    timeout=20
    Write Line To Uart        pair    testerId=${hub}
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}    timeout=20
    Write Line To Uart        log    testerId=${hub}
    # The log outlasts the reset: it is the oximeter's own record, not the patient's.
    Wait For Line On Uart     boot 1 \\+[0-9.]+ s: reset for a new patient    testerId=${hub}    treatAsRegex=true

Can't Be Paired Without A Device Key
    Start System              provisioning=${UNPROVISIONED}
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Start Emulation
    Wait For Line On Uart     no device key provisioned    testerId=${oximeter}
    # Without a pairing key it can't seal its pairing data, so no hub can pair with it, and nothing
    # it protects can be read or changed.
    Wait For Line On Uart     couldn't read the oximeter's pairing data    testerId=${hub}

Verifies Every Record Of The Log
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    # Each record carries a MAC under the oximeter's log key, which the backend can derive.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     4 security records, 4 verified    testerId=${hub}

Can't Verify The Log Without The Signing Master
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent

    # A gateway without the signing master can read the log but can neither vouch for it nor forge it.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     [0-9]+ security records, 0 verified    testerId=${hub}    treatAsRegex=true

Shows Whether The Hub Read The Whole Log
    [Template]                The Log Check Should Show
    0                         the oximeter's log is complete up to record #
    1                         the oximeter has 1 record the hub hasn't read

Keeps Health Data Off Its Console
    Start System
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Monitor A Patient At 94 Percent
    # A signed change the patient then alarms on: neither the limits, nor the alarm, nor its cause
    # may reach the console, which anyone holding the oximeter can read (threat T-20).
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     ALARM from oximeter: SpO2 low    testerId=${hub}
    Should Not Be On Uart     SpO2|bpm|ALARM|no pulse    testerId=${oximeter}    treatAsRegex=true
    ...                       timeout=2

Refuses A Fake Oximeter
    Start A Fake Oximeter In The Oximeter's Place
    Wait For Line On Uart     connected to a pulse oximeter    testerId=${hub}    timeout=10
    # The fake can't seal its pairing data, so the hub doesn't pair with it, and never subscribes.
    Should Not Be On Uart     hub: SpO2    testerId=${hub}    timeout=10

Advertises Under A Private Address
    # Anyone in range sees an address that changes, and no name: nothing to follow the oximeter by
    # from one place, or one day, to the next (threat T-13).
    Start A Phone In The Hub's Place
    ${before}=                Oximeter's Address    ${hub}
    # A resolvable private address: the top two bits of its most significant byte are 01.
    Should Match Regexp       ${before}    ^[4-7]
    # Scanning actively, the phone asks for the scan response that carried the name.
    Write Line To Uart        bt scan on    testerId=${hub}
    Should Not Be On Uart     medsec-oximeter    testerId=${hub}    timeout=3
    Write Line To Uart        bt scan off    testerId=${hub}
    Reset The Oximeter
    ${after}=                 Oximeter's Address    ${hub}
    Should Not Be Equal       ${before}    ${after}
