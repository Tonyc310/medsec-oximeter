# The backend's master secrets, which the hub stands in for. Each is made once per checkout from a
# secure random source, kept in build/ (never committed), and built into the hub only: in the real
# system they never leave the backend. medsec_master(signing) gives the hub `signing_master`, read
# from SIGNING_MASTER_FILE, so a hub can be built with a master of its own.
function(medsec_master name)
  string(TOUPPER ${name} upper)
  set(${upper}_MASTER_FILE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../build/${name}-master.key
      CACHE FILEPATH "Hex file holding the 32-byte ${name} master; made if missing")
  set(master_file ${${upper}_MASTER_FILE})

  if(NOT EXISTS ${master_file})
    execute_process(
      COMMAND ${PYTHON_EXECUTABLE} -c "import secrets; print(secrets.token_hex(32))"
      OUTPUT_VARIABLE master_hex OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY)
    file(WRITE ${master_file} "${master_hex}\n")
  endif()

  file(STRINGS ${master_file} master_hex LIMIT_COUNT 1)
  string(LENGTH "${master_hex}" master_digits)
  if(NOT master_hex MATCHES "^[0-9a-f]+$" OR NOT master_digits EQUAL 64)
    message(FATAL_ERROR "${master_file} must hold 64 lowercase hex digits")
  endif()

  set(MASTER_NAME ${name}_master)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," MASTER_BYTES "${master_hex}")
  configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/master.c.in
                 ${CMAKE_CURRENT_BINARY_DIR}/${name}_master.c)
  target_sources(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${name}_master.c)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${master_file})
endfunction()
