#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro::Internal {
    struct ZPrepassPassState;
    struct PbrPassState;
    struct SkyboxPassState;
    struct CompositePassState;
}

namespace Manro {
    class SceneRenderer {
    public:
        explicit SceneRenderer();

        void Flush(VkCommandBuffer cmd);

        void SetZPrepassState(const Internal::ZPrepassPassState *state);

        void SetPbrPassState(const Internal::PbrPassState *state);

        void SetSkyboxPassState(const Internal::SkyboxPassState *state);

        void SetCompositePassState(const Internal::CompositePassState *state);

    private:
        const Internal::ZPrepassPassState *m_ZPrepassState{nullptr};
        const Internal::PbrPassState *m_PbrPassState{nullptr};
        const Internal::SkyboxPassState *m_SkyboxPassState{nullptr};
        const Internal::CompositePassState *m_CompositePassState{nullptr};
    };
} // namespace Manro