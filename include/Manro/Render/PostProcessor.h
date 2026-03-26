#pragma once

#include <Manro/Render/RHI/IRenderDevice.h>
#include <Manro/Render/Tonemap/Tonemapper.h>

namespace Manro {

    class PostProcessor {
    public:
        explicit PostProcessor(RHI::IRenderDevice &device);

        void Apply(RHI::ICommandList &cmd,
                   RHI::TextureHandle offscreenTex,
                   RHI::TextureHandle targetTex,
                   const TonemapperData &settings);

    private:
        RHI::IRenderDevice &m_Device;
        RHI::PipelineHandle m_CompositePipeline;
    };

} // namespace Manro