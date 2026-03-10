#pragma once

#include "VulkanContext.h"
#include "VulkanHelpers.h"
#include "Swapchain.h"
#include "Pipeline.h"
#include "SlangCompiler.h"
#include "TextureManager.h"
#include "MeshManager.h"
#include <Core/Types.h>
#include <vector>

namespace Engine {
    class IWindow;

    class Renderer {
    public:
        Renderer(IWindow &window, u32 width, u32 height,
                 VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);

        ~Renderer();

        Renderer(const Renderer &) = delete;

        Renderer &operator=(const Renderer &) = delete;

        void OnResize(u32 width, u32 height);

        bool BeginFrame();
        void EndFrameAndPresent();

        void BeginRenderPass(Vec4 clearColor = {0.02f, 0.02f, 0.05f, 1.0f});
        void EndRenderPass();

        MeshHandle UploadMesh(const ModelData &data) { return m_Meshes.Upload(data); }
        TextureHandle UploadTexture(const TextureData &data) { return m_Textures.Upload(data); }
        void BindTexture(TextureHandle id) { m_Textures.Bind(id); }
        void SetTintColor(const Vec3 &color) { m_TintColor = color; }

        void DrawMesh(MeshHandle meshId, const Mat4 &mvp);

        float GetAspectRatio() const {
            if (!m_Swapchain) return 16.f / 9.f;
            auto ext = m_Swapchain->GetExtent();
            return (ext.height > 0)
                       ? static_cast<float>(ext.width) / static_cast<float>(ext.height)
                       : 16.f / 9.f;
        }

        VulkanContext &GetContext() { return m_Context; }
        TextureManager &GetTextureManager() { return m_Textures; }
        MeshManager &GetMeshManager() { return m_Meshes; }

    private:
        void CreateDepthResources(u32 width, u32 height);
        void CreateColorResources(u32 width, u32 height);
        void RecreateSwapchain();
        void CreateCommandBuffers();
        void CreateSyncObjects();
        void LoadShadersAndPipeline();

        VulkanContext m_Context;
        Scope<Swapchain> m_Swapchain;

        SlangCompiler m_ShaderCompiler;
        Scope<Pipeline> m_Pipeline;
        TextureManager m_Textures;
        MeshManager m_Meshes;

        AllocatedImage m_DepthImage{};
        VkFormat m_DepthFormat{VK_FORMAT_D32_SFLOAT};
        VkSampleCountFlagBits m_MsaaSamples{VK_SAMPLE_COUNT_1_BIT};

        AllocatedImage m_ColorImage{};

        Vec3 m_TintColor{1.0f, 1.0f, 1.0f};

        struct FrameData {
            VkCommandPool commandPool{VK_NULL_HANDLE};
            VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
            VkSemaphore imageAvailableSemaphore{VK_NULL_HANDLE};
            VkSemaphore renderFinishedSemaphore{VK_NULL_HANDLE};
            VkFence inFlightFence{VK_NULL_HANDLE};
        };

        static constexpr int MAX_FRAMES_IN_FLIGHT = 5;
        std::vector<FrameData> m_Frames;

        u32 m_CurrentImageIndex{0};
        u32 m_CurrentFrame{0};

        u32 m_PendingWidth{0};
        u32 m_PendingHeight{0};
        bool m_PendingResize{false};
    };
} // namespace Engine
