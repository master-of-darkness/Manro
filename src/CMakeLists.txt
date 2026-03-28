set(SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Networking/Schema")
set(SCHEMA_FILE "${SCHEMA_DIR}/NetworkMessage.fbs")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
        OUTPUT "${GEN_DIR}/NetworkMessage_generated.h"
        COMMAND flatc --cpp -o "${GEN_DIR}" "${SCHEMA_FILE}"
        DEPENDS "${SCHEMA_FILE}" flatc
        COMMENT "Building C++ header for ${SCHEMA_FILE}"
)
add_custom_target(GenerateNetworkSchema DEPENDS "${GEN_DIR}/NetworkMessage_generated.h")

add_library(Engine STATIC
        # Core systems
        Core/EngineLoop.cpp
        Core/Logger.cpp
        Core/JobSystem.cpp
        Core/VirtualFS.cpp
        Core/EmbeddedShaders.cpp
        Input/InputManager.cpp
        Networking/NetworkClient.cpp
        Networking/NetworkServer.cpp
        # Main rendering code
        Render/DebugDraw.cpp
        Render/Material.cpp
        Render/Vulkan/VulkanContext.cpp
        Render/Vulkan/VulkanHelpers.cpp
        Render/Vulkan/Swapchain.cpp
        Render/Vulkan/Buffer.cpp
        Render/Vulkan/Pipeline.cpp
        Render/Vulkan/DescriptorAllocator.cpp
        Render/Vulkan/PipelineCache.cpp
        Render/RenderGraph.cpp
        Render/Renderer.cpp
        Render/RHI/StubRenderDevice.cpp
        Render/RHI/VulkanCommandList.cpp
        Render/SceneRenderer.cpp
        Render/UIRenderer.cpp
        Render/TextureManager.cpp
        Render/MeshManager.cpp
        Render/Model.cpp
        Render/Gui/ImGuiLayer.cpp
        Resource/ModelLoader.cpp
        Resource/TextureLoader.cpp
        Physics/PhysicsWorld.cpp
        # Windows, audio and input
        Platform/Audio/AudioManager.cpp
        Platform/Audio/SDL3AudioBackend.cpp
        Platform/Input/SDL3InputBackend.cpp
        Platform/Window/SDL3Window.cpp
        Platform/Window/WindowManager.cpp
        Platform/PlatformContext.cpp
        ../deps/mikktspace/mikktspace.c
        ../deps/imgui/imgui.cpp
        ../deps/imgui/imgui_draw.cpp
        ../deps/imgui/imgui_widgets.cpp
        ../deps/imgui/imgui_tables.cpp
        ../deps/imgui/imgui_demo.cpp
        ../deps/imgui/backends/imgui_impl_sdl3.cpp
        ../deps/imgui/backends/imgui_impl_vulkan.cpp
)

set_source_files_properties(../deps/mikktspace/mikktspace.c PROPERTIES COMPILE_FLAGS "-Wno-unused-but-set-variable")

add_dependencies(Engine GenerateNetworkSchema PrecompileShaders EmbedShaders)

target_include_directories(Engine SYSTEM PUBLIC
        ../deps/vk-bootstrap/src
        ../deps/imgui
        ../deps/imgui/backends
        ../deps/enet/include
        ../deps/tinyobjloader
        ../deps/stb
        ../deps/mikktspace
        ../deps/VulkanMemoryAllocator/include
)

target_include_directories(Engine PUBLIC
        ../include
        ${GEN_DIR}
        ${SHADER_BIN_DIR}
        ${SHADER_SRC_DIR}
)

target_include_directories(Engine PRIVATE Render ../deps/stb)
target_compile_definitions(Engine PUBLIC VK_NO_PROTOTYPES)

target_link_libraries(Engine PUBLIC
        vk-bootstrap::vk-bootstrap volk::volk GPUOpen::VulkanMemoryAllocator
        flatbuffers glm::glm enet tinyobjloader Jolt spdlog::spdlog
        SDL3::SDL3-static SDL3_mixer::SDL3_mixer-static tinygltf
)

if(WIN32)
    target_link_libraries(Engine PRIVATE ws2_32 winmm)
else()
    target_link_libraries(Engine PRIVATE m dl pthread)
endif()

target_compile_options(Engine PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter -Wunused
        $<$<CONFIG:Release>:-O3 -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections>
)

target_link_options(Engine PUBLIC
        $<$<CONFIG:Release>:-Wl$<$<NOT:$<PLATFORM_ID:Apple>>:-Wl>>
)