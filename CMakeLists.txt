cmake_minimum_required(VERSION 3.20)
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
    add_compile_options(/DNOMINMAX)
endif()

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)

set(SDL3MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL3MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_VORBIS OFF CACHE BOOL "" FORCE)

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

FetchContent_MakeAvailable(SDL3 SDL3_mixer)

add_subdirectory(Engine/deps/vk-bootstrap)
add_subdirectory(Engine/deps/volk)
add_subdirectory(Engine/deps/VulkanMemoryAllocator)

set(SLANG_ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "" FORCE)
set(SLANG_LIB_TYPE "STATIC" CACHE STRING "" FORCE)
add_subdirectory(Engine/deps/slang)

FetchContent_Declare(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG        v23.5.26
)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(flatbuffers)

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/lsalzman/enet.git
        GIT_TAG        v1.3.17
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
        ufbx
        GIT_REPOSITORY https://github.com/ufbx/ufbx.git
        GIT_TAG        v0.21.3
)
FetchContent_MakeAvailable(ufbx)

FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG        v5.2.0
        SOURCE_SUBDIR  Build
)
set(DOUBLE_PRECISION                    OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS              ON  CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE       OFF CACHE BOOL "" FORCE)
set(ENABLE_ASSERTS                      OFF CACHE BOOL "" FORCE)
set(USE_STD_VECTOR                      ON  CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS                   OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD                  OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST             OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES                      OFF CACHE BOOL "" FORCE)
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

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(_target
            SDL3-static SDL3_mixer-static
            vk-bootstrap volk
            slang flatbuffers enet Jolt spdlog
    )
        if(TARGET ${_target})
            get_target_property(_type ${_target} TYPE)
            if(NOT _type STREQUAL "INTERFACE_LIBRARY")
                if(MSVC)
                    target_compile_options(${_target} PRIVATE
                            /O2 /Ob3 /GL /DNDEBUG /GS-
                    )
                else()
                    target_compile_options(${_target} PRIVATE
                            -O3 -DNDEBUG
                            -fvisibility=hidden
                            -fvisibility-inlines-hidden
                            -ffunction-sections
                            -fdata-sections
                            -fno-unwind-tables
                            -fno-asynchronous-unwind-tables
                    )
                endif()
            endif()
        endif()
    endforeach()
endif()

set(SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/src/Networking/Schema")
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

# Shader precompilation
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
set(SHADER_BIN_DIR "${SHADER_SRC_DIR}")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(SLANG_BIN_PKG_DIR "${CMAKE_CURRENT_BINARY_DIR}/Engine/deps/slang/slang-2026.3.1-linux-x86_64")
set(SLANG_LIB_DIR "${SLANG_BIN_PKG_DIR}/lib")

set(SLANGC_EXE "$<TARGET_FILE:slangc>")

macro(compile_shader SHADER_FILE ENTRY_POINT STAGE OUTPUT_FILE)
    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${SLANG_LIB_DIR}"
                ${SLANGC_EXE} "${SHADER_SRC_DIR}/${SHADER_FILE}"
                -entry ${ENTRY_POINT}
                -stage ${STAGE}
                -target spirv
                -o "${OUTPUT_FILE}"
        DEPENDS "${SHADER_SRC_DIR}/${SHADER_FILE}" slangc
        COMMENT "Compiling shader: ${SHADER_FILE} [${ENTRY_POINT}]"
    )
    list(APPEND PRECOMPILED_SHADERS "${OUTPUT_FILE}")
endmacro()

compile_shader("cube.slang" "vertexMain" "vertex" "${SHADER_BIN_DIR}/cube.vert.spv")
compile_shader("cube.slang" "fragmentMain" "fragment" "${SHADER_BIN_DIR}/cube.frag.spv")

add_custom_target(PrecompileShaders
    DEPENDS ${PRECOMPILED_SHADERS}
)

add_library(Engine STATIC
        Engine/src/Core/EngineContext.cpp
        Engine/src/Core/Logger.cpp
        Engine/src/Core/JobSystem.cpp
        Engine/src/Scene/SceneManager.cpp
        Engine/src/Input/InputManager.cpp
        Engine/src/Networking/NetworkClient.cpp
        Engine/src/Networking/NetworkServer.cpp
        Engine/src/Render/Renderer.cpp
        Engine/src/Render/Vulkan/Swapchain.cpp
        Engine/src/Render/Vulkan/VulkanContext.cpp
        Engine/src/Render/Vulkan/VulkanHelpers.cpp
        Engine/src/Render/TextureManager.cpp
        Engine/src/Render/MeshManager.cpp
        Engine/src/Render/Vulkan/Buffer.cpp
        Engine/src/Render/Vulkan/Pipeline.cpp
        Engine/src/Resource/ModelLoader.cpp
        Engine/src/Resource/FBXLoader.cpp
        Engine/src/Resource/TextureLoader.cpp
        Engine/src/Physics/PhysicsWorld.cpp
        Engine/src/Platform/Audio/AudioManager.cpp
        Engine/src/Platform/Audio/SDL3AudioBackend.cpp
        Engine/src/Platform/Input/SDL3InputBackend.cpp
        Engine/src/Platform/Window/SDL3Window.cpp
        Engine/src/Platform/Window/WindowManager.cpp
        Engine/src/Platform/PlatformContext.cpp
        ${ufbx_SOURCE_DIR}/ufbx.c
)

add_dependencies(Engine GenerateNetworkSchema PrecompileShaders)

target_include_directories(Engine PUBLIC
        Engine/src
        Engine/deps/vk-bootstrap/src
        ${GEN_DIR}
        ${SHADER_BIN_DIR}
        ${enet_SOURCE_DIR}/include
        ${tinyobjloader_SOURCE_DIR}
        ${stb_SOURCE_DIR}
        ${ufbx_SOURCE_DIR}
)

target_include_directories(Engine PRIVATE
        ${joltphysics_SOURCE_DIR}
)

target_compile_definitions(Engine PUBLIC VK_NO_PROTOTYPES)

target_link_libraries(Engine PUBLIC
        vk-bootstrap::vk-bootstrap
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        slang
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
        >
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wno-unused-parameter>

        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/Od /Zi /RTC1>
        $<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O0 -g3>

        $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:MSVC>>:/O2 /Zi /Ob2 /DNDEBUG>
        $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2 -g -DNDEBUG>

        $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2 /Ob3 /GL /DNDEBUG /GS->
        $<$<AND:$<CONFIG:Release>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:
        -O3 -DNDEBUG
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
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
            $<$<CONFIG:RelWithDebInfo>:
            -Wl,--build-id
            >
    )
endif()

add_subdirectory(tests/Multiplayer)
add_subdirectory(tests/Sponza)