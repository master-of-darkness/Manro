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

set(MANRO_MIKKTSPACE_C "${MANRO_THIRD_PARTY_DIR}/mikktspace/mikktspace.c")

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
        Render/Vulkan/Buffer.cpp
        Render/Vulkan/Pipeline.cpp
        Render/Vulkan/DescriptorAllocator.cpp
        Render/Vulkan/PipelineCache.cpp
        Render/RenderGraph.cpp
        Render/Renderer.cpp
        Render/RHI/RenderDeviceFactory.cpp
        Render/RHI/VulkanRenderDevice.cpp
        Render/RHI/VulkanCommandList.cpp
        Render/SceneRenderer.cpp
        Render/TextureManager.cpp
        Render/MeshManager.cpp
        Render/Model.cpp
        Render/Gui/ImGuiLayer.cpp
        Resource/ModelLoader.cpp
        Resource/TextureLoader.cpp
        Resource/Primitives.cpp
        Physics/PhysicsWorld.cpp
        # Windows, audio and input
        Platform/Audio/AudioManager.cpp
        Platform/Audio/SDL3AudioBackend.cpp
        Platform/Input/SDL3InputBackend.cpp
        Platform/Window/SDL3Window.cpp
        Platform/Window/WindowManager.cpp
        Platform/PlatformContext.cpp
        ${MANRO_MIKKTSPACE_C}
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl3.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)

set_source_files_properties(${MANRO_MIKKTSPACE_C} PROPERTIES COMPILE_FLAGS "-Wno-unused-but-set-variable")

add_dependencies(Engine GenerateNetworkSchema PrecompileShaders EmbedShaders)

target_include_directories(Engine SYSTEM PUBLIC
        ${MANRO_THIRD_PARTY_DIR}/vk-bootstrap/src
        ${MANRO_THIRD_PARTY_DIR}/imgui
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends
        ${MANRO_THIRD_PARTY_DIR}/enet/include
        ${MANRO_THIRD_PARTY_DIR}/tinyobjloader
        ${MANRO_THIRD_PARTY_DIR}/stb
        ${MANRO_THIRD_PARTY_DIR}/mikktspace
        ${MANRO_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
)

target_include_directories(Engine PUBLIC
        ${MANRO_PUBLIC_INCLUDE_DIR}
        ${GEN_DIR}
        ${SHADER_BIN_DIR}
        ${SHADER_SRC_DIR}
)

target_include_directories(Engine PRIVATE Render ${MANRO_THIRD_PARTY_DIR}/stb)
target_compile_definitions(Engine PUBLIC VK_NO_PROTOTYPES)

target_link_libraries(Engine PUBLIC
        vk-bootstrap::vk-bootstrap volk::volk GPUOpen::VulkanMemoryAllocator
        flatbuffers glm::glm enet tinyobjloader Jolt spdlog::spdlog
        SDL3::SDL3-static SDL3_mixer::SDL3_mixer-static tinygltf
)

if (WIN32)
    target_link_libraries(Engine PRIVATE ws2_32 winmm)
else ()
    target_link_libraries(Engine PRIVATE m dl pthread)
endif ()

target_compile_options(Engine PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter -Wunused
        $<$<CONFIG:Release>:-O3 -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections>
)

target_link_options(Engine PUBLIC
        $<$<CONFIG:Release>:-Wl$<$<NOT:$<PLATFORM_ID:Apple>>:-Wl>>
)