#pragma once
#include "Simulation.h"
#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
namespace mooring {
inline JPH::Vec3 toJolt(Vec3 v) { return {v.x,v.y,v.z}; }
inline Vec3 fromJolt(JPH::Vec3Arg v) { return {v.GetX(),v.GetY(),v.GetZ()}; }
inline JPH::Quat toJolt(Rotation q) { return {q.x,q.y,q.z,q.w}; }
}
