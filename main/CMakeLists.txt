option(KVS_PLAT_ESP_FREERTOS   "Build for ESP FreeRTOS"   ON)
option(KVS_PLAT_ANYKE_FREERTOS "Build for ANYKE FreeRTOS" OFF)
option(ENABLE_STREAMING "enable the streaming" ON)
option(ENABLE_DATA_CHANNEL "enable the data channel" ON)
option(ENABLE_HEADER_CONFIG_CLIENT_INFO "enable config app client info with config header" ON)

if (ENABLE_HEADER_CONFIG_CLIENT_INFO AND KVS_PLAT_ANYKE_FREERTOS)
        include(AppConfig.cmake)
        set(KVS_SDK_ROOT "${PROJECT_SOURCE_DIR}/main/lib/amazon-kinesis-video-streams-webrtc-sdk-c")
        configure_file(cmake_config_aws.h.in "${KVS_SDK_ROOT}/src/source/utils/cmake_config_aws.h")
        configure_file(cmake_config_aws.c.in "${KVS_SDK_ROOT}/src/source/utils/cmake_config_aws.c")
        add_definitions(-DENABLE_HEADER_CONFIG_CLIENT_INFO)
endif()

if (KVS_PLAT_ANYKE_FREERTOS)
	set(PLATFORM_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../os/kernel/bsp"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/bsp/configs"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/bsp/include"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/bsp/include/linux"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/bsp/osal"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/driver/core"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/libc/pthreads"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/libc/time"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/msh"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/net/lwip-2.1.0/src"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/net/lwip-2.1.0/src/arch/include"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/net/lwip-2.1.0/src/include"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/net/lwip-2.1.0/src/include/compat/posix"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/net/netdev/include"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/components/vfs/include"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/FreeRTOS/Source/include"
                                  "${CMAKE_SOURCE_DIR}/../../os/kernel/FreeRTOS/Source/portable/GCC/ARM926EJ-S")
	include_directories("${PLATFORM_INCLUDE_DIRS}")

	set(COMPONENT_LIB webrtc_test)
endif()

set(LIB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/main/lib")

set(LIBKVSWEBRTC_NAME "amazon-kinesis-video-streams-webrtc-sdk-c")
set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/include"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/api_call"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/credential"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/crypto"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/ice"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Json"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/net"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/PeerConnection"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtcp"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtp"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Rtp/Codecs"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/sctp"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/Sdp"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/signaling"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/srtp"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/state_machine"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/stun"
                              "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/utils")

set(OPEN_SRC_INCLUDE_DIRS "${LIBKVSWEBRTC_INCLUDE_DIRS}")
set(APP_INCLUDE_DIRS "./include")

if (KVS_PLAT_ESP_FREERTOS)
	idf_component_register(SRCS "kvswebrtc_esp32_main.c"
                                    "AppCommon.c"
                                    "AppCredential.c"
                                    "AppDataChannel.c"
                                    "AppMediaSrc_FileSrc.c"
                                    "AppMain.c"
                                    "AppMessageQueue.c"
                                    "AppMetrics.c"
                                    "AppSignaling.c"
                                    "AppWebRTC.c"
                    INCLUDE_DIRS "." "${OPEN_SRC_INCLUDE_DIRS}" "${APP_INCLUDE_DIRS}"
                    REQUIRES nvs_flash mbedtls lwip fatfs esp_common pthread
                    )

	idf_component_get_property(nvs_flash_lib nvs_flash COMPONENT_LIB)
	idf_component_get_property(mbedtls_lib mbedtls COMPONENT_LIB)
	idf_component_get_property(esp_common_lib esp_common COMPONENT_LIB)
	idf_component_get_property(pthread_lib pthread COMPONENT_LIB)
endif()

