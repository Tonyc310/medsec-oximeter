cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../common/limits_crypto.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/limits_store.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(medsec_oximeter LANGUAGES C)

# core/ is the portable C the host tests run; the firmware builds it from the same sources.
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_sources(app PRIVATE src/main.c src/alarm_service.c src/limits_crypto.c src/limits_store.c
    src/plx_service.c
    ${CORE}/src/alarm.c ${CORE}/src/limits_auth.c ${CORE}/src/oximetry.c ${CORE}/src/plx.c)
# common/ holds what the oximeter and the hub share.
target_include_directories(app PRIVATE ${CORE}/include ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/limits_key.cmake)
