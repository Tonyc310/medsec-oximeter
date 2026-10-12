cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/limits_crypto.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(limits_signer_tests LANGUAGES C)

# The hub's signer against the oximeter's own checks: core/ and the app's PSA binding.
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_sources(app PRIVATE src/main.c ${ROOT}/hub/src/limits_signer.c
    ${ROOT}/app/src/limits_crypto.c ${ROOT}/core/src/limits_auth.c ${ROOT}/core/src/alarm.c)
target_include_directories(app PRIVATE ${ROOT}/hub/src ${ROOT}/app/src ${ROOT}/core/include
    ${ROOT}/common/include)
