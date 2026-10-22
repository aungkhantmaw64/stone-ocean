idf_component_register(SRCS "node_hal_err.c" "node_hal_gpio.c" "node_hal_i2c.c"
                            "node_hal_selftest.c" "node_hal_time.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_hw_support
                                     esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
