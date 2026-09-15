#pragma once
#include "Simulation/Simulation.h"
#include "Common/Utilities/Interfaces/IMath.h"
namespace mooring
{
struct Camera
{
    Vec3     focus, target;
    float    distance = 34, targetDistance = 34;
    float    elevation = 32.6f, azimuth = 147.4f;
    unsigned width = 1440, height = 1000;
};
void updateCamera(Camera& camera, float dt);
void panCamera(Camera& camera, float dx, float dy);
void zoomCamera(Camera& camera, float amount);
Vec3 project(const Camera& camera, Vec3 world);
Vec3 deckToWorld(const Snapshot& snapshot, Vec3 deck);
Vec3 stationPosition(Station station);
f4x4 cameraMatrix(const Camera& camera);
Vec3 cameraEye(const Camera& camera);
} // namespace mooring
