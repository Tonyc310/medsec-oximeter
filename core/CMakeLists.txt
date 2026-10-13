add_library(core STATIC src/alarm.c src/limits_auth.c src/oximetry.c src/plx.c src/supervision.c)
target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC m PRIVATE project_warnings)
