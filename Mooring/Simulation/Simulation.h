#pragma once
#include <cstdint>

namespace mooring
{
constexpr unsigned MaxCrew = 5;
constexpr float    FixedStep = 1.0f / 60.0f;
constexpr float    Gravity = 9.81f;        // m/s^2
constexpr float    WaterDensity = 1025.0f; // kg/m^3

struct Vec3
{
    float x, y, z;
};
struct Rotation
{
    float x, y, z, w;
};
enum class Station : uint8_t
{
    Helm = 0,
    Cockpit = 1,
    Port = 2,
    Starboard = 3,
    Bow = 6
};
enum class Difficulty : uint8_t
{
    Beginner,
    Intermediate,
    Advanced
};
enum class CommandType : uint8_t
{
    Attend,
    Wheel,
    Throttle,
    Brake
};
struct Command
{
    CommandType type;
    uint8_t     actor;
    Station     station;
    uint8_t     engine;
    float       value;
};
struct Actor
{
    Vec3    deckPosition;
    uint8_t node, destination, path[8], pathCount, pathCursor;
    uint8_t helmSkill;
    bool    travelling;
};
struct Helm
{
    uint64_t occupant;
    float    throttle[2], rudder, rudderVelocity;
    bool     brake;
};
struct PhysicsBody
{
    uint32_t handle;
};
struct Snapshot
{
    Vec3     position, velocity, angularVelocity, skipperDeck;
    Rotation rotation;
    float    throttle[2], rpm[2], rudder, time, speedKnots, travelRemaining;
    uint8_t  destination;
    bool     travelling, atHelm, brake;
};
struct Stats
{
    uint64_t steps, rejectedCommands, commandOverflow, physicsErrors;
    uint32_t dockContacts, groundings;
    float    maximumContactSpeed;
    uint64_t allocationCount;
    uint64_t steadyStepAllocations;
    uint32_t liveAllocationBytes;
};
struct World;
struct Ocean;
struct SeaState;
struct VesselLayout;
struct InitialConditions
{
    Vec3     position = {}, velocity = {}, angularVelocity = {};
    Rotation rotation = { 0, 0, 0, 1 };
};
World*              createWorld(bool catamaran = false);
World*              createWorld(const VesselLayout& layout);
void                destroyWorld(World* world);
void                resetWorld(World* world, const InitialConditions* conditions = nullptr);
bool                enqueue(World* world, Command command);
void                advance(World* world, float seconds, float timeScale);
void                step(World* world);
Snapshot            snapshot(const World* world, bool interpolate = true);
const Stats&        statistics(const World* world);
const char*         lastCommandResult(const World* world);
const char*         stationName(Station station);
bool                canOperateHelm(const World* world, uint8_t actor);
void                setHelmSkill(World* world, uint8_t actor, uint8_t skill);
float               interactionTimeScale(Difficulty difficulty, bool inStationInteraction);
Ocean*              worldOcean(World* world);
const VesselLayout& worldLayout(const World* world);
void                setEnvironment(World* world, const SeaState& environment);
} // namespace mooring
