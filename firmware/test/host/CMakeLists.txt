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

add_library(node_ao_api INTERFACE)
target_include_directories(node_ao_api INTERFACE
  ${FIRMWARE_DIR}/components/node_ao/include)

add_library(node_ao_header_check OBJECT node_ao_header_check.c)
target_compile_options(node_ao_header_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_ao_header_check PRIVATE node_ao_api)

# The real node_ao core, with the synchronous host port in place of the
# FreeRTOS node_ao_port.c. Time events run on the mock HAL clock.
set(NODE_AO_DIR ${FIRMWARE_DIR}/components/node_ao)
add_library(node_ao STATIC
  ${NODE_AO_DIR}/node_ao.c
  ${NODE_AO_DIR}/node_ao_pubsub.c
  ${NODE_AO_DIR}/node_ao_time.c
  mocks/node_ao_port.c)
target_include_directories(node_ao PUBLIC mocks PRIVATE ${NODE_AO_DIR})
target_compile_options(node_ao PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_ao PUBLIC node_ao_api node_hal_mock)

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
  mocks/node_config_log.c)
target_include_directories(node_config PUBLIC mocks PRIVATE ${NODE_CONFIG_DIR})
target_compile_options(node_config PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_config PUBLIC node_config_api PRIVATE cjson)

add_library(node_dev_api INTERFACE)
target_include_directories(node_dev_api INTERFACE
  ${FIRMWARE_DIR}/components/node_dev/include)
target_link_libraries(node_dev_api INTERFACE node_ao_api node_config_api)

add_library(node_dev_header_check OBJECT node_dev_header_check.c)
target_compile_options(node_dev_header_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_dev_header_check PRIVATE node_dev_api)

# The real node_dev sources, on the host node_ao port and the mock HAL, with
# the host log mock in place of node_dev_log.c.
set(NODE_DEV_DIR ${FIRMWARE_DIR}/components/node_dev)
add_library(node_dev STATIC
  ${NODE_DEV_DIR}/node_dev.c
  ${NODE_DEV_DIR}/node_dev_led.c
  ${NODE_DEV_DIR}/node_dev_pir.c
  ${NODE_DEV_DIR}/node_dev_report.c
  ${NODE_DEV_DIR}/node_dev_shtc3.c
  mocks/node_dev_log.c)
target_include_directories(node_dev PUBLIC mocks PRIVATE ${NODE_DEV_DIR})
target_compile_options(node_dev PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_dev PUBLIC node_dev_api node_ao node_config PRIVATE m)

add_library(node_status_api INTERFACE)
target_include_directories(node_status_api INTERFACE
  ${FIRMWARE_DIR}/components/node_status/include)

add_library(node_status_header_check OBJECT node_status_header_check.c)
target_compile_options(node_status_header_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_status_header_check PRIVATE node_status_api)

# The real node_status source, on the host node_ao port and the mock HAL,
# with the host log mock in place of node_status_log.c.
set(NODE_STATUS_DIR ${FIRMWARE_DIR}/components/node_status)
add_library(node_status STATIC
  ${NODE_STATUS_DIR}/node_status.c
  mocks/node_status_log.c)
target_include_directories(node_status PUBLIC mocks PRIVATE ${NODE_STATUS_DIR})
target_compile_options(node_status PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_status PUBLIC node_status_api node_ao)

add_library(node_health_api INTERFACE)
target_include_directories(node_health_api INTERFACE
  ${FIRMWARE_DIR}/components/node_health/include)

add_library(node_health_header_check OBJECT node_health_header_check.c)
target_compile_options(node_health_header_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_health_header_check PRIVATE node_health_api)

# The real node_health logic, with the in-memory fake in place of
# node_health_port.c, the host log mock in place of node_health_log.c, and
# the real config_store on its in-memory file fake. The health AO runs on the
# host node_ao port and the mock HAL.
set(NODE_HEALTH_DIR ${FIRMWARE_DIR}/components/node_health)
add_library(node_health STATIC
  ${NODE_HEALTH_DIR}/node_health.c
  ${NODE_HEALTH_DIR}/node_health_ao.c
  mocks/node_health_port.c
  mocks/node_health_log.c)
