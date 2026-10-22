idf_component_register(SRCS "app_main.c" "boot_sequence.c"
                       INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
