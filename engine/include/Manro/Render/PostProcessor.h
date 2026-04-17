#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Render/Tonemap/Tonemapper.h>

namespace Manro {
    class CPostProcessor {
    public:
        CPostProcessor() = default;

        void Apply(TextureHandle offscreenTex,
                   TextureHandle targetTex,
                   const TonemapperData_t &settings) {
            (void) offscreenTex;
            (void) targetTex;
            (void) settings;
        }

    private:
        PipelineHandle m_CompositePipeline{};
    };
} // namespace Manro