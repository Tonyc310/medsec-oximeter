cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../common/limits_crypto.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/device_keys.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(medsec_hub LANGUAGES C)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_sources(app PRIVATE src/main.c src/device_keys.c src/limits_signer.c src/log_verifier.c
    ${CORE}/src/alarm.c
    ${CORE}/src/limits_auth.c ${CORE}/src/plx.c ${CORE}/src/security_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/medsec_crypto.c)
target_include_directories(app PRIVATE ${CORE}/include ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/master.cmake)
medsec_master(signing)
