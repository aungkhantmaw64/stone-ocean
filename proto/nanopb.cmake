# Generates nanopb C code from the .proto files in this directory. Included by
# the firmware component node_proto and by the host test build, so both
# compile the same generated code.
#
#   nanopb_generate(<python> <venv_dir> <out_dir> <out_srcs_var>)
#
# Installs proto/requirements.txt into a venv at <venv_dir> at configure time,
# made with <python>, and again whenever the file changes. Adds a build rule
# that writes the .pb.c and .pb.h files under <out_dir>, keeping their path
# below proto/: include them as "stone_ocean/mesh/v1/node.pb.h". Sets
# <out_srcs_var> to the generated .pb.c files.

set(NANOPB_PROTO_DIR ${CMAKE_CURRENT_LIST_DIR})

# The schemas to generate, relative to NANOPB_PROTO_DIR.
set(NANOPB_PROTO_FILES stone_ocean/mesh/v1/node.proto)

function(nanopb_venv python venv_dir)
  set(requirements ${NANOPB_PROTO_DIR}/requirements.txt)
  set(installed ${venv_dir}/requirements.txt)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${requirements})
  if(EXISTS ${installed})
    file(SHA256 ${requirements} wanted_hash)
    file(SHA256 ${installed} installed_hash)
    if(wanted_hash STREQUAL installed_hash)
      return()
    endif()
  endif()
  message(STATUS "Installing the nanopb generator into ${venv_dir}")
  execute_process(COMMAND ${python} -m venv ${venv_dir} RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "could not create the nanopb venv in ${venv_dir}")
  endif()
  execute_process(COMMAND ${venv_dir}/bin/pip install --quiet -r ${requirements}
                  RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "could not install ${requirements} into ${venv_dir}")
  endif()
  configure_file(${requirements} ${installed} COPYONLY)
endfunction()

function(nanopb_generate python venv_dir out_dir out_srcs_var)
  nanopb_venv(${python} ${venv_dir})
  set(srcs)
  foreach(proto ${NANOPB_PROTO_FILES})
    string(REGEX REPLACE "\\.proto$" "" stem ${proto})
    set(pb_c ${out_dir}/${stem}.pb.c)
    set(pb_h ${out_dir}/${stem}.pb.h)
    add_custom_command(
      OUTPUT ${pb_c} ${pb_h}
      COMMAND ${venv_dir}/bin/nanopb_generator --quiet -I . -D ${out_dir}
              ${proto}
      WORKING_DIRECTORY ${NANOPB_PROTO_DIR}
      DEPENDS ${NANOPB_PROTO_DIR}/${proto} ${NANOPB_PROTO_DIR}/${stem}.options
              ${venv_dir}/requirements.txt
      COMMENT "Generating nanopb code from ${proto}"
      VERBATIM)
    list(APPEND srcs ${pb_c})
  endforeach()
  file(MAKE_DIRECTORY ${out_dir})
  set(${out_srcs_var} ${srcs} PARENT_SCOPE)
endfunction()
