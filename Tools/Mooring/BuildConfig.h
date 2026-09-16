#pragma once
#include "../../Common/Application/Config.h"
#include "Tracy.h"

// Development shader tools are enabled by the Debug CMake configuration.
#undef ENABLE_FORGE_SCRIPTING
#if !defined(MOORING_SHADER_LAB)
#undef ENABLE_FORGE_RELOAD_SHADER
#endif
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
