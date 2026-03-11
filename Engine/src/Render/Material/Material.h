#pragma once

#include <Render/Vulkan/Pipeline.h>
#include <Core/Types.h>

namespace Engine {
    class Material {
    public:
        Material(Scope<Pipeline> pipeline) : m_Pipeline(std::move(pipeline)) {}
        
        const Pipeline& GetPipeline() const { return *m_Pipeline; }
        VkPipeline GetHandle() const { return m_Pipeline->GetHandle(); }
        VkPipelineLayout GetLayout() const { return m_Pipeline->GetLayout(); }

    private:
        Scope<Pipeline> m_Pipeline;
    };
} // namespace Engine
