#include "Tracy.h"
#include <tracy/Tracy.hpp>
#include <chrono>
#include <thread>
#include "Common_3/Utilities/Interfaces/ILog.h"
#include "Common_3/Utilities/Interfaces/IMemory.h"

namespace {
char sceneName[128];
uint32_t sceneSection = 0;
uint64_t sceneConnection = 0;
uint64_t connectionId()
{
#ifdef TRACY_ON_DEMAND
    return tracy::GetProfiler().ConnectionId();
#else
    return 0;
#endif
}
void updateScene()
{
    const auto connection = connectionId();
    if (sceneName[0] && (!sceneSection || sceneConnection != connection)) {
        sceneConnection = connection;
        sceneSection = TracySectionEnterCategory(1, "%s", sceneName);
    }
}
}

void mooringTracyMetalImages(unsigned interval);
unsigned mooringTracyMetalImageInterval();
void mooringTracyLog(unsigned level, const char* message, size_t size)
{
    auto severity = level & eERROR ? tracy::MessageSeverity::Error : level & eWARNING ? tracy::MessageSeverity::Warning :
                    level & eDEBUG ? tracy::MessageSeverity::Debug : tracy::MessageSeverity::Info;
    TracyLogString(severity, level & eERROR ? 0xEE5544 : level & eWARNING ? 0xEEBB44 : 0, 0, size, message);
}

void mooringTracyAppInfo(const char* app)
{
    TracyAppInfo(app, strlen(app));
    const char* configuration = "Mooring / Forge Metal / Jolt 5.6 / Flecs 4.0.1 / Tracy 0.14.1; "
#ifdef TRACY_ON_DEMAND
        "on-demand capture; "
#else
        "startup capture; "
#endif
#ifdef MOORING_TRACY_FINE
        "fine water queries enabled";
#else
        "coarse water queries";
#endif
    TracyAppInfo(configuration, strlen(configuration));
    TracySectionSetup(1, "Sea scenario");
    TracyPlotConfig("CPU / live bytes", tracy::PlotFormatType::Memory, true, true, 0x55AA88);
    TracyPlotConfig("CPU / peak bytes", tracy::PlotFormatType::Memory, true, false, 0xAA8855);
    TracyPlotConfig("CPU / live allocations", tracy::PlotFormatType::Number, true, false, 0);
    TracyPlotConfig("Metal / driver allocated bytes", tracy::PlotFormatType::Memory, true, true, 0x5599CC);
    TracySetProgramName(app);
    TracyParameterRegister([](void*, uint32_t index, int32_t value) {
        if (index == 0) mooringTracyMetalImages(unsigned(value > 0 ? value : 0));
    }, nullptr);
    TracyParameterSetup(0, "Thumbnail interval (0 disables)", false, int32_t(mooringTracyMetalImageInterval()));
    mooringTracyScene("Interactive");
}

void mooringTracyMetalFrame();
void mooringTracyFrame(void)
{
    updateScene();
#ifdef ENABLE_MEMORY_TRACKING
    const auto stats = memGetStatistics();
    TracyPlot("CPU / live bytes", int64_t(stats.totalReportedMemory));
    TracyPlot("CPU / peak bytes", int64_t(stats.peakReportedMemory));
    TracyPlot("CPU / live allocations", int64_t(stats.totalAllocUnitCount));
#endif
    FrameMark;
    mooringTracyMetalFrame();
}

void mooringTracyShutdown(void)
{
    // Drain while the main thread's producer and source metadata still exist.
    // Waiting for C++ static destruction loses the final events on macOS.
    auto& profiler = tracy::GetProfiler();
    profiler.RequestShutdown();
    while (!profiler.HasShutdownFinished()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void mooringTracyScene(const char* scene)
{
    if (sceneSection && sceneConnection == connectionId()) TracySectionLeave(sceneSection);
    sceneSection = 0;
    snprintf(sceneName, sizeof(sceneName), "%s", scene ? scene : "");
    updateScene();
}

namespace {
thread_local TracyCZoneCtx forgeZones[128];
thread_local unsigned forgeDepth = 0;
}
void mooringTracyCpuEnter(const char* name, const char* group, unsigned color)
{
    if (forgeDepth++ >= 128) {
        if (forgeDepth == 129) TracyMessageL("Forge CPU profiling exceeded 128 nested zones; deeper scopes are omitted.");
        return;
    }
    bool active = true;
#ifdef TRACY_ON_DEMAND
    active = TracyIsConnected;
#endif
    name = name ? name : "Unnamed Forge scope";
    group = group ? group : "Forge";
    const uint64_t source = active ? tracy::Profiler::AllocSourceLocation(__LINE__, __FILE__, sizeof(__FILE__)-1,
        group, strlen(group), name, strlen(name), color) : 0;
#if MOORING_TRACY_ZONE_STACK_DEPTH > 0
    forgeZones[forgeDepth-1] = ___tracy_emit_zone_begin_alloc_callstack(source, MOORING_TRACY_ZONE_STACK_DEPTH, active);
#else
    forgeZones[forgeDepth-1] = ___tracy_emit_zone_begin_alloc(source, active);
#endif
}
void mooringTracyCpuLeave(void)
{
    if (forgeDepth && --forgeDepth < 128) TracyCZoneEnd(forgeZones[forgeDepth]);
}
