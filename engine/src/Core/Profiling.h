#pragma once

#ifdef MANRO_PROFILING

#include <tracy/Tracy.hpp>
#include <volk.h>
#include <tracy/TracyVulkan.hpp>

#define MNR_PROFILE_FRAME()             FrameMark
#define MNR_PROFILE_SCOPE(name)         ZoneScopedN(name)
#define MNR_PROFILE_FUNCTION()          ZoneScoped
#define MNR_PROFILE_TAG(text, len)      ZoneText(text, len)
#define MNR_PROFILE_VALUE(name, val)    TracyPlot(name, val)
#define MNR_PROFILE_THREAD(name)        tracy::SetThreadName(name)
#define MNR_GPU_CONTEXT(instance, physDev, dev, queue, cmdBuf) \
TracyVkContext(instance, physDev, dev, queue, cmdBuf, vkGetInstanceProcAddr, vkGetDeviceProcAddr)
#define MNR_GPU_DESTROY(ctx)            TracyVkDestroy(ctx)
#define MNR_GPU_ZONE(ctx, cb, name)     TracyVkZone(ctx, cb, name)
#define MNR_GPU_COLLECT(ctx, cb)        TracyVkCollect(ctx, cb)

using MnrGpuProfileCtx = TracyVkCtx;

#else

#define MNR_PROFILE_FRAME()             (void)0
#define MNR_PROFILE_SCOPE(name)         (void)0
#define MNR_PROFILE_FUNCTION()          (void)0
#define MNR_PROFILE_TAG(text, len)      (void)0
#define MNR_PROFILE_VALUE(name, val)    (void)0
#define MNR_PROFILE_THREAD(name)        (void)0
#define MNR_GPU_CONTEXT(instance, physDev, dev, queue, cmdBuf) (void)0
#define MNR_GPU_DESTROY(ctx)            (void)0
#define MNR_GPU_ZONE(ctx, cb, name)     (void)0
#define MNR_GPU_COLLECT(ctx, cb)        (void)0

using MnrGpuProfileCtx = void *;
#endif