if (KVS_PLAT_ANYKE_FREERTOS)
	add_library(${COMPONENT_LIB} STATIC kvswebrtc_anyke_main.c
                                            AppCommon.c
                                            AppCredential.c
                                            AppDataChannel.c
                                            AppMediaSrc_FileSrc.c
                                            AppMain.c
                                            AppMessageQueue.c
                                            AppMetrics.c
                                            AppSignaling.c
                                            AppWebRTC.c)

	target_include_directories(${COMPONENT_LIB} PUBLIC
                                   ${PLATFORM_INCLUDE_DIRS}
                                   ${OPEN_SRC_INCLUDE_DIRS}
                                   ${APP_INCLUDE_DIRS}
	)

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb -O2 -mcpu=arm926ej-s -std=c99 -fgnu89-inline -mlittle-endian -fno-short-enums -fno-builtin -nostdlib -static -nostartfiles  -mlong-calls -fdiagnostics-color=auto")
	add_definitions(-DRT_USING_LWIP_IPV6)
	add_definitions(-DAK_RTOS)
	add_definitions(-DKERNEL_FREERTOS)
	add_definitions(-DFAST_LAUNCH)
	add_definitions(-DISP_QUICK_START_ENABLE)
	add_definitions(-D__CHIP_AK3918AV100_SERIES)
	add_definitions(-DMACH_AK3918AV100)
	add_definitions(-DMBEDTLS_CONFIG_FILE="mbedtls/anyke_config.h")
endif()

if (ENABLE_STREAMING)
        add_definitions(-DENABLE_STREAMING)
        set(ENABLE_STREAMING ON CACHE BOOL "Enable support for streaming")
        set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIBKVSWEBRTC_INCLUDE_DIRS}" "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/srtp")
else()
        set(ENABLE_STREAMING OFF CACHE BOOL "Enable support for streaming")
endif()
if (ENABLE_DATA_CHANNEL)
        add_definitions(-DENABLE_DATA_CHANNEL)
        set(ENABLE_DATA_CHANNEL ON CACHE BOOL "Enable support for data channel")
        set(LIBKVSWEBRTC_INCLUDE_DIRS "${LIBKVSWEBRTC_INCLUDE_DIRS}" "${LIB_SOURCE_DIR}/${LIBKVSWEBRTC_NAME}/src/source/sctp")
else()
        set(ENABLE_DATA_CHANNEL OFF CACHE BOOL "Enable support for data channel")
endif()


message("############################## start llhttp ##############################")
add_subdirectory(lib/libllhttp)
set_property(TARGET llhttp APPEND PROPERTY LINK_LIBRARIES
        ${nvs_flash_lib}
	${mbedtls_lib})
set_property(TARGET llhttp APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${nvs_flash_lib}
	${mbedtls_lib})
