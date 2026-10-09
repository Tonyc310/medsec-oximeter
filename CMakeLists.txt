# The host build: core/ and its unit tests. The firmware builds with west, from app/.
cmake_minimum_required(VERSION 3.25)
project(medsec_oximeter_host LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

# Stricter than the firmware build: CERT C's conversion and float rules show up as warnings here.
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    -Wsign-conversion -Wdouble-promotion -Wfloat-equal -Werror)

add_subdirectory(core)

enable_testing()
add_subdirectory(tests)
