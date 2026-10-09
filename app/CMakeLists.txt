cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(medsec_oximeter LANGUAGES C)

# core/ is the portable C the host tests run; the firmware builds it from the same sources.
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
target_sources(app PRIVATE src/main.c ${CORE}/src/oximetry.c)
target_include_directories(app PRIVATE ${CORE}/include)
