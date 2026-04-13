#pragma once

#ifdef MANRO_PROFILING

#include <tracy/Tracy.hpp>

#define MNR_PROFILE_FRAME()             FrameMark
#define MNR_PROFILE_SCOPE(name)         ZoneScopedN(name)
#define MNR_PROFILE_FUNCTION()          ZoneScoped
#define MNR_PROFILE_TAG(text, len)      ZoneText(text, len)
#define MNR_PROFILE_VALUE(name, val)    TracyPlot(name, val)
#define MNR_PROFILE_THREAD(name)        tracy::SetThreadName(name)

#else

#define MNR_PROFILE_FRAME()             (void)0
#define MNR_PROFILE_SCOPE(name)         (void)0
#define MNR_PROFILE_FUNCTION()          (void)0
#define MNR_PROFILE_TAG(text, len)      (void)0
#define MNR_PROFILE_VALUE(name, val)    (void)0
#define MNR_PROFILE_THREAD(name)        (void)0

#endif
