idf_component_register(SRCS "node_config_err.c" "node_config_log.c"
                       INCLUDE_DIRS "include"
                       REQUIRES node_hal
                       PRIV_REQUIRES json log)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
