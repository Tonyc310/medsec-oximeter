# SYSTEM, so the strict warnings apply to our code and not to Unity's macros.
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.7.0
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(unity)

foreach(module alarm oximetry plx)
    add_executable(test_${module} test_${module}.c)
    target_link_libraries(test_${module} PRIVATE core unity project_warnings)
    add_test(NAME ${module} COMMAND test_${module})
endforeach()
