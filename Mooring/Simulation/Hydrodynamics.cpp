#include "Hydrodynamics.h"
#include "PhysicsMath.h"
#include <cmath>
#include <algorithm>

namespace mooring
{
constexpr float AirDensity = 1.225f; // kg/m^3
VesselLayout    monohullLayout(bool twinRudders)
{
    VesselLayout layout = {};
    layout.name = "Generic single-rudder cruiser";
    layout.lightMass = 7985;
    layout.payload = 815;
    layout.mass = layout.lightMass + layout.payload;
    layout.length = 11.99f;
    layout.waterlineLength = 11.70f;
    layout.beam = 4.18f;
    layout.draft = 2.17f;
    layout.centerOfMass = { 0, -.35f, 0 };
    layout.inertia = { layout.mass * 3.0f * 3.0f, layout.mass * 3.15f * 3.15f, layout.mass * 1.25f * 1.25f };
    layout.resistanceLinear = { 1120, 0, 96 };
    layout.resistanceQuadratic = { 1680, 0, 152 };
    layout.heaveDampingPerArea = 750;
    layout.propellerCount = 1;
    layout.rudderCount = twinRudders ? 2 : 1;
    layout.propellers[0] = { { 0, -.75f, -3.3f }, .46f, 3000, .62f, 1.2f, 1, .025f, .16f };
    layout.rudders[0] = { { twinRudders ? -1.35f : 0, -.75f, -4.8f }, .65f, 1.1f, .61f };
    layout.rudders[1] = { { 1.35f, -.75f, -4.8f }, .65f, 1.1f, .61f };
    layout.windAreaCount = 4;
    layout.windAreas[0] = { { 0, .5f, 2.8f }, 5.4f, 1.3f, .95f };
    layout.windAreas[1] = { { 0, .8f, -2.8f }, 4.2f, 1.9f, .95f };
    layout.windAreas[2] = { { 0, 1.4f, .1f }, 4.8f, 3.6f, 1.1f };
    layout.windAreas[3] = { { 0, 7.0f, .6f }, 3.2f, 1.8f, 1.15f };
    buildHullVolumes(layout);
    return layout;
}
VesselLayout oceanis401Layout()
{
    // BENETEAU: 11.99 m hull, 4.18 m beam, 7,985 kg light, 45 hp shaft,
    // twin rudders. Load, foil offsets, shaft angle and coefficients are tunable estimates.
    auto layout = monohullLayout(true);
    layout.name = "Oceanis 40.1";
    layout.propellers[0].axis = { 0, .105f, .99447f }; // Thrust follows the inclined shaft.
    layout.rudders[0].spanDirection = { -.208f, .97813f, 0 };
    layout.rudders[1].spanDirection = { .208f, .97813f, 0 };
    return layout;
}
VesselLayout catamaranLayout()
{
    // Lagoon 42, May 2021 inventory: 12.80 x 7.70 m, 12.1 t light,
    // 2 x Yanmar 4JH45 saildrives. The brochure locates props AFT of rudders.
    VesselLayout layout = monohullLayout(true);
    layout.name = "Lagoon 42";
    layout.lightMass = 12100;
    layout.payload = 1900;
    layout.mass = layout.lightMass + layout.payload;
    layout.length = 12.80f;
    layout.waterlineLength = 12.5f;
    layout.beam = 7.70f;
    layout.draft = 1.25f;
    layout.hullSpacing = 6.0f;
    layout.centerOfMass = { 0, .45f, 0 };
    layout.inertia = { layout.mass * 3.3f * 3.3f, layout.mass * 4.1f * 4.1f, layout.mass * 3.0f * 3.0f };
    layout.resistanceLinear = { 1450, 0, 130 };
    layout.resistanceQuadratic = { 2100, 0, 210 };
    layout.propellerCount = 2;
    for (unsigned engineIndex = 0; engineIndex < 2; ++engineIndex)
    {
        layout.propellers[engineIndex] = layout.propellers[0];
        layout.propellers[engineIndex].position = { engineIndex ? 3.0f : -3.0f, -.85f, -4.8f };
        // Both left-hand propellers are an explicit provisional fit, not an
        // assumption that twin engines must counter-rotate. SD60 supports both.
        layout.propellers[engineIndex].handedness = -1;
        layout.propellers[engineIndex].walkAhead = .008f;
        layout.propellers[engineIndex].walkAstern = .04f;
        layout.rudders[engineIndex].position = { layout.propellers[engineIndex].position.x, -.70f, -3.35f };
        layout.rudders[engineIndex].area = .60f;
        layout.rudders[engineIndex].span = 1.0f;
    }
    layout.windAreas[0].lateralArea = 7.2f;
    layout.windAreas[1].lateralArea = 6.5f;
    layout.windAreas[2] = { { 0, 1.8f, .2f }, 11.5f, 13.0f, 1.1f };
    layout.windAreas[3] = { { 0, 8.0f, .6f }, 4.3f, 2.8f, 1.15f };
    buildHullVolumes(layout);
    return layout;
}
float propellerImmersion(float centerDepth, float diameter)
{
    MTRACY_FINE_ZONE("propellerImmersion");
    if (diameter <= 0 || centerDepth <= -.5f * diameter)
        return 0;
    if (centerDepth >= .5f * diameter)
        return 1;
    float normalizedDepth = std::clamp(2 * centerDepth / diameter, -1.0f, 1.0f);
    return (std::acos(-normalizedDepth) + normalizedDepth * std::sqrt(std::max(0.0f, 1 - normalizedDepth * normalizedDepth))) / 3.14159265f;
}
void updatePropeller(const Propeller& propeller, PropellerState& state, float throttle, float time, float dt, float immersion,
                     float advanceVelocity)
{
    MTRACY_FINE_ZONE("updatePropeller");
    state.rpm += (std::clamp(throttle, -1.0f, 1.0f) - state.rpm) * (1 - std::exp(-dt / propeller.responseSeconds));
    state.immersion = std::clamp(immersion, 0.0f, 1.0f);
    float advance = advanceVelocity * std::copysign(1.0f, state.rpm) /
                    std::max(.25f, std::fabs(state.rpm) * propeller.maximumRevolutionsPerSecond * propeller.diameter);
    state.advanceFactor = std::clamp(1 - propeller.advanceLoss * advance, .1f, 1.25f);
    const float thrust = propellerThrust(propeller, state);
    const float area = 0.25f * 3.14159265f * propeller.diameter * propeller.diameter;
    const float velocity = -std::copysign(std::sqrt(2 * std::fabs(thrust) / (WaterDensity * area)), thrust);
    state.history[state.next++ % PropellerHistorySize] = { time, std::fabs(thrust) < 0.01f ? 0 : velocity };
}
float propellerThrust(const Propeller& propeller, const PropellerState& state)
{
    MTRACY_FINE_ZONE("propellerThrust");
    return propeller.maximumThrust * state.rpm * std::fabs(state.rpm) * (state.rpm < 0 ? propeller.asternRatio : 1) * state.immersion *
           state.advanceFactor;
}
float propellerWalk(const Propeller& propeller, const PropellerState& state)
{
    MTRACY_FINE_ZONE("propellerWalk");
    // Right-handed shaft: astern walks the stern to port. This is not rudder force.
    return std::fabs(propellerThrust(propeller, state)) * propeller.handedness *
           (state.rpm < 0 ? -propeller.walkAstern : propeller.walkAhead);
}
Vec3 rudderWash(const Propeller& propeller, const PropellerState& state, Vec3 position, float time)
{
    MTRACY_FINE_ZONE("rudderWash");
    const auto  delta = toJolt(position) - toJolt(propeller.position);
    const auto  axis = toJolt(propeller.axis).NormalizedOr(JPH::Vec3::sAxisZ());
    const float axial = delta.Dot(axis);
    const float radius = propeller.diameter * 0.5f + std::fabs(axial) * 0.12f;
    const float radial = std::sqrt(std::max(0.0f, delta.LengthSq() - axial * axial));
    if (radial >= radius)
        return {};
    float          bestError = 1e9f, flow = 0;
    const unsigned count = std::min(state.next, PropellerHistorySize);
    for (unsigned sampleIndex = 0; sampleIndex < count; ++sampleIndex)
    {
        const FlowSample sample = state.history[(state.next - 1 - sampleIndex) % PropellerHistorySize];
        if (sample.velocity * axial <= 0 || std::fabs(sample.velocity) < 0.05f)
            continue;
        const float travel = axial / sample.velocity;
        const float error = std::fabs(time - sample.time - travel);
        if (error < bestError && time - sample.time >= travel - FixedStep)
        {
            bestError = error;
            flow = sample.velocity;
        }
    }
    if (bestError > 2.5f * FixedStep)
        return {};
    const float spreading = propeller.diameter * 0.5f / radius;
    return fromJolt(axis * (flow * spreading * (1 - radial / radius)));
}
Vec3 rudderForce(const Rudder& rudder, float angle, Vec3 water, float immersion)
{
    MTRACY_FINE_ZONE("rudderForce");
    angle = std::clamp(angle, -rudder.maximumAngle, rudder.maximumAngle);
    const auto  span = toJolt(rudder.spanDirection).NormalizedOr(JPH::Vec3::sAxisY());
    const auto  initial = toJolt(rudder.chord).NormalizedOr(JPH::Vec3::sAxisZ());
    const auto  chord = JPH::Quat::sRotation(span, angle) * initial;
    const auto  normal = span.Cross(chord).NormalizedOr(JPH::Vec3::sAxisX());
    const auto  flow = toJolt(water) - span * toJolt(water).Dot(span);
    const float speed = flow.Length();
    if (speed < 0.001f)
        return {};
    const float normalSpeed = flow.Dot(normal);
    // Water on the opposite face reverses the load naturally. Stall is bounded.
    float       aspect = rudder.span * rudder.span / rudder.area;
    float       liftSlope = 6.2831853f * aspect / (2 + std::sqrt(4 + aspect * aspect));
    const float lift = std::clamp(normalSpeed / speed * liftSlope, -1.15f, 1.15f);
    const float reverseEfficiency = flow.Dot(initial) > 0 ? rudder.reverseEfficiency : 1.0f;
    const float pressure = 0.5f * WaterDensity * speed * speed * rudder.area * std::clamp(immersion, 0.0f, 1.0f) * reverseEfficiency;
    float       drag = .012f + lift * lift / (3.14159265f * aspect * .85f);
    return fromJolt(normal * (pressure * lift) + flow * (pressure * drag / speed));
}
Vec3 windForce(const WindArea& area, Vec3 wind)
{
    MTRACY_FINE_ZONE("windForce");
    return { 0.5f * AirDensity * area.coefficient * area.lateralArea * wind.x * std::fabs(wind.x), 0,
             0.5f * AirDensity * area.coefficient * area.frontalArea * wind.z * std::fabs(wind.z) };
}
} // namespace mooring