target_include_directories(node_health PUBLIC mocks ${NODE_HEALTH_DIR})
target_compile_options(node_health PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_health PUBLIC node_health_api config_store node_ao)

add_library(node_mesh_api INTERFACE)
target_include_directories(node_mesh_api INTERFACE
  ${FIRMWARE_DIR}/components/node_mesh/include)

# node_mesh.h needs ESP-BLE-MESH, so only the ESP-IDF-free headers are
# checked on the host.
set(NODE_MESH_DIR ${FIRMWARE_DIR}/components/node_mesh)
add_library(node_mesh_header_check OBJECT node_mesh_header_check.c)
target_compile_options(node_mesh_header_check PRIVATE -Wall -Wextra -Werror)
target_include_directories(node_mesh_header_check PRIVATE ${NODE_MESH_DIR})
target_link_libraries(node_mesh_header_check PRIVATE node_mesh_api
                      node_config_api node_dev_api)

# The node_mesh sources free of ESP-BLE-MESH: the device UUID, the mesh AO,
# the vendor model messages and the models AO, on the host node_ao port,
# with fakes in place of node_mesh_port.c, node_mesh_models_port.c and
# node_dev.c's node_dev_set_onoff(), and the host log mock in place of
# node_mesh_log.c.
add_library(node_mesh STATIC
  ${NODE_MESH_DIR}/node_mesh_ao.c
  ${NODE_MESH_DIR}/node_mesh_models.c
  ${NODE_MESH_DIR}/node_mesh_msg.c
  ${NODE_MESH_DIR}/node_mesh_uuid.c
  mocks/node_dev.c
  mocks/node_mesh_models_port.c
  mocks/node_mesh_port.c
  mocks/node_mesh_log.c)
target_include_directories(node_mesh PUBLIC mocks ${NODE_MESH_DIR})
target_compile_options(node_mesh PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_mesh PUBLIC node_mesh_api node_config node_ao
                      node_dev_api PRIVATE node_proto m)

# The vendored nanopb runtime, built unchanged with its own flags, and the
# mesh messages generated from proto/ by the same rule as the firmware's
# node_proto component.
set(NANOPB_DIR ${FIRMWARE_DIR}/components/nanopb)
add_library(nanopb STATIC
  ${NANOPB_DIR}/pb_common.c
  ${NANOPB_DIR}/pb_decode.c
  ${NANOPB_DIR}/pb_encode.c)
