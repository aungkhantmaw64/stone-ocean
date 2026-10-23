idf_component_register(SRCS "node_health.c" "node_health_ao.c" "node_health_log.c" "node_health_port.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES config_store node_ao node_hal esp_system log nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
