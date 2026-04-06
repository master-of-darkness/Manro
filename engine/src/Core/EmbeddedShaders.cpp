#include <Manro/Core/VirtualFS.h>

#include "pbr_vert_spv.h"
#include "pbr_frag_spv.h"
#include "forward_plus_cull_comp_spv.h"
#include "composite_vert_spv.h"
#include "composite_frag_spv.h"
#include "tonemapper_comp_spv.h"
#include "shadow_depth_vert_spv.h"
#include "tonemapper_histogram_comp_spv.h"
#include "tonemapper_autoexposure_comp_spv.h"
#include "mesh_cull_comp_spv.h"
#include "skybox_vert_spv.h"
#include "skybox_frag_spv.h"
#include "gizmo_frag_spv.h"
#include "gizmo_vert_spv.h"
#include "line_expand_comp_spv.h"

namespace Manro {
    void RegisterEmbeddedShaders() {
        auto &vfs = VirtualFS::Get();

        vfs.Mount("shaders://pbr.vert.spv", pbr_vert_spv, pbr_vert_spv_len);
        vfs.Mount("shaders://pbr.frag.spv", pbr_frag_spv, pbr_frag_spv_len);
        vfs.Mount("shaders://forward_plus_cull.comp.spv", forward_plus_cull_comp_spv, forward_plus_cull_comp_spv_len);
        vfs.Mount("shaders://composite.vert.spv", composite_vert_spv, composite_vert_spv_len);
        vfs.Mount("shaders://composite.frag.spv", composite_frag_spv, composite_frag_spv_len);
        vfs.Mount("shaders://tonemapper.comp.spv", tonemapper_comp_spv, tonemapper_comp_spv_len);
        vfs.Mount("shaders://tonemapper_histogram.comp.spv", tonemapper_histogram_comp_spv,
                  tonemapper_histogram_comp_spv_len);
        vfs.Mount("shaders://tonemapper_autoexposure.comp.spv", tonemapper_autoexposure_comp_spv,
                  tonemapper_autoexposure_comp_spv_len);
        vfs.Mount("shaders://mesh_cull.comp.spv", mesh_cull_comp_spv, mesh_cull_comp_spv_len);
        vfs.Mount("shaders://shadow_depth.vert.spv", shadow_depth_vert_spv, shadow_depth_vert_spv_len);
        vfs.Mount("shaders://skybox.vert.spv", skybox_vert_spv, skybox_vert_spv_len);
        vfs.Mount("shaders://skybox.frag.spv", skybox_frag_spv, skybox_frag_spv_len);
        vfs.Mount("shaders://gizmo.vert.spv", gizmo_vert_spv, gizmo_vert_spv_len);
        vfs.Mount("shaders://gizmo.frag.spv", gizmo_frag_spv, gizmo_frag_spv_len);
        vfs.Mount("shaders://line_expand.comp.spv", line_expand_comp_spv, line_expand_comp_spv_len);
    }
} // namespace Manro