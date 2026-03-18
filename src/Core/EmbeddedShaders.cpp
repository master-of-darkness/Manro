#include <Manro/Core/VirtualFS.h>

#include "pbr_vert_spv.h"
#include "pbr_frag_spv.h"
#include "forward_plus_cull_comp_spv.h"
#include "composite_vert_spv.h"
#include "composite_frag_spv.h"
#include "tonemapper_comp_spv.h"
#include "tonemapper_histogram_comp_spv.h"
#include "tonemapper_autoexposure_comp_spv.h"

namespace Manro {
    void RegisterEmbeddedShaders() {
        auto &vfs = VirtualFS::Get();

        vfs.Mount("shaders://pbr.vert.spv",
                  pbr_vert_spv, pbr_vert_spv_len);

        vfs.Mount("shaders://pbr.frag.spv",
                  pbr_frag_spv, pbr_frag_spv_len);

        vfs.Mount("shaders://forward_plus_cull.comp.spv",
                  forward_plus_cull_comp_spv, forward_plus_cull_comp_spv_len);

        vfs.Mount("shaders://composite.vert.spv",
                  composite_vert_spv, composite_vert_spv_len);

        vfs.Mount("shaders://composite.frag.spv",
                  composite_frag_spv, composite_frag_spv_len);

        vfs.Mount("shaders://tonemapper.comp.spv",
                  tonemapper_comp_spv, tonemapper_comp_spv_len);

        vfs.Mount("shaders://tonemapper_histogram.comp.spv",
                  tonemapper_histogram_comp_spv, tonemapper_histogram_comp_spv_len);

        vfs.Mount("shaders://tonemapper_autoexposure.comp.spv",
                  tonemapper_autoexposure_comp_spv, tonemapper_autoexposure_comp_spv_len);
    }
} // namespace Manro