cmake_minimum_required(VERSION 3.16...3.30)
project(mesh_node_host_tests C)

include(FetchContent)
FetchContent_Declare(unity
  URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
  URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c)
FetchContent_MakeAvailable(unity)

enable_testing()

function(add_host_test name)
  add_executable(${name} ${ARGN})
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(${name} PRIVATE unity)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_host_test(test_host_build test_host_build.c)
