cmake_minimum_required(VERSION 3.20.0)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../../app)
set(CORE ${APP}/../core)
set(COMMON ${APP}/../common)
# The oximeter's flash and crypto settings, as its own build applies them.
list(APPEND EXTRA_CONF_FILE ${COMMON}/limits_crypto.conf ${COMMON}/pairing_crypto.conf
    ${APP}/limits_store.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bt_host_fuzz LANGUAGES C)

# The oximeter's Bluetooth services and admission, built from its own sources.
target_sources(app PRIVATE src/main.c src/hci_fuzz.c src/fixed_identity.c src/paired_only.c
    ${APP}/src/admission.c ${APP}/src/alarm_service.c ${APP}/src/event_log.c
    ${APP}/src/limits_crypto.c ${APP}/src/limits_store.c ${APP}/src/log_mac.c ${APP}/src/pairing.c
    ${APP}/src/plx_service.c
    ${CORE}/src/alarm.c ${CORE}/src/device_key.c ${CORE}/src/limits_auth.c
    ${CORE}/src/link_admission.c ${CORE}/src/plx.c ${CORE}/src/security_log.c
    ${COMMON}/src/medsec_crypto.c ${COMMON}/src/pairing_seal.c)
target_include_directories(app PRIVATE ${APP}/src ${CORE}/include ${COMMON}/include)
