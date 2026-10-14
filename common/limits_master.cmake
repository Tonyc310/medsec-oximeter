# The master secret the hub derives each oximeter's limits key from, standing in for the monitoring
# backend's. It's made once per checkout from a secure random source, kept in build/ (never
# committed), and built into the hub only: in the real system it never leaves the backend. Point
# LIMITS_MASTER_FILE elsewhere for a hub with a different master.
set(LIMITS_MASTER_FILE ${CMAKE_CURRENT_LIST_DIR}/../build/limits-master.key CACHE FILEPATH
    "Hex file holding the 32-byte master secret; made if missing")

if(NOT EXISTS ${LIMITS_MASTER_FILE})
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c "import secrets; print(secrets.token_hex(32))"
    OUTPUT_VARIABLE limits_master_hex OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE ${LIMITS_MASTER_FILE} "${limits_master_hex}\n")
endif()

file(STRINGS ${LIMITS_MASTER_FILE} limits_master_hex LIMIT_COUNT 1)
string(LENGTH "${limits_master_hex}" limits_master_digits)
if(NOT limits_master_hex MATCHES "^[0-9a-f]+$" OR NOT limits_master_digits EQUAL 64)
  message(FATAL_ERROR "${LIMITS_MASTER_FILE} must hold 64 lowercase hex digits")
endif()

string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," LIMITS_MASTER_BYTES "${limits_master_hex}")
configure_file(${CMAKE_CURRENT_LIST_DIR}/limits_master.c.in
               ${CMAKE_CURRENT_BINARY_DIR}/limits_master.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/limits_master.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LIMITS_MASTER_FILE})
