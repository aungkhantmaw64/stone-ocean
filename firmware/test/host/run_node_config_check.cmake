# Runs node_config_check once and checks its exit code and stderr.
#   cmake -DCHECKER=<node_config_check> -DCONFIG=<file> -DEXPECTED_EXIT=<code>
#         -DEXPECTED_STDERR=<regex> -P run_node_config_check.cmake
# Used by the add_config_checker_test() tests in CMakeLists.txt.
execute_process(COMMAND ${CHECKER} ${CONFIG}
                RESULT_VARIABLE exit_code
                ERROR_VARIABLE stderr)
string(STRIP "${stderr}" stderr)

if(NOT exit_code STREQUAL EXPECTED_EXIT)
  message(FATAL_ERROR "node_config_check ${CONFIG} exited with ${exit_code}, "
                      "expected ${EXPECTED_EXIT}; stderr:\n${stderr}")
endif()
if(NOT stderr MATCHES "${EXPECTED_STDERR}")
  message(FATAL_ERROR "node_config_check ${CONFIG} printed:\n${stderr}\n"
                      "expected it to match: ${EXPECTED_STDERR}")
endif()
