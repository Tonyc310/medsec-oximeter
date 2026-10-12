cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../limits_store.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(limits_store_tests LANGUAGES C)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_sources(app PRIVATE src/main.c ${APP}/src/limits_store.c ${APP}/../core/src/alarm.c)
target_include_directories(app PRIVATE ${APP}/src ${APP}/../core/include)
