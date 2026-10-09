cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(medsec_oximeter LANGUAGES C)

# core/ is the portable C the host tests run; the firmware builds it from the same sources.
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_sources(app PRIVATE src/main.c src/alarm_service.c src/plx_service.c
    ${CORE}/src/alarm.c ${CORE}/src/oximetry.c ${CORE}/src/plx.c)
# common/ holds the Bluetooth definitions the oximeter and the hub share.
target_include_directories(app PRIVATE ${CORE}/include ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)
