#pragma once
#include "Ocean.h"
#include "WaterLook.h"
#include "../Camera.h"
struct TFRenderer; struct TFQueue; struct TFCmd; struct TFRenderTarget; struct TFBuffer; struct TFTexture;
namespace mooring {
struct OceanRenderer;
struct WaterProfile;
OceanRenderer* createOceanRenderer(TFRenderer*,unsigned resolution=256);
void destroyOceanRenderer(OceanRenderer*);
void loadOceanRenderer(OceanRenderer*,uint32_t format);
void unloadOceanRenderer(OceanRenderer*);
void computeOcean(OceanRenderer*,TFCmd*,const Ocean*,unsigned frame,float time,const WaterLook& look=WaterLook{});
void prepareOceanEffects(OceanRenderer*,TFCmd*,const Camera&,unsigned frame,float level,const f4x4& shadowMatrix,const WaterLook&,const Snapshot&,const VesselLayout&,float4 reflectionBounds={0,0,1,1},const WaterProfile* profile=nullptr,float4 shadowBounds={0,0,1,1});
void drawOcean(OceanRenderer*,TFCmd*,unsigned frame);
void drawOceanLight(OceanRenderer*,TFCmd*,unsigned frame);
void setOceanScene(OceanRenderer*,TFRenderTarget* opaque,TFRenderTarget* reflection,TFRenderTarget* shadow,TFRenderTarget* waterLight);
bool verifyOceanGPU(OceanRenderer*,TFQueue*);
uint64_t oceanGPUBytes(const OceanRenderer*);
TFTexture* oceanSkyTexture(const OceanRenderer*,unsigned& height);
TFTexture* oceanCloudNoise(const OceanRenderer*);
}
