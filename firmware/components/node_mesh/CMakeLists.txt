# The mesh node needs Bluetooth, which the QEMU build disables; there the
# component has no sources. The requirements stay unconditional, because
# ESP-IDF reads them before the Kconfig values are known.
set(srcs)
if(CONFIG_NODE_MESH_ENABLE)
  list(APPEND srcs "node_mesh.c" "node_mesh_ao.c" "node_mesh_comp.c"
                   "node_mesh_log.c" "node_mesh_port.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES bt node_config
                       PRIV_REQUIRES esp_hw_support esp_system heap log
                                     node_ao node_hal node_health nvs_flash)

if(CONFIG_NODE_MESH_ENABLE)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
endif()
