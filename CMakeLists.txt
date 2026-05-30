cmake_minimum_required(VERSION 3.20)

project(webgpu_native_cts VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CTS_BACKEND "wgpu-native" CACHE STRING "Backend to test")
set_property(CACHE CTS_BACKEND PROPERTY STRINGS "wgpu-native" "yawgpu")

set(CTS_WGPU_NATIVE_DIR "" CACHE PATH "Path to a wgpu-native checkout or distribution")
set(CTS_WGPU_NATIVE_LIB "" CACHE FILEPATH "Explicit path to libwgpu_native")
set(CTS_YAWGPU_DIR "" CACHE PATH "Path to a yawgpu checkout or distribution")
set(CTS_YAWGPU_LIB "" CACHE FILEPATH "Explicit path to libyawgpu")

set(CTS_BACKEND_SOURCE "")
set(CTS_BACKEND_LIB "")
set(CTS_BACKEND_DEFINE "")

if(CTS_BACKEND STREQUAL "wgpu-native")
    if(NOT CTS_WGPU_NATIVE_DIR)
        message(FATAL_ERROR "CTS_WGPU_NATIVE_DIR is required for CTS_BACKEND=wgpu-native")
    endif()

    set(CTS_WGPU_HEADER_DIR "${CTS_WGPU_NATIVE_DIR}/ffi")
    if(NOT EXISTS "${CTS_WGPU_HEADER_DIR}/webgpu-headers/webgpu.h")
        set(CTS_WGPU_HEADER_DIR "${CTS_WGPU_NATIVE_DIR}/include")
    endif()

    if(NOT EXISTS "${CTS_WGPU_HEADER_DIR}/webgpu-headers/webgpu.h")
        message(FATAL_ERROR "Could not find webgpu-headers/webgpu.h under ${CTS_WGPU_NATIVE_DIR}")
    endif()

    if(CTS_WGPU_NATIVE_LIB)
        set(CTS_BACKEND_LIB "${CTS_WGPU_NATIVE_LIB}")
    else()
        set(_cts_original_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
        if(APPLE OR UNIX)
            set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
        endif()
        find_library(WGPU_NATIVE_LIB
            NAMES wgpu_native
            PATHS
                "${CTS_WGPU_NATIVE_DIR}/target/release"
                "${CTS_WGPU_NATIVE_DIR}/lib"
            NO_DEFAULT_PATH
        )
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${_cts_original_suffixes}")
        set(CTS_BACKEND_LIB "${WGPU_NATIVE_LIB}")
    endif()

    if(NOT CTS_BACKEND_LIB)
        message(FATAL_ERROR "Could not find libwgpu_native. Build wgpu-native or set CTS_WGPU_NATIVE_LIB.")
    endif()

    set(CTS_BACKEND_SOURCE src/common/webgpu/backend_wgpu.cpp)
    set(CTS_BACKEND_DEFINE CTS_BACKEND_WGPU=1)
elseif(CTS_BACKEND STREQUAL "yawgpu")
    if(NOT CTS_YAWGPU_DIR)
        message(FATAL_ERROR "CTS_YAWGPU_DIR is required for CTS_BACKEND=yawgpu")
    endif()

    set(CTS_WGPU_HEADER_DIR "${CTS_YAWGPU_DIR}/yawgpu/ffi")
    if(NOT EXISTS "${CTS_WGPU_HEADER_DIR}/webgpu-headers/webgpu.h")
        message(FATAL_ERROR "Could not find yawgpu webgpu-headers/webgpu.h under ${CTS_YAWGPU_DIR}")
    endif()

    if(CTS_YAWGPU_LIB)
        set(CTS_BACKEND_LIB "${CTS_YAWGPU_LIB}")
    else()
        set(_cts_original_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
        if(APPLE OR UNIX)
            set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
        endif()
        find_library(YAWGPU_LIB
            NAMES yawgpu
            PATHS
                "${CTS_YAWGPU_DIR}/target/release"
                "${CTS_YAWGPU_DIR}/target-metal/release"
                "${CTS_YAWGPU_DIR}/target-vulkan/release"
                "${CTS_YAWGPU_DIR}/lib"
            NO_DEFAULT_PATH
        )
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${_cts_original_suffixes}")
        set(CTS_BACKEND_LIB "${YAWGPU_LIB}")
    endif()

    if(NOT CTS_BACKEND_LIB)
        message(FATAL_ERROR "Could not find libyawgpu. Build yawgpu or set CTS_YAWGPU_LIB.")
    endif()

    set(CTS_BACKEND_SOURCE src/common/webgpu/backend_yawgpu.cpp)
    set(CTS_BACKEND_DEFINE CTS_BACKEND_YAWGPU=1)
else()
    message(FATAL_ERROR "Unsupported CTS_BACKEND=${CTS_BACKEND}")
endif()

file(GLOB_RECURSE CTS_SPEC_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/webgpu/**/*.spec.cpp"
)

add_library(cts_harness
    src/common/harness.cpp
    src/common/params.cpp
    src/common/query.cpp
    src/common/runner.cpp
    ${CTS_BACKEND_SOURCE}
    src/common/webgpu/sync.cpp
)
target_include_directories(cts_harness
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${CTS_WGPU_HEADER_DIR}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)
target_compile_definitions(cts_harness PUBLIC ${CTS_BACKEND_DEFINE})
target_link_libraries(cts_harness PUBLIC "${CTS_BACKEND_LIB}")

if(APPLE)
    target_link_libraries(cts_harness
        PUBLIC
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework Foundation"
            "-framework IOKit"
            "-framework IOSurface"
            "-framework Metal"
            "-framework QuartzCore"
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(cts_harness PUBLIC dl pthread m)
endif()

function(cts_configure_target target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    )
endfunction()

cts_configure_target(cts_harness)

add_executable(cts src/common/runtime/main.cpp)
target_sources(cts PRIVATE ${CTS_SPEC_SOURCES})
target_link_libraries(cts PRIVATE cts_harness)
target_include_directories(cts PRIVATE "${PROJECT_SOURCE_DIR}/src")
cts_configure_target(cts)

add_executable(cts_unittests src/unittests/main.cpp)
target_link_libraries(cts_unittests PRIVATE cts_harness)
target_include_directories(cts_unittests PRIVATE "${PROJECT_SOURCE_DIR}/src")
cts_configure_target(cts_unittests)

add_executable(gen_listings tools/gen_listings/main.cpp)
target_sources(gen_listings PRIVATE ${CTS_SPEC_SOURCES})
target_link_libraries(gen_listings PRIVATE cts_harness)
target_include_directories(gen_listings PRIVATE "${PROJECT_SOURCE_DIR}/src")
cts_configure_target(gen_listings)

install(TARGETS cts cts_unittests gen_listings)
