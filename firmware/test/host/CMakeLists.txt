cmake_minimum_required(VERSION 3.16...3.30)
project(mesh_node_host_tests C)

# AddressSanitizer (with LeakSanitizer) and UndefinedBehaviorSanitizer for
# every target, third-party code included, so a leak or undefined behaviour
# anywhere fails the test that triggers it.
option(HOST_SANITIZE "Build host tests with ASan, LSan and UBSan" ON)
if(HOST_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c)
FetchContent_MakeAvailable(unity)

# Same cJSON version as ESP-IDF v5.4.4's json component. SOURCE_SUBDIR points
# at a directory that does not exist so only the sources are downloaded; cJSON's
# own CMake project is not used.
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
  SOURCE_SUBDIR do-not-configure)
FetchContent_MakeAvailable(cjson)

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson SYSTEM PUBLIC ${cjson_SOURCE_DIR})

set(FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(node_hal_api INTERFACE)
target_include_directories(node_hal_api INTERFACE
  ${FIRMWARE_DIR}/components/node_hal/include)

add_library(node_hal_mock STATIC
  mocks/node_hal_mock.c
  mocks/node_hal_gpio.c
  mocks/node_hal_i2c.c
  mocks/node_hal_time.c)
target_include_directories(node_hal_mock PUBLIC mocks)
target_compile_options(node_hal_mock PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_hal_mock PUBLIC node_hal_api)

add_library(node_hal_header_check OBJECT node_hal_header_check.c)
target_compile_options(node_hal_header_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_hal_header_check PRIVATE node_hal_api)

add_library(node_config_api INTERFACE)
target_include_directories(node_config_api INTERFACE
  ${FIRMWARE_DIR}/components/node_config/include)
target_link_libraries(node_config_api INTERFACE node_hal_api)

add_library(node_config_header_check OBJECT node_config_header_check.c)
target_compile_options(node_config_header_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_config_header_check PRIVATE node_config_api)

# The real parser sources, with the host log mock in place of
# node_config_log.c.
set(NODE_CONFIG_DIR ${FIRMWARE_DIR}/components/node_config)
add_library(node_config STATIC
  ${NODE_CONFIG_DIR}/node_config_err.c
  ${NODE_CONFIG_DIR}/node_config_parse.c
  ${NODE_CONFIG_DIR}/node_config_validate.c
  ${NODE_CONFIG_DIR}/node_config_composition.c
  mocks/node_config_log.c)
target_include_directories(node_config PUBLIC mocks PRIVATE ${NODE_CONFIG_DIR})
target_compile_options(node_config PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_config PUBLIC node_config_api PRIVATE cjson)

enable_testing()

function(add_host_test name)
  add_executable(${name} ${ARGN})
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(${name} PRIVATE unity)
  add_test(NAME ${name} COMMAND ${name})
  if(HOST_SANITIZE)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
      "ASAN_OPTIONS=detect_leaks=1;UBSAN_OPTIONS=print_stacktrace=1")
  endif()
endfunction()

add_host_test(test_host_build test_host_build.c)

add_host_test(test_node_hal_gpio test_node_hal_gpio.c)
target_link_libraries(test_node_hal_gpio PRIVATE node_hal_mock)

add_host_test(test_node_hal_i2c test_node_hal_i2c.c)
target_link_libraries(test_node_hal_i2c PRIVATE node_hal_mock)

add_host_test(test_node_hal_time test_node_hal_time.c)
target_link_libraries(test_node_hal_time PRIVATE node_hal_mock)
