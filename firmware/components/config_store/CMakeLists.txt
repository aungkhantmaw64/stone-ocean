idf_component_register(SRCS "config_store.c" "config_store_crc.c" "config_store_fs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES node_config
                       PRIV_REQUIRES joltwallet__littlefs log)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
