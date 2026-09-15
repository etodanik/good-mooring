#pragma once
#include "../../Common_3/Application/Config.h"
#include "Tracy.h"

// The PoC has no script VM, asset conversion, animation, or shader reload server.
#undef ENABLE_FORGE_SCRIPTING
#undef ENABLE_FORGE_RELOAD_SHADER
#undef ENABLE_FORGE_ANIMATION_DEBUG
#undef ENABLE_FORGE_MATERIALS
#ifndef ENABLE_SCREENSHOT
#define ENABLE_SCREENSHOT
#endif
#undef ENABLE_MESHOPTIMIZER
#undef ENABLE_THREAD_PERFORMANCE_STATS

// Public Apple sources used these availability names before the API cleanup.
#if defined(__APPLE__)
#define IOS16_API API_AVAILABLE(macos(13.0), ios(16.0))
#define IOS17_API API_AVAILABLE(macos(14.0), ios(17.0))
#if defined(__OBJC__)
#define IOS16_RUNTIME @available(macOS 13.0, iOS 16.0, *)
#define IOS17_RUNTIME @available(macOS 14.0, iOS 17.0, *)
#endif
#endif
