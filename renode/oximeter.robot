*** Settings ***
Library                     Process

*** Variables ***
${OXIMETER_IMAGE}           ${CURDIR}/../build/oximeter/app/zephyr/zephyr.signed.hex
${HUB_ELF}                  ${CURDIR}/../build/hub/zephyr/zephyr.elf
# The same hub built with a signing master of its own, as a gateway the backend let pair but won't
# sign for; and with both masters of its own, as a stranger.
${HUB_OTHER_KEY_ELF}        ${CURDIR}/../build/hub-other-key/zephyr/zephyr.elf
${HUB_STRANGER_ELF}         ${CURDIR}/../build/hub-stranger/zephyr/zephyr.elf
# Zephyr's Bluetooth shell (tests/bluetooth/shell) in the hub's place: a phone in range, trying
# what any central can.
${PHONE_ELF}                ${CURDIR}/../build/phone/zephyr/zephyr.elf
${SCRIPT}                   ${CURDIR}/oximeter.resc
${SIGNING_MASTER}           ${CURDIR}/../build/signing-master.key
${PAIRING_MASTER}           ${CURDIR}/../build/pairing-master.key
# The simulated oximeter's ID: the FICR DEVICEID values in nrf52840dk-oximeter.repl.
${DEVICE_ID}                0123456789abcdef
${PROVISIONED}              ${CURDIR}/../build/renode/provision.resc
${UNPROVISIONED}            ${CURDIR}/unprovisioned.resc
${UART}                     sysbus.uart0
${SENSOR}                   sysbus.twi0.max30101
${ALARM_LED}                sysbus.gpio0.led0

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
Pairs With The Hub Before Sending It Readings
    Start System
    Start Emulation
    # Authenticated LE Secure Connections: the hub opened the oximeter's sealed out-of-band data
    # with the pairing key it derives, which no one without the pairing master can.
    Wait For Line On Uart     pairing: paired with the oximeter    testerId=${hub}    timeout=10
    Wait For Line On Uart     link to the oximeter at security level 4    testerId=${hub}
    Wait For Line On Uart     hub: (SpO2|no reading)    testerId=${hub}    treatAsRegex=true

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
    Wait For Line On Uart     hub: SpO2 94%    testerId=${hub}

Can't Pair With A Hub Without The Pairing Master
    Start System              ${HUB_STRANGER_ELF}
    Start Emulation
    # It can read the sealed pairing data but not open it, so it never pairs and hears nothing.
    Wait For Line On Uart     couldn't open the oximeter's pairing data    testerId=${hub}    timeout=10
    Should Not Be On Uart     hub: (SpO2|no reading)    testerId=${hub}    timeout=5    treatAsRegex=true

Refuses A Phone That Pairs With Just Works
    Start System              ${PHONE_ELF}
    Start Emulation
    Wait For Line On Uart     to initialize the stack    testerId=${hub}
    Write Line To Uart        bt init    testerId=${hub}
    Wait For Line On Uart     Bluetooth initialized    testerId=${hub}
    Write Line To Uart        bt connect-name medsec-oximeter    testerId=${hub}
    Wait For Line On Uart     Connected:    testerId=${hub}    timeout=10
    # Unauthenticated pairing, as a phone without the oximeter's sealed data would try.
    Write Line To Uart        bt security 2    testerId=${hub}
    Wait For Line On Uart     Security failed:    testerId=${hub}    timeout=10
    # Unpaired, it can read neither the readings service nor the security log: insufficient
    # authentication. The shell reads by 16-bit UUID, so the log is read by its handle, the one
    # after its declaration's.
    Write Line To Uart        gatt read-uuid 2a60    testerId=${hub}
    Wait For Line On Uart     Read complete: err 0x05    testerId=${hub}    timeout=10
    Write Line To Uart        gatt discover-characteristic    testerId=${hub}
    ${found}=                 Wait For Line On Uart    Characteristic 2f6a0005-0f8a-4e3c-9d2b-6c1e4a7b9d10 found: handle ([0-9a-f]+)
    ...                       testerId=${hub}    treatAsRegex=true    timeout=10
    ${handle}=                Evaluate    hex(int(re.search("handle ([0-9a-f]+)", str($found)).group(1), 16) + 1)
    ...                       modules=re
    Write Line To Uart        gatt read ${handle}    testerId=${hub}
    Wait For Line On Uart     Read complete: err 0x05    testerId=${hub}    timeout=10

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

Raises The Technical Alarm When The Sensor Stops
    Start System
    Monitor A Patient At 94 Percent

    # Without samples there's nothing to measure: the oximeter must say so, not fall silent. The
    # LED times it, as log lines can trail by up to a second.
    Execute Command           ${SENSOR} Stopped true
    Assert LED State          true    timeout=2
    Wait For Line On Uart     ALARM from oximeter: no pulse found    testerId=${hub}
    Wait For Line On Uart     hub: no reading    testerId=${hub}

Recovers From A Hang Through The Watchdog
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    # The processor stops, as in a hang: the watchdog resets it, and it measures again under the
    # clinician's limits.
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Execute Command           mach set "oximeter"
    Execute Command           cpu IsHalted true
    Wait For Line On Uart     alarm limits: SpO2 below 95%    testerId=${oximeter}    timeout=5
    Wait For Line On Uart     SpO2 94%, pulse (89|90|91) bpm    testerId=${oximeter}    treatAsRegex=true    timeout=10
    Assert LED State          true

Logs The Security Events The Hub Reads
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#1 boot 1 \\+[0-9.]+ s: started    testerId=${hub}    treatAsRegex=true
    Wait For Line On Uart     \#2 boot 1 \\+[0-9.]+ s: connected: C0:00:AA:BB:CC:DD \\(random\\)    testerId=${hub}    treatAsRegex=true
    Wait For Line On Uart     \#3 boot 1 \\+[0-9.]+ s: limits accepted: SpO2 below 95%, pulse below 50 or above 120 bpm    testerId=${hub}    treatAsRegex=true

Logs A Refused Change From A Hub Without Its Key
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter rejected the alarm limits (ATT error 0x13)    testerId=${hub}

    # Anyone in range can read the log until pairing, the stranger included.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     \#3 boot 1 \\+[0-9.]+ s: limits refused: tag doesn't match    testerId=${hub}    treatAsRegex=true

Can't Be Paired Or Changed Without A Device Key
    Start System              provisioning=${UNPROVISIONED}
    ${oximeter}=              Create Terminal Tester    ${UART}    machine=oximeter
    Start Emulation
    Wait For Line On Uart     no device key provisioned    testerId=${oximeter}
    # Without a pairing key it can't seal its pairing data, so no hub can pair with it, and nothing
    # it protects can be read or changed.
    Wait For Line On Uart     couldn't read the oximeter's pairing data    testerId=${hub}
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     couldn't read the oximeter's challenge    testerId=${hub}

Verifies Every Record Of The Log
    Start System
    Monitor A Patient At 94 Percent
    Write Line To Uart        limits 95 50 120    testerId=${hub}
    Wait For Line On Uart     oximeter accepted the alarm limits    testerId=${hub}

    # Each record carries a MAC under the oximeter's log key, which the backend can derive.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     3 security records, 3 verified    testerId=${hub}

Can't Verify The Log Without The Signing Master
    Start System              ${HUB_OTHER_KEY_ELF}
    Monitor A Patient At 94 Percent

    # A gateway without the signing master can read the log but can neither vouch for it nor forge it.
    Write Line To Uart        log    testerId=${hub}
    Wait For Line On Uart     [0-9]+ security records, 0 verified    testerId=${hub}    treatAsRegex=true
