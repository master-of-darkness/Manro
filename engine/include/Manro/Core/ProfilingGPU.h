#pragma once

#ifdef MANRO_PROFILING

#include <tracy/TracyVulkan.hpp>

#define MNR_GPU_CONTEXT(instance, physDev, dev, queue, cmdBuf) \
    TracyVkContext(instance, physDev, dev, queue, cmdBuf, vkGetInstanceProcAddr, vkGetDeviceProcAddr)
#define MNR_GPU_DESTROY(ctx)            TracyVkDestroy(ctx)
#define MNR_GPU_ZONE(ctx, cb, name)     TracyVkZone(ctx, cb, name)
#define MNR_GPU_COLLECT(ctx, cb)        TracyVkCollect(ctx, cb)

#else

#define MNR_GPU_CONTEXT(instance, physDev, dev, queue, cmdBuf) (void)0
#define MNR_GPU_DESTROY(ctx)            (void)0
#define MNR_GPU_ZONE(ctx, cb, name)     (void)0
#define MNR_GPU_COLLECT(ctx, cb)        (void)0

#endif