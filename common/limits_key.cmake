# The key the oximeter and the hub share for signed alarm-limit changes. A stand-in until devices
# get keys of their own: it's made once per checkout from a secure random source, kept in build/
# (never committed), and built into both images. Point LIMITS_KEY_FILE elsewhere for a device with
# a different key.
set(LIMITS_KEY_FILE ${CMAKE_CURRENT_LIST_DIR}/../build/limits.key CACHE FILEPATH
    "Hex file holding the 32-byte alarm-limits key; made if missing")

if(NOT EXISTS ${LIMITS_KEY_FILE})
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c "import secrets; print(secrets.token_hex(32))"
    OUTPUT_VARIABLE limits_key_hex OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE ${LIMITS_KEY_FILE} "${limits_key_hex}\n")
endif()

file(STRINGS ${LIMITS_KEY_FILE} limits_key_hex LIMIT_COUNT 1)
if(NOT limits_key_hex MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "${LIMITS_KEY_FILE} must hold 64 lowercase hex digits")
endif()
string(LENGTH "${limits_key_hex}" limits_key_digits)
if(NOT limits_key_digits EQUAL 64)
  message(FATAL_ERROR "${LIMITS_KEY_FILE} must hold 64 lowercase hex digits")
endif()

string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," LIMITS_KEY_BYTES "${limits_key_hex}")
configure_file(${CMAKE_CURRENT_LIST_DIR}/limits_key.c.in ${CMAKE_CURRENT_BINARY_DIR}/limits_key.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/limits_key.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LIMITS_KEY_FILE})
