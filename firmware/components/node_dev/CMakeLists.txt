idf_component_register(SRCS "node_dev.c" "node_dev_report.c"
                       INCLUDE_DIRS "include"
                       REQUIRES node_ao node_config node_hal)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
