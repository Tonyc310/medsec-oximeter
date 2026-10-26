# libFuzzer targets: each feeds any bytes at all to a parser of what another device sends.
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "The fuzz targets need clang's libFuzzer: CC=clang cmake --preset fuzz")
endif()

foreach(target limits_command)
    add_executable(fuzz_${target} fuzz_${target}.c)
    target_link_libraries(fuzz_${target} PRIVATE core project_warnings)
    # libFuzzer brings its own main(), so it's linked into the targets alone, not CMake's checks.
    target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
endforeach()
