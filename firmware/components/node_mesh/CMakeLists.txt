# The mesh node needs Bluetooth, which the QEMU build disables; there the
# component has no sources. The requirements stay unconditional, because
# ESP-IDF reads them before the Kconfig values are known.
set(srcs)
if(CONFIG_NODE_MESH_ENABLE)
  list(APPEND srcs "node_mesh_comp.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES bt node_config)

if(CONFIG_NODE_MESH_ENABLE)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
endif()
