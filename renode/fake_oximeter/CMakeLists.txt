cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fake_oximeter LANGUAGES C)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_sources(app PRIVATE src/main.c ${REPO}/core/src/plx.c)
target_include_directories(app PRIVATE ${REPO}/core/include ${REPO}/common/include)
