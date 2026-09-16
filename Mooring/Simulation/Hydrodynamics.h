#pragma once
#include "Simulation.h"

namespace mooring
{
struct Propeller
{
    Vec3  position;
    float diameter, maximumThrust, asternRatio, responseSeconds, handedness, walkAhead, walkAstern;
    Vec3  axis = { 0, 0, 1 }; // Positive thrust axis, including shaft inclination.
    float maximumRevolutionsPerSecond = 18, advanceLoss = .7f;
};
struct Rudder
{
    Vec3  position;
    float area, span, maximumAngle;
    Vec3  chord = { 0, 0, 1 }, spanDirection = { 0, 1, 0 };
    float stockOffset = .05f, reverseEfficiency = .65f;
};
struct WindArea
{
    Vec3  position;
    float lateralArea, frontalArea, coefficient;
};
struct SteeringMechanism
{
    float frictionMoment = 6, damping = 40, inertia = 8;
    bool  backdrivable = true;
};
constexpr unsigned HullSections = 8, HullSectionVertices = 7;
constexpr unsigned HullVertices = HullSections * HullSectionVertices;
constexpr unsigned HullTriangles = (HullSections - 1) * HullSectionVertices * 2 + 2 * (HullSectionVertices - 2);
struct BuoyancyCell
{
    Vec3  vertices[4], center;
    float volume;
};
struct SubmergedVolume
{
    float volume;
    Vec3  center;
};
struct HydrostaticState
{
    float volume;
    Vec3  center;
    float rightingArm, height;
};
struct VesselLayout
{
    float             mass, length, beam, draft;
    float             lightMass, payload, hullSpacing, waterlineLength;
    Vec3              centerOfMass, inertia;
    Vec3              resistanceLinear, resistanceQuadratic;
    float             wakeResistanceFraction = .08f, wakePressureCoefficient = .6f;
    float             heaveDampingPerArea;
    float             hullDraft, hullVolume;
    uint16_t          cellCount;
    BuoyancyCell      cells[2 * (HullTriangles + 12)];
    const char*       name;
    uint8_t           propellerCount, rudderCount, windAreaCount;
    Propeller         propellers[2];
    Rudder            rudders[2];
    WindArea          windAreas[6];
    SteeringMechanism steering;
};
struct FlowSample
{
    float time, velocity;
};
constexpr unsigned PropellerHistorySize = 256;
struct PropellerState
{
    float      rpm;
    float      immersion = 1, advanceFactor = 1;
    uint32_t   next;
    FlowSample history[PropellerHistorySize];
};
VesselLayout     monohullLayout(bool twinRudders = false);
VesselLayout     catamaranLayout();
VesselLayout     oceanis401Layout();
Vec3             hullVertex(const VesselLayout& layout, unsigned hull, unsigned vertex);
void             hullTriangle(const VesselLayout&, unsigned hull, unsigned triangle, Vec3 out[3]);
void             buildHullVolumes(VesselLayout&);
SubmergedVolume  submergedTetrahedron(const BuoyancyCell&, Vec3 normal, float planeDistance);
HydrostaticState hydrostaticState(const VesselLayout&, float heel, float height);
HydrostaticState equilibriumAtHeel(const VesselLayout&, float heel);
void  updatePropeller(const Propeller& definition, PropellerState& state, float throttle, float time, float dt, float immersion = 1,
                      float advanceVelocity = 0);
float propellerImmersion(float centerDepth, float diameter);
float propellerThrust(const Propeller& definition, const PropellerState& state);
float propellerWalk(const Propeller& definition, const PropellerState& state);
Vec3  rudderWash(const Propeller& definition, const PropellerState& state, Vec3 rudderPosition, float time);
Vec3  rudderForce(const Rudder& definition, float angle, Vec3 relativeFlow, float immersion);
Vec3  windForce(const WindArea& area, Vec3 apparentWind);
} // namespace mooring
