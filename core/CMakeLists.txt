add_library(core STATIC src/alarm.c src/limits_auth.c src/oximetry.c src/device_key.c src/plx.c src/security_log.c src/sensor_fifo.c src/supervision.c)
target_include_directories(core PUBLIC include)
target_link_libraries(core PUBLIC m PRIVATE project_warnings)
