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
#include "Common_3/Game/ThirdParty/OpenSource/flecs/flecs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common_3/Utilities/Interfaces/ILog.h"
#include "Common_3/Utilities/Interfaces/IMemory.h"

#ifdef TRACY_ENABLE
void mooringTracyInstallECS();
#endif

namespace mooring {
// Framework bridges are the only simulation types with virtual interfaces.
struct BroadPhase final : JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() ? "Vessels" : "Static"; }
#endif
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(layer); }
};
struct ObjectFilter final : JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a || b; }
};
struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override { return a || b.GetValue(); }
};
struct ContactMonitor final : JPH::ContactListener {
    JPH::BodyID dock,seabed;
    bool dockNow=false,groundNow=false,dockPreviously=false,groundPreviously=false;
    float speed=0;
    void record(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold) {
        dockNow|=a.GetID()==dock || b.GetID()==dock;
        groundNow|=a.GetID()==seabed || b.GetID()==seabed;
        auto point=manifold.GetWorldSpaceContactPointOn1(0);
        speed=std::max(speed,std::fabs((a.GetPointVelocity(point)-b.GetPointVelocity(point)).Dot(manifold.mWorldSpaceNormal)));
    }
    void OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,JPH::ContactSettings&) override { record(a,b,m); }
    void OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,JPH::ContactSettings&) override { record(a,b,m); }
};
#ifdef TRACY_ENABLE
// Keep Jolt's allocator and LIFO semantics; expose temporary usage separately
// from its backing block, which is already reported by the Forge allocator.
struct ProfiledTempAllocator final : JPH::TempAllocator {
    JPH::TempAllocatorImpl backing;
    explicit ProfiledTempAllocator(size_t size) : backing(size) {}
    void* Allocate(JPH::uint size) override {
        void* p = backing.Allocate(size);
        MTRACY_ALLOC(p, size, "Jolt scratch (suballocated)");
        return p;
    }
    void Free(void* p, JPH::uint size) override {
        MTRACY_FREE(p, "Jolt scratch (suballocated)");
        backing.Free(p, size);
    }
};
#else
using ProfiledTempAllocator = JPH::TempAllocatorImpl;
#endif
struct World {
    BroadPhase broad;
    ObjectFilter objects;
    BroadFilter filter;
    JPH::PhysicsSystem physics;
    ContactMonitor contacts;
    JPH::BodyID dock, seabed;
    ProfiledTempAllocator temp{8 * TF_MB};
    JPH::JobSystemSingleThreaded jobs{512};
    ecs_world_t* ecs;
    ecs_entity_t actorType, helmType, bodyType, travelSystem, vesselSystem;
    ecs_entity_t vessel, crew[MaxCrew];
    VesselLayout layout;
    Ocean* ocean;
    PropellerState propulsion[2];
    Command commands[64];
    unsigned commandRead, commandWrite;
    float accumulator, time;
    float wakeEnergy,wakeTime;
    Snapshot previous, current;
    Stats stats;
    const char* result;
};
static unsigned gWorldCount;
static constexpr Vec3 Deck[] = {
    {0,1.05f,-3.2f}, {0,1.05f,-1.8f}, {-1.45f,1.05f,-1.4f}, {1.45f,1.05f,-1.4f},
    {-1.45f,1.05f,2.8f}, {1.45f,1.05f,2.8f}, {0,1.05f,4.7f}
};
static constexpr uint8_t Links[7] = {2, 1|4|8, 2|16, 2|32, 4|64, 8|64, 16|32};
static Actor* actor(World* w, unsigned i) { return static_cast<Actor*>(ecs_get_mut_id(w->ecs,w->crew[i],w->actorType)); }
static const Actor* actor(const World* w, unsigned i) { return static_cast<const Actor*>(ecs_get_id(w->ecs,w->crew[i],w->actorType)); }
static Helm* helm(World* w) { return static_cast<Helm*>(ecs_get_mut_id(w->ecs,w->vessel,w->helmType)); }
static const Helm* helm(const World* w) { return static_cast<const Helm*>(ecs_get_id(w->ecs,w->vessel,w->helmType)); }
static JPH::BodyID bodyID(const World* w) {
    return JPH::BodyID(static_cast<const PhysicsBody*>(ecs_get_id(w->ecs,w->vessel,w->bodyType))->handle);
}
static ecs_entity_t component(ecs_world_t* ecs, const char* name, size_t size, size_t alignment)
{
    ecs_entity_desc_t entity = {}; entity.name = name;
    ecs_component_desc_t desc = {}; desc.entity = ecs_entity_init(ecs,&entity);
    desc.type.size = static_cast<ecs_size_t>(size); desc.type.alignment = static_cast<ecs_size_t>(alignment);
    return ecs_component_init(ecs,&desc);
}
static void setupAllocators()
{
    MTRACY_ZONE("setupAllocators");
    JPH::Allocate = [](size_t n) -> void* { return tf_malloc(n); };
    JPH::Reallocate = [](void* p, size_t, size_t n) -> void* { return tf_realloc(p,n); };
    JPH::Free = [](void* p) { tf_free(p); };
    JPH::AlignedAllocate = [](size_t n, size_t a) -> void* { return tf_memalign(a,n); };
    JPH::AlignedFree = [](void* p) { tf_free(p); };
    JPH::Factory::sInstance = tf_new(JPH::Factory);
    JPH::RegisterTypes();
    // The bundled flecs already routes allocation, clocks, and profiling
    // through Forge. Its adapter must be installed before creating a world.
    initEntityComponentSystem();
#ifdef TRACY_ENABLE
    mooringTracyInstallECS();
#endif
}
bool canOperateHelm(const World* w, uint8_t i)
{
    return i < MaxCrew && !actor(w,i)->travelling && actor(w,i)->node == 0 && actor(w,i)->helmSkill > 0 &&
           helm(w)->occupant == w->crew[i];
}
static bool route(Actor& a, uint8_t destination)
{
    if (destination >= 7 || a.travelling) return false;
    uint8_t queue[7] = {a.node}, parent[7];
    memset(parent,255,sizeof parent); parent[a.node] = a.node;
    unsigned first = 0, end = 1;
    while (first < end && parent[destination] == 255) {
        uint8_t node = queue[first++];
        for (uint8_t n = 0; n < 7; ++n)
            if ((Links[node] & (1 << n)) && parent[n] == 255) { parent[n] = node; queue[end++] = n; }
    }
    if (parent[destination] == 255) return false;
    uint8_t reverse[7], count = 0;
    for (uint8_t n = destination; n != a.node; n = parent[n]) reverse[count++] = n;
    a.pathCount = count; a.pathCursor = 0; a.destination = destination;
    for (unsigned n = 0; n < count; ++n) a.path[n] = reverse[count-1-n];
    a.travelling = count > 0;
    return true;
}
static void processCommands(World* w)
{
    MTRACY_ZONE("processCommands");
    while (w->commandRead != w->commandWrite) {
        const Command c = w->commands[w->commandRead++ % 64];
        if (c.actor >= MaxCrew || !std::isfinite(c.value)) { ++w->stats.rejectedCommands; continue; }
        Actor& a = *actor(w,c.actor); Helm& h = *helm(w);
        bool accepted = false;
        if (c.type == CommandType::Attend) {
            const uint8_t target = static_cast<uint8_t>(c.station);
            if (target == 0 && a.helmSkill == 0) w->result = "Helm training required.";
            else if (target == 0 && h.occupant && h.occupant != w->crew[c.actor]) w->result = "The helm is occupied.";
            else if (route(a,target)) {
                if (a.travelling && h.occupant == w->crew[c.actor]) h.occupant = 0;
                if (!a.travelling && target == 0) h.occupant = w->crew[c.actor];
                w->result = a.travelling ? "Travelling to the station." : "Already at the station.";
                accepted = true;
            } else w->result = "Finish travelling before choosing another station.";
        } else if (!canOperateHelm(w,c.actor)) w->result = "Reach and occupy the helm before using its controls.";
        else {
            if (c.type == CommandType::Wheel) { h.rudder = std::clamp(c.value,-0.61f,0.61f); h.rudderVelocity=0; accepted = true; }
            if (c.type == CommandType::Throttle && c.engine < w->layout.propellerCount) {
                h.throttle[c.engine] = std::clamp(c.value,-1.0f,1.0f); accepted = true;
            }
            if (c.type == CommandType::Brake) { h.brake = c.value != 0; accepted = true; }
            w->result = accepted ? "Helm control applied." : "Invalid control.";
        }
        if (!accepted) ++w->stats.rejectedCommands;
    }
}
static void travelSystem(ecs_iter_t* it)
{
    MTRACY_ZONE("travelSystem");
    auto* w = static_cast<World*>(it->ctx);
    auto* actors = static_cast<Actor*>(ecs_field_w_size(it,sizeof(Actor),0));
    for (int i = 0; i < it->count; ++i) {
        Actor& a = actors[i];
        if (!a.travelling) continue;
        const Vec3 goal = Deck[a.path[a.pathCursor]];
        JPH::Vec3 delta = toJolt(goal) - toJolt(a.deckPosition);
        float distance = delta.Length();
        const float stride = 1.25f * it->delta_time;
        if (distance <= stride) {
            a.deckPosition = goal; a.node = a.path[a.pathCursor++];
            if (a.pathCursor == a.pathCount) {
                a.travelling = false;
                if (a.node == 0 && a.helmSkill && !helm(w)->occupant) helm(w)->occupant = it->entities[i];
            }
        } else a.deckPosition = fromJolt(toJolt(a.deckPosition) + delta * (stride / distance));
    }
}
static void vesselSystem(ecs_iter_t* it)
{
    MTRACY_ZONE("vesselSystem");
    auto* w = static_cast<World*>(it->ctx);
    const auto* bodies = static_cast<const PhysicsBody*>(ecs_field_w_size(it,sizeof(PhysicsBody),0));
    auto* helms = static_cast<Helm*>(ecs_field_w_size(it,sizeof(Helm),1));
    auto& bi = w->physics.GetBodyInterface();
    const VesselLayout& layout = w->layout;
    for (int v = 0; v < it->count; ++v) {
        JPH::BodyID id(bodies[v].handle);
        auto position = bi.GetPosition(id);
        auto rotation = bi.GetRotation(id);
        auto inverse = rotation.Conjugated();
        auto velocity = bi.GetLinearVelocity(id), omega = bi.GetAngularVelocity(id);
        auto apply = [&](JPH::Vec3 force, Vec3 local) { bi.AddForce(id,force,position + rotation * toJolt(local)); };
        auto waterAt = [&](Vec3 local) {
            auto p=position+rotation*toJolt(local);
            // Empirical resistance already includes self-wave drag. Applying
            // our emitted wake again would make handling depend on its sampling.
            return sampleOcean(w->ocean,p.GetX(),p.GetZ(),std::max(0.0f,seaState(w->ocean).level-p.GetY()),bodies[v].handle);
        };
        auto localFlow = [&](Vec3 local) {
            auto water=waterAt(local);
            return inverse * (toJolt(water.velocity)-velocity-omega.Cross(rotation*(toJolt(local)-toJolt(layout.centerOfMass))));
        };
        const float referenceVolume=layout.mass/1025.0f;
        for(unsigned i=0;i<layout.cellCount;++i) {
            const auto& cell=layout.cells[i];
            auto samplePoint=position+rotation*toJolt(cell.center);
            auto water=waterAt(cell.center);
            auto normal=inverse*toJolt(water.normal);
            float plane=normal.Dot(toJolt(cell.center))+(water.height-samplePoint.GetY())*water.normal.y;
            auto wet=submergedTetrahedron(cell,fromJolt(normal),plane);
            if(wet.volume<=1e-8f) continue;
            const auto point=position+rotation*toJolt(wet.center);
            auto offset=point-(position+rotation*toJolt(layout.centerOfMass));
            auto pointVelocity=velocity+omega.Cross(offset);
            float weight=wet.volume/referenceVolume;
            float damping=(water.velocity.y-pointVelocity.GetY())*layout.heaveDampingPerArea*referenceVolume/layout.hullDraft*weight;
            // Froude-Krylov excitation: the pressure gradient follows the
            // material acceleration of the shared water solution at this depth.
            auto pressure=(JPH::Vec3(0,9.81f,0)+toJolt(water.acceleration))*(1025.0f*wet.volume);
            bi.AddForce(id,pressure+JPH::Vec3(0,damping,0),point);
            auto flow=inverse*(toJolt(water.velocity)-pointVelocity);
            const auto& linear=layout.resistanceLinear; const auto& quadratic=layout.resistanceQuadratic;
            auto drag=JPH::Vec3(flow.GetX()*(linear.x+quadratic.x*std::fabs(flow.GetX())),0,
                                flow.GetZ()*(linear.z+quadratic.z*std::fabs(flow.GetZ())))*weight;
            bi.AddForce(id,rotation*drag,point);
            w->wakeEnergy+=std::max(0.0f,drag.Dot(flow))*it->delta_time*layout.wakeResistanceFraction;
        }
        Helm& controls = helms[v];
        const auto& environment=seaState(w->ocean);
        float gust=1+environment.gust*(.6f*std::sin(w->time*.53f)+.4f*std::sin(w->time*1.37f));
        JPH::Vec3 wind(environment.windSpeed*gust*std::cos(environment.windDirection),0,environment.windSpeed*gust*std::sin(environment.windDirection));
        for(unsigned i=0;i<layout.windAreaCount;++i) {
            const auto& area=layout.windAreas[i];
            auto pointVelocity=velocity+omega.Cross(rotation*(toJolt(area.position)-toJolt(layout.centerOfMass)));
            apply(rotation*toJolt(windForce(area,fromJolt(inverse*(wind-pointVelocity)))),area.position);
        }
        for (unsigned i = 0; i < layout.propellerCount; ++i) {
            const Propeller& p = layout.propellers[i];
            auto axis=toJolt(p.axis).Normalized();
            auto location=position+rotation*toJolt(p.position);
            auto water=waterAt(p.position);
            float immersed=propellerImmersion(water.height-location.GetY(),p.diameter);
            updatePropeller(p,w->propulsion[i],controls.throttle[i],w->time,it->delta_time,immersed,-localFlow(p.position).Dot(axis));
            auto transverse=JPH::Vec3::sAxisY().Cross(axis).Normalized();
            apply(rotation*(axis*propellerThrust(p,w->propulsion[i])+transverse*propellerWalk(p,w->propulsion[i])),p.position);
        }
        float stockMoment = 0;
        for (unsigned i = 0; i < layout.rudderCount; ++i) {
            const Rudder& rudder = layout.rudders[i];
            auto flow = localFlow(rudder.position);
            for (unsigned p = 0; p < layout.propellerCount; ++p)
                flow += toJolt(rudderWash(layout.propellers[p],w->propulsion[p],rudder.position,w->time));
            // Positive wheel requests turn the bow to starboard (+X).
            float waterHeight=waterAt(rudder.position).height;
            float rudderY=(position+rotation*toJolt(rudder.position)).GetY();
            float verticalSpan=std::max(.1f,std::fabs((rotation*toJolt(rudder.spanDirection)).GetY())*rudder.span);
            float immersion=std::clamp((waterHeight-rudderY)/verticalSpan+.5f,0.0f,1.0f);
            auto force = rudderForce(rudder,-controls.rudder,fromJolt(flow),immersion);
            apply(rotation*toJolt(force),rudder.position);
            stockMoment += force.x * rudder.stockOffset;
        }
        if (!controls.occupant && !controls.brake && layout.steering.backdrivable) {
            const auto& steering=layout.steering;
            if(std::fabs(stockMoment)<=steering.frictionMoment && std::fabs(controls.rudderVelocity)<.001f) controls.rudderVelocity=0;
            else {
                float friction=std::copysign(steering.frictionMoment,std::fabs(controls.rudderVelocity)>.001f?controls.rudderVelocity:stockMoment);
                controls.rudderVelocity=std::clamp(controls.rudderVelocity+(stockMoment-friction-steering.damping*controls.rudderVelocity)/steering.inertia*it->delta_time,-.8f,.8f);
                controls.rudder=std::clamp(controls.rudder+controls.rudderVelocity*it->delta_time,-.61f,.61f);
            }
        } else controls.rudderVelocity=0;
        auto localOmega=inverse*omega;
        bi.AddTorque(id,rotation*(-localOmega*JPH::Vec3(3500,1100,3500)));
        w->wakeTime+=it->delta_time;
        auto throughWater=velocity-toJolt(environment.current);
        auto hullForward=rotation*JPH::Vec3::sAxisZ(); hullForward.SetY(0); hullForward=hullForward.Normalized();
        float surge=throughWater.Dot(hullForward);
        HullWake attached{}; attached.position=fromJolt(position); attached.velocity=fromJolt(velocity);
        attached.forward=fromJolt(hullForward*(surge<0?-1.0f:1.0f));
        attached.length=layout.length; attached.width=layout.beam-layout.hullSpacing; attached.spacing=layout.hullSpacing;
        attached.amplitude=std::min(.4f,layout.wakePressureCoefficient*surge*surge/(2*9.81f)); attached.sourceBody=bodies[v].handle;
        setHullWake(w->ocean,unsigned(v),attached);
        if(w->wakeTime>=1.2f) {
            auto current=toJolt(seaState(w->ocean).current);
            auto relative=velocity-current; relative.SetY(0); float speed=relative.Length();
            if(speed>.35f && w->wakeEnergy>0) {
                auto forward=relative.Normalized(); auto right=JPH::Vec3::sAxisY().Cross(forward).Normalized();
                // Quadrature across transverse, cusp and divergent directions.
                // Each hull emits both sides, allowing interference in the
                // tunnel. The weights sum to one resistance-energy budget.
                constexpr float branches[][3]={{1,0,.14f},{.9238795f,.3826834f,.10f},{.9238795f,-.3826834f,.10f},
                    {.81649658f,.57735027f,.17f},{.81649658f,-.57735027f,.17f},
                    {.6427876f,.7660444f,.10f},{.6427876f,-.7660444f,.10f},
                    {.4226183f,.9063078f,.06f},{.4226183f,-.9063078f,.06f}};
                const unsigned hulls=layout.hullSpacing>0?2:1;
                for(const auto& branch:branches) {
                    float wavelength=wakeWavelength(speed*branch[0],environment.depth);
                    if(wavelength<1.0f || wavelength>32.0f) continue;
                    float side=branch[1]==0?0:std::copysign(1.0f,branch[1]);
                    auto direction=forward*branch[0]+right*branch[1];
                    for(unsigned hull=0;hull<hulls;++hull) {
                        float hullCenter=(hull==0?-.5f:.5f)*layout.hullSpacing;
                        auto point=position+rotation*JPH::Vec3(hullCenter+side*(layout.beam-layout.hullSpacing)*.25f,-.1f,(surge<0?-1:1)*layout.length*.40f);
                        emitWavePacket(w->ocean,fromJolt(point),fromJolt(direction),wavelength,w->wakeEnergy*branch[2]/hulls,false,bodies[v].handle);
                    }
                }
            }
            w->wakeEnergy=w->wakeTime=0;
        }
    }
}
static void capture(World* w)
{
    MTRACY_ZONE("capture");
    auto& bi = w->physics.GetBodyInterface(); const auto id = bodyID(w); auto q = bi.GetRotation(id);
    const Helm& h = *helm(w); const Actor& a = *actor(w,0);
    Snapshot& s = w->current;
    s.position = fromJolt(bi.GetPosition(id)); s.velocity = fromJolt(bi.GetLinearVelocity(id));
    s.angularVelocity=fromJolt(bi.GetAngularVelocity(id));
    s.rotation = {q.GetX(),q.GetY(),q.GetZ(),q.GetW()};
    s.skipperDeck = a.deckPosition; s.time = w->time;
    for (unsigned i = 0; i < 2; ++i) { s.throttle[i] = h.throttle[i]; s.rpm[i] = w->propulsion[i].rpm; }
    s.rudder = h.rudder; s.brake = h.brake; s.travelling = a.travelling; s.destination = a.destination;
    s.atHelm = canOperateHelm(w,0); s.speedKnots = std::sqrt(s.velocity.x*s.velocity.x+s.velocity.z*s.velocity.z)*1.9438445f;
    s.travelRemaining = 0;
    Vec3 last = a.deckPosition;
    for (unsigned n = a.pathCursor; n < a.pathCount; ++n) {
        Vec3 next = Deck[a.path[n]]; s.travelRemaining += (toJolt(next)-toJolt(last)).Length()/1.25f; last = next;
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
    const bool cat=definition.hullSpacing>0;
    if (!gWorldCount++) setupAllocators();
    World* w = tf_new(World);
    w->ecs = ecs_init(); w->layout = definition;
    SeaState calm; calm.windSpeed=calm.windWaveHeight=calm.swellHeight=0; calm.current={};
    w->ocean=createOcean(calm);
    w->physics.Init(256,0,1024,512,w->broad,w->filter,w->objects);
    w->actorType = component(w->ecs,"CrewActor",sizeof(Actor),alignof(Actor));
    w->helmType = component(w->ecs,"HelmControls",sizeof(Helm),alignof(Helm));
    w->bodyType = component(w->ecs,"VesselBody",sizeof(PhysicsBody),alignof(PhysicsBody));
    w->vessel = ecs_new(w->ecs);
    for (auto& crew : w->crew) crew = ecs_new(w->ecs);
    JPH::StaticCompoundShapeSettings compound;
    for(unsigned hull=0; hull<(cat?2u:1u); ++hull) {
        JPH::Vec3 points[HullVertices];
        for(unsigned i=0; i<HullVertices; ++i) points[i]=toJolt(hullVertex(w->layout,hull,i));
        JPH::ConvexHullShapeSettings shell(points,HullVertices,.015f);
        compound.AddShape(JPH::Vec3::sZero(),JPH::Quat::sIdentity(),shell.Create().Get());
        float keelHeight=w->layout.draft-w->layout.hullDraft;
        compound.AddShape(JPH::Vec3(cat?(hull?.5f:-.5f)*w->layout.hullSpacing:0,-w->layout.hullDraft-keelHeight*.5f,0),
                          JPH::Quat::sIdentity(),tf_new(JPH::BoxShape,JPH::Vec3(cat?.14f:.10f,keelHeight*.5f,1.0f)));
    }
    auto hullShape=compound.Create().Get();
    JPH::RefConst<JPH::Shape> shape=tf_new(JPH::OffsetCenterOfMassShape,hullShape,toJolt(w->layout.centerOfMass)-hullShape->GetCenterOfMass());
    JPH::BodyCreationSettings settings(shape,JPH::RVec3::sZero(),JPH::Quat::sIdentity(),JPH::EMotionType::Dynamic,1);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.mMass=w->layout.mass;
    settings.mMassPropertiesOverride.mInertia=JPH::Mat44::sScale(toJolt(w->layout.inertia));
    settings.mAllowSleeping = false;
    settings.mMotionQuality=JPH::EMotionQuality::LinearCast;
    settings.mFriction=.35f; settings.mRestitution=.02f;
    settings.mLinearDamping = settings.mAngularDamping = 0;
    auto id = w->physics.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::Activate);
    JPH::RefConst<JPH::Shape> dockShape = tf_new(JPH::BoxShape,JPH::Vec3(1.2f,.5f,15));
    JPH::BodyCreationSettings dock(dockShape,JPH::RVec3(10,.25f,0),JPH::Quat::sIdentity(),JPH::EMotionType::Static,0);
    w->dock = w->physics.GetBodyInterface().CreateAndAddBody(dock,JPH::EActivation::DontActivate);
    JPH::RefConst<JPH::Shape> bottomShape=tf_new(JPH::BoxShape,JPH::Vec3(500,.5f,500));
    JPH::BodyCreationSettings bottom(bottomShape,JPH::RVec3(0,-8.5f,0),JPH::Quat::sIdentity(),JPH::EMotionType::Static,0);
    w->seabed=w->physics.GetBodyInterface().CreateAndAddBody(bottom,JPH::EActivation::DontActivate);
    PhysicsBody body = {id.GetIndexAndSequenceNumber(),static_cast<uint8_t>(cat)};
    ecs_set_id(w->ecs,w->vessel,w->bodyType,sizeof body,&body);
    ecs_system_desc_t travel = {}; travel.query.terms[0].id = w->actorType;
    travel.query.cache_kind = EcsQueryCacheAll; travel.callback = travelSystem; travel.ctx = w;
    w->travelSystem = ecs_system_init(w->ecs,&travel);
    ecs_system_desc_t vessel = {}; vessel.query.terms[0].id = w->bodyType; vessel.query.terms[1].id = w->helmType;
    vessel.query.cache_kind = EcsQueryCacheAll; vessel.callback = vesselSystem; vessel.ctx = w;
    w->vesselSystem = ecs_system_init(w->ecs,&vessel);
#ifdef TRACY_ENABLE
    ecs_set_name(w->ecs, w->travelSystem, "Crew travel");
    ecs_set_name(w->ecs, w->vesselSystem, "Vessel forces");
#endif
    resetWorld(w);
    for (unsigned i = 0; i < 120; ++i) step(w);
    resetWorld(w);
    return w;
}
void destroyWorld(World* w)
{
    MTRACY_ZONE("destroyWorld");
    auto id = bodyID(w); auto& bi = w->physics.GetBodyInterface(); bi.RemoveBody(id); bi.DestroyBody(id);
    bi.RemoveBody(w->dock); bi.DestroyBody(w->dock);
    bi.RemoveBody(w->seabed); bi.DestroyBody(w->seabed);
    destroyOcean(w->ocean); ecs_fini(w->ecs); tf_delete(w);
    if (!--gWorldCount) { JPH::UnregisterTypes(); tf_delete(JPH::Factory::sInstance); JPH::Factory::sInstance = nullptr; }
}
void resetWorld(World* w, const InitialConditions* conditions)
{
    MTRACY_ZONE("resetWorld");
    w->commandRead = w->commandWrite = 0; w->accumulator = w->time = 0; w->stats = {};
    w->contacts.dock=w->dock; w->contacts.seabed=w->seabed;
    w->contacts.dockPreviously=w->contacts.groundPreviously=false;
    w->physics.SetContactListener(&w->contacts);
    updateOcean(w->ocean,0);
    clearWavePackets(w->ocean); w->wakeEnergy=w->wakeTime=0;
    memset(w->propulsion,0,sizeof w->propulsion);
    for (unsigned i = 0; i < MaxCrew; ++i) {
        Actor a = {}; a.node = a.destination = i == 0 ? 0 : static_cast<uint8_t>(i);
        a.deckPosition = Deck[a.node]; a.helmSkill = i == 0 ? 3 : 0;
        ecs_set_id(w->ecs,w->crew[i],w->actorType,sizeof a,&a);
    }
    Helm h = {}; h.occupant = w->crew[0]; h.brake = true;
    ecs_set_id(w->ecs,w->vessel,w->helmType,sizeof h,&h);
    auto& bi = w->physics.GetBodyInterface(); auto id = bodyID(w);
    const float equilibrium = seaState(w->ocean).level;
    bi.SetPositionAndRotation(w->seabed,JPH::RVec3(0,equilibrium-seaState(w->ocean).depth-.5f,0),JPH::Quat::sIdentity(),JPH::EActivation::DontActivate);
    InitialConditions initial=conditions ? *conditions : InitialConditions{};
    const auto& q=initial.rotation;
    bi.SetPositionAndRotation(id,toJolt(initial.position)+JPH::RVec3(0,equilibrium,0),JPH::Quat(q.x,q.y,q.z,q.w).Normalized(),JPH::EActivation::Activate);
    bi.SetLinearAndAngularVelocity(id,toJolt(initial.velocity),toJolt(initial.angularVelocity));
    capture(w); w->previous = w->current; w->result = "At the helm.";
}
bool enqueue(World* w, Command c)
{
    MTRACY_ZONE("enqueue");
    if (w->commandWrite-w->commandRead == 64) { ++w->stats.commandOverflow; w->result = "Command queue is full."; return false; }
    w->commands[w->commandWrite++ % 64] = c; return true;
}
void step(World* w)
{
    MTRACY_ZONE_COLOR("Physics fixed step", 0x9977CC);
    MTRACY_VALUE(w->stats.steps);
#ifdef TRACY_ENABLE
    TracyCFrameMarkStart("Physics 60 Hz");
#endif
#ifdef ENABLE_MEMORY_TRACKING
    auto allocationsBefore=memGetStatistics().accumulatedAllocUnitCount;
#endif
    processCommands(w); w->previous = w->current;
    w->contacts.dockNow=w->contacts.groundNow=false; w->contacts.speed=0;
    updateOcean(w->ocean,w->time);
    advanceWavePackets(w->ocean,FixedStep,w->current.position);
    ecs_run(w->ecs,w->travelSystem,FixedStep,nullptr);
    ecs_run(w->ecs,w->vesselSystem,FixedStep,nullptr);
    auto errors = w->physics.Update(FixedStep,1,&w->temp,&w->jobs);
    w->stats.physicsErrors += errors != JPH::EPhysicsUpdateError::None;
    auto& contact=w->contacts;
    w->stats.dockContacts+=contact.dockNow && !contact.dockPreviously;
    w->stats.groundings+=contact.groundNow && !contact.groundPreviously;
    w->stats.maximumContactSpeed=std::max(w->stats.maximumContactSpeed,contact.speed);
    contact.dockPreviously=contact.dockNow; contact.groundPreviously=contact.groundNow;
    w->time += FixedStep; ++w->stats.steps; capture(w);
#ifdef ENABLE_MEMORY_TRACKING
    auto m = memGetStatistics(); w->stats.allocationCount = m.accumulatedAllocUnitCount;
    if(w->stats.steps>120) w->stats.steadyStepAllocations+=m.accumulatedAllocUnitCount-allocationsBefore;
    w->stats.liveAllocationBytes = m.totalReportedMemory;
#endif
    MTRACY_PLOT("Physics / errors", w->stats.physicsErrors);
    MTRACY_PLOT("Physics / step allocations", w->stats.steadyStepAllocations);
    MTRACY_PLOT("Physics / speed (knots)", w->current.speedKnots);
    MTRACY_PLOT("Physics / wake packets", activeWavePackets(w->ocean));
    MTRACY_PLOT("Physics / dropped packets", droppedWavePackets(w->ocean));
    MTRACY_PLOT("Physics / command backlog", w->commandWrite-w->commandRead);
#ifdef TRACY_ENABLE
    TracyCFrameMarkEnd("Physics 60 Hz");
#endif
}
void advance(World* w, float seconds, float timeScale)
{
    MTRACY_ZONE("advance");
    processCommands(w); capture(w);
    w->accumulator += std::clamp(seconds,0.0f,.25f)*std::clamp(timeScale,0.0f,1.0f);
    while (w->accumulator >= FixedStep) { step(w); w->accumulator -= FixedStep; }
}
Snapshot snapshot(const World* w, bool interpolate)
{
    Snapshot s = w->current;
    if (interpolate) {
        const float t = w->accumulator/FixedStep;
        s.position = fromJolt(toJolt(w->previous.position)*(1-t) + toJolt(s.position)*t);
        const auto& a = w->previous.rotation; const auto& b = s.rotation;
        auto q = JPH::Quat(a.x,a.y,a.z,a.w).SLERP(JPH::Quat(b.x,b.y,b.z,b.w),t);
        s.rotation = {q.GetX(),q.GetY(),q.GetZ(),q.GetW()};
        s.time = w->previous.time+(s.time-w->previous.time)*t;
    }
    return s;
}
const Stats& statistics(const World* w) { return w->stats; }
Ocean* worldOcean(World* w) { return w->ocean; }
const VesselLayout& worldLayout(const World* w) { return w->layout; }
void setEnvironment(World* w,const SeaState& environment) {
    MTRACY_ZONE("setEnvironment"); configureOcean(w->ocean,environment); resetWorld(w); }
const char* lastCommandResult(const World* w) { return w->result; }
void setHelmSkill(World* w, uint8_t i, uint8_t level) { if (i < MaxCrew) actor(w,i)->helmSkill = std::min<uint8_t>(level,3); }
float interactionTimeScale(Difficulty d, bool modal) { return !modal ? 1 : d == Difficulty::Beginner ? 0 : d == Difficulty::Intermediate ? .25f : 1; }
const char* stationName(Station station)
{
    switch (station) { case Station::Helm:return "Helm"; case Station::Cockpit:return "Cockpit";
        case Station::Port:return "Port rail"; case Station::Starboard:return "Starboard rail"; case Station::Bow:return "Bow"; }
    return "Deck";
}
}
