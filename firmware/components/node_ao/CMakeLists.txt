idf_component_register(SRCS "node_ao.c" "node_ao_port.c" "node_ao_time.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES freertos node_hal)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
