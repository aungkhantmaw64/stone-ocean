# Checks that the NODE_HEALTH_DEBUG_HANG option (spec 09) defaults to off and
# is never switched on in the files the hardware and QEMU builds start from.
#   cmake -DFIRMWARE_DIR=<firmware> -P check_debug_hang_is_off.cmake
# Used by test_debug_hang_option_is_never_enabled_in_the_hardware_or_qemu_defaults.
foreach(defaults sdkconfig.defaults sdkconfig.qemu)
  file(STRINGS ${FIRMWARE_DIR}/${defaults} enabled
       REGEX "^[ \t]*CONFIG_NODE_HEALTH_DEBUG_HANG[ \t]*=[ \t]*y")
  if(enabled)
    message(FATAL_ERROR "${defaults} enables the debug hang: ${enabled}")
  endif()
endforeach()

file(READ ${FIRMWARE_DIR}/components/node_health/Kconfig kconfig)
if(NOT kconfig MATCHES "config NODE_HEALTH_DEBUG_HANG[^\n]*\n[^\n]*\n[ \t]*default n")
  message(FATAL_ERROR "NODE_HEALTH_DEBUG_HANG in node_health/Kconfig does not "
                      "default to n")
endif()
