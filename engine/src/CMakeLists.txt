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
        Render/Renderer.cpp
        Render/RendererImpl.cpp

        Render/Passes/DebugDrawSystem.cpp
        Render/Passes/ShadowSystem.cpp
        Render/Passes/SkyboxRenderer.cpp

        Render/Pipelines/MaterialSystem.cpp
        Render/Pipelines/PipelineManager.cpp

        Render/Scene/GpuCullDispatcher.cpp
        Render/Scene/InstanceBatcher.cpp
        Render/Scene/SceneRenderer.cpp

        Render/Targets/RenderTargetManager.cpp
        Render/Targets/SwapchainManager.cpp

        Render/Resources/MeshManager.cpp
        Render/Resources/Model.cpp
        Render/Resources/Material/Material.cpp
        Render/Resources/Texture/TextureManager.cpp

        Render/Vulkan/VulkanContext.cpp
        Render/Vulkan/VulkanHelpers.cpp
        Render/Vulkan/Buffer.cpp
        Render/Vulkan/Pipeline.cpp
        Render/Vulkan/DescriptorAllocator.cpp
        Render/Vulkan/PipelineCache.cpp

        Render/Overlay/Overlay.cpp
)

set(ENGINE_RESOURCE_SOURCES
        Resource/ModelLoader.cpp
        Resource/TextureLoader.cpp
        Resource/Primitives.cpp
        Resource/RresMount.cpp
        Resource/Map.cpp
        Resource/Pack.cpp
)

set(ENGINE_PHYSICS_SOURCES
        Physics/PhysicsWorld.cpp
)

set(ENGINE_PLATFORM_SOURCES
        Platform/Audio/AudioManager.cpp
        Platform/Audio/AudioBackend.cpp
        Platform/Input/InputBackend.cpp
        Platform/Window/Window.cpp
        Platform/Window/WindowManager.cpp
        Platform/PlatformContext.cpp
)

set(ENGINE_THIRD_PARTY_SOURCES
        ${MANRO_MIKKTSPACE_C}
        ${MANRO_THIRD_PARTY_DIR}/rres/rres_impl.c
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl3.cpp
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_vulkan.cpp
        ${MANRO_THIRD_PARTY_DIR}/imguizmo/ImGuizmo.cpp
)

set_source_files_properties(${MANRO_THIRD_PARTY_DIR}/imguizmo/ImGuizmo.cpp
        PROPERTIES COMPILE_FLAGS "-Wno-everything")

add_library(Manro STATIC
        ${ENGINE_CORE_SOURCES}
        ${ENGINE_INPUT_SOURCES}
        ${ENGINE_RENDER_SOURCES}
        ${ENGINE_RESOURCE_SOURCES}
        ${ENGINE_PHYSICS_SOURCES}
        ${ENGINE_PLATFORM_SOURCES}
        ${ENGINE_THIRD_PARTY_SOURCES}
)

set_source_files_properties(${MANRO_MIKKTSPACE_C} PROPERTIES COMPILE_FLAGS "-Wno-unused-but-set-variable")
set_source_files_properties(${MANRO_THIRD_PARTY_DIR}/rres/rres_impl.c PROPERTIES COMPILE_FLAGS "-Wno-everything")

add_dependencies(Manro PrecompileShaders EmbedShaders)

target_include_directories(Manro SYSTEM PUBLIC
        ${MANRO_THIRD_PARTY_DIR}/vk-bootstrap/src
        ${MANRO_THIRD_PARTY_DIR}/imgui
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends
        ${MANRO_THIRD_PARTY_DIR}/imguizmo
        ${MANRO_THIRD_PARTY_DIR}/json
        ${MANRO_THIRD_PARTY_DIR}/tinyobjloader
        ${MANRO_THIRD_PARTY_DIR}/stb
        ${MANRO_THIRD_PARTY_DIR}/mikktspace
        ${MANRO_THIRD_PARTY_DIR}/rres
        ${MANRO_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
)

target_include_directories(Manro PUBLIC
        ${MANRO_PUBLIC_INCLUDE_DIR}
        ${GEN_DIR}
        ${SHADER_BIN_DIR}
        ${SHADER_SRC_DIR}
)

target_include_directories(Manro PRIVATE Render ${MANRO_THIRD_PARTY_DIR}/stb)
target_compile_definitions(Manro PUBLIC VK_NO_PROTOTYPES)

target_link_libraries(Manro PUBLIC
        vk-bootstrap::vk-bootstrap volk::volk GPUOpen::VulkanMemoryAllocator
        glm::glm tinyobjloader Jolt spdlog::spdlog
        SDL3::SDL3-static SDL3_mixer::SDL3_mixer-static tinygltf
)

if (MANRO_ENABLE_PROFILING)
    target_link_libraries(Manro PUBLIC TracyClient)
    target_compile_definitions(Manro PUBLIC MANRO_PROFILING TRACY_VK_USE_SYMBOL_TABLE)
endif ()

if (WIN32)
    target_link_libraries(Manro PRIVATE ws2_32 winmm)
else ()
    target_link_libraries(Manro PRIVATE m dl pthread)
endif ()

target_precompile_headers(Manro PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp>"
)

target_compile_options(Manro PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter -Wunused
        $<$<CONFIG:Release>:-O3 -DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -flto>
)

target_link_options(Manro PUBLIC
        $<$<CONFIG:Release>:-flto=thin>
        $<$<AND:$<CONFIG:Release>,$<NOT:$<PLATFORM_ID:Apple>>>:-Wl,--gc-sections>
)
