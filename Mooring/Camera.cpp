#include "Camera.h"
#include "GraphicsMath.h"
#include <algorithm>
#include <cmath>

namespace mooring
{
Vec3 deckToWorld(const Snapshot& boat, Vec3 deckPosition)
{
    auto rotation = boat.rotation;
    return fromForge(
        f3Add(toForge(boat.position), quatRotateVector(make_quat(rotation.x, rotation.y, rotation.z, rotation.w), toForge(deckPosition))));
}
Vec3 stationPosition(Station station)
{
    switch (station)
    {
    case Station::Helm:
        return { 0, 1.05f, -3.2f };
    case Station::Cockpit:
        return { 0, 1.05f, -1.8f };
    case Station::Port:
        return { -1.45f, 1.05f, -1.4f };
    case Station::Starboard:
        return { 1.45f, 1.05f, -1.4f };
    case Station::Bow:
        return { 0, 1.05f, 4.7f };
    }
    return {};
}
static float3 cameraOffset(const Camera& camera)
{
    float elevation = camera.elevation * .01745329252f, azimuth = camera.azimuth * .01745329252f;
    return { std::sin(azimuth) * std::cos(elevation), std::sin(elevation), std::cos(azimuth) * std::cos(elevation) };
}
Vec3 cameraEye(const Camera& camera) { return fromForge(f3Add(toForge(camera.focus), f3MulScalar(cameraOffset(camera), camera.distance))); }
void updateCamera(Camera& camera, float dt)
{
    MTRACY_ZONE("updateCamera");
    float blend = 1 - std::exp(-7 * std::min(dt, .1f));
    camera.focus = fromForge(f3Lerp(toForge(camera.focus), toForge(camera.target), blend));
    camera.distance += (camera.targetDistance - camera.distance) * blend;
}
void zoomCamera(Camera& camera, float amount)
{
    camera.targetDistance = std::clamp(camera.targetDistance * std::exp(-std::clamp(amount, -5.0f, 5.0f) * .1f), 9.0f, 85.0f);
}
void panCamera(Camera& camera, float dx, float dy)
{
    auto  forward = f3Normalize(f3Sub(toForge(camera.focus), toForge(cameraEye(camera))));
    auto  right = f3Normalize(f3Cross(make_float3(0, 1, 0), forward));
    auto  up = f3Cross(forward, right);
    float scale = camera.distance * .9f / std::max(1u, camera.height);
    auto  offset = f3Add(f3MulScalar(right, -dx * scale), f3MulScalar(f3Normalize(make_float3(up.x, 0, up.z)), dy * scale));
    camera.target = fromForge(f3Add(toForge(camera.target), offset));
    camera.focus = fromForge(f3Add(toForge(camera.focus), offset));
}
f4x4 cameraMatrix(const Camera& camera)
{
    float inverseAspect = float(camera.height) / std::max(1u, camera.width);
    float horizontalFov = 2 * std::atan(1 / (2.41421356f * inverseAspect));
    return f4x4Mul(f4x4PerspectiveLH(horizontalFov, inverseAspect, .2f, 10000),
                   f4x4LookAtLH(toForge(cameraEye(camera)), toForge(camera.focus), make_float3(0, 1, 0)));
}
Vec3 project(const Camera& camera, Vec3 world)
{
    auto clip = f4x4Mulf4(cameraMatrix(camera), make_float4(world.x, world.y, world.z, 1));
    if (clip.w <= .2f)
        return { -10000, -10000, clip.w };
    return { (1 + clip.x / clip.w) * camera.width * .5f, (1 - clip.y / clip.w) * camera.height * .5f, clip.w };
}
} // namespace mooring
