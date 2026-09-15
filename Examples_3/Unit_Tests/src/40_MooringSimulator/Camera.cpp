#include "Camera.h"
#include "GraphicsMath.h"
#include <algorithm>
#include <cmath>

namespace mooring {
Vec3 deckToWorld(const Snapshot& s,Vec3 p) {
    auto q=s.rotation;
    return fromForge(f3Add(toForge(s.position),quatRotateVector(make_quat(q.x,q.y,q.z,q.w),toForge(p))));
}
Vec3 stationPosition(Station station) {
    switch(station) {
        case Station::Helm:return {0,1.05f,-3.2f}; case Station::Cockpit:return {0,1.05f,-1.8f};
        case Station::Port:return {-1.45f,1.05f,-1.4f}; case Station::Starboard:return {1.45f,1.05f,-1.4f};
        case Station::Bow:return {0,1.05f,4.7f};
    } return {};
}
static float3 cameraOffset(const Camera& c) {
    float elevation=c.elevation*.01745329252f, azimuth=c.azimuth*.01745329252f;
    return {std::sin(azimuth)*std::cos(elevation),std::sin(elevation),std::cos(azimuth)*std::cos(elevation)};
}
Vec3 cameraEye(const Camera& c) { return fromForge(f3Add(toForge(c.focus),f3MulScalar(cameraOffset(c),c.distance))); }
void updateCamera(Camera& c,float dt) {
    MTRACY_ZONE("updateCamera");
    float t=1-std::exp(-7*std::min(dt,.1f));
    c.focus=fromForge(f3Lerp(toForge(c.focus),toForge(c.target),t)); c.distance+=(c.targetDistance-c.distance)*t;
}
void zoomCamera(Camera& c,float amount) { c.targetDistance=std::clamp(c.targetDistance*std::exp(-std::clamp(amount,-5.0f,5.0f)*.1f),9.0f,85.0f); }
void panCamera(Camera& c,float dx,float dy) {
    auto forward=f3Normalize(f3Sub(toForge(c.focus),toForge(cameraEye(c))));
    auto right=f3Normalize(f3Cross(make_float3(0,1,0),forward));
    auto up=f3Cross(forward,right);
    float scale=c.distance*.9f/std::max(1u,c.height);
    auto offset=f3Add(f3MulScalar(right,-dx*scale),f3MulScalar(f3Normalize(make_float3(up.x,0,up.z)),dy*scale));
    c.target=fromForge(f3Add(toForge(c.target),offset)); c.focus=fromForge(f3Add(toForge(c.focus),offset));
}
f4x4 cameraMatrix(const Camera& c) {
    float inverseAspect=float(c.height)/std::max(1u,c.width);
    float horizontalFov=2*std::atan(1/(2.41421356f*inverseAspect));
    return f4x4Mul(f4x4PerspectiveLH(horizontalFov,inverseAspect,.2f,10000),
        f4x4LookAtLH(toForge(cameraEye(c)),toForge(c.focus),make_float3(0,1,0)));
}
Vec3 project(const Camera& c,Vec3 world) {
    auto clip=f4x4Mulf4(cameraMatrix(c),make_float4(world.x,world.y,world.z,1));
    if(clip.w<=.2f) return {-10000,-10000,clip.w};
    return {(1+clip.x/clip.w)*c.width*.5f,(1-clip.y/clip.w)*c.height*.5f,clip.w};
}
}
