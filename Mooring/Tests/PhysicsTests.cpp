#include "../Simulation/Simulation.h"
#include "../Simulation/Hydrodynamics.h"
#include "../Water/Ocean.h"
#include <algorithm>
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
static void run(World* world, unsigned steps)
{
    for (unsigned stepIndex = 0; stepIndex < steps; ++stepIndex)
        step(world);
}
static float heading(const Snapshot& snapshot)
{
    auto rotation = snapshot.rotation;
    return std::atan2(2 * (rotation.x * rotation.z + rotation.w * rotation.y),
                      1 - 2 * (rotation.x * rotation.x + rotation.y * rotation.y)) *
           57.2957795f;
}
static float roll(const Snapshot& snapshot)
{
    auto rotation = snapshot.rotation;
    return std::atan2(2 * (rotation.w * rotation.z + rotation.x * rotation.y), 1 - 2 * (rotation.z * rotation.z + rotation.x * rotation.x));
}
static float firstWashArrival(const Propeller& propeller, float distance)
{
    PropellerState state = {};
    for (unsigned stepIndex = 0; stepIndex < 300; ++stepIndex)
    {
        float time = stepIndex * FixedStep;
        updatePropeller(propeller, state, 1, time, FixedStep);
        Vec3 point = propeller.position;
        point.z -= distance;
        if (rudderWash(propeller, state, point, time).z < -.001f)
            return time;
    }
    return 100;
}
int main(int argc, char** argv)
{
    initMemAlloc(nullptr);
    FILE* csv = argc > 1 ? std::fopen(argv[1], "w") : nullptr;
    if (argc > 1)
        CHECK(csv != nullptr);
    if (csv)
        std::fprintf(csv, "vessel,scenario,seconds,speed_kn,heading_deg,x_m,z_m,roll_deg\n");
    auto record = [&](const char* vessel, const char* scenario, Snapshot snapshot)
    {
        if (csv)
            std::fprintf(csv, "%s,%s,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f\n", vessel, scenario, snapshot.time, snapshot.speedKnots,
                         heading(snapshot), snapshot.position.x, snapshot.position.z, roll(snapshot) * 57.2957795f);
        printf("%s / %s: %.2f kn, heading %.2f deg\n", vessel, scenario, snapshot.speedKnots, heading(snapshot));
    };
    const auto single = monohullLayout(), mono = oceanis401Layout(), cat = catamaranLayout();
    for (auto layout : { mono, cat })
    {
        const float angle = .08726646f;
        CHECK(std::fabs(hydrostaticState(layout, 0, 0).volume * 1025 - layout.mass) < .03f);
        CHECK(equilibriumAtHeel(layout, angle).rightingArm > 0);
        for (int degree = 0; degree <= 180; degree += 5)
        {
            float heelAngle = degree * .01745329252f;
            auto  positive = equilibriumAtHeel(layout, heelAngle), negative = equilibriumAtHeel(layout, -heelAngle);
            CHECK(std::fabs(positive.volume * 1025 - layout.mass) < .05f);
            CHECK(std::fabs(positive.rightingArm + negative.rightingArm) < .0002f);
            CHECK(std::isfinite(positive.rightingArm));
        }
        auto* world = createWorld(layout);
        run(world, 600);
        CHECK(std::fabs(snapshot(world, false).position.y) < .01f);
        InitialConditions displaced;
        displaced.position.y = .3f;
        displaced.rotation = { 0, 0, std::sin(angle * .5f), std::cos(angle * .5f) };
        resetWorld(world, &displaced);
        run(world, 1800);
        CHECK(std::fabs(snapshot(world, false).position.y) < .03f);
        CHECK(std::fabs(roll(snapshot(world, false))) < .01f);
        record(layout.name, "heave_roll_decay", snapshot(world, false));
        resetWorld(world);
        for (unsigned engine = 0; engine < layout.propellerCount; ++engine)
            enqueue(world, { CommandType::Throttle, 0, Station::Helm, uint8_t(engine), .7f });
        run(world, 3600);
        auto ahead = snapshot(world, false);
        CHECK(ahead.velocity.z > 0);
        CHECK(ahead.speedKnots > 2 && ahead.speedKnots < 9);
        record(layout.name, "ahead_70_percent", ahead);
        float wakeCrest = 0, wakeTrough = 0;
        for (int column = -12; column <= 12; ++column)
            for (int row = -24; row <= 24; ++row)
            {
                float height = sampleWavePackets(worldOcean(world), ahead.position.x + column * .5f, ahead.position.z + row * .5f).height;
                wakeCrest = std::max(wakeCrest, height);
                wakeTrough = std::min(wakeTrough, height);
            }
        CHECK(wakeCrest > .08f && wakeCrest < 1);
        CHECK(wakeTrough < -.02f);
        printf("%s / wake: trough %.3f m, crest %.3f m\n", layout.name, wakeTrough, wakeCrest);
        CHECK(droppedWavePackets(worldOcean(world)) == 0);
        if (layout.hullSpacing > 0)
        {
            bool        portInward = false, starboardInward = false;
            const auto* ocean = worldOcean(world);
            for (unsigned packetIndex = 0; packetIndex < MaxWavePackets; ++packetIndex)
            {
                const auto& packet = oceanPackets(ocean)[packetIndex];
                if (packet.lifetime <= 0 || packet.age > 1.2f)
                    continue;
                // Reconstruct the source in this straight, still-water run.
                float sourceX = packet.freeX - packet.freeDirectionX * groupVelocity(packet.waveNumber, seaState(ocean).depth) * packet.age;
                portInward |= sourceX < ahead.position.x - layout.hullSpacing * .1f && packet.freeDirectionX > .2f;
                starboardInward |= sourceX > ahead.position.x + layout.hullSpacing * .1f && packet.freeDirectionX < -.2f;
            }
            CHECK(portInward && starboardInward);
        }
        for (unsigned engine = 0; engine < layout.propellerCount; ++engine)
            enqueue(world, { CommandType::Throttle, 0, Station::Helm, uint8_t(engine), 0 });
        run(world, 1200);
        auto coast = snapshot(world, false);
        CHECK(coast.speedKnots < ahead.speedKnots * .8f);
        record(layout.name, "neutral_coast_20_seconds", coast);
        InitialConditions approach;
        approach.velocity = { 0, 0, 1.5f };
        resetWorld(world, &approach);
        for (unsigned engine = 0; engine < layout.propellerCount; ++engine)
            enqueue(world, { CommandType::Throttle, 0, Station::Helm, uint8_t(engine), -.7f });
        run(world, 1800);
        auto astern = snapshot(world, false);
        CHECK(astern.velocity.z < 0);
        record(layout.name, "reverse_from_3_kn", astern);
        CHECK(droppedWavePackets(worldOcean(world)) == 0);
        CHECK(statistics(world).physicsErrors == 0);
        CHECK(statistics(world).steadyStepAllocations == 0);
        // A fast lateral approach must be caught by Jolt's continuous contact
        // detection even when the hull crosses much of a dock in one step.
        InitialConditions collision;
        collision.position = { 3, 0, 0 };
        collision.velocity = { 12, 0, 0 };
        resetWorld(world, &collision);
        run(world, 90);
        CHECK(statistics(world).dockContacts > 0);
        CHECK(snapshot(world, false).position.x < 11.2f);
        CHECK(statistics(world).physicsErrors == 0);
        SeaState storm;
        storm.windSpeed = 26;
        storm.gust = .35f;
        storm.windWaveHeight = 5;
        storm.swellHeight = 2.5f;
        storm.swellPeriod = 10;
        storm.fetch = 65000;
        storm.depth = 30;
        storm.choppiness = 1;
        setEnvironment(world, storm);
        InitialConditions offshore;
        offshore.position = { -40, 0, 0 };
        resetWorld(world, &offshore);
        float peakRoll = 0, peakHeave = 0;
        for (unsigned frame = 0; frame < 1200; ++frame)
        {
            step(world);
            auto motion = snapshot(world, false);
            peakRoll = std::max(peakRoll, std::fabs(roll(motion)));
            peakHeave = std::max(peakHeave, std::fabs(motion.position.y));
            CHECK(std::isfinite(motion.position.y) && std::isfinite(motion.speedKnots));
            CHECK(motion.speedKnots < 40 && std::fabs(motion.position.y) < 15);
        }
        CHECK(peakRoll > .02f);
        CHECK(peakHeave > .1f);
        CHECK(statistics(world).physicsErrors == 0);
        CHECK(statistics(world).steadyStepAllocations == 0);
        printf("%s / storm: peak roll %.2f deg, heave %.2f m\n", layout.name, peakRoll * 57.2957795f, peakHeave);
        destroyWorld(world);
    }
    CHECK(firstWashArrival(single.propellers[0], .75f) < firstWashArrival(single.propellers[0], 2.5f));
    PropellerState state = {};
    for (unsigned stepIndex = 0; stepIndex < 240; ++stepIndex)
        updatePropeller(cat.propellers[0], state, 1, stepIndex * FixedStep, FixedStep);
    CHECK(rudderWash(cat.propellers[0], state, cat.rudders[0].position, 239 * FixedStep).z == 0);
    for (unsigned stepIndex = 240; stepIndex < 720; ++stepIndex)
        updatePropeller(cat.propellers[0], state, -1, stepIndex * FixedStep, FixedStep);
    CHECK(rudderWash(cat.propellers[0], state, cat.rudders[0].position, 719 * FixedStep).z > 0);
    state = {};
    updatePropeller(single.propellers[0], state, 1, 0, 2, 0);
    CHECK(propellerThrust(single.propellers[0], state) == 0);
    CHECK(rudderForce(single.rudders[0], .3f, { 0, 0, -3 }, 0).x == 0);
    auto* turn = createWorld(cat);
    enqueue(turn, { CommandType::Throttle, 0, Station::Helm, 0, .7f });
    enqueue(turn, { CommandType::Throttle, 0, Station::Helm, 1, -.7f });
    run(turn, 600);
    CHECK(heading(snapshot(turn, false)) > 15);
    record(cat.name, "differential_thrust_10_seconds", snapshot(turn, false));
    destroyWorld(turn);
    auto*             helm = createWorld(single);
    InitialConditions motion;
    motion.velocity = { 0, 0, 2 };
    resetWorld(helm, &motion);
    enqueue(helm, { CommandType::Wheel, 0, Station::Helm, 0, .3f });
    enqueue(helm, { CommandType::Brake, 0, Station::Helm, 0, 0 });
    enqueue(helm, { CommandType::Attend, 0, Station::Bow });
    run(helm, 120);
    CHECK(std::fabs(snapshot(helm, false).rudder) < .29f);
    CHECK(!canOperateHelm(helm, 0));
    resetWorld(helm, &motion);
    enqueue(helm, { CommandType::Wheel, 0, Station::Helm, 0, .3f });
    enqueue(helm, { CommandType::Attend, 0, Station::Bow });
    run(helm, 120);
    CHECK(std::fabs(snapshot(helm, false).rudder - .3f) < 1e-6f);
    destroyWorld(helm);
    auto*    grounded = createWorld(mono);
    SeaState shoal;
    shoal.windSpeed = shoal.windWaveHeight = shoal.swellHeight = 0;
    shoal.current = {};
    shoal.depth = 1.7f;
    setEnvironment(grounded, shoal);
    InitialConditions above;
    above.position.y = .8f;
    resetWorld(grounded, &above);
    run(grounded, 600);
    CHECK(snapshot(grounded, false).position.y > .43f);
    CHECK(statistics(grounded).groundings > 0);
    CHECK(statistics(grounded).physicsErrors == 0);
    destroyWorld(grounded);
    auto shallow = orbitalAttenuation(.02f, 2, 2);
    CHECK(shallow.x > .99f);
    CHECK(shallow.y == 0);
    auto surface = orbitalAttenuation(2, 10, 0);
    CHECK(surface.x == 1 && surface.y == 1);
    auto deep = orbitalAttenuation(2, 100, 2);
    CHECK(std::fabs(deep.x - std::exp(-4)) < 1e-6f);
    if (csv)
        std::fclose(csv);
    printf(
        "%s: hydrostatic balance, restoring moment, decay, coast, reverse, differential thrust, wash travel and steering (%u failures).\n",
        failures ? "FAIL" : "PASS", failures);
    fflush(stdout);
    exitMemAlloc();
    return failures ? 1 : 0;
}
