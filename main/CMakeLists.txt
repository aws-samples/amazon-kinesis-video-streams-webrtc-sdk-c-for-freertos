set(LIB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/main/lib")
set(LIBKVSPIC_NAME "amazon-kinesis-video-streams-pic")
set(LIBKVSPIC_INCLUDE_DIRS "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/common/include" 
                                "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/client/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/utils/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/mkvgen/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/view/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/heap/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSPIC_NAME}/src/state/include")
set(LIBKVSCOMMONLWS_NAME "amazon-kinesis-video-streams-producer-c")
set(LIBKVSCOMMONLWS_INCLUDE_DIRS "${LIB_SOURCE_DIR}/${LIBKVSCOMMONLWS_NAME}/src/include")
set(LIBKVSWEBRTC_NAME "amazon-kinesis-video-streams-webrtc-sdk-c")
set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/include"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Crypto"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Ice"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/PeerConnection"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtcp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Sctp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Sdp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Signaling"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Srtp"
                                "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Stun")

set(OPEN_SRC_INCLUDE_DIRS "${LIBKVSPIC_INCLUDE_DIRS}" "${LIBKVSCOMMONLWS_INCLUDE_DIRS}" "${LIBKVSWEBRTC_INCLUDE_DIRS}")
idf_component_register(SRCS "kvswebrtc_example_main.c" "Common.c" "kvsWebRTCClientMaster.c"
                    INCLUDE_DIRS "." "${OPEN_SRC_INCLUDE_DIRS}"
                    REQUIRES nvs_flash mbedtls lwip fatfs
                    EMBED_FILES "cert.pem")


idf_component_get_property(nvs_flash_lib nvs_flash COMPONENT_LIB)
idf_component_get_property(mbedtls_lib mbedtls COMPONENT_LIB)

add_definitions(-DKVS_PLAT_ESP_FREERTOS)

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

message("############################## start srtp ##############################")
set(KVS_PLAT_ESP_FREERTOS ON CACHE BOOL "Build for ESP FreeRTOS")
add_subdirectory(lib/libsrtp)
set_property(TARGET srtp2 APPEND PROPERTY LINK_LIBRARIES ${mbedtls_lib})
set_property(TARGET srtp2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${mbedtls_lib})        
target_link_libraries(${COMPONENT_LIB} PUBLIC srtp2)
set(KVS_LIBSRTP_INCLUDE_DIRS "${LIB_SOURCE_DIR}/libsrtp/include")

###



message("############################## start pic ##############################")
add_subdirectory(lib/amazon-kinesis-video-streams-pic)
target_link_libraries(${COMPONENT_LIB} PUBLIC kvspicState kvspicUtils)

message("############################## start producer c ##############################")
## this needs to be fixed. #YC_TBD.
set(BUILD_DEPENDENCIES OFF CACHE BOOL "Whether or not to build depending libraries from source")
set(USE_OPENSSL OFF CACHE BOOL "Use openssl as crypto library")
set(USE_MBEDTLS ON CACHE BOOL "Use openssl as crypto library")
set(BUILD_COMMON_LWS ON CACHE BOOL "Whether or not to build ProducerC libwebsockets common library")
set(BUILD_COMMON_CURL OFF CACHE BOOL "Whether or not to build ProducerC curl common library")
set(BUILD_KVSPIC OFF CACHE BOOL "Whether or not to build the part of kvs pic")
set(KVS_PLAT_ESP_FREERTOS ON CACHE BOOL "Build for ESP FreeRTOS")
set(OPEN_SRC_INCLUDE_DIRS "${OPEN_SRC_INCLUDE_DIRS}" "${KVS_LIBWEBSOCKETS_INCLUDE_DIRS}")
add_subdirectory(lib/amazon-kinesis-video-streams-producer-c)

set_property(TARGET kvsCommonLws APPEND PROPERTY LINK_LIBRARIES kvspicUtils websockets ${mbedtls_lib})
set_property(TARGET kvsCommonLws APPEND PROPERTY INTERFACE_LINK_LIBRARIES kvspicUtils websockets ${mbedtls_lib})
target_link_libraries(${COMPONENT_LIB} PUBLIC kvsCommonLws)

message("############################## start webrtc ##############################")
set(BUILD_DEPENDENCIES OFF CACHE BOOL "Whether or not to build depending libraries from source")
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build all libraries statically. (This includes third-party libraries.)")
set(BUILD_SAMPLE OFF CACHE BOOL "Build available samples")
set(ENABLE_DATA_CHANNEL OFF CACHE BOOL "Enable support for data channel")
set(OPEN_SRC_INCLUDE_DIRS "${OPEN_SRC_INCLUDE_DIRS}" "${KVS_LIBWEBSOCKETS_INCLUDE_DIRS} ${KVS_LIBSRTP_INCLUDE_DIRS}")

add_subdirectory(lib/amazon-kinesis-video-streams-webrtc-sdk-c)

set_property(TARGET kvsWebrtcSignalingClient APPEND PROPERTY LINK_LIBRARIES
        kvspicUtils
        websockets
        srtp2
        ${mbedtls_lib}
        kvsCommonLws)
set_property(TARGET kvsWebrtcSignalingClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        kvspicUtils
        websockets
        srtp2
        ${mbedtls_lib}
        kvsCommonLws)
target_link_libraries(${COMPONENT_LIB} PUBLIC kvsWebrtcSignalingClient)

set_property(TARGET kvsWebrtcClient APPEND PROPERTY LINK_LIBRARIES
        kvspicUtils
        mbedtls
        srtp2
        ${mbedtls_lib}
        ${nvs_flash_lib})
set_property(TARGET kvsWebrtcClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        kvspicUtils
        mbedtls
        srtp2
        ${mbedtls_lib}
        ${nvs_flash_lib})
target_link_libraries(${COMPONENT_LIB} PUBLIC kvsWebrtcClient)