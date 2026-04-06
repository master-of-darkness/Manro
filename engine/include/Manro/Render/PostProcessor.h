#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Render/Tonemap/Tonemapper.h>

namespace Manro {
    class PostProcessor {
    public:
        PostProcessor() = default;

        void Apply(TextureHandle offscreenTex,
                   TextureHandle targetTex,
                   const TonemapperData &settings) {
            (void) offscreenTex;
            (void) targetTex;
            (void) settings;
        }

    private:
        PipelineHandle m_CompositePipeline{};
    };
} // namespace Manro