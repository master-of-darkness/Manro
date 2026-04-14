cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
endif ()

if(WIN32)
    add_definitions(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(GameEngine VERSION 0.1 LANGUAGES C CXX)

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Project requires Clang. Detected: ${CMAKE_CXX_COMPILER_ID}")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(FETCHCONTENT_QUIET OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

option(MANRO_ENABLE_PROFILING "Enable Tracy profiler instrumentation" OFF)
if(MANRO_ENABLE_PROFILING)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/tracy)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL3MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_OPUS ON CACHE BOOL "" FORCE)
set(SDL3MIXER_FLAC ON CACHE BOOL "" FORCE)
set(SDL3MIXER_VORBIS ON CACHE BOOL "" FORCE)

# Make sure Jolt doesnt compile HLSL shader since  their build is broken on linux (idk why)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)

function(manro_pick_path OUT_VAR)
    foreach(_candidate IN LISTS ARGN)
        if(EXISTS "${_candidate}")
            set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "Unable to resolve path for ${OUT_VAR}. Tried: ${ARGN}")
endfunction()

manro_pick_path(MANRO_THIRD_PARTY_DIR
        "${CMAKE_SOURCE_DIR}/third_party"
        "${CMAKE_SOURCE_DIR}/deps")
manro_pick_path(MANRO_ENGINE_ROOT
        "${CMAKE_SOURCE_DIR}/engine"
        "${CMAKE_SOURCE_DIR}")
manro_pick_path(MANRO_SRC_DIR
        "${CMAKE_SOURCE_DIR}/engine/src"
        "${CMAKE_SOURCE_DIR}/src")
manro_pick_path(MANRO_SAMPLES_ROOT
        "${CMAKE_SOURCE_DIR}/samples"
        "${CMAKE_SOURCE_DIR}/tests")

set(MANRO_PUBLIC_INCLUDE_DIR "${MANRO_ENGINE_ROOT}/include")

add_subdirectory("${MANRO_THIRD_PARTY_DIR}/SDL3")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/SDL3_mixer")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/vk-bootstrap")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/volk")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/VulkanMemoryAllocator")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/glm")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/tinyobjloader")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/tinygltf")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/JoltPhysics/Build")
add_subdirectory("${MANRO_THIRD_PARTY_DIR}/spdlog")

if(WIN32)
    set(SLANG_OS "windows")
    set(SLANG_ARCH "x86_64")
    set(SLANG_EXT "zip")
elseif(APPLE)
    set(SLANG_OS "macos")
    set(SLANG_ARCH "x86_64")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(SLANG_ARCH "aarch64")
    endif()
    set(SLANG_EXT "tar.gz")
else()
    set(SLANG_OS "linux")
    set(SLANG_ARCH "x86_64")
    set(SLANG_EXT "tar.gz")
endif()

include(FetchContent)
set(SLANG_BINARY_VERSION "2026.4.2")
set(SLANG_BINARY_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_BINARY_VERSION}/slang-${SLANG_BINARY_VERSION}-${SLANG_OS}-${SLANG_ARCH}.${SLANG_EXT}")

FetchContent_Declare(slang_bin URL ${SLANG_BINARY_URL})
FetchContent_MakeAvailable(slang_bin)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(shaders)

add_subdirectory(engine/src)

option(MANRO_BUILD_SAMPLES "Build sample applications" ON)
if(MANRO_BUILD_SAMPLES)
    add_subdirectory("${MANRO_SAMPLES_ROOT}/Sponza")
endif()
