cmake_minimum_required(VERSION 3.20.0)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common)
list(APPEND EXTRA_CONF_FILE ${COMMON}/limits_crypto.conf ${COMMON}/pairing_crypto.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/limits_store.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(medsec_oximeter LANGUAGES C)

# core/ is the portable C the host tests run; the firmware builds it from the same sources.
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_sources(app PRIVATE src/main.c src/alarm_service.c src/device_identity.c src/event_log.c
    src/limits_crypto.c src/limits_store.c src/log_mac.c src/new_patient.c src/pairing.c
    src/plx_service.c
    src/supervisor.c src/update_guard.c src/update_log.c
    ${CORE}/src/alarm.c ${CORE}/src/device_key.c ${CORE}/src/limits_auth.c ${CORE}/src/oximetry.c ${CORE}/src/plx.c
    ${CORE}/src/security_log.c ${CORE}/src/supervision.c
    ${COMMON}/src/medsec_crypto.c ${COMMON}/src/pairing_seal.c)
# common/ holds what the oximeter and the hub share.
target_include_directories(app PRIVATE ${CORE}/include ${COMMON}/include)
# The update guard names image-manager commands, and that header includes MCUboot's image headers.
target_link_libraries(app PRIVATE MCUBOOT_BOOTUTIL)
