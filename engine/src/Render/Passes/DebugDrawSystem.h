#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro {
    class CVulkanContext;
    class CBuffer;
    class CPipeline;
    class CVirtualFS;

    struct DrawLineCmd_t {
        Vec3 start;
        u32 color;
        Vec3 end;
        u32 depthTest;
    };

    struct DrawBoxCmd_t {
        Vec3 center;
        u32 color;
        Vec3 halfExtents;
        u32 depthTest;
        Mat4 transform;
    };

    struct DrawSphereCmd_t {
        Vec3 center;
        float radius;
        u32 color;
        u32 segments;
        u32 depthTest;
        u32 _pad0;
    };

    struct DrawFrustumCmd_t {
        Mat4 invViewProj;
        u32 color;
        u32 depthTest;
        u32 _pad0;
        u32 _pad1;
    };

    struct DrawCrossCmd_t {
        Vec3 center;
        float size;
        u32 color;
        u32 depthTest;
    };

    struct LineVertex_t {
        Vec3 position;
        u32 color;
    };

    struct DrawIndirectCmd_t {
        u32 vertexCount;
        u32 instanceCount;
        u32 firstVertex;
        u32 firstInstance;
    };

    class CDrawSystem {
    public:
        explicit CDrawSystem(const CVulkanContext &context, CVirtualFS &vfs);

        ~CDrawSystem();

        CDrawSystem(const CDrawSystem &) = delete;

        CDrawSystem &operator=(const CDrawSystem &) = delete;

        void Init(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);

        void Shutdown();

        void BeginFrame();

        void DispatchExpand(VkCommandBuffer cmd);

        void Draw(VkCommandBuffer cmd, const Mat4 &viewProj,
                  VkImageView colorTarget,
                  VkImageView resolveColorTarget,
                  VkImageView depthTarget,
                  u32 width, u32 height,
                  bool useMsaaResolve);

        void SubmitLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest);

        void SubmitBox(const Vec3 &center, const Vec3 &halfExtents, const Mat4 &transform, u32 color, bool depthTest);

        void SubmitSphere(const Vec3 &center, float radius, u32 color, int segments, bool depthTest);

        void SubmitFrustum(const Mat4 &invViewProj, u32 color, bool depthTest);

        void SubmitCross(const Vec3 &center, float size, u32 color, bool depthTest);

    private:
        void CreateBuffers();

        void CreateComputePipeline();

        void CreateRenderPipelines(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);

        const CVulkanContext &m_Context;
        CVirtualFS &m_Vfs;

        static constexpr u32 kMaxLines = 65536;
        static constexpr u32 kMaxBoxes = 8192;
        static constexpr u32 kMaxSpheres = 4096;
        static constexpr u32 kMaxFrustums = 256;
        static constexpr u32 kMaxCrosses = 4096;
        static constexpr u32 kMaxVertices = 1024 * 1024;

        Scope<CBuffer> m_LineCommandBuffer;
        Scope<CBuffer> m_BoxCommandBuffer;
        Scope<CBuffer> m_SphereCommandBuffer;
        Scope<CBuffer> m_FrustumCommandBuffer;
        Scope<CBuffer> m_CrossCommandBuffer;

        Scope<CBuffer> m_VertexBuffer;
        Scope<CBuffer> m_VertexBufferNoDepth;
        Scope<CBuffer> m_IndirectBuffer;
        Scope<CBuffer> m_IndirectBufferNoDepth;
        Scope<CBuffer> m_CounterBuffer;

        Scope<CPipeline> m_ExpandPipeline;
        Scope<CPipeline> m_RenderPipeline;
        Scope<CPipeline> m_RenderPipelineNoDepth;

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_ComputeSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_ComputeDescriptorSet{VK_NULL_HANDLE};
        VkDescriptorSet m_ComputeDescriptorSetNoDepth{VK_NULL_HANDLE};

        u32 m_unLineCount{0};
        u32 m_unBoxCount{0};
        u32 m_unSphereCount{0};
        u32 m_unFrustumCount{0};
        u32 m_unCrossCount{0};
        u32 m_unDepthVertexCount{0};
        u32 m_unNoDepthVertexCount{0};
    };
} // namespace Manro
