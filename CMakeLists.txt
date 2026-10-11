cmake_minimum_required(VERSION 3.22)

project(obs-phone-camera VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(OCP_BUILD_TESTS "Build unit and integration tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(ocp_core STATIC
    src/core/crypto/sha256.cpp
    src/core/crypto/sha256.h
    src/core/crypto/hmac.cpp
    src/core/crypto/hmac.h
    src/core/crypto/hkdf.cpp
    src/core/crypto/hkdf.h
    src/core/crypto/pbkdf2.cpp
    src/core/crypto/pbkdf2.h
    src/core/crypto/chacha20.cpp
    src/core/crypto/chacha20.h
    src/core/crypto/random.cpp
    src/core/crypto/random.h
    src/core/crypto/hex.cpp
    src/core/crypto/hex.h
    src/core/json/json.cpp
    src/core/json/json.h
    src/core/protocol/constants.h
    src/core/protocol/control_frame.cpp
    src/core/protocol/control_frame.h
    src/core/protocol/messages.cpp
    src/core/protocol/messages.h
    src/core/protocol/media_packet.cpp
    src/core/protocol/media_packet.h
    src/core/protocol/reassembler.cpp
    src/core/protocol/reassembler.h
    src/core/protocol/fragmenter.cpp
    src/core/protocol/fragmenter.h
    src/core/pairing/pairing.cpp
    src/core/pairing/pairing.h
    src/core/discovery/discovery.cpp
    src/core/discovery/discovery.h
    src/core/settings/config.cpp
    src/core/settings/config.h
    src/core/codec/h264.cpp
    src/core/codec/h264.h
)

target_include_directories(ocp_core PUBLIC src/core)

if(MSVC)
    target_compile_options(ocp_core PUBLIC /W4 /permissive- /utf-8 /MP)
    target_compile_definitions(ocp_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(ocp_core PUBLIC -Wall -Wextra)
endif()

if(WIN32)
    target_link_libraries(ocp_core PUBLIC bcrypt)

    add_library(ocp_decoder STATIC
        src/plugin/decoder_win.cpp
        src/plugin/decoder.h
    )
    target_include_directories(ocp_decoder PUBLIC src/plugin)
    target_link_libraries(ocp_decoder PUBLIC ocp_core mfplat mfuuid strmiids ole32)
endif()

find_package(LibObs)
if(LibObs_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/CMakeLists.txt")
    add_subdirectory(src/plugin)
elseif(NOT LibObs_FOUND)
    message(WARNING "libobs not found - only ocp_core and tests will be built")
endif()

if(OCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
