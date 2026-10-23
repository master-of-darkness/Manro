#pragma once

#include <Manro/Core/Types.h>

namespace Manro {
    class CVoxelRuntime;
}

namespace VoxelStateMap {
    Manro::u32 Orient(Manro::CVoxelRuntime &runtime, Manro::u32 base, const Manro::Vec3 &fwd,
                      float hitY, int nx, int ny, int nz, bool targetFluid);
}