target_link_libraries(${COMPONENT_LIB} PUBLIC llhttp)
if (KVS_PLAT_ESP_FREERTOS)
	set(KVS_LIBLLHTTP_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/esp-idf/main/lib/libllhttp/include" "${LIB_SOURCE_DIR}/libllhttp/include")
endif()
if (KVS_PLAT_ANYKE_FREERTOS)
	set(KVS_LIBLLHTTP_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/main/lib/libllhttp/include" "${LIB_SOURCE_DIR}/libllhttp/include")
endif()

message("############################## start wslay ##############################")
add_subdirectory(lib/wslay)
set_property(TARGET wslay APPEND PROPERTY LINK_LIBRARIES
        ${nvs_flash_lib}
	${mbedtls_lib})
set_property(TARGET wslay APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${nvs_flash_lib}
	${mbedtls_lib})
target_link_libraries(${COMPONENT_LIB} PUBLIC wslay)
if (KVS_PLAT_ESP_FREERTOS)
	set(KVS_LIBWSLAY_INCLUDE_DIRS "${LIB_SOURCE_DIR}/wslay/lib/includes" "${CMAKE_BINARY_DIR}/esp-idf/main/lib/wslay/lib/includes")
endif()
if (KVS_PLAT_ANYKE_FREERTOS)
	set(KVS_LIBWSLAY_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/main/lib/wslay/lib/includes" "${LIB_SOURCE_DIR}/wslay/lib/includes")
endif()

if (KVS_PLAT_ANYKE_FREERTOS)
	message("############################## start mbedtls ##############################")
	add_subdirectory(lib/mbedtls)
	set_property(TARGET mbedtls APPEND PROPERTY LINK_LIBRARIES
	        ${nvs_flash_lib}
	        ${mbedtls_lib})
	set_property(TARGET mbedtls APPEND PROPERTY INTERFACE_LINK_LIBRARIES
	        ${nvs_flash_lib}
	        ${mbedtls_lib})
	target_link_libraries(${COMPONENT_LIB} PUBLIC mbedtls)
	set(KVS_LIBMBEDTLS_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/main/lib/mbedtls/vendor/anyke/mbedtls/lib/includes"
                                "${LIB_SOURCE_DIR}/mbedtls/vendor/anyke/mbedtls/include"
                                "${LIB_SOURCE_DIR}/mbedtls/vendor/anyke/port/include")
	set(MBEDTLS_INCLUDE_DIRS "${KVS_LIBMBEDTLS_INCLUDE_DIRS}")
endif()

if(ENABLE_STREAMING)
        message("############################## start srtp ##############################")
        add_subdirectory(lib/libsrtp)
        set_property(TARGET srtp2 APPEND PROPERTY LINK_LIBRARIES ${mbedtls_lib})
        set_property(TARGET srtp2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${mbedtls_lib})
        target_link_libraries(${COMPONENT_LIB} PUBLIC srtp2)
        set(KVS_LIBSRTP_INCLUDE_DIRS "${LIB_SOURCE_DIR}/libsrtp/include")
endif()

if(ENABLE_DATA_CHANNEL)
        message("############################## start sctp ##############################")
        add_definitions(-DSCTP_USE_LWIP)
        add_definitions(-DSCTP_USE_RTOS)
        set(sctp_link_programs_static ON CACHE BOOL "Link example programs static")
        add_subdirectory(lib/usrsctp)
        set_property(TARGET usrsctp APPEND PROPERTY LINK_LIBRARIES ${mbedtls_lib} ${esp_common_lib} ${pthread_lib})
        set_property(TARGET usrsctp APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${mbedtls_lib} ${esp_common_lib} ${pthread_lib})
        set_property(TARGET usrsctp-static APPEND PROPERTY LINK_LIBRARIES ${mbedtls_lib} ${esp_common_lib} ${pthread_lib})
        set_property(TARGET usrsctp-static APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${mbedtls_lib} ${esp_common_lib} ${pthread_lib})
        target_link_libraries(${COMPONENT_LIB} PUBLIC usrsctp)
        set(KVS_USRSCTP_INCLUDE_DIRS "${LIB_SOURCE_DIR}/usrsctp/usrsctplib")
endif()

message("############################## start webrtc ##############################")
set(BUILD_DEPENDENCIES OFF CACHE BOOL "Whether or not to build depending libraries from source")
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build all libraries statically. (This includes third-party libraries.)")
set(BUILD_SAMPLE OFF CACHE BOOL "Build available samples")
set(OPEN_SRC_INCLUDE_DIRS "${OPEN_SRC_INCLUDE_DIRS}" "${KVS_LIBMBEDTLS_INCLUDE_DIRS}" "${KVS_LIBWSLAY_INCLUDE_DIRS}" "${KVS_LIBLLHTTP_INCLUDE_DIRS}" "${KVS_LIBSRTP_INCLUDE_DIRS}" "${KVS_USRSCTP_INCLUDE_DIRS}")

add_subdirectory(lib/amazon-kinesis-video-streams-webrtc-sdk-c)

set_property(TARGET kvsWebrtcStateMachine APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib} ${mbedtls_lib} ${nvs_flash_lib})
set_property(TARGET kvsWebrtcStateMachine APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib} ${mbedtls_lib} ${nvs_flash_lib})

set_property(TARGET kvsWebrtcUtils APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib} ${mbedtls_lib} ${nvs_flash_lib})
set_property(TARGET kvsWebrtcUtils APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib} ${mbedtls_lib} ${nvs_flash_lib})

set_property(TARGET kvsWebrtcSignalingClient APPEND PROPERTY LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        wslay
        llhttp
        ${mbedtls_lib})
set_property(TARGET kvsWebrtcSignalingClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        wslay
        llhttp
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
if(ENABLE_DATA_CHANNEL)
        set_property(TARGET kvsWebrtcClient APPEND PROPERTY LINK_LIBRARIES usrsctp)
endif()

set_property(TARGET kvsWebrtcClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${esp_common_lib} ${pthread_lib}
        mbedtls
        ${mbedtls_lib}
        ${nvs_flash_lib})

if(ENABLE_STREAMING)
        set_property(TARGET kvsWebrtcClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES srtp2)
endif()
if(ENABLE_DATA_CHANNEL)
        set_property(TARGET kvsWebrtcClient APPEND PROPERTY INTERFACE_LINK_LIBRARIES usrsctp)
endif()
target_link_libraries(${COMPONENT_LIB} PUBLIC kvsWebrtcClient)

if (KVS_PLAT_ANYKE_FREERTOS)
	install(TARGETS ${COMPONENT_LIB} DESTINATION lib)
endif()
