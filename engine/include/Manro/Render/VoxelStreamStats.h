#pragma once

#include <Manro/Core/Types.h>

namespace Manro {
    struct VoxelStreamStats_t {
        float rebuildMs{0.f};
        float evictMs{0.f};
        float enqueueMs{0.f};
        float drainMs{0.f};
        float commitMs{0.f};
        float totalMs{0.f};

        u64 blockingUploads{0};
        u32 deferredBricks{0};
        u64 deferredDropped{0};
        u32 stageHighWater[3]{0, 0, 0};
        u32 midFrameBinds{0};
    };
} // namespace Manro
