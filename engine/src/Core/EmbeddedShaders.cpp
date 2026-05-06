#include "EmbeddedShaders.h"
#include <Manro/Core/VirtualFS.h>

#include "pbr_vert_spv.h"
#include "pbr_frag_spv.h"
#include "pbr_zprepass_frag_spv.h"
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
    void RegisterEmbeddedShaders(CVirtualFS &vfs) {
        auto v = [](const u8 *p, size_t n) { return std::span<const u8>(p, n); };

        vfs.MountStaticView("shaders://pbr.vert.spv", v(pbr_vert_spv, pbr_vert_spv_len));
        vfs.MountStaticView("shaders://pbr.frag.spv", v(pbr_frag_spv, pbr_frag_spv_len));
        vfs.MountStaticView("shaders://pbr_zprepass.frag.spv",
                            v(pbr_zprepass_frag_spv, pbr_zprepass_frag_spv_len));
        vfs.MountStaticView("shaders://forward_plus_cull.comp.spv",
                            v(forward_plus_cull_comp_spv, forward_plus_cull_comp_spv_len));
        vfs.MountStaticView("shaders://composite.vert.spv", v(composite_vert_spv, composite_vert_spv_len));
        vfs.MountStaticView("shaders://composite.frag.spv", v(composite_frag_spv, composite_frag_spv_len));
        vfs.MountStaticView("shaders://tonemapper.comp.spv", v(tonemapper_comp_spv, tonemapper_comp_spv_len));
        vfs.MountStaticView("shaders://tonemapper_histogram.comp.spv",
                            v(tonemapper_histogram_comp_spv, tonemapper_histogram_comp_spv_len));
        vfs.MountStaticView("shaders://tonemapper_autoexposure.comp.spv",
                            v(tonemapper_autoexposure_comp_spv, tonemapper_autoexposure_comp_spv_len));
        vfs.MountStaticView("shaders://mesh_cull.comp.spv", v(mesh_cull_comp_spv, mesh_cull_comp_spv_len));
        vfs.MountStaticView("shaders://shadow_depth.vert.spv",
                            v(shadow_depth_vert_spv, shadow_depth_vert_spv_len));
        vfs.MountStaticView("shaders://skybox.vert.spv", v(skybox_vert_spv, skybox_vert_spv_len));
        vfs.MountStaticView("shaders://skybox.frag.spv", v(skybox_frag_spv, skybox_frag_spv_len));
        vfs.MountStaticView("shaders://gizmo.vert.spv", v(gizmo_vert_spv, gizmo_vert_spv_len));
        vfs.MountStaticView("shaders://gizmo.frag.spv", v(gizmo_frag_spv, gizmo_frag_spv_len));
        vfs.MountStaticView("shaders://line_expand.comp.spv", v(line_expand_comp_spv, line_expand_comp_spv_len));
    }
} // namespace Manro
