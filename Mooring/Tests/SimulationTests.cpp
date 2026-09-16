#include "../Simulation/Simulation.h"
#include "../Simulation/Hydrodynamics.h"
#include "../Camera.h"
#include "../GraphicsMath.h"
#include "../Water/Ocean.h"
#include <cmath>
#include <cstdio>
#include "Common/Utilities/Interfaces/IMemory.h"
using namespace mooring;
static unsigned failures;
#define CHECK(condition)                                        \
    do                                                          \
    {                                                           \
        if (!(condition))                                       \
        {                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #condition); \
            ++failures;                                         \
        }                                                       \
    } while (0)
int main()
{
    initMemAlloc(nullptr);
    // Translating far from world zero must not clip the light volume. The
    // projection still measures physical path length along the sunlight ray.
    auto sunlight = f3Normalize(make_float3(-.6f, .7f, .4f));
    for (Vec3 center : { Vec3{ 0, 0, 0 }, Vec3{ 10000, 37, -5000 } })
    {
        auto matrix = lightProjection(center, sunlight, 1024, 256, 512);
        auto entry = f4x4Mulf4(matrix, float4(toForge(center), 1));
        auto interior = f4x4Mulf4(matrix, float4(f3Sub(toForge(center), f3MulScalar(sunlight, 3)), 1));
        CHECK(std::fabs(entry.z - .5f) < .00002f);
        CHECK(std::fabs((interior.z - entry.z) * 512 - 3) < .002f);
        CHECK(std::fabs(entry.x - interior.x) < .00002f && std::fabs(entry.y - interior.y) < .00002f);
    }
    World* world = createWorld();
    CHECK(canOperateHelm(world, 0));
    CHECK(!canOperateHelm(world, 1));
    enqueue(world, { CommandType::Throttle, 0, Station::Helm, 0, .4f });
    enqueue(world, { CommandType::Wheel, 0, Station::Helm, 0, .3f });
    step(world);
    enqueue(world, { CommandType::Attend, 0, Station::Bow });
    advance(world, 1, 0); // Queue travel while paused, with no physical progress.
    CHECK(snapshot(world, false).travelling);
    CHECK(!canOperateHelm(world, 0));
    CHECK(snapshot(world, false).travelRemaining > 5);
    Camera camera = {};
    camera.target = deckToWorld(snapshot(world, false), stationPosition(Station::Helm));
    camera.targetDistance = 9;
    for (unsigned frameIndex = 0; frameIndex < 120; ++frameIndex)
    {
        updateCamera(camera, FixedStep);
        panCamera(camera, 1, -1);
        zoomCamera(camera, .1f);
    }
    CHECK(!canOperateHelm(world, 0));
    CHECK(snapshot(world, false).travelling);
    CHECK(snapshot(world, false).travelRemaining > 5);
    enqueue(world, { CommandType::Throttle, 0, Station::Helm, 0, -1 });
    enqueue(world, { CommandType::Wheel, 0, Station::Helm, 0, -.5f });
    step(world);
    CHECK(std::fabs(snapshot(world, false).throttle[0] - .4f) < .0001f);
    CHECK(std::fabs(snapshot(world, false).rudder - .3f) < .0001f);
    CHECK(statistics(world).rejectedCommands == 2);
    for (unsigned stepIndex = 0; stepIndex < 600; ++stepIndex)
        step(world);
    CHECK(!snapshot(world, false).travelling);
    CHECK(!canOperateHelm(world, 0));
    enqueue(world, { CommandType::Attend, 1, Station::Helm });
    step(world);
    CHECK(!canOperateHelm(world, 1));
    setHelmSkill(world, 1, 2);
    enqueue(world, { CommandType::Attend, 1, Station::Helm });
    step(world);
    CHECK(!canOperateHelm(world, 1));
    for (unsigned stepIndex = 0; stepIndex < 100; ++stepIndex)
        step(world);
    CHECK(canOperateHelm(world, 1));
    CHECK(interactionTimeScale(Difficulty::Beginner, true) == 0);
    CHECK(interactionTimeScale(Difficulty::Intermediate, true) == .25f);
    CHECK(interactionTimeScale(Difficulty::Advanced, true) == 1);
    resetWorld(world);
    for (unsigned stepIndex = 0; stepIndex < 300; ++stepIndex)
        step(world);
    const auto warm = statistics(world).allocationCount;
    const auto bytes = statistics(world).liveAllocationBytes;
    for (unsigned stepIndex = 0; stepIndex < 36000; ++stepIndex)
        step(world);
    CHECK(statistics(world).allocationCount == warm);
    enqueue(world, { CommandType::Throttle, 0, Station::Helm, 0, .7f });
    enqueue(world, { CommandType::Wheel, 0, Station::Helm, 0, .25f });
    for (unsigned stepIndex = 0; stepIndex < 3600; ++stepIndex)
        step(world);
    CHECK(droppedWavePackets(worldOcean(world)) == 0);
    CHECK(statistics(world).allocationCount == warm);
    CHECK(std::fabs(snapshot(world, false).position.y) < 2);
    CHECK(statistics(world).liveAllocationBytes == bytes);
    CHECK(statistics(world).physicsErrors == 0);
    CHECK(std::isfinite(snapshot(world, false).position.y));
    CHECK(std::fabs(snapshot(world, false).position.y) < 2);
    for (unsigned resetIndex = 0; resetIndex < 20; ++resetIndex)
    {
        resetWorld(world);
        step(world);
    }
    CHECK(statistics(world).allocationCount == warm);
    for (unsigned commandIndex = 0; commandIndex < 64; ++commandIndex)
        CHECK(enqueue(world, { CommandType::Brake, 0, Station::Helm, 0, 1 }));
    CHECK(!enqueue(world, { CommandType::Brake, 0, Station::Helm, 0, 1 }));
    CHECK(statistics(world).commandOverflow == 1);
    step(world);
    destroyWorld(world);

    auto           boat = monohullLayout(true);
    PropellerState flow = {};
    updatePropeller(boat.propellers[0], flow, 1, 0, FixedStep);
    Vec3 behind = { 0, -.65f, -4.8f };
    CHECK(rudderWash(boat.propellers[0], flow, behind, 0).z == 0);
    for (unsigned stepIndex = 1; stepIndex < 240; ++stepIndex)
        updatePropeller(boat.propellers[0], flow, 1, stepIndex * FixedStep, FixedStep);
    CHECK(rudderWash(boat.propellers[0], flow, behind, 239 * FixedStep).z < 0);
    CHECK(rudderWash(boat.propellers[0], flow, boat.rudders[0].position, 239 * FixedStep).z == 0);
    auto ahead = behind;
    ahead.z = -2;
    CHECK(rudderWash(boat.propellers[0], flow, ahead, 239 * FixedStep).z == 0);
    flow.rpm = -1;
    CHECK(propellerWalk(boat.propellers[0], flow) < 0);
    CHECK(propellerThrust(boat.propellers[0], flow) < 0);
    CHECK(propellerImmersion(-1, .46f) == 0);
    CHECK(propellerImmersion(1, .46f) == 1);
    CHECK(std::fabs(propellerImmersion(0, .46f) - .5f) < .00001f);
    CHECK(rudderForce(boat.rudders[0], -.3f, { 0, 0, -3 }, 1).x < 0);
    CHECK(rudderForce(boat.rudders[0], -.3f, { 0, 0, 3 }, 1).x > 0);
    CHECK(windForce(boat.windAreas[0], { 4, 0, 0 }).x > 0);
    auto larger = boat.windAreas[0];
    larger.lateralArea *= 2;
    CHECK(std::fabs(windForce(larger, { 4, 0, 0 }).x / windForce(boat.windAreas[0], { 4, 0, 0 }).x - 2) < .001f);
    // Propeller walk is independent of rudder angle and works with no rudder wash.
    auto           single = monohullLayout();
    PropellerState moving = {};
    for (unsigned stepIndex = 0; stepIndex < 240; ++stepIndex)
        updatePropeller(single.propellers[0], moving, 1, stepIndex * FixedStep, FixedStep);
    float aheadWalk = propellerWalk(single.propellers[0], moving);
    CHECK(aheadWalk > 0);
    CHECK(rudderForce(single.rudders[0], 0, { 0, 0, -3 }, 1).x == 0);
    updatePropeller(single.propellers[0], moving, 0, 4, FixedStep);
    CHECK(rudderWash(single.propellers[0], moving, single.rudders[0].position, 4).z < 0); // Residual flow.
    auto aft = single.propellers[0];
    aft.position.z = -6;
    CHECK(rudderWash(aft, moving, single.rudders[0].position, 4).z == 0);
    for (unsigned stepIndex = 241; stepIndex < 800; ++stepIndex)
        updatePropeller(single.propellers[0], moving, -1, stepIndex * FixedStep, FixedStep);
    CHECK(rudderWash(single.propellers[0], moving, { 0, -.65f, -2 }, 799 * FixedStep).z > 0);
    CHECK(rudderWash(single.propellers[0], moving, single.rudders[0].position, 799 * FixedStep).z == 0);
    auto sideways = single.propellers[0];
    sideways.axis = { 1, 0, 0 };
    PropellerState sidewaysFlow = {};
    for (unsigned stepIndex = 0; stepIndex < 240; ++stepIndex)
        updatePropeller(sideways, sidewaysFlow, 1, stepIndex * FixedStep, FixedStep);
    CHECK(rudderWash(sideways, sidewaysFlow, { -1.5f, -.65f, -3.3f }, 239 * FixedStep).x < 0);
    auto* cat = createWorld(true);
    for (unsigned stepIndex = 0; stepIndex < 300; ++stepIndex)
        step(cat);
    CHECK(std::isfinite(snapshot(cat, false).position.y));
    enqueue(cat, { CommandType::Throttle, 0, Station::Helm, 0, .7f });
    for (unsigned stepIndex = 0; stepIndex < 180; ++stepIndex)
        step(cat);
    CHECK(snapshot(cat, false).rotation.y > 0);
    resetWorld(cat);
    enqueue(cat, { CommandType::Throttle, 0, Station::Helm, 1, .7f });
    for (unsigned stepIndex = 0; stepIndex < 180; ++stepIndex)
        step(cat);
    CHECK(snapshot(cat, false).rotation.y < 0);
    destroyWorld(cat);
    auto*    drift = createWorld();
    SeaState environment;
    environment.windWaveHeight = environment.swellHeight = 0;
    environment.windSpeed = 0;
    environment.current = { .5f, 0, 0 };
    setEnvironment(drift, environment);
    for (unsigned stepIndex = 0; stepIndex < 480; ++stepIndex)
        step(drift);
    CHECK(snapshot(drift, false).velocity.x > 0);
    environment.current = {};
    environment.windSpeed = 7;
    environment.windDirection = 0;
    setEnvironment(drift, environment);
    for (unsigned stepIndex = 0; stepIndex < 480; ++stepIndex)
        step(drift);
    CHECK(snapshot(drift, false).velocity.x > 0);
    CHECK(snapshot(drift, false).rotation.y > 0);
    CHECK(statistics(drift).steadyStepAllocations == 0);
    destroyWorld(drift);
    printf("%s: station access, travel, geometry, reverse flow, 10-minute run, resets and queue capacity (%u failures).\n",
           failures ? "FAIL" : "PASS", failures);
    fflush(stdout);
    exitMemAlloc();
    return failures ? 1 : 0;
}
