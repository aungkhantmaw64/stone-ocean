idf_component_register(SRCS "hal_esp_err.c" "hal_esp_gpio.c" "hal_esp_time.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_hw_support esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
