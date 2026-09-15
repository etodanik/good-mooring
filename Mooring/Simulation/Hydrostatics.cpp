#include "Hydrodynamics.h"
#include "PhysicsMath.h"
#include <Jolt/Physics/Collision/Shape/PolyhedronSubmergedVolumeCalculator.h>
#include <algorithm>
#include <cmath>
namespace mooring
{
static SubmergedVolume tetra(JPH::Vec3Arg a, JPH::Vec3Arg b, JPH::Vec3Arg c, JPH::Vec3Arg d)
{
    float volume = std::fabs((b - a).Dot((c - a).Cross(d - a))) / 6;
    return { volume, fromJolt((a + b + c + d) * .25f) };
}
static void accumulate(SubmergedVolume& result, const SubmergedVolume& part)
{
    auto moment = toJolt(result.center) * result.volume + toJolt(part.center) * part.volume;
    result.volume += part.volume;
    result.center = result.volume > 1e-10f ? fromJolt(moment / result.volume) : Vec3{};
}
SubmergedVolume submergedTetrahedron(const BuoyancyCell& cell, Vec3 normal, float distance)
{
    MTRACY_FINE_ZONE("submergedTetrahedron");
    JPH::Vec3 vertices[4];
    for (unsigned i = 0; i < 4; ++i)
        vertices[i] = toJolt(cell.vertices[i]);
    JPH::PolyhedronSubmergedVolumeCalculator::Point scratch[4];
    JPH::PolyhedronSubmergedVolumeCalculator        calculator(JPH::Mat44::sIdentity(), vertices, sizeof(JPH::Vec3), 4,
                                                               JPH::Plane(toJolt(normal), -distance),
                                                               scratch JPH_IF_DEBUG_RENDERER(, JPH::RVec3::sZero()));
    if (calculator.AreAllAbove())
        return {};
    if (calculator.AreAllBelow())
        return { cell.volume, cell.center };
    // Only the face opposite Jolt's submerged reference vertex contributes.
    unsigned face[3], count = 0, reference = unsigned(calculator.GetReferencePointIdx());
    for (unsigned i = 0; i < 4; ++i)
        if (i != reference)
            face[count++] = i;
    auto a = vertices[face[0]], b = vertices[face[1]], c = vertices[face[2]];
    if ((b - a).Cross(c - a).Dot(vertices[reference] - a) > 0)
        std::swap(face[1], face[2]);
    calculator.AddFace(int(face[0]), int(face[1]), int(face[2]));
    float     volume;
    JPH::Vec3 center;
    calculator.GetResult(volume, center);
    return { volume, fromJolt(center) };
}
Vec3 hullVertex(const VesselLayout& v, unsigned hull, unsigned vertex)
{
    // Symmetric displacement hull with an immersed transom and a fine bow.
    // The same authored sections feed graphics, collision and hydrostatics.
    constexpr float widths[HullSections] = { .69f, .92f, 1, 1, .91f, .73f, .43f, .015f };
    constexpr float xs[HullSectionVertices] = { 0, -.55f, -.94f, -1, 1, .94f, .55f };
    constexpr float ys[HullSectionVertices] = { -1, -.65f, -.10f, 0, 0, -.10f, -.65f };
    unsigned        row = vertex / HullSectionVertices, corner = vertex % HullSectionVertices;
    float           halfWidth = (v.hullSpacing > 0 ? (v.beam - v.hullSpacing) : v.beam) * .5f;
    float           x = xs[corner] * widths[row] * halfWidth + (v.hullSpacing > 0 ? (hull ? .5f : -.5f) * v.hullSpacing : 0);
    float           y = corner == 3 || corner == 4 ? .85f : ys[corner] * v.hullDraft;
    float           z = (float(row) / (HullSections - 1) - .5f) * v.length;
    return { x, y, z };
}
void hullTriangle(const VesselLayout& v, unsigned hull, unsigned triangle, Vec3 out[3])
{
    constexpr unsigned sides = (HullSections - 1) * HullSectionVertices * 2;
    if (triangle < sides)
    {
        unsigned edge = triangle / 2, row = edge / HullSectionVertices, a = edge % HullSectionVertices, b = (a + 1) % HullSectionVertices;
        out[0] = hullVertex(v, hull, row * HullSectionVertices + a);
        out[1] = hullVertex(v, hull, (row + 1) * HullSectionVertices + (triangle % 2 ? b : a));
        out[2] = hullVertex(v, hull, (triangle % 2 ? row : row + 1) * HullSectionVertices + b);
        // Mirror the diagonal on the opposite side. Twisted section quads
        // otherwise introduce an artificial port/starboard stability bias.
        if (a >= 4)
        {
            out[0] = hullVertex(v, hull, row * HullSectionVertices + (triangle % 2 ? b : a));
            out[1] = hullVertex(v, hull, (row + 1) * HullSectionVertices + a);
            out[2] = hullVertex(v, hull, (triangle % 2 ? row + 1 : row) * HullSectionVertices + b);
        }
    }
    else
    {
        unsigned cap = (triangle - sides) / (HullSectionVertices - 2), i = (triangle - sides) % (HullSectionVertices - 2) + 1;
        unsigned row = cap ? HullSections - 1 : 0;
        out[0] = hullVertex(v, hull, row * HullSectionVertices);
        out[1] = hullVertex(v, hull, row * HullSectionVertices + i + (cap ? 1 : 0));
        out[2] = hullVertex(v, hull, row * HullSectionVertices + i + (cap ? 0 : 1));
    }
}
static void makeCells(VesselLayout& v)
{
    v.cellCount = 0;
    v.hullVolume = 0;
    for (unsigned hull = 0; hull < (v.hullSpacing > 0 ? 2u : 1u); ++hull)
    {
        Vec3 origin = { v.hullSpacing > 0 ? (hull ? .5f : -.5f) * v.hullSpacing : 0, 0, 0 };
        for (unsigned i = 0; i < HullTriangles; ++i)
        {
            Vec3 points[3];
            hullTriangle(v, hull, i, points);
            auto volume = tetra(toJolt(origin), toJolt(points[0]), toJolt(points[1]), toJolt(points[2]));
            if (volume.volume < 1e-8f)
                continue;
            auto& cell = v.cells[v.cellCount++];
            cell = { { origin, points[0], points[1], points[2] }, volume.center, volume.volume };
            v.hullVolume += volume.volume;
        }
        // Fin keel volume uses the same box as the Jolt compound collider.
        float     halfWidth = v.hullSpacing > 0 ? .14f : .10f, h = v.draft - v.hullDraft;
        auto      center = toJolt(origin) + JPH::Vec3(0, -v.hullDraft - h * .5f, 0);
        JPH::Vec3 corners[8];
        for (unsigned i = 0; i < 8; ++i)
            corners[i] = center + JPH::Vec3((i & 1 ? 1 : -1) * halfWidth, (i & 2 ? 1 : -1) * h * .5f, i & 4 ? 1 : -1);
        constexpr unsigned faces[6][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };
        for (const auto& face : faces)
            for (unsigned i = 1; i < 3; ++i)
            {
                auto a = corners[face[0]], b = corners[face[i]], c = corners[face[i + 1]];
                auto volume = tetra(center, a, b, c);
                v.cells[v.cellCount++] = { { fromJolt(center), fromJolt(a), fromJolt(b), fromJolt(c) }, volume.center, volume.volume };
                v.hullVolume += volume.volume;
            }
    }
}
void buildHullVolumes(VesselLayout& v)
{
    // Solve the hull's immersed depth for the chosen displacement. No force
    // multiplier can hide a mismatch between geometry and displaced volume.
    float low = .08f, high = v.draft * .95f;
    for (unsigned iteration = 0; iteration < 24; ++iteration)
    {
        v.hullDraft = (low + high) * .5f;
        makeCells(v);
        float volume = 0;
        for (unsigned i = 0; i < v.cellCount; ++i)
            volume += submergedTetrahedron(v.cells[i], { 0, 1, 0 }, 0).volume;
        if (volume * 1025 < v.mass)
            low = v.hullDraft;
        else
            high = v.hullDraft;
    }
    // Balance fore-and-aft moments at the authored waterline as well as mass.
    SubmergedVolume submerged = {};
    for (unsigned i = 0; i < v.cellCount; ++i)
        accumulate(submerged, submergedTetrahedron(v.cells[i], { 0, 1, 0 }, 0));
    v.centerOfMass.z = submerged.center.z;
}
HydrostaticState hydrostaticState(const VesselLayout& v, float heel, float height)
{
    auto            q = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), heel);
    auto            normal = q.Conjugated() * JPH::Vec3::sAxisY();
    SubmergedVolume wet = {};
    for (unsigned i = 0; i < v.cellCount; ++i)
        accumulate(wet, submergedTetrahedron(v.cells[i], fromJolt(normal), -height));
    auto arm = q * (toJolt(wet.center) - toJolt(v.centerOfMass));
    return { wet.volume, fromJolt(q * toJolt(wet.center) + JPH::Vec3(0, height, 0)), -arm.GetX(), height };
}
HydrostaticState equilibriumAtHeel(const VesselLayout& v, float heel)
{
    float low = -v.beam - v.draft, high = v.beam + v.draft;
    for (unsigned i = 0; i < 30; ++i)
    {
        float height = (low + high) * .5f;
        auto  state = hydrostaticState(v, heel, height);
        if (state.volume * 1025 > v.mass)
            low = height;
        else
            high = height;
    }
    return hydrostaticState(v, heel, (low + high) * .5f);
}
} // namespace mooring
