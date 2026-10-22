cmake_minimum_required(VERSION 3.16...3.30)
project(mesh_node_host_tests C)

include(FetchContent)
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c)
FetchContent_MakeAvailable(unity)

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

enable_testing()

function(add_host_test name)
  add_executable(${name} ${ARGN})
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(${name} PRIVATE unity)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_host_test(test_host_build test_host_build.c)
