# SYSTEM, so the strict warnings apply to our code and not to Unity's macros.
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.7.0
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(unity)

add_executable(test_oximetry test_oximetry.c)
target_link_libraries(test_oximetry PRIVATE core unity project_warnings)
add_test(NAME oximetry COMMAND test_oximetry)

add_executable(test_plx test_plx.c)
target_link_libraries(test_plx PRIVATE core unity project_warnings)
add_test(NAME plx COMMAND test_plx)
