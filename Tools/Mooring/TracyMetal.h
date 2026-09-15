#pragma once
#include "Common/Graphics/Interfaces/IGraphics.h"
#ifdef TRACY_ENABLE
void                      mooringTracyMetalInitQueue(TFQueue* queue, TFRenderer* renderer);
void                      mooringTracyMetalExitQueue(TFQueue* queue);
void                      mooringTracyMetalInitCmd(TFCmd* cmd);
void                      mooringTracyMetalExitCmd(TFCmd* cmd);
void                      mooringTracyMetalBeginCmd(TFCmd* cmd);
void                      mooringTracyMetalCommit(TFCmd* cmd);
void                      mooringTracyMetalRender(TFCmd* cmd, MTLRenderPassDescriptor* desc);
void                      mooringTracyMetalCompute(TFCmd* cmd, MTLComputePassDescriptor* desc);
id<MTLBlitCommandEncoder> mooringTracyMetalBlit(TFCmd* cmd);
void                      mooringTracyMetalEndEncoder(TFCmd* cmd);
void                      mooringTracyMetalWork(TFCmd* cmd, const char* kind, uint64_t count);
void                      mooringTracyMetalImages(unsigned interval);
void                      mooringTracyMetalFrameImage(TFCmd* cmd, TFRenderTarget* target);
#else
#define mooringTracyMetalInitQueue(...)  ((void)0)
#define mooringTracyMetalExitQueue(...)  ((void)0)
#define mooringTracyMetalInitCmd(...)    ((void)0)
#define mooringTracyMetalExitCmd(...)    ((void)0)
#define mooringTracyMetalBeginCmd(...)   ((void)0)
#define mooringTracyMetalCommit(cmd)     [(cmd)->pCommandBuffer commit]
#define mooringTracyMetalRender(...)     ((void)0)
#define mooringTracyMetalCompute(...)    ((void)0)
#define mooringTracyMetalBlit(cmd)       [(cmd)->pCommandBuffer blitCommandEncoder]
#define mooringTracyMetalEndEncoder(...) ((void)0)
#define mooringTracyMetalWork(...)       ((void)0)
#define mooringTracyMetalImages(...)     ((void)0)
#define mooringTracyMetalFrameImage(...) ((void)0)
#endif
