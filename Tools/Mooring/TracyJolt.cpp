#include <Jolt/Jolt.h>
#include <Jolt/Core/Profiler.h>
#include <tracy/Tracy.hpp>
#include "Common/Game/ThirdParty/OpenSource/flecs/flecs.h"
#include <cstring>

// Jolt's supported external-profiler ABI owns 64 bytes per measurement.
JPH::ExternalProfileMeasurement::ExternalProfileMeasurement(const char* name, uint32 color)
{
    static_assert(sizeof(TracyCZoneCtx) <= sizeof(mUserData));
    const bool active =
#ifdef TRACY_ON_DEMAND
        TracyIsConnected;
#else
        true;
#endif
    const uint64_t source = active ? tracy::Profiler::AllocSourceLocation(__LINE__, __FILE__, sizeof(__FILE__) - 1, name, strlen(name),
                                                                          name, strlen(name), color)
                                   : 0;
#if MOORING_TRACY_ZONE_STACK_DEPTH > 0
    const auto zone = ___tracy_emit_zone_begin_alloc_callstack(source, MOORING_TRACY_ZONE_STACK_DEPTH, active);
#else
    const auto zone = ___tracy_emit_zone_begin_alloc(source, active);
#endif
    memcpy(mUserData, &zone, sizeof(zone));
}
JPH::ExternalProfileMeasurement::~ExternalProfileMeasurement()
{
    TracyCZoneCtx zone;
    memcpy(&zone, mUserData, sizeof(zone));
    TracyCZoneEnd(zone);
}

// Use Flecs' Forge profiling callbacks, including names supplied by ECS.
void mooringTracyInstallECS()
{
    setEntityComponentSystemProfileCallbacks(
        [](const char* group, const char* name, uint32_t color)
        {
            static_assert(sizeof(TracyCZoneCtx) <= sizeof(EcsTFProfileToken));
            static const ___tracy_source_location_data source = { "ECS system", "ecs_run", __FILE__, __LINE__, 0x9977CC };
            auto                                       zone = MTRACY_BEGIN(&source);
            name = name ? name : "Unnamed ECS system";
            TracyCZoneName(zone, name, strlen(name));
            if (group)
                TracyCZoneText(zone, group, strlen(group));
            EcsTFProfileToken token{};
            memcpy(&token, &zone, sizeof(zone));
            return token;
        },
        [](EcsTFProfileToken token)
        {
            TracyCZoneCtx zone;
            memcpy(&zone, &token, sizeof(zone));
            TracyCZoneEnd(zone);
        });
}
