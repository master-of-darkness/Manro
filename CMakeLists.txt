cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(GameEngine VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/DNOMINMAX /utf-8)
endif()

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)

set(SDL3MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL3MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_OPUS ON CACHE BOOL "" FORCE)
set(SDL3MIXER_FLAC ON CACHE BOOL "" FORCE)
set(SDL3MIXER_VORBIS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        main
        OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
        SDL3_mixer
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
        GIT_TAG        main
)

add_subdirectory(deps/vk-bootstrap)
add_subdirectory(deps/volk)
add_subdirectory(deps/VulkanMemoryAllocator)

# --- Slang Binary Fetching ---
if(WIN32)
    set(SLANG_OS "windows")
    set(SLANG_ARCH "x86_64")
    set(SLANG_EXT "zip")
elseif(APPLE)
    set(SLANG_OS "macos")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(SLANG_ARCH "aarch64")
    else()
        set(SLANG_ARCH "x86_64")
    endif()
    set(SLANG_EXT "tar.gz")
else()
    set(SLANG_OS "linux")
    set(SLANG_ARCH "x86_64")
    set(SLANG_EXT "tar.gz")
endif()

set(SLANG_BINARY_VERSION "2026.4.2")
set(SLANG_BINARY_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_BINARY_VERSION}/slang-${SLANG_BINARY_VERSION}-${SLANG_OS}-${SLANG_ARCH}.${SLANG_EXT}")

FetchContent_Declare(
        slang_bin
        URL ${SLANG_BINARY_URL}
)
FetchContent_MakeAvailable(slang_bin)

add_executable(slangc IMPORTED GLOBAL)
add_library(slang SHARED IMPORTED GLOBAL)