target_include_directories(nanopb SYSTEM PUBLIC ${NANOPB_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(${FIRMWARE_DIR}/../proto/nanopb.cmake)
set(NODE_PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/node_proto)
nanopb_generate(${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/nanopb_venv
                ${NODE_PROTO_GENERATED_DIR} node_proto_srcs)
add_library(node_proto STATIC ${node_proto_srcs})
target_include_directories(node_proto PUBLIC ${NODE_PROTO_GENERATED_DIR})
target_compile_options(node_proto PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_proto PUBLIC nanopb)

add_library(config_store_api INTERFACE)
target_include_directories(config_store_api INTERFACE
  ${FIRMWARE_DIR}/components/config_store/include)
target_link_libraries(config_store_api INTERFACE node_config_api)

add_library(config_store_header_check OBJECT config_store_header_check.c)
target_compile_options(config_store_header_check PRIVATE -Wall -Wextra -Werror)
target_include_directories(config_store_header_check PRIVATE
  ${FIRMWARE_DIR}/components/config_store)
target_link_libraries(config_store_header_check PRIVATE config_store_api)

# In-memory stand-in for config_store_fs.c, the LittleFS file layer.
add_library(config_store_fs_mock STATIC mocks/config_store_fs.c)
target_include_directories(config_store_fs_mock PUBLIC mocks
  ${FIRMWARE_DIR}/components/config_store)
target_compile_options(config_store_fs_mock PRIVATE -Wall -Wextra -Werror)
target_link_libraries(config_store_fs_mock PUBLIC config_store_api)

# The real config_store sources, with the in-memory file fake in place of
# config_store_fs.c and the host log mock in place of config_store_log.c.
set(CONFIG_STORE_DIR ${FIRMWARE_DIR}/components/config_store)
add_library(config_store STATIC
  ${CONFIG_STORE_DIR}/config_store.c
  ${CONFIG_STORE_DIR}/config_store_crc.c
  ${CONFIG_STORE_DIR}/config_store_mode.c
  mocks/config_store_log.c)
target_include_directories(config_store PUBLIC mocks PRIVATE ${CONFIG_STORE_DIR})
target_compile_options(config_store PRIVATE -Wall -Wextra -Werror)
target_link_libraries(config_store PUBLIC config_store_api config_store_fs_mock
                      node_config)

# Embed configs/example-shtc3-pir-led.json in the tests as a byte array, so
# they check the same file the JSON Schema validates without reading files at
# run time. Editing the JSON re-runs this step.
set(EXAMPLE_CONFIG ${FIRMWARE_DIR}/../configs/example-shtc3-pir-led.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EXAMPLE_CONFIG})
file(READ ${EXAMPLE_CONFIG} example_config_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," example_config_bytes
       "${example_config_hex}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/node_config_example.h
"#pragma once\n\n"
"/** @brief Bytes of configs/example-shtc3-pir-led.json, not NUL-terminated.\n"
" *         Generated by firmware/test/host/CMakeLists.txt. */\n"
"static const char g_example_config[] = {${example_config_bytes}};\n")
add_library(node_config_example INTERFACE)
target_include_directories(node_config_example INTERFACE
  ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Embed proto/testdata/node_vectors.json, the mesh message vectors shared with
# the gateway tool's tests (spec 11), the same way. Editing the JSON re-runs
# this step.
set(NODE_VECTORS ${FIRMWARE_DIR}/../proto/testdata/node_vectors.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NODE_VECTORS})
file(READ ${NODE_VECTORS} node_vectors_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," node_vectors_bytes
       "${node_vectors_hex}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/node_vectors.h
"#pragma once\n\n"
"/** @brief Bytes of proto/testdata/node_vectors.json, not NUL-terminated.\n"
" *         Generated by firmware/test/host/CMakeLists.txt. */\n"
"static const char g_node_vectors[] = {${node_vectors_bytes}};\n")
add_library(node_vectors INTERFACE)
target_include_directories(node_vectors INTERFACE
  ${CMAKE_CURRENT_BINARY_DIR}/generated)

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

add_host_test(test_node_ao test_node_ao.c)
target_link_libraries(test_node_ao PRIVATE node_ao)

add_host_test(test_node_ao_time test_node_ao_time.c)
target_link_libraries(test_node_ao_time PRIVATE node_ao)

add_host_test(test_node_ao_pubsub test_node_ao_pubsub.c)
target_link_libraries(test_node_ao_pubsub PRIVATE node_ao)

add_host_test(test_node_dev_report test_node_dev_report.c)
target_include_directories(test_node_dev_report PRIVATE ${NODE_DEV_DIR})
target_link_libraries(test_node_dev_report PRIVATE node_dev)

add_host_test(test_node_dev test_node_dev.c)
target_include_directories(test_node_dev PRIVATE ${NODE_DEV_DIR})
target_link_libraries(test_node_dev PRIVATE node_dev)

add_host_test(test_node_dev_pir test_node_dev_pir.c)
target_include_directories(test_node_dev_pir PRIVATE ${NODE_DEV_DIR})
target_link_libraries(test_node_dev_pir PRIVATE node_dev)

add_host_test(test_node_dev_led test_node_dev_led.c)
target_include_directories(test_node_dev_led PRIVATE ${NODE_DEV_DIR})
target_link_libraries(test_node_dev_led PRIVATE node_dev)

add_host_test(test_node_dev_shtc3 test_node_dev_shtc3.c)
target_include_directories(test_node_dev_shtc3 PRIVATE ${NODE_DEV_DIR})
target_link_libraries(test_node_dev_shtc3 PRIVATE node_dev)

add_host_test(test_node_status test_node_status.c)
target_include_directories(test_node_status PRIVATE ${NODE_STATUS_DIR})
target_link_libraries(test_node_status PRIVATE node_status)

add_host_test(test_node_health test_node_health.c)
target_link_libraries(test_node_health PRIVATE node_health)

add_host_test(test_node_health_ao test_node_health_ao.c)
target_link_libraries(test_node_health_ao PRIVATE node_health)

add_host_test(test_node_mesh_uuid test_node_mesh_uuid.c)
target_link_libraries(test_node_mesh_uuid PRIVATE node_mesh)

add_host_test(test_node_mesh_ao test_node_mesh_ao.c)
target_link_libraries(test_node_mesh_ao PRIVATE node_mesh)

add_host_test(test_node_mesh_msg test_node_mesh_msg.c)
target_link_libraries(test_node_mesh_msg PRIVATE node_mesh node_proto
                      node_vectors cjson)

add_host_test(test_node_config_parse test_node_config_parse.c)
target_link_libraries(test_node_config_parse PRIVATE node_config
                      node_config_example)

add_host_test(test_node_config_validate test_node_config_validate.c)
target_link_libraries(test_node_config_validate PRIVATE node_config)

add_host_test(test_config_store_crc test_config_store_crc.c)
target_include_directories(test_config_store_crc PRIVATE ${CONFIG_STORE_DIR})
target_link_libraries(test_config_store_crc PRIVATE config_store)

add_host_test(test_config_store test_config_store.c)
target_include_directories(test_config_store PRIVATE ${CONFIG_STORE_DIR})
target_link_libraries(test_config_store PRIVATE config_store)

add_host_test(test_config_store_mode test_config_store_mode.c)
target_link_libraries(test_config_store_mode PRIVATE config_store
                      node_config_example)

# The PC config checker (spec 05), built from the same parser sources and run
# on config files. Each test runs it once and checks its exit code and stderr.
add_subdirectory(${FIRMWARE_DIR}/../tools/node_config_check
                 ${CMAKE_CURRENT_BINARY_DIR}/node_config_check)

function(add_config_checker_test name config expected_exit expected_stderr)
  add_test(NAME ${name}
           COMMAND ${CMAKE_COMMAND}
                   -DCHECKER=$<TARGET_FILE:node_config_check>
                   -DCONFIG=${config}
                   -DEXPECTED_EXIT=${expected_exit}
                   -DEXPECTED_STDERR=${expected_stderr}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/run_node_config_check.cmake)
  if(HOST_SANITIZE)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
      "ASAN_OPTIONS=detect_leaks=1;UBSAN_OPTIONS=print_stacktrace=1")
  endif()
endfunction()

# AC: specs/05-config-flash-tool.md #6
add_config_checker_test(test_example_config_is_accepted_by_the_pc_config_checker
  ${EXAMPLE_CONFIG} 0 "^$")

# AC: specs/05-config-flash-tool.md #6
add_config_checker_test(
  test_config_using_a_flash_pin_is_rejected_by_the_pc_config_checker_with_its_path
  ${CMAKE_CURRENT_SOURCE_DIR}/configs/config_with_a_flash_pin.json 1
  "devices\\[0\\]\\.gpio: GPIO 7 is used by the flash")

# AC: specs/16-status-led.md #4
add_config_checker_test(
  test_config_using_the_status_led_pin_is_rejected_by_the_pc_config_checker
  ${CMAKE_CURRENT_SOURCE_DIR}/configs/config_with_the_status_led_pin.json 1
  "devices\\[0\\]\\.gpio: GPIO 0 is used by the status LED")

# AC: specs/09-health-factory-reset.md #6
add_test(NAME test_debug_hang_option_is_never_enabled_in_the_hardware_or_qemu_defaults
         COMMAND ${CMAKE_COMMAND} -DFIRMWARE_DIR=${FIRMWARE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_debug_hang_is_off.cmake)
