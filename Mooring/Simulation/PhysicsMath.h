#pragma once
#include "Simulation.h"
#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
namespace mooring
{
inline JPH::Vec3 toJolt(Vec3 value) { return { value.x, value.y, value.z }; }
inline Vec3      fromJolt(JPH::Vec3Arg value) { return { value.GetX(), value.GetY(), value.GetZ() }; }
inline JPH::Quat toJolt(Rotation rotation) { return { rotation.x, rotation.y, rotation.z, rotation.w }; }
} // namespace mooring
