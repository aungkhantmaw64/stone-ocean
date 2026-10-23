# The mesh messages of spec 11, generated at build time from proto/ by the
# pinned nanopb generator. Include them as "stone_ocean/mesh/v1/node.pb.h".
# Generated files are not committed.
set(generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(srcs)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../proto/nanopb.cmake)
  idf_build_get_property(python PYTHON)
  idf_build_get_property(build_dir BUILD_DIR)
  nanopb_generate(${python} ${build_dir}/nanopb_venv ${generated_dir} srcs)
endif()
file(MAKE_DIRECTORY ${generated_dir})

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${generated_dir}
                       REQUIRES nanopb)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
endif()
