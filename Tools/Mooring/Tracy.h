#pragma once

// C API throughout the Forge boundary: usable from C, C++, ObjC and ObjC++.
// Include before IMemory.h, which deliberately prohibits system allocators.
#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#include <string.h>
static inline void mooringTracyEndZone(TracyCZoneCtx* zone) { TracyCZoneEnd(*zone); }
#if MOORING_TRACY_ZONE_STACK_DEPTH > 0
#define MTRACY_BEGIN(source) ___tracy_emit_zone_begin_callstack(source, MOORING_TRACY_ZONE_STACK_DEPTH, 1)
#else
#define MTRACY_BEGIN(source) ___tracy_emit_zone_begin(source, 1)
#endif
#define MTRACY_ZONE_COLOR(name, color) \
    static const struct ___tracy_source_location_data TracyConcat(mtracySource, __LINE__) = {name, __func__, __FILE__, __LINE__, color}; \
    TracyCZoneCtx mtracyZone __attribute__((cleanup(mooringTracyEndZone))) = \
        MTRACY_BEGIN(&TracyConcat(mtracySource, __LINE__))
#define MTRACY_ZONE(name) MTRACY_ZONE_COLOR(name, 0)
#define MTRACY_TEXT(text) do { if (mtracyZone.active) { const char* t = (text); if (t) TracyCZoneText(mtracyZone, t, strlen(t)); } } while (0)
#define MTRACY_VALUE(value) TracyCZoneValue(mtracyZone, value)
#define MTRACY_PLOT(name, value) TracyCPlot(name, value)
#define MTRACY_FRAME() TracyCFrameMark
#if MOORING_TRACY_STACK_DEPTH > 0
#define MTRACY_ALLOC(ptr, size, pool) do { if (ptr) TracyCAllocNS(ptr, size, MOORING_TRACY_STACK_DEPTH, pool); } while (0)
#define MTRACY_FREE(ptr, pool) do { if (ptr) TracyCFreeNS(ptr, MOORING_TRACY_STACK_DEPTH, pool); } while (0)
#else
#define MTRACY_ALLOC(ptr, size, pool) do { if (ptr) TracyCAllocN(ptr, size, pool); } while (0)
#define MTRACY_FREE(ptr, pool) do { if (ptr) TracyCFreeN(ptr, pool); } while (0)
#endif
#ifdef __cplusplus
extern "C" {
#endif
void mooringTracyLog(unsigned level, const char* message, size_t size);
void mooringTracyAppInfo(const char* app);
void mooringTracyFrame(void);
void mooringTracyShutdown(void);
void mooringTracyCpuEnter(const char* name, const char* group, unsigned color);
void mooringTracyCpuLeave(void);
void mooringTracyScene(const char* scene);
#ifdef __cplusplus
}
#endif
#else
#define MTRACY_ZONE_COLOR(name, color) ((void)0)
#define MTRACY_ZONE(name) ((void)0)
#define MTRACY_TEXT(text) ((void)0)
#define MTRACY_VALUE(value) ((void)0)
#define MTRACY_PLOT(name, value) ((void)0)
#define MTRACY_FRAME() ((void)0)
#define MTRACY_ALLOC(ptr, size, pool) ((void)0)
#define MTRACY_FREE(ptr, pool) ((void)0)
#define mooringTracyLog(...) ((void)0)
#define mooringTracyAppInfo(...) ((void)0)
#define mooringTracyFrame() ((void)0)
#define mooringTracyShutdown() ((void)0)
#define mooringTracyCpuEnter(...) ((void)0)
#define mooringTracyCpuLeave() ((void)0)
#define mooringTracyScene(...) ((void)0)
#endif

#ifdef MOORING_TRACY_FINE
#define MTRACY_FINE_ZONE(name) MTRACY_ZONE(name)
#else
#define MTRACY_FINE_ZONE(name) ((void)0)
#endif
