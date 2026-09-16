#include "Simulation.h"
#include "Hydrodynamics.h"
#include "PhysicsMath.h"
#include <Jolt/Physics/Collision/ContactListener.h>
#include "../Water/Ocean.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include "Common/Game/ThirdParty/OpenSource/flecs/flecs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common/Utilities/Interfaces/ILog.h"
#include "Common/Utilities/Interfaces/IMemory.h"

#ifdef TRACY_ENABLE
void mooringTracyInstallECS();
#endif

namespace mooring
{
// Framework bridges are the only simulation types with virtual interfaces.
struct BroadPhase final: JPH::BroadPhaseLayerInterface
{
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() ? "Vessels" : "Static"; }
#endif
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(layer); }
};
struct ObjectFilter final: JPH::ObjectLayerPairFilter
{
    bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::ObjectLayer otherLayer) const override { return objectLayer || otherLayer; }
};
struct BroadFilter final: JPH::ObjectVsBroadPhaseLayerFilter
{
    bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer otherLayer) const override
    {
        return objectLayer || otherLayer.GetValue();
    }
};
struct ContactMonitor final: JPH::ContactListener
{
    JPH::BodyID dock, seabed;
    bool        dockNow = false, groundNow = false, dockPreviously = false, groundPreviously = false;
    float       speed = 0;
    void        record(const JPH::Body& firstBody, const JPH::Body& secondBody, const JPH::ContactManifold& manifold)
    {
        dockNow |= firstBody.GetID() == dock || secondBody.GetID() == dock;
        groundNow |= firstBody.GetID() == seabed || secondBody.GetID() == seabed;
        auto point = manifold.GetWorldSpaceContactPointOn1(0);
        speed = std::max(
            speed, std::fabs((firstBody.GetPointVelocity(point) - secondBody.GetPointVelocity(point)).Dot(manifold.mWorldSpaceNormal)));
    }
    void OnContactAdded(const JPH::Body& firstBody, const JPH::Body& secondBody, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override
    {
        record(firstBody, secondBody, manifold);
    }
    void OnContactPersisted(const JPH::Body& firstBody, const JPH::Body& secondBody, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings&) override
    {
        record(firstBody, secondBody, manifold);
    }
};
#ifdef TRACY_ENABLE
// Keep Jolt's allocator and LIFO semantics; expose temporary usage separately
// from its backing block, which is already reported by the Forge allocator.
struct ProfiledTempAllocator final: JPH::TempAllocator
{
    JPH::TempAllocatorImpl backing;
    explicit ProfiledTempAllocator(size_t size): backing(size) {}
    void* Allocate(JPH::uint size) override
    {
        void* memory = backing.Allocate(size);
        MTRACY_ALLOC(memory, size, "Jolt scratch (suballocated)");
        return memory;
    }
    void Free(void* memory, JPH::uint size) override
    {
        MTRACY_FREE(memory, "Jolt scratch (suballocated)");
        backing.Free(memory, size);
    }
};
#else
using ProfiledTempAllocator = JPH::TempAllocatorImpl;
#endif
constexpr unsigned CommandCapacity = 64;
struct World
{
    BroadPhase                   broad;
    ObjectFilter                 objects;
    BroadFilter                  filter;
    JPH::PhysicsSystem           physics;
    ContactMonitor               contacts;
    JPH::BodyID                  dock, seabed;
    ProfiledTempAllocator        temp{ 8 * TF_MB };
    JPH::JobSystemSingleThreaded jobs{ 512 };
    ecs_world_t*                 ecs;
    ecs_entity_t                 actorType, helmType, bodyType, travelSystem, vesselSystem;
    ecs_entity_t                 vessel, crew[MaxCrew];
    VesselLayout                 layout;
    Ocean*                       ocean;
    PropellerState               propulsion[2];
    Command                      commands[CommandCapacity];
    unsigned                     commandRead, commandWrite;
    float                        accumulator, time;
    float                        wakeEnergy, wakeTime;
    Snapshot                     previous, current;
    Stats                        stats;
    const char*                  result;
};
static unsigned           gWorldCount;
static constexpr Vec3     Deck[] = { { 0, 1.05f, -3.2f },     { 0, 1.05f, -1.8f },    { -1.45f, 1.05f, -1.4f }, { 1.45f, 1.05f, -1.4f },
                                     { -1.45f, 1.05f, 2.8f }, { 1.45f, 1.05f, 2.8f }, { 0, 1.05f, 4.7f } };
static constexpr unsigned DeckNodeCount = TF_ARRAY_COUNT(Deck);
static constexpr uint8_t  NoDeckNode = UINT8_MAX;
static constexpr uint8_t  Links[DeckNodeCount] = { 2, 1 | 4 | 8, 2 | 16, 2 | 32, 4 | 64, 8 | 64, 16 | 32 };
static Actor*             actor(World* world, unsigned crewIndex)
{
    return static_cast<Actor*>(ecs_get_mut_id(world->ecs, world->crew[crewIndex], world->actorType));
}
static const Actor* actor(const World* world, unsigned crewIndex)
{
    return static_cast<const Actor*>(ecs_get_id(world->ecs, world->crew[crewIndex], world->actorType));
}
static Helm*       helm(World* world) { return static_cast<Helm*>(ecs_get_mut_id(world->ecs, world->vessel, world->helmType)); }
static const Helm* helm(const World* world) { return static_cast<const Helm*>(ecs_get_id(world->ecs, world->vessel, world->helmType)); }
static JPH::BodyID bodyID(const World* world)
{
    return JPH::BodyID(static_cast<const PhysicsBody*>(ecs_get_id(world->ecs, world->vessel, world->bodyType))->handle);
}
static ecs_entity_t component(ecs_world_t* ecs, const char* name, size_t size, size_t alignment)
{
    ecs_entity_desc_t entity = {};
    entity.name = name;
    ecs_component_desc_t desc = {};
    desc.entity = ecs_entity_init(ecs, &entity);
    desc.type.size = static_cast<ecs_size_t>(size);
    desc.type.alignment = static_cast<ecs_size_t>(alignment);
    return ecs_component_init(ecs, &desc);
}
static void setupAllocators()
{
    MTRACY_ZONE("setupAllocators");
    JPH::Allocate = [](size_t byteCount) -> void* { return tf_malloc(byteCount); };
    JPH::Reallocate = [](void* memory, size_t, size_t byteCount) -> void* { return tf_realloc(memory, byteCount); };
    JPH::Free = [](void* memory) { tf_free(memory); };
    JPH::AlignedAllocate = [](size_t byteCount, size_t alignment) -> void* { return tf_memalign(alignment, byteCount); };
    JPH::AlignedFree = [](void* memory) { tf_free(memory); };
    JPH::Factory::sInstance = tf_new(JPH::Factory);
    JPH::RegisterTypes();
    // The bundled flecs already routes allocation, clocks, and profiling
    // through Forge. Its adapter must be installed before creating a world.
    initEntityComponentSystem();
#ifdef TRACY_ENABLE
    mooringTracyInstallECS();
#endif
}
bool canOperateHelm(const World* world, uint8_t crewIndex)
{
    if (crewIndex >= MaxCrew)
        return false;
    const Actor& crew = *actor(world, crewIndex);
    return !crew.travelling && crew.node == 0 && crew.helmSkill > 0 && helm(world)->occupant == world->crew[crewIndex];
}
static bool route(Actor& actorState, uint8_t destination)
{
    if (destination >= DeckNodeCount || actorState.travelling)
        return false;
    uint8_t queue[DeckNodeCount] = { actorState.node }, parent[DeckNodeCount];
    memset(parent, NoDeckNode, sizeof parent);
    parent[actorState.node] = actorState.node;
    unsigned first = 0, end = 1;
    while (first < end && parent[destination] == NoDeckNode)
    {
        uint8_t node = queue[first++];
        for (uint8_t neighbor = 0; neighbor < DeckNodeCount; ++neighbor)
            if ((Links[node] & (1 << neighbor)) && parent[neighbor] == NoDeckNode)
            {
                parent[neighbor] = node;
                queue[end++] = neighbor;
            }
    }
    if (parent[destination] == NoDeckNode)
        return false;
    uint8_t reverse[DeckNodeCount], count = 0;
    for (uint8_t pathNode = destination; pathNode != actorState.node; pathNode = parent[pathNode])
        reverse[count++] = pathNode;
    actorState.pathCount = count;
    actorState.pathCursor = 0;
    actorState.destination = destination;
    for (unsigned pathIndex = 0; pathIndex < count; ++pathIndex)
        actorState.path[pathIndex] = reverse[count - 1 - pathIndex];
    actorState.travelling = count > 0;
    return true;
}
static void processCommands(World* world)
{
    MTRACY_ZONE("processCommands");
    while (world->commandRead != world->commandWrite)
    {
        const Command command = world->commands[world->commandRead++ % CommandCapacity];
        if (command.actor >= MaxCrew || !std::isfinite(command.value))
        {
            ++world->stats.rejectedCommands;
            continue;
        }
        Actor& actorState = *actor(world, command.actor);
        Helm&  helmState = *helm(world);
        bool   accepted = false;
        if (command.type == CommandType::Attend)
        {
            const uint8_t target = static_cast<uint8_t>(command.station);
            if (target == 0 && actorState.helmSkill == 0)
                world->result = "Helm training required.";
            else if (target == 0 && helmState.occupant && helmState.occupant != world->crew[command.actor])
                world->result = "The helm is occupied.";
            else if (route(actorState, target))
            {
                if (actorState.travelling && helmState.occupant == world->crew[command.actor])
                    helmState.occupant = 0;
                if (!actorState.travelling && target == 0)
                    helmState.occupant = world->crew[command.actor];
                world->result = actorState.travelling ? "Travelling to the station." : "Already at the station.";
                accepted = true;
            }
            else
                world->result = "Finish travelling before choosing another station.";
        }
        else if (!canOperateHelm(world, command.actor))
            world->result = "Reach and occupy the helm before using its controls.";
        else
        {
            if (command.type == CommandType::Wheel)
            {
                helmState.rudder = std::clamp(command.value, -0.61f, 0.61f);
                helmState.rudderVelocity = 0;
                accepted = true;
            }
            if (command.type == CommandType::Throttle && command.engine < world->layout.propellerCount)
            {
                helmState.throttle[command.engine] = std::clamp(command.value, -1.0f, 1.0f);
                accepted = true;
            }
            if (command.type == CommandType::Brake)
            {
                helmState.brake = command.value != 0;
                accepted = true;
            }
            world->result = accepted ? "Helm control applied." : "Invalid control.";
        }
        if (!accepted)
            ++world->stats.rejectedCommands;
    }
}
static void travelSystem(ecs_iter_t* it)
{
    MTRACY_ZONE("travelSystem");
    auto* world = static_cast<World*>(it->ctx);
    auto* actors = static_cast<Actor*>(ecs_field_w_size(it, sizeof(Actor), 0));
    for (int actorIndex = 0; actorIndex < it->count; ++actorIndex)
    {
        Actor& actorState = actors[actorIndex];
        if (!actorState.travelling)
            continue;
        const Vec3  goal = Deck[actorState.path[actorState.pathCursor]];
        JPH::Vec3   delta = toJolt(goal) - toJolt(actorState.deckPosition);
        float       distance = delta.Length();
        const float stride = 1.25f * it->delta_time;
        if (distance <= stride)
        {
            actorState.deckPosition = goal;
            actorState.node = actorState.path[actorState.pathCursor++];
            if (actorState.pathCursor == actorState.pathCount)
            {
                actorState.travelling = false;
                if (actorState.node == 0 && actorState.helmSkill && !helm(world)->occupant)
                    helm(world)->occupant = it->entities[actorIndex];
            }
        }
        else
            actorState.deckPosition = fromJolt(toJolt(actorState.deckPosition) + delta * (stride / distance));
    }
}
static void vesselSystem(ecs_iter_t* it)
{
    MTRACY_ZONE("vesselSystem");
    auto*               world = static_cast<World*>(it->ctx);
    const auto*         bodies = static_cast<const PhysicsBody*>(ecs_field_w_size(it, sizeof(PhysicsBody), 0));
    auto*               helms = static_cast<Helm*>(ecs_field_w_size(it, sizeof(Helm), 1));
    auto&               bodyInterface = world->physics.GetBodyInterface();
    const VesselLayout& layout = world->layout;
    for (int vesselIndex = 0; vesselIndex < it->count; ++vesselIndex)
    {
        JPH::BodyID id(bodies[vesselIndex].handle);
        auto        position = bodyInterface.GetPosition(id);
        auto        rotation = bodyInterface.GetRotation(id);
        auto        inverse = rotation.Conjugated();
        auto        velocity = bodyInterface.GetLinearVelocity(id), omega = bodyInterface.GetAngularVelocity(id);
        auto        apply = [&](JPH::Vec3 force, Vec3 local) { bodyInterface.AddForce(id, force, position + rotation * toJolt(local)); };
        auto        waterAt = [&](Vec3 local)
        {
            auto worldPoint = position + rotation * toJolt(local);
            // Empirical resistance already includes self-wave drag. Applying
            // our emitted wake again would make handling depend on its sampling.
            return sampleOcean(world->ocean, worldPoint.GetX(), worldPoint.GetZ(),
                               std::max(0.0f, seaState(world->ocean).level - worldPoint.GetY()), bodies[vesselIndex].handle);
        };
        auto localFlow = [&](Vec3 local)
        {
            auto water = waterAt(local);
            return inverse * (toJolt(water.velocity) - velocity - omega.Cross(rotation * (toJolt(local) - toJolt(layout.centerOfMass))));
        };
        const float referenceVolume = layout.mass / WaterDensity;
        for (unsigned cellIndex = 0; cellIndex < layout.cellCount; ++cellIndex)
        {
            const auto& cell = layout.cells[cellIndex];
            auto        samplePoint = position + rotation * toJolt(cell.center);
            auto        water = waterAt(cell.center);
            auto        normal = inverse * toJolt(water.normal);
            float       plane = normal.Dot(toJolt(cell.center)) + (water.height - samplePoint.GetY()) * water.normal.y;
            auto        wet = submergedTetrahedron(cell, fromJolt(normal), plane);
            if (wet.volume <= 1e-8f)
                continue;
            const auto point = position + rotation * toJolt(wet.center);
            auto       offset = point - (position + rotation * toJolt(layout.centerOfMass));
            auto       pointVelocity = velocity + omega.Cross(offset);
            float      weight = wet.volume / referenceVolume;
            float      damping =
                (water.velocity.y - pointVelocity.GetY()) * layout.heaveDampingPerArea * referenceVolume / layout.hullDraft * weight;
            // Froude-Krylov excitation: the pressure gradient follows the
            // material acceleration of the shared water solution at this depth.
            auto pressure = (JPH::Vec3(0, Gravity, 0) + toJolt(water.acceleration)) * (WaterDensity * wet.volume);
            bodyInterface.AddForce(id, pressure + JPH::Vec3(0, damping, 0), point);
            auto        flow = inverse * (toJolt(water.velocity) - pointVelocity);
            const auto& linear = layout.resistanceLinear;
            const auto& quadratic = layout.resistanceQuadratic;
            auto        drag = JPH::Vec3(flow.GetX() * (linear.x + quadratic.x * std::fabs(flow.GetX())), 0,
                                         flow.GetZ() * (linear.z + quadratic.z * std::fabs(flow.GetZ()))) *
                        weight;
            bodyInterface.AddForce(id, rotation * drag, point);
            world->wakeEnergy += std::max(0.0f, drag.Dot(flow)) * it->delta_time * layout.wakeResistanceFraction;
        }
        Helm&       controls = helms[vesselIndex];
        const auto& environment = seaState(world->ocean);
        float       gust = 1 + environment.gust * (.6f * std::sin(world->time * .53f) + .4f * std::sin(world->time * 1.37f));
        JPH::Vec3   wind(environment.windSpeed * gust * std::cos(environment.windDirection), 0,
                         environment.windSpeed * gust * std::sin(environment.windDirection));
        for (unsigned windAreaIndex = 0; windAreaIndex < layout.windAreaCount; ++windAreaIndex)
        {
            const auto& area = layout.windAreas[windAreaIndex];
            auto        pointVelocity = velocity + omega.Cross(rotation * (toJolt(area.position) - toJolt(layout.centerOfMass)));
            apply(rotation * toJolt(windForce(area, fromJolt(inverse * (wind - pointVelocity)))), area.position);
        }
        for (unsigned propellerIndex = 0; propellerIndex < layout.propellerCount; ++propellerIndex)
        {
            const Propeller& propeller = layout.propellers[propellerIndex];
            auto             axis = toJolt(propeller.axis).Normalized();
            auto             location = position + rotation * toJolt(propeller.position);
            auto             water = waterAt(propeller.position);
            float            immersed = propellerImmersion(water.height - location.GetY(), propeller.diameter);
            updatePropeller(propeller, world->propulsion[propellerIndex], controls.throttle[propellerIndex], world->time, it->delta_time,
                            immersed, -localFlow(propeller.position).Dot(axis));
            auto transverse = JPH::Vec3::sAxisY().Cross(axis).Normalized();
            apply(rotation * (axis * propellerThrust(propeller, world->propulsion[propellerIndex]) +
                              transverse * propellerWalk(propeller, world->propulsion[propellerIndex])),
                  propeller.position);
        }
        float stockMoment = 0;
        for (unsigned rudderIndex = 0; rudderIndex < layout.rudderCount; ++rudderIndex)
        {
            const Rudder& rudder = layout.rudders[rudderIndex];
            auto          flow = localFlow(rudder.position);
            for (unsigned propellerIndex = 0; propellerIndex < layout.propellerCount; ++propellerIndex)
                flow +=
                    toJolt(rudderWash(layout.propellers[propellerIndex], world->propulsion[propellerIndex], rudder.position, world->time));
            // Positive wheel requests turn the bow to starboard (+X).
            float waterHeight = waterAt(rudder.position).height;
            float rudderY = (position + rotation * toJolt(rudder.position)).GetY();
            float verticalSpan = std::max(.1f, std::fabs((rotation * toJolt(rudder.spanDirection)).GetY()) * rudder.span);
            float immersion = std::clamp((waterHeight - rudderY) / verticalSpan + .5f, 0.0f, 1.0f);
            auto  force = rudderForce(rudder, -controls.rudder, fromJolt(flow), immersion);
            apply(rotation * toJolt(force), rudder.position);
            stockMoment += force.x * rudder.stockOffset;
        }
        if (!controls.occupant && !controls.brake && layout.steering.backdrivable)
        {
            const auto& steering = layout.steering;
            if (std::fabs(stockMoment) <= steering.frictionMoment && std::fabs(controls.rudderVelocity) < .001f)
                controls.rudderVelocity = 0;
            else
            {
                float friction = std::copysign(steering.frictionMoment,
                                               std::fabs(controls.rudderVelocity) > .001f ? controls.rudderVelocity : stockMoment);
                controls.rudderVelocity =
                    std::clamp(controls.rudderVelocity + (stockMoment - friction - steering.damping * controls.rudderVelocity) /
                                                             steering.inertia * it->delta_time,
                               -.8f, .8f);
                controls.rudder = std::clamp(controls.rudder + controls.rudderVelocity * it->delta_time, -.61f, .61f);
            }
        }
        else
            controls.rudderVelocity = 0;
        auto localOmega = inverse * omega;
        bodyInterface.AddTorque(id, rotation * (-localOmega * JPH::Vec3(3500, 1100, 3500)));
        world->wakeTime += it->delta_time;
        auto throughWater = velocity - toJolt(environment.current);
        auto hullForward = rotation * JPH::Vec3::sAxisZ();
        hullForward.SetY(0);
        hullForward = hullForward.Normalized();
        float    surge = throughWater.Dot(hullForward);
        HullWake attached{};
        attached.position = fromJolt(position);
        attached.velocity = fromJolt(velocity);
        attached.forward = fromJolt(hullForward * (surge < 0 ? -1.0f : 1.0f));
        attached.length = layout.length;
        attached.width = layout.beam - layout.hullSpacing;
        attached.spacing = layout.hullSpacing;
        attached.amplitude = std::min(.4f, layout.wakePressureCoefficient * surge * surge / (2 * Gravity));
        attached.sourceBody = bodies[vesselIndex].handle;
        setHullWake(world->ocean, unsigned(vesselIndex), attached);
        if (world->wakeTime >= 1.2f)
        {
            auto current = toJolt(seaState(world->ocean).current);
            auto relative = velocity - current;
            relative.SetY(0);
            float speed = relative.Length();
            if (speed > .35f && world->wakeEnergy > 0)
            {
                auto            forward = relative.Normalized();
                auto            right = JPH::Vec3::sAxisY().Cross(forward).Normalized();
                // Quadrature across transverse, cusp and divergent directions.
                // Each hull emits both sides, allowing interference in the
                // tunnel. The weights sum to one resistance-energy budget.
                constexpr float branches[][3] = { { 1, 0, .14f },
                                                  { .9238795f, .3826834f, .10f },
                                                  { .9238795f, -.3826834f, .10f },
                                                  { .81649658f, .57735027f, .17f },
                                                  { .81649658f, -.57735027f, .17f },
                                                  { .6427876f, .7660444f, .10f },
                                                  { .6427876f, -.7660444f, .10f },
                                                  { .4226183f, .9063078f, .06f },
                                                  { .4226183f, -.9063078f, .06f } };
                const unsigned  hulls = layout.hullSpacing > 0 ? 2 : 1;
                for (const auto& branch : branches)
                {
                    float wavelength = wakeWavelength(speed * branch[0], environment.depth);
                    if (wavelength < 1.0f || wavelength > 32.0f)
                        continue;
                    float side = branch[1] == 0 ? 0 : std::copysign(1.0f, branch[1]);
                    auto  direction = forward * branch[0] + right * branch[1];
                    for (unsigned hull = 0; hull < hulls; ++hull)
                    {
                        float hullCenter = (hull == 0 ? -.5f : .5f) * layout.hullSpacing;
                        auto  point = position + rotation * JPH::Vec3(hullCenter + side * (layout.beam - layout.hullSpacing) * .25f, -.1f,
                                                                      (surge < 0 ? -1 : 1) * layout.length * .40f);
                        emitWavePacket(world->ocean, fromJolt(point), fromJolt(direction), wavelength,
                                       world->wakeEnergy * branch[2] / hulls, false, bodies[vesselIndex].handle);
                    }
                }
            }
            world->wakeEnergy = world->wakeTime = 0;
        }
    }
}
static void capture(World* world)
{
    MTRACY_ZONE("capture");
    auto&        bodies = world->physics.GetBodyInterface();
    const auto   id = bodyID(world);
    auto         rotation = bodies.GetRotation(id);
    const Helm&  helmState = *helm(world);
    const Actor& actorState = *actor(world, 0);
    Snapshot&    state = world->current;
    state.position = fromJolt(bodies.GetPosition(id));
    state.velocity = fromJolt(bodies.GetLinearVelocity(id));
    state.angularVelocity = fromJolt(bodies.GetAngularVelocity(id));
    state.rotation = { rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() };
    state.skipperDeck = actorState.deckPosition;
    state.time = world->time;
    for (unsigned engineIndex = 0; engineIndex < 2; ++engineIndex)
    {
        state.throttle[engineIndex] = helmState.throttle[engineIndex];
        state.rpm[engineIndex] = world->propulsion[engineIndex].rpm;
    }
    state.rudder = helmState.rudder;
    state.brake = helmState.brake;
    state.travelling = actorState.travelling;
    state.destination = actorState.destination;
    state.atHelm = canOperateHelm(world, 0);
    state.speedKnots = std::sqrt(state.velocity.x * state.velocity.x + state.velocity.z * state.velocity.z) * 1.9438445f;
    state.travelRemaining = 0;
    Vec3 last = actorState.deckPosition;
    for (unsigned nextNode = actorState.pathCursor; nextNode < actorState.pathCount; ++nextNode)
    {
        Vec3 next = Deck[actorState.path[nextNode]];
        state.travelRemaining += (toJolt(next) - toJolt(last)).Length() / 1.25f;
        last = next;
    }
}
World* createWorld(bool cat)
{
    MTRACY_ZONE("createWorld");
    return createWorld(cat ? catamaranLayout() : oceanis401Layout());
}
World* createWorld(const VesselLayout& definition)
{
    MTRACY_ZONE("createWorld");
    const bool cat = definition.hullSpacing > 0;
    if (!gWorldCount++)
        setupAllocators();
    World* world = tf_new(World);
    world->ecs = ecs_init();
    world->layout = definition;
    SeaState calm;
    calm.windSpeed = calm.windWaveHeight = calm.swellHeight = 0;
    calm.current = {};
    world->ocean = createOcean(calm);
    world->physics.Init(256, 0, 1024, 512, world->broad, world->filter, world->objects);
    world->actorType = component(world->ecs, "CrewActor", sizeof(Actor), alignof(Actor));
    world->helmType = component(world->ecs, "HelmControls", sizeof(Helm), alignof(Helm));
    world->bodyType = component(world->ecs, "VesselBody", sizeof(PhysicsBody), alignof(PhysicsBody));
    world->vessel = ecs_new(world->ecs);
    for (auto& crew : world->crew)
        crew = ecs_new(world->ecs);
    JPH::StaticCompoundShapeSettings compound;
    for (unsigned hull = 0; hull < (cat ? 2u : 1u); ++hull)
    {
        JPH::Vec3 points[HullVertices];
        for (unsigned vertexIndex = 0; vertexIndex < HullVertices; ++vertexIndex)
            points[vertexIndex] = toJolt(hullVertex(world->layout, hull, vertexIndex));
        JPH::ConvexHullShapeSettings shell(points, HullVertices, .015f);
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shell.Create().Get());
        float keelHeight = world->layout.draft - world->layout.hullDraft;
        compound.AddShape(
            JPH::Vec3(cat ? (hull ? .5f : -.5f) * world->layout.hullSpacing : 0, -world->layout.hullDraft - keelHeight * .5f, 0),
            JPH::Quat::sIdentity(), tf_new(JPH::BoxShape, JPH::Vec3(cat ? .14f : .10f, keelHeight * .5f, 1.0f)));
    }
    auto                      hullShape = compound.Create().Get();
    JPH::RefConst<JPH::Shape> shape =
        tf_new(JPH::OffsetCenterOfMassShape, hullShape, toJolt(world->layout.centerOfMass) - hullShape->GetCenterOfMass());
    JPH::BodyCreationSettings settings(shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, 1);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.mMass = world->layout.mass;
    settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(toJolt(world->layout.inertia));
    settings.mAllowSleeping = false;
    settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    settings.mFriction = .35f;
    settings.mRestitution = .02f;
    settings.mLinearDamping = settings.mAngularDamping = 0;
    auto                      id = world->physics.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::Activate);
    JPH::RefConst<JPH::Shape> dockShape = tf_new(JPH::BoxShape, JPH::Vec3(1.2f, .5f, 15));
    JPH::BodyCreationSettings dock(dockShape, JPH::RVec3(10, .25f, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0);
    world->dock = world->physics.GetBodyInterface().CreateAndAddBody(dock, JPH::EActivation::DontActivate);
    JPH::RefConst<JPH::Shape> bottomShape = tf_new(JPH::BoxShape, JPH::Vec3(500, .5f, 500));
    JPH::BodyCreationSettings bottom(bottomShape, JPH::RVec3(0, -8.5f, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0);
    world->seabed = world->physics.GetBodyInterface().CreateAndAddBody(bottom, JPH::EActivation::DontActivate);
    PhysicsBody body = { id.GetIndexAndSequenceNumber() };
    ecs_set_id(world->ecs, world->vessel, world->bodyType, sizeof body, &body);
    ecs_system_desc_t travel = {};
    travel.query.terms[0].id = world->actorType;
    travel.query.cache_kind = EcsQueryCacheAll;
    travel.callback = travelSystem;
    travel.ctx = world;
    world->travelSystem = ecs_system_init(world->ecs, &travel);
    ecs_system_desc_t vessel = {};
    vessel.query.terms[0].id = world->bodyType;
    vessel.query.terms[1].id = world->helmType;
    vessel.query.cache_kind = EcsQueryCacheAll;
    vessel.callback = vesselSystem;
    vessel.ctx = world;
    world->vesselSystem = ecs_system_init(world->ecs, &vessel);
#ifdef TRACY_ENABLE
    ecs_set_name(world->ecs, world->travelSystem, "Crew travel");
    ecs_set_name(world->ecs, world->vesselSystem, "Vessel forces");
#endif
    resetWorld(world);
    for (unsigned warmupStep = 0; warmupStep < 120; ++warmupStep)
        step(world);
    resetWorld(world);
    return world;
}
void destroyWorld(World* world)
{
    MTRACY_ZONE("destroyWorld");
    auto  id = bodyID(world);
    auto& bodies = world->physics.GetBodyInterface();
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
    bodies.RemoveBody(world->dock);
    bodies.DestroyBody(world->dock);
    bodies.RemoveBody(world->seabed);
    bodies.DestroyBody(world->seabed);
    destroyOcean(world->ocean);
    ecs_fini(world->ecs);
    tf_delete(world);
    if (!--gWorldCount)
    {
        JPH::UnregisterTypes();
        tf_delete(JPH::Factory::sInstance);
        JPH::Factory::sInstance = nullptr;
    }
}
void resetWorld(World* world, const InitialConditions* conditions)
{
    MTRACY_ZONE("resetWorld");
    world->commandRead = world->commandWrite = 0;
    world->accumulator = world->time = 0;
    world->stats = {};
    world->contacts.dock = world->dock;
    world->contacts.seabed = world->seabed;
    world->contacts.dockPreviously = world->contacts.groundPreviously = false;
    world->physics.SetContactListener(&world->contacts);
    updateOcean(world->ocean, 0);
    clearWavePackets(world->ocean);
    world->wakeEnergy = world->wakeTime = 0;
    memset(world->propulsion, 0, sizeof world->propulsion);
    for (unsigned crewIndex = 0; crewIndex < MaxCrew; ++crewIndex)
    {
        Actor actorState = {};
        actorState.node = actorState.destination = crewIndex == 0 ? 0 : static_cast<uint8_t>(crewIndex);
        actorState.deckPosition = Deck[actorState.node];
        actorState.helmSkill = crewIndex == 0 ? 3 : 0;
        ecs_set_id(world->ecs, world->crew[crewIndex], world->actorType, sizeof actorState, &actorState);
    }
    Helm helmState = {};
    helmState.occupant = world->crew[0];
    helmState.brake = true;
    ecs_set_id(world->ecs, world->vessel, world->helmType, sizeof helmState, &helmState);
    auto&       bodies = world->physics.GetBodyInterface();
    auto        id = bodyID(world);
    const float equilibrium = seaState(world->ocean).level;
    bodies.SetPositionAndRotation(world->seabed, JPH::RVec3(0, equilibrium - seaState(world->ocean).depth - .5f, 0), JPH::Quat::sIdentity(),
                                  JPH::EActivation::DontActivate);
    InitialConditions initial = conditions ? *conditions : InitialConditions{};
    const auto&       rotation = initial.rotation;
    bodies.SetPositionAndRotation(id, toJolt(initial.position) + JPH::RVec3(0, equilibrium, 0),
                                  JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized(), JPH::EActivation::Activate);
    bodies.SetLinearAndAngularVelocity(id, toJolt(initial.velocity), toJolt(initial.angularVelocity));
    capture(world);
    world->previous = world->current;
    world->result = "At the helm.";
}
bool enqueue(World* world, Command command)
{
    MTRACY_ZONE("enqueue");
    if (world->commandWrite - world->commandRead == CommandCapacity)
    {
        ++world->stats.commandOverflow;
        world->result = "Command queue is full.";
        return false;
    }
    world->commands[world->commandWrite++ % CommandCapacity] = command;
    return true;
}
void step(World* world)
{
    MTRACY_ZONE_COLOR("Physics fixed step", 0x9977CC);
    MTRACY_VALUE(world->stats.steps);
#ifdef TRACY_ENABLE
    TracyCFrameMarkStart("Physics 60 Hz");
#endif
#ifdef ENABLE_MEMORY_TRACKING
    auto allocationsBefore = memGetStatistics().accumulatedAllocUnitCount;
#endif
    processCommands(world);
    world->previous = world->current;
    world->contacts.dockNow = world->contacts.groundNow = false;
    world->contacts.speed = 0;
    updateOcean(world->ocean, world->time);
    advanceWavePackets(world->ocean, FixedStep, world->current.position);
    ecs_run(world->ecs, world->travelSystem, FixedStep, nullptr);
    ecs_run(world->ecs, world->vesselSystem, FixedStep, nullptr);
    auto errors = world->physics.Update(FixedStep, 1, &world->temp, &world->jobs);
    world->stats.physicsErrors += errors != JPH::EPhysicsUpdateError::None;
    auto& contact = world->contacts;
    world->stats.dockContacts += contact.dockNow && !contact.dockPreviously;
    world->stats.groundings += contact.groundNow && !contact.groundPreviously;
    world->stats.maximumContactSpeed = std::max(world->stats.maximumContactSpeed, contact.speed);
    contact.dockPreviously = contact.dockNow;
    contact.groundPreviously = contact.groundNow;
    world->time += FixedStep;
    ++world->stats.steps;
    capture(world);
#ifdef ENABLE_MEMORY_TRACKING
    auto memoryStats = memGetStatistics();
    world->stats.allocationCount = memoryStats.accumulatedAllocUnitCount;
    if (world->stats.steps > 120)
        world->stats.steadyStepAllocations += memoryStats.accumulatedAllocUnitCount - allocationsBefore;
    world->stats.liveAllocationBytes = memoryStats.totalReportedMemory;
#endif
    MTRACY_PLOT("Physics / errors", world->stats.physicsErrors);
    MTRACY_PLOT("Physics / step allocations", world->stats.steadyStepAllocations);
    MTRACY_PLOT("Physics / speed (knots)", world->current.speedKnots);
    MTRACY_PLOT("Physics / wake packets", activeWavePackets(world->ocean));
    MTRACY_PLOT("Physics / dropped packets", droppedWavePackets(world->ocean));
    MTRACY_PLOT("Physics / command backlog", world->commandWrite - world->commandRead);
#ifdef TRACY_ENABLE
    TracyCFrameMarkEnd("Physics 60 Hz");
#endif
}
void advance(World* world, float seconds, float timeScale)
{
    MTRACY_ZONE("advance");
    processCommands(world);
    capture(world);
    world->accumulator += std::clamp(seconds, 0.0f, .25f) * std::clamp(timeScale, 0.0f, 1.0f);
    while (world->accumulator >= FixedStep)
    {
        step(world);
        world->accumulator -= FixedStep;
    }
}
Snapshot snapshot(const World* world, bool interpolate)
{
    Snapshot renderState = world->current;
    if (interpolate)
    {
        const float interpolation = world->accumulator / FixedStep;
        renderState.position =
            fromJolt(toJolt(world->previous.position) * (1 - interpolation) + toJolt(renderState.position) * interpolation);
        const auto& previousRotation = world->previous.rotation;
        const auto& currentRotation = renderState.rotation;
        auto        rotation = JPH::Quat(previousRotation.x, previousRotation.y, previousRotation.z, previousRotation.w)
                            .SLERP(JPH::Quat(currentRotation.x, currentRotation.y, currentRotation.z, currentRotation.w), interpolation);
        renderState.rotation = { rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() };
        renderState.time = world->previous.time + (renderState.time - world->previous.time) * interpolation;
    }
    return renderState;
}
const Stats&        statistics(const World* world) { return world->stats; }
Ocean*              worldOcean(World* world) { return world->ocean; }
const VesselLayout& worldLayout(const World* world) { return world->layout; }
void                setEnvironment(World* world, const SeaState& environment)
{
    MTRACY_ZONE("setEnvironment");
    configureOcean(world->ocean, environment);
    resetWorld(world);
}
const char* lastCommandResult(const World* world) { return world->result; }
void        setHelmSkill(World* world, uint8_t crewIndex, uint8_t level)
{
    if (crewIndex < MaxCrew)
        actor(world, crewIndex)->helmSkill = std::min<uint8_t>(level, 3);
}
float interactionTimeScale(Difficulty difficulty, bool modal)
{
    return !modal ? 1 : difficulty == Difficulty::Beginner ? 0 : difficulty == Difficulty::Intermediate ? .25f : 1;
}
const char* stationName(Station station)
{
    switch (station)
    {
    case Station::Helm:
        return "Helm";
    case Station::Cockpit:
        return "Cockpit";
    case Station::Port:
        return "Port rail";
    case Station::Starboard:
        return "Starboard rail";
    case Station::Bow:
        return "Bow";
    }
    return "Deck";
}
} // namespace mooring
