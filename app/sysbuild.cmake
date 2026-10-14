# The release key: MCUboot trusts its public half, and the build signs the oximeter with it. It's
# made once per checkout and kept in build/, never committed. Without it, sysbuild signs with the
# development key that comes with MCUboot, whose private half anyone can download.
string(CONFIGURE "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}" release_key)
if(NOT EXISTS ${release_key})
  cmake_path(GET release_key PARENT_PATH release_key_dir)
  file(MAKE_DIRECTORY ${release_key_dir})
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_MCUBOOT_MODULE_DIR}/scripts/imgtool.py keygen
            --key ${release_key} --type ecdsa-p256
    COMMAND_ERROR_IS_FATAL ANY)
endif()
