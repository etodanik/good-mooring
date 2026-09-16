#pragma once
#include "Camera.h"
#include "Water/WaterLook.h"
struct TFRenderer;
struct TFCmd;
struct TFRenderTarget;
namespace mooring
{
struct Scene;
struct OceanRenderer;
struct WaterProfile;
Scene* createScene(TFRenderer* renderer);
void   destroyScene(Scene* scene);
void   loadScene(Scene* scene, uint32_t colorFormat, unsigned width, unsigned height, const WaterLook&);
void   connectSceneWater(Scene*, OceanRenderer*);
void   unloadScene(Scene* scene);
bool   reloadSceneShaders(Scene*, uint32_t colorFormat);
void   drawScene(Scene* scene, TFCmd* cmd, TFRenderTarget* target, unsigned frame, const Camera& camera, const Snapshot& state,
                 const VesselLayout&, OceanRenderer* water, float level, float waterDepth, const WaterLook&,
                 const WaterProfile* profile = nullptr);
} // namespace mooring
