cmake_minimum_required(VERSION 3.20.0)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common)
list(APPEND EXTRA_CONF_FILE ${COMMON}/limits_crypto.conf ${COMMON}/pairing_crypto.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/device_keys.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(medsec_hub LANGUAGES C)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_sources(app PRIVATE src/main.c src/device_keys.c src/limits_signer.c src/log_verifier.c
    src/pairing.c
    ${CORE}/src/alarm.c
    ${CORE}/src/limits_auth.c ${CORE}/src/plx.c ${CORE}/src/security_log.c
    ${COMMON}/src/medsec_crypto.c ${COMMON}/src/pairing_seal.c)
target_include_directories(app PRIVATE ${CORE}/include ${COMMON}/include)
include(${COMMON}/master.cmake)
medsec_master(signing)
medsec_master(pairing)
