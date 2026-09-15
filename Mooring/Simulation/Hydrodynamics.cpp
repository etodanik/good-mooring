#include "Hydrodynamics.h"
#include "PhysicsMath.h"
#include <cmath>
#include <algorithm>

namespace mooring
{
VesselLayout monohullLayout(bool twinRudders)
{
    VesselLayout v = {};
    v.name = "Generic single-rudder cruiser";
    v.lightMass = 7985;
    v.payload = 815;
    v.mass = v.lightMass + v.payload;
    v.length = 11.99f;
    v.waterlineLength = 11.70f;
    v.beam = 4.18f;
    v.draft = 2.17f;
    v.centerOfMass = { 0, -.35f, 0 };
    v.inertia = { v.mass * 3.0f * 3.0f, v.mass * 3.15f * 3.15f, v.mass * 1.25f * 1.25f };
    v.resistanceLinear = { 1120, 0, 96 };
    v.resistanceQuadratic = { 1680, 0, 152 };
    v.heaveDampingPerArea = 750;
    v.propellerCount = 1;
    v.rudderCount = twinRudders ? 2 : 1;
    v.propellers[0] = { { 0, -.75f, -3.3f }, .46f, 3000, .62f, 1.2f, 1, .025f, .16f };
    v.rudders[0] = { { twinRudders ? -1.35f : 0, -.75f, -4.8f }, .65f, 1.1f, .61f };
    v.rudders[1] = { { 1.35f, -.75f, -4.8f }, .65f, 1.1f, .61f };
    v.windAreaCount = 4;
    v.windAreas[0] = { { 0, .5f, 2.8f }, 5.4f, 1.3f, .95f };
    v.windAreas[1] = { { 0, .8f, -2.8f }, 4.2f, 1.9f, .95f };
    v.windAreas[2] = { { 0, 1.4f, .1f }, 4.8f, 3.6f, 1.1f };
    v.windAreas[3] = { { 0, 7.0f, .6f }, 3.2f, 1.8f, 1.15f };
    buildHullVolumes(v);
    return v;
}
VesselLayout oceanis401Layout()
{
    // BENETEAU: 11.99 m hull, 4.18 m beam, 7,985 kg light, 45 hp shaft,
    // twin rudders. Load, foil offsets, shaft angle and coefficients are tunable estimates.
    auto v = monohullLayout(true);
    v.name = "Oceanis 40.1";
    v.propellers[0].axis = { 0, .105f, .99447f }; // Thrust follows the inclined shaft.
    v.rudders[0].spanDirection = { -.208f, .97813f, 0 };
    v.rudders[1].spanDirection = { .208f, .97813f, 0 };
    return v;
}
VesselLayout catamaranLayout()
{
    // Lagoon 42, May 2021 inventory: 12.80 x 7.70 m, 12.1 t light,
    // 2 x Yanmar 4JH45 saildrives. The brochure locates props AFT of rudders.
    VesselLayout v = monohullLayout(true);
    v.name = "Lagoon 42";
    v.lightMass = 12100;
    v.payload = 1900;
    v.mass = v.lightMass + v.payload;
    v.length = 12.80f;
    v.waterlineLength = 12.5f;
    v.beam = 7.70f;
    v.draft = 1.25f;
    v.hullSpacing = 6.0f;
    v.centerOfMass = { 0, .45f, 0 };
    v.inertia = { v.mass * 3.3f * 3.3f, v.mass * 4.1f * 4.1f, v.mass * 3.0f * 3.0f };
    v.resistanceLinear = { 1450, 0, 130 };
    v.resistanceQuadratic = { 2100, 0, 210 };
    v.propellerCount = 2;
    for (unsigned i = 0; i < 2; ++i)
    {
        v.propellers[i] = v.propellers[0];
        v.propellers[i].position = { i ? 3.0f : -3.0f, -.85f, -4.8f };
        // Both left-hand propellers are an explicit provisional fit, not an
        // assumption that twin engines must counter-rotate. SD60 supports both.
        v.propellers[i].handedness = -1;
        v.propellers[i].walkAhead = .008f;
        v.propellers[i].walkAstern = .04f;
        v.rudders[i].position = { v.propellers[i].position.x, -.70f, -3.35f };
        v.rudders[i].area = .60f;
        v.rudders[i].span = 1.0f;
    }
    v.windAreas[0].lateralArea = 7.2f;
    v.windAreas[1].lateralArea = 6.5f;
    v.windAreas[2] = { { 0, 1.8f, .2f }, 11.5f, 13.0f, 1.1f };
    v.windAreas[3] = { { 0, 8.0f, .6f }, 4.3f, 2.8f, 1.15f };
    buildHullVolumes(v);
    return v;
}
float propellerImmersion(float centerDepth, float diameter)
{
    MTRACY_FINE_ZONE("propellerImmersion");
    if (diameter <= 0 || centerDepth <= -.5f * diameter)
        return 0;
    if (centerDepth >= .5f * diameter)
        return 1;
    float u = std::clamp(2 * centerDepth / diameter, -1.0f, 1.0f);
    return (std::acos(-u) + u * std::sqrt(std::max(0.0f, 1 - u * u))) / 3.14159265f;
}
void updatePropeller(const Propeller& p, PropellerState& state, float throttle, float time, float dt, float immersion,
                     float advanceVelocity)
{
    MTRACY_FINE_ZONE("updatePropeller");
    state.rpm += (std::clamp(throttle, -1.0f, 1.0f) - state.rpm) * (1 - std::exp(-dt / p.responseSeconds));
    state.immersion = std::clamp(immersion, 0.0f, 1.0f);
    float advance = advanceVelocity * std::copysign(1.0f, state.rpm) /
                    std::max(.25f, std::fabs(state.rpm) * p.maximumRevolutionsPerSecond * p.diameter);
    state.advanceFactor = std::clamp(1 - p.advanceLoss * advance, .1f, 1.25f);
    const float thrust = propellerThrust(p, state);
    const float area = 0.25f * 3.14159265f * p.diameter * p.diameter;
    const float velocity = -std::copysign(std::sqrt(2 * std::fabs(thrust) / (1025.0f * area)), thrust);
    state.history[state.next++ % 256] = { time, std::fabs(thrust) < 0.01f ? 0 : velocity };
}
float propellerThrust(const Propeller& p, const PropellerState& state)
{
    MTRACY_FINE_ZONE("propellerThrust");
    return p.maximumThrust * state.rpm * std::fabs(state.rpm) * (state.rpm < 0 ? p.asternRatio : 1) * state.immersion * state.advanceFactor;
}
float propellerWalk(const Propeller& p, const PropellerState& state)
{
    MTRACY_FINE_ZONE("propellerWalk");
    // Right-handed shaft: astern walks the stern to port. This is not rudder force.
    return std::fabs(propellerThrust(p, state)) * p.handedness * (state.rpm < 0 ? -p.walkAstern : p.walkAhead);
}
Vec3 rudderWash(const Propeller& p, const PropellerState& state, Vec3 position, float time)
{
    MTRACY_FINE_ZONE("rudderWash");
    const auto  delta = toJolt(position) - toJolt(p.position);
    const auto  axis = toJolt(p.axis).NormalizedOr(JPH::Vec3::sAxisZ());
    const float axial = delta.Dot(axis);
    const float radius = p.diameter * 0.5f + std::fabs(axial) * 0.12f;
    const float radial = std::sqrt(std::max(0.0f, delta.LengthSq() - axial * axial));
    if (radial >= radius)
        return {};
    float          bestError = 1e9f, flow = 0;
    const unsigned count = std::min(state.next, 256u);
    for (unsigned i = 0; i < count; ++i)
    {
        const FlowSample sample = state.history[(state.next - 1 - i) % 256];
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
    const float spreading = p.diameter * 0.5f / radius;
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
    const float pressure = 0.5f * 1025 * speed * speed * rudder.area * std::clamp(immersion, 0.0f, 1.0f) * reverseEfficiency;
    float       drag = .012f + lift * lift / (3.14159265f * aspect * .85f);
    return fromJolt(normal * (pressure * lift) + flow * (pressure * drag / speed));
}
Vec3 windForce(const WindArea& area, Vec3 wind)
{
    MTRACY_FINE_ZONE("windForce");
    return { 0.5f * 1.225f * area.coefficient * area.lateralArea * wind.x * std::fabs(wind.x), 0,
             0.5f * 1.225f * area.coefficient * area.frontalArea * wind.z * std::fabs(wind.z) };
}
} // namespace mooring
