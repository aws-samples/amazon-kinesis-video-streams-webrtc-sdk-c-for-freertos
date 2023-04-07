# Only build mbedtls libraries
set(ENABLE_TESTING CACHE BOOL OFF)
set(ENABLE_PROGRAMS CACHE BOOL OFF)
# Only build mbedtls include
set(INSTALL_MBEDTLS_HEADERS CACHE BOOL OFF)

# Needed to for include_next includes to work from within mbedtls
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/port/include")

# Import mbedtls library targets
add_subdirectory(mbedtls)

# Use port specific implementation of net_socket.c instead of one from mbedtls
get_target_property(src_tls mbedtls SOURCES)
list(REMOVE_ITEM src_tls net_sockets.c)
set_property(TARGET mbedtls PROPERTY SOURCES ${src_tls})

set(mbedtls_targets mbedtls mbedcrypto mbedx509)

set(mbedtls_target_sources "${CMAKE_CURRENT_SOURCE_DIR}/port/net_sockets.c")

# Add port files to mbedtls targets
target_sources(mbedtls PRIVATE ${mbedtls_target_sources})



target_sources(mbedcrypto PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/port/entropy_alt.c")

# foreach(target ${mbedtls_targets})
#     target_compile_definitions(${target} PUBLIC -DMBEDTLS_CONFIG_FILE="mbedtls/anyke_config.h")
# endforeach()

set_property(TARGET mbedcrypto APPEND PROPERTY LINK_INTERFACE_LIBRARIES mbedtls)

# Link mbedtls libraries to component library
#target_link_libraries(${COMPONENT_LIB} PUBLIC ${mbedtls_targets})
