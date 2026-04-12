set(MANRO_MIKKTSPACE_C "${MANRO_THIRD_PARTY_DIR}/mikktspace/mikktspace.c")

set(ENGINE_CORE_SOURCES
        Core/EngineLoop.cpp
        Core/Logger.cpp
        Core/JobSystem.cpp
        Core/VirtualFS.cpp
        Core/EmbeddedShaders.cpp
)

set(ENGINE_INPUT_SOURCES
        Input/InputManager.cpp
)

set(ENGINE_RENDER_SOURCES
        Render/DrawSystem.cpp
        Render/Material/Material.cpp
        Render/Vulkan/VulkanContext.cpp
        Render/Vulkan/VulkanHelpers.cpp
        Render/Vulkan/Buffer.cpp
        Render/Vulkan/Pipeline.cpp
        Render/Vulkan/DescriptorAllocator.cpp
        Render/Vulkan/PipelineCache.cpp
        Render/Renderer.cpp
        Render/RendererImpl.cpp
        Render/SceneRenderer.cpp
        Render/Texture/TextureManager.cpp
        Render/MeshManager.cpp
        Render/Model.cpp
        Render/Overlay/Overlay.cpp
        Render/Internal/SwapchainManager.cpp
        Render/Internal/RenderTargetManager.cpp
        Render/Internal/ShadowSystem.cpp
        Render/Internal/SkyboxRenderer.cpp
        Render/Internal/MaterialSystem.cpp
        Render/Internal/InstanceBatcher.cpp
        Render/Internal/GpuCullDispatcher.cpp
        Render/Internal/PipelineManager.cpp
)

set(ENGINE_RESOURCE_SOURCES
        Resource/ModelLoader.cpp
        Resource/TextureLoader.cpp
        Resource/Primitives.cpp
)

set(ENGINE_PHYSICS_SOURCES
        Physics/PhysicsWorld.cpp
)

set(ENGINE_PLATFORM_SOURCES
        Platform/Audio/AudioManager.cpp
        Platform/Audio/SDL3AudioBackend.cpp
        Platform/Input/SDL3InputBackend.cpp
        Platform/Window/Window.cpp
        Platform/Window/WindowManager.cpp
        Platform/PlatformContext.cpp
)

set(ENGINE_THIRD_PARTY_SOURCES
        ${MANRO_MIKKTSPACE_C}
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl3.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)

add_library(Engine STATIC
        ${ENGINE_CORE_SOURCES}
        ${ENGINE_INPUT_SOURCES}
        ${ENGINE_RENDER_SOURCES}
        ${ENGINE_RESOURCE_SOURCES}
        ${ENGINE_PHYSICS_SOURCES}
        ${ENGINE_PLATFORM_SOURCES}
        ${ENGINE_THIRD_PARTY_SOURCES}
)

set_source_files_properties(${MANRO_MIKKTSPACE_C} PROPERTIES COMPILE_FLAGS "-Wno-unused-but-set-variable")

add_dependencies(Engine PrecompileShaders EmbedShaders)

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
        glm::glm enet tinyobjloader Jolt spdlog::spdlog
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
