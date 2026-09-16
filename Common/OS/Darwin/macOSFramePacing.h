#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
// Call on the main thread. Zero removes the application timer's frame limit.
void setMacOSFrameRateLimit(uint32_t framesPerSecond);
#ifdef __cplusplus
}
#endif
