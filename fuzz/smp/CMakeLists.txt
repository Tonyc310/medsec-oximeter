cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smp_fuzz LANGUAGES C)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../../app)
target_sources(app PRIVATE src/main.c ${APP}/src/update_guard.c)
# MCUmgr's reassembly header is its own, not public; its Bluetooth transport uses it the same way.
target_include_directories(app PRIVATE ${APP}/src
    ${ZEPHYR_BASE}/subsys/mgmt/mcumgr/transport/include)
# The update guard names image-manager commands, and that header includes MCUboot's image headers.
target_link_libraries(app PRIVATE MCUBOOT_BOOTUTIL)
