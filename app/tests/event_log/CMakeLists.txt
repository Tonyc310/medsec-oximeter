cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../limits_store.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/limits_crypto.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(event_log_tests LANGUAGES C)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_sources(app PRIVATE src/main.c ${APP}/src/event_log.c ${APP}/src/log_mac.c
    ${APP}/../core/src/security_log.c ${APP}/../common/src/medsec_crypto.c)
target_include_directories(app PRIVATE ${APP}/src ${APP}/../core/include ${APP}/../common/include)
