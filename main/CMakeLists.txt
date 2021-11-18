option(ENABLE_STREAMING "enable the streaming" ON)
option(ENABLE_DATA_CHANNEL "enable the data channel" OFF)

set(LIB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/main/lib")

set(LIBKVSWEBRTC_NAME "amazon-kinesis-video-streams-webrtc-sdk-c")
set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Crypto"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Ice"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/PeerConnection"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtcp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Sdp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Signaling"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Stun")

set(OPEN_SRC_INCLUDE_DIRS "${LIBKVSWEBRTC_INCLUDE_DIRS}")
idf_component_register(SRCS "kvswebrtc_example_main.c" "Common.c" "kvsWebRTCClientMaster.c"
                    INCLUDE_DIRS "." "${OPEN_SRC_INCLUDE_DIRS}"
                    REQUIRES nvs_flash mbedtls lwip fatfs esp_common pthread
                    EMBED_FILES "cert.pem")


idf_component_get_property(nvs_flash_lib nvs_flash COMPONENT_LIB)
idf_component_get_property(mbedtls_lib mbedtls COMPONENT_LIB)
idf_component_get_property(esp_common_lib esp_common COMPONENT_LIB)
idf_component_get_property(pthread_lib pthread COMPONENT_LIB)

set(KVS_PLAT_ESP_FREERTOS ON CACHE BOOL "Build for ESP FreeRTOS")
add_definitions(-DKVS_PLAT_ESP_FREERTOS)
if (ENABLE_STREAMING)
        add_definitions(-DENABLE_STREAMING)
        set(ENABLE_STREAMING ON CACHE BOOL "Enable support for streaming")
        set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIBKVSWEBRTC_INCLUDE_DIRS}" "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Srtp")
else()
        set(ENABLE_STREAMING OFF CACHE BOOL "Enable support for streaming")
endif()
if (ENABLE_DATA_CHANNEL)
        add_definitions(-DENABLE_DATA_CHANNEL)
        set(ENABLE_DATA_CHANNEL ON CACHE BOOL "Enable support for data channel")
        set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIBKVSWEBRTC_INCLUDE_DIRS}" "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Sctp")
else()
        set(ENABLE_DATA_CHANNEL OFF CACHE BOOL "Enable support for data channel")
endif()

message("############################## start libwebsockets ##############################")
set(ESP_PLATFORM 1) 
set(LWS_CTEST_INTERNET_AVAILABLE OFF CACHE BOOL "CTest will performs tests that need the Internet")
set(LWS_WITH_MBEDTLS ON CACHE BOOL "Use mbedTLS (>=2.0) replacement for OpenSSL. When setting this, you also may need to specify LWS_MBEDTLS_LIBRARIES and LWS_MBEDTLS_INCLUDE_DIRS")
set(LWS_WITH_SHARED OFF CACHE BOOL "Build the shared version of the library")
set(LWS_WITH_ESP32 ON CACHE BOOL "Build for ESP32")
set(LWS_PLAT_FREERTOS ON CACHE BOOL "Build for FreeRTOS")
set(LWS_WITHOUT_SERVER ON CACHE BOOL "Don't build the server part of the library")
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "Don't build the libwebsocket-test-apps")
set(LWS_WITHOUT_TEST_SERVER ON CACHE BOOL "Don't build the test server")
set(LWS_WITHOUT_TEST_SERVER_EXTPOLL ON CACHE BOOL "Don't build the test server version that uses external poll")
set(LWS_WITHOUT_TEST_PING ON CACHE BOOL "Don't build the ping test application")
set(LWS_WITHOUT_TEST_CLIENT ON CACHE BOOL "Don't build the client test application")
set(LWS_WITH_EXPORT_LWSTARGETS OFF CACHE BOOL "Export libwebsockets CMake targets.  Disable if they conflict with an outer cmake project.")
set(LWS_CLIENT_HTTP_PROXYING OFF CACHE BOOL "Support external http proxies for client connections")

add_subdirectory(lib/libwebsockets)
set_property(TARGET websockets APPEND PROPERTY LINK_LIBRARIES
        ${nvs_flash_lib}
	${mbedtls_lib})
set_property(TARGET websockets APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${nvs_flash_lib}
	${mbedtls_lib})
target_link_libraries(${COMPONENT_LIB} PUBLIC websockets)
set(KVS_LIBWEBSOCKETS_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/esp-idf/main/lib/libwebsockets/include" "${LIB_SOURCE_DIR}/libwebsockets/include")


if(ENABLE_STREAMING)
        message("############################## start srtp ##############################")
        add_subdirectory(lib/libsrtp)
        set_property(TARGET srtp2 APPEND PROPERTY LINK_LIBRARIES ${mbedtls_lib})
        set_property(TARGET srtp2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${mbedtls_lib})
        target_link_libraries(${COMPONENT_LIB} PUBLIC srtp2)
        set(KVS_LIBSRTP_INCLUDE_DIRS "${LIB_SOURCE_DIR}/libsrtp/include")
endif()

message("############################## start webrtc ##############################")
set(BUILD_DEPENDENCIES OFF CACHE BOOL "Whether or not to build depending libraries from source")
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build all libraries statically. (This includes third-party libraries.)")
set(BUILD_SAMPLE OFF CACHE BOOL "Build available samples")
set(OPEN_SRC_INCLUDE_DIRS ${OPEN_SRC_INCLUDE_DIRS} ${KVS_LIBWEBSOCKETS_INCLUDE_DIRS} ${KVS_LIBSRTP_INCLUDE_DIRS} ${KVS_USRSCTP_INCLUDE_DIRS})

add_subdirectory(lib/amazon-kinesis-video-streams-webrtc-sdk-c)

set_property(TARGET kvspicState APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib})
set_property(TARGET kvspicState APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib})

set_property(TARGET kvspicUtils APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib})
set_property(TARGET kvspicUtils APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib})

set_property(TARGET kvsWebrtcSignalingClient APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        websockets
        ${mbedtls_lib})
set_property(TARGET kvsWebrtcSignalingClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        websockets
        ${mbedtls_lib})
target_link_libraries(${COMPONENT_LIB} PUBLIC kvsWebrtcSignalingClient)

set_property(TARGET kvsWebrtcClient APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        mbedtls
        ${mbedtls_lib}
        ${nvs_flash_lib})

if(ENABLE_STREAMING)
        set_property(TARGET kvsWebrtcClient APPEND PROPERTY LINK_LIBRARIES srtp2)
endif()

set_property(TARGET kvsWebrtcClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        mbedtls
        ${mbedtls_lib}
        ${nvs_flash_lib})

if(ENABLE_STREAMING)
        set_property(TARGET kvsWebrtcClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES srtp2)
endif()

target_link_libraries(${COMPONENT_LIB} PUBLIC kvsWebrtcClient)
