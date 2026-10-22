idf_component_register(SRCS "config_store.c" "config_store_crc.c"
                       INCLUDE_DIRS "include"
                       REQUIRES node_config)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
