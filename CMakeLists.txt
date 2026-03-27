cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
endif ()

if(WIN32)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

project(GameEngine VERSION 0.1 LANGUAGES C CXX)

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MSVC is forbidden. Project requires Clang. Detected: ${CMAKE_CXX_COMPILER_ID}")
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

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL3MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL3MIXER_OPUS ON CACHE BOOL "" FORCE)
set(SDL3MIXER_FLAC ON CACHE BOOL "" FORCE)
set(SDL3MIXER_VORBIS ON CACHE BOOL "" FORCE)

add_subdirectory(deps/SDL3)
add_subdirectory(deps/SDL3_mixer)
add_subdirectory(deps/vk-bootstrap)
add_subdirectory(deps/volk)
add_subdirectory(deps/VulkanMemoryAllocator)
add_subdirectory(deps/flatbuffers)
add_subdirectory(deps/glm)
add_subdirectory(deps/enet)
add_subdirectory(deps/tinyobjloader)
add_subdirectory(deps/tinygltf)
add_subdirectory(deps/JoltPhysics/Build)
add_subdirectory(deps/spdlog)

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

include(FetchContent)

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
            IMPORTED_IMPLIB "${slang_bin_SOURCE_DIR}/lib/slang.lib")
    set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/bin")
else()
    set_target_properties(slangc PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slangc")
    set_target_properties(slang PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/lib/libslang.so")
    set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/lib")
endif ()

target_include_directories(slang INTERFACE "${slang_bin_SOURCE_DIR}/include")

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

compile_shader("pbr.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/pbr.vert.spv")
compile_shader("pbr.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/pbr.frag.spv")
compile_shader("forward_plus_cull.slang" "main" "compute" "${SHADER_BIN_DIR}/forward_plus_cull.comp.spv")
compile_shader("composite.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/composite.vert.spv")
compile_shader("composite.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/composite.frag.spv")
compile_shader("mesh_cull.slang" "main" "compute" "${SHADER_BIN_DIR}/mesh_cull.comp.spv")
compile_shader("shadow_depth.slang" "VSMain" "vertex" "${SHADER_BIN_DIR}/shadow_depth.vert.spv")
compile_shader("imgui.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/imgui.vert.spv")
compile_shader("imgui.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/imgui.frag.spv")
compile_shader("skybox.slang" "VSMain" "vertex" "${SHADER_BIN_DIR}/skybox.vert.spv")
compile_shader("skybox.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/skybox.frag.spv")
compile_shader("nvshaders/tonemapper.slang" "Tonemap"      "compute" "${SHADER_BIN_DIR}/tonemapper.comp.spv")
compile_shader("nvshaders/tonemapper.slang" "Histogram"    "compute" "${SHADER_BIN_DIR}/tonemapper_histogram.comp.spv")
compile_shader("nvshaders/tonemapper.slang" "AutoExposure" "compute" "${SHADER_BIN_DIR}/tonemapper_autoexposure.comp.spv")

add_custom_target(PrecompileShaders DEPENDS ${PRECOMPILED_SHADERS})

add_library(Engine STATIC
        src/Core/EngineLoop.cpp
        src/Core/Logger.cpp
        src/Core/JobSystem.cpp
        src/Core/VirtualFS.cpp
        src/Core/EmbeddedShaders.cpp
        src/Input/InputManager.cpp
        src/Networking/NetworkClient.cpp
        src/Networking/NetworkServer.cpp
        src/Render/Material.cpp
        src/Render/Vulkan/VulkanContext.cpp
        src/Render/Vulkan/VulkanHelpers.cpp
        src/Render/Vulkan/Swapchain.cpp
        src/Render/Vulkan/Buffer.cpp
        src/Render/Vulkan/Pipeline.cpp
        src/Render/Vulkan/DescriptorAllocator.cpp
        src/Render/Vulkan/PipelineCache.cpp
        src/Render/RenderGraph.cpp
        src/Render/Renderer.cpp
        src/Render/RHI/StubRenderDevice.cpp
        src/Render/RHI/VulkanCommandList.cpp
        src/Render/SceneRenderer.cpp
        src/Render/UIRenderer.cpp
        src/Render/TextureManager.cpp
        src/Render/MeshManager.cpp
        src/Render/Model.cpp
        src/Render/Gui/ImGuiLayer.cpp
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
        deps/imgui/imgui.cpp
        deps/imgui/imgui_draw.cpp
        deps/imgui/imgui_widgets.cpp
        deps/imgui/imgui_tables.cpp
        deps/imgui/imgui_demo.cpp
        deps/imgui/backends/imgui_impl_sdl3.cpp
        deps/imgui/backends/imgui_impl_vulkan.cpp
)

set_source_files_properties(deps/mikktspace/mikktspace.c
        PROPERTIES COMPILE_FLAGS "-Wno-unused-but-set-variable"
)

set(_EMBED_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/embed_spv.cmake")
file(WRITE "${_EMBED_SCRIPT}" [=[
cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED INPUT OR NOT DEFINED VAR_NAME OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "Usage: cmake -D INPUT=<spv> -D VAR_NAME=<name> -D OUTPUT=<h> -P embed_spv.cmake")
endif()

file(READ "${INPUT}" raw HEX)
string(LENGTH "${raw}" hex_len)
math(EXPR byte_count "${hex_len} / 2")

set(out "/* Auto-generated by embed_spv.cmake — do not edit */\n")
string(APPEND out "unsigned char ${VAR_NAME}[] = {\n    ")

set(col 0)
set(pos 0)
while(pos LESS hex_len)
    string(SUBSTRING "${raw}" ${pos} 2 byte)
    if(col GREATER 0)
        string(APPEND out ", ")
    endif()
    string(APPEND out "0x${byte}")
    math(EXPR col "${col} + 1")
    if(col EQUAL 16)
        string(APPEND out ",\n    ")
        set(col 0)
    endif()
    math(EXPR pos "${pos} + 2")
endwhile()

string(APPEND out "\n};\n")
string(APPEND out "unsigned int ${VAR_NAME}_len = ${byte_count};\n")

file(WRITE "${OUTPUT}" "${out}")
]=])

macro(embed_shader SHADER_SPV_FILE VAR_NAME)
    set(_header "${SHADER_BIN_DIR}/${VAR_NAME}.h")
    add_custom_command(
            OUTPUT "${_header}"
            COMMAND ${CMAKE_COMMAND}
            -D INPUT=${SHADER_SPV_FILE}
            -D VAR_NAME=${VAR_NAME}
            -D OUTPUT=${_header}
            -P "${_EMBED_SCRIPT}"
            DEPENDS "${SHADER_SPV_FILE}" PrecompileShaders
            COMMENT "Embedding shader: ${VAR_NAME}"
    )
    list(APPEND EMBEDDED_SHADER_HEADERS "${_header}")
endmacro()

embed_shader("${SHADER_BIN_DIR}/pbr.vert.spv" pbr_vert_spv)
embed_shader("${SHADER_BIN_DIR}/pbr.frag.spv" pbr_frag_spv)
embed_shader("${SHADER_BIN_DIR}/forward_plus_cull.comp.spv" forward_plus_cull_comp_spv)
embed_shader("${SHADER_BIN_DIR}/composite.vert.spv" composite_vert_spv)
embed_shader("${SHADER_BIN_DIR}/composite.frag.spv" composite_frag_spv)
embed_shader("${SHADER_BIN_DIR}/tonemapper.comp.spv" tonemapper_comp_spv)
embed_shader("${SHADER_BIN_DIR}/tonemapper_histogram.comp.spv" tonemapper_histogram_comp_spv)
embed_shader("${SHADER_BIN_DIR}/tonemapper_autoexposure.comp.spv" tonemapper_autoexposure_comp_spv)
embed_shader("${SHADER_BIN_DIR}/mesh_cull.comp.spv" mesh_cull_comp_spv)
embed_shader("${SHADER_BIN_DIR}/shadow_depth.vert.spv" shadow_depth_vert_spv)
embed_shader("${SHADER_BIN_DIR}/imgui.vert.spv" imgui_vert_spv)
embed_shader("${SHADER_BIN_DIR}/imgui.frag.spv" imgui_frag_spv)
embed_shader("${SHADER_BIN_DIR}/skybox.vert.spv" skybox_vert_spv)
embed_shader("${SHADER_BIN_DIR}/skybox.frag.spv" skybox_frag_spv)

add_custom_target(EmbedShaders DEPENDS ${EMBEDDED_SHADER_HEADERS})

add_dependencies(Engine GenerateNetworkSchema PrecompileShaders EmbedShaders)

target_include_directories(Engine SYSTEM PUBLIC
        deps/vk-bootstrap/src
        deps/imgui
        deps/imgui/backends
        deps/enet/include
        deps/tinyobjloader
        deps/stb
        deps/mikktspace
        deps/VulkanMemoryAllocator/include
)

target_include_directories(Engine PUBLIC
        include
        ${GEN_DIR}
        ${SHADER_BIN_DIR}
        ${SHADER_SRC_DIR}
)

target_include_directories(Engine PRIVATE src/Render deps/stb)
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
        tinygltf
)

if(WIN32)
    target_link_libraries(Engine PRIVATE ws2_32 winmm)
else()
    target_link_libraries(Engine PRIVATE m dl pthread)
endif()

target_compile_options(Engine PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter -Wunused
        $<$<CONFIG:Release>:
        -O3 -DNDEBUG
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
        >
)

target_link_options(Engine PUBLIC
        $<$<CONFIG:Release>:
        -Wl,--gc-sections
        $<$<NOT:$<PLATFORM_ID:Apple>>:-Wl,--strip-all>
        >
)

add_subdirectory(tests/Sponza)