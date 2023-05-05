# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(KVS_PLAT_ESP_FREERTOS   ON   CACHE BOOL "Build for ESP FreeRTOS")
set(KVS_PLAT_ANYKE_FREERTOS OFF  CACHE BOOL "Build for ANYKE FreeRTOS")

if(KVS_PLAT_ESP_FREERTOS)
  add_definitions(-DKVS_PLAT_ESP_FREERTOS)
endif()

if(KVS_PLAT_ANYKE_FREERTOS)
  add_definitions(-DKVS_PLAT_ANYKE_FREERTOS)
endif()

if(KVS_PLAT_ESP_FREERTOS)
  include($ENV{IDF_PATH}/tools/cmake/project.cmake)
endif()

project(webrtc_example)

if(KVS_PLAT_ANYKE_FREERTOS)
  add_subdirectory(main)
endif()
