idf_component_register(SRCS "node_status.c" "node_status_log.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES log node_ao node_hal)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
