idf_component_register(SRCS "node_ao.c" "node_ao_port.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES freertos)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
