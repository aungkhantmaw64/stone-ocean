cmake_minimum_required(VERSION 3.16...3.30)
project(node_config_check C)

# Same cJSON version as ESP-IDF v5.4.4's json component and the host tests.
# SOURCE_SUBDIR points at a directory that does not exist so only the sources
# are downloaded; cJSON's own CMake project is not used.
include(FetchContent)
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
  SOURCE_SUBDIR do-not-configure)
FetchContent_MakeAvailable(cjson)

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson SYSTEM PUBLIC ${cjson_SOURCE_DIR})

# The firmware's parser sources, with node_config_log.c from this directory
# in place of the ESP-IDF one, so warnings go to stderr.
set(FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware)
set(NODE_CONFIG_DIR ${FIRMWARE_DIR}/components/node_config)
add_executable(node_config_check
  node_config_check.c
  node_config_log.c
  ${NODE_CONFIG_DIR}/node_config_err.c
  ${NODE_CONFIG_DIR}/node_config_parse.c
  ${NODE_CONFIG_DIR}/node_config_validate.c
  ${NODE_CONFIG_DIR}/node_config_composition.c)
target_include_directories(node_config_check PRIVATE
  ${NODE_CONFIG_DIR}
  ${NODE_CONFIG_DIR}/include
  ${FIRMWARE_DIR}/components/node_hal/include)
target_compile_options(node_config_check PRIVATE -Wall -Wextra -Werror)
target_link_libraries(node_config_check PRIVATE cjson)
