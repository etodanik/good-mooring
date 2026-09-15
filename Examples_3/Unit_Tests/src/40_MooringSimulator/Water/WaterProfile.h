#pragma once
#include "Common_3/Application/Interfaces/IProfiler.h"
#include "Common_3/Graphics/Interfaces/IGraphics.h"
namespace mooring {
enum WaterPass { WaterFFT, WaterEffects, WaterSky, WaterGeometry, WaterSurface, WaterPost, WaterPassCount };
struct WaterProfile { ProfileToken passes[WaterPassCount]; };
inline void beginWaterPass(TFCmd* cmd,const WaterProfile* profile,WaterPass pass) {
    if(profile) cmdBeginGpuFrameProfile(cmd,profile->passes[pass]);
}
inline void endWaterPass(TFCmd* cmd,const WaterProfile* profile,WaterPass pass) {
    if(!profile) return;
    // Metal permits one active stage query. Finish its encoder before the
    // next independent query begins; no nested profiler scopes are used.
    cmdBindRenderTargets(cmd,nullptr);
    cmdEndGpuFrameProfile(cmd,profile->passes[pass]);
}
}
