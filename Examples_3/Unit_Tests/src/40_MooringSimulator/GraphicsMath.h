#pragma once
#include "Simulation/Simulation.h"
#include "Common_3/Utilities/Interfaces/IMath.h"
#include <cmath>

namespace mooring {
// Keep ECS and GPU layouts plain data; use Forge math at the rendering boundary.
inline float3 toForge(Vec3 v) { return make_float3(v.x,v.y,v.z); }
inline Vec3 fromForge(float3 v) { return {v.x,v.y,v.z}; }
inline f4x4 lightProjection(Vec3 center,float3 light,unsigned resolution,float span,float depth) {
    auto forward=f3MulScalar(light,-1);
    auto right=f3Normalize(f3Cross(make_float3(0,1,0),forward)), up=f3Cross(forward,right);
    float x=std::floor(f3Dot(toForge(center),right)*resolution/span)*span/resolution;
    float y=std::floor(f3Dot(toForge(center),up)*resolution/span)*span/resolution;
    auto target=f3Add(f3Add(f3MulScalar(right,x),f3MulScalar(up,y)),f3MulScalar(forward,f3Dot(toForge(center),forward)));
    auto eye=f3Add(target,f3MulScalar(light,depth*.5f));
    return f4x4Mul(f4x4OrthographicLH(-span*.5f,span*.5f,-span*.5f,span*.5f,0,depth),f4x4LookAtLH(eye,target,up));
}
}
