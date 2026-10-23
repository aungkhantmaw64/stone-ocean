# nanopb runtime, vendored unchanged from the release in README.md. The code
# it runs is generated from proto/ by the component node_proto.
idf_component_register(SRCS "pb_common.c" "pb_decode.c" "pb_encode.c"
                       INCLUDE_DIRS ".")