if(WIN32)
    set_target_properties(slangc PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slangc.exe")
    set_target_properties(slang PROPERTIES 
            IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slang.dll"
            IMPORTED_IMPLIB "${slang_bin_SOURCE_DIR}/lib/slang.lib"
    )
else()
    set_target_properties(slangc PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slangc")
    if(EXISTS "${slang_bin_SOURCE_DIR}/lib/libslang.so")
        set_target_properties(slang PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/lib/libslang.so")
    else()
        set_target_properties(slang PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/libslang.so")
    endif()
endif()
target_include_directories(slang INTERFACE "${slang_bin_SOURCE_DIR}/include")

FetchContent_Declare(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG v25.12.19
)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(flatbuffers)

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.3
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/lsalzman/enet.git
        GIT_TAG v1.3.18
)
FetchContent_MakeAvailable(enet)

FetchContent_Declare(
        tinyobjloader
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG        v2.0.0rc13
)
FetchContent_MakeAvailable(tinyobjloader)

FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.5.0
        SOURCE_SUBDIR  Build
)
set(USE_STD_VECTOR OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

if(MSVC)
    target_compile_options(Jolt PRIVATE /wd4865 /wd5105)
endif()

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_MakeAvailable(SDL3 SDL3_mixer)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(_target
            SDL3-static SDL3_mixer-static
            vk-bootstrap volk
            slang flatbuffers enet Jolt spdlog
    )
        if(TARGET ${_target})
            get_target_property(_type ${_target} TYPE)
            get_target_property(_imported ${_target} IMPORTED)
            if(NOT _type STREQUAL "INTERFACE_LIBRARY" AND NOT _imported)
                if(MSVC)
                    target_compile_options(${_target} PRIVATE
                            /O2 /Ob3 /GL /DNDEBUG /GS-
                    )
                else()
                    target_compile_options(${_target} PRIVATE
                            -O3 -DNDEBUG -fvisibility=hidden
                    )
                endif()
            endif()
        endif()
    endforeach()
endif()

set(SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Networking/Schema")
set(SCHEMA_FILE "${SCHEMA_DIR}/NetworkMessage.fbs")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
        OUTPUT "${GEN_DIR}/NetworkMessage_generated.h"
        COMMAND flatc --cpp -o "${GEN_DIR}" "${SCHEMA_FILE}"
        DEPENDS "${SCHEMA_FILE}" flatc
        COMMENT "Building C++ header for ${SCHEMA_FILE}"
)

add_custom_target(GenerateNetworkSchema
        DEPENDS "${GEN_DIR}/NetworkMessage_generated.h"
)

if (WIN32)
    set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/bin")
else ()
    if(EXISTS "${slang_bin_SOURCE_DIR}/lib")
        set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/lib")
    else()
        set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/bin")
    endif()
endif ()

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/spv")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

if (WIN32)
    set(_SLANG_ENV_VAR "PATH")
    set(_SLANG_ENV_SEP ";")
else ()
    set(_SLANG_ENV_VAR "LD_LIBRARY_PATH")
    set(_SLANG_ENV_SEP ":")
endif ()

macro(compile_shader SHADER_FILE ENTRY_POINT STAGE OUTPUT_FILE)
    add_custom_command(
            OUTPUT "${OUTPUT_FILE}"
            COMMAND ${CMAKE_COMMAND} -E env
            "${_SLANG_ENV_VAR}=$<TARGET_FILE_DIR:slangc>${_SLANG_ENV_SEP}${SLANG_EXTRA_LIB_DIR}"
            $<TARGET_FILE:slangc> "${SHADER_SRC_DIR}/${SHADER_FILE}"
            -entry ${ENTRY_POINT}
            -stage ${STAGE}
            -target spirv
            -I "${SHADER_SRC_DIR}"
            -o "${OUTPUT_FILE}"
            DEPENDS "${SHADER_SRC_DIR}/${SHADER_FILE}" slangc
            COMMENT "Compiling shader: ${SHADER_FILE} [${ENTRY_POINT}]"
    )
    list(APPEND PRECOMPILED_SHADERS "${OUTPUT_FILE}")
endmacro()

compile_shader("pbr.slang" "VSMain" "vertex" "${SHADER_BIN_DIR}/pbr.vert.spv")
compile_shader("pbr.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/pbr.frag.spv")
compile_shader("forward_plus_cull.slang" "main" "compute" "${SHADER_BIN_DIR}/forward_plus_cull.comp.spv")
compile_shader("composite.slang" "VSMain" "vertex" "${SHADER_BIN_DIR}/composite.vert.spv")
compile_shader("composite.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/composite.frag.spv")

compile_shader("nvshaders/tonemapper.slang" "Tonemap" "compute" "${SHADER_BIN_DIR}/tonemapper.comp.spv")
compile_shader("nvshaders/tonemapper.slang" "Histogram" "compute" "${SHADER_BIN_DIR}/tonemapper_histogram.comp.spv")
compile_shader("nvshaders/tonemapper.slang" "AutoExposure" "compute" "${SHADER_BIN_DIR}/tonemapper_autoexposure.comp.spv")

add_custom_target(PrecompileShaders
        DEPENDS ${PRECOMPILED_SHADERS}
)

add_library(Engine STATIC
        src/Core/EngineContext.cpp
        src/Core/Logger.cpp
        src/Core/JobSystem.cpp
        src/Scene/SceneManager.cpp
        src/Input/InputManager.cpp
        src/Networking/NetworkClient.cpp
        src/Networking/NetworkServer.cpp
        src/Render/Renderer.cpp
        src/Render/Vulkan/Swapchain.cpp
        src/Render/Vulkan/VulkanContext.cpp
        src/Render/Vulkan/VulkanHelpers.cpp
        src/Render/TextureManager.cpp
        src/Render/MeshManager.cpp
        src/Render/Model.cpp
        src/Render/Vulkan/Buffer.cpp
        src/Render/Vulkan/Pipeline.cpp
        src/Resource/ModelLoader.cpp
        src/Resource/TextureLoader.cpp
        src/Physics/PhysicsWorld.cpp
        src/Platform/Audio/AudioManager.cpp
        src/Platform/Audio/SDL3AudioBackend.cpp
        src/Platform/Input/SDL3InputBackend.cpp
        src/Platform/Window/SDL3Window.cpp
        src/Platform/Window/WindowManager.cpp
        src/Platform/PlatformContext.cpp
        deps/mikktspace/mikktspace.c
)

add_dependencies(Engine GenerateNetworkSchema PrecompileShaders)

target_include_directories(Engine PUBLIC
        include
        deps/vk-bootstrap/src
        ${GEN_DIR}
        ${SHADER_BIN_DIR}
        ${SHADER_SRC_DIR}
        ${enet_SOURCE_DIR}/include
        ${tinyobjloader_SOURCE_DIR}
        ${stb_SOURCE_DIR}
        deps/mikktspace
)

target_compile_definitions(Engine PUBLIC VK_NO_PROTOTYPES)

target_link_libraries(Engine PUBLIC
        vk-bootstrap::vk-bootstrap
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        flatbuffers
        glm::glm
        enet
        tinyobjloader
        Jolt
        spdlog::spdlog
        SDL3::SDL3-static
        SDL3_mixer::SDL3_mixer-static
)

if(WIN32)
    target_link_libraries(Engine PRIVATE ws2_32 winmm)
else()
    target_link_libraries(Engine PRIVATE m dl pthread)
endif()

target_compile_options(Engine PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /WX /utf-8
        /wd4865 /wd5105 /wd4201 /wd4100 /wd4324 /wd4189 /wd4267
        $<$<CONFIG:Release>:/O2 /Ob3 /GL /DNDEBUG /GS->
        >
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
        -Wall -Wextra -Wno-unused-parameter
        $<$<CONFIG:Release>:
        -O3 -DNDEBUG
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
        >
        >
)

if(MSVC)
    target_link_options(Engine PUBLIC
            $<$<CONFIG:Release>:      /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE>
            $<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL /OPT:REF /OPT:ICF>
            $<$<CONFIG:Debug>:        /DEBUG:FULL>
    )
else()
    target_link_options(Engine PUBLIC
            $<$<CONFIG:Release>:
            -Wl,--gc-sections
            -Wl,--strip-all
            >
    )
endif()

add_subdirectory(tests/Sponza)