#include "Hydrodynamics.h"
#include "PhysicsMath.h"
#include <Jolt/Physics/Collision/Shape/PolyhedronSubmergedVolumeCalculator.h>
#include <algorithm>
#include <cmath>
namespace mooring
{
static SubmergedVolume tetra(JPH::Vec3Arg vertexA, JPH::Vec3Arg vertexB, JPH::Vec3Arg vertexC, JPH::Vec3Arg vertexD)
{
    float volume = std::fabs((vertexB - vertexA).Dot((vertexC - vertexA).Cross(vertexD - vertexA))) / 6;
    return { volume, fromJolt((vertexA + vertexB + vertexC + vertexD) * .25f) };
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
    for (unsigned vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
        vertices[vertexIndex] = toJolt(cell.vertices[vertexIndex]);
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
    for (unsigned vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
        if (vertexIndex != reference)
            face[count++] = vertexIndex;
    auto vertexA = vertices[face[0]], vertexB = vertices[face[1]], vertexC = vertices[face[2]];
    if ((vertexB - vertexA).Cross(vertexC - vertexA).Dot(vertices[reference] - vertexA) > 0)
        std::swap(face[1], face[2]);
    calculator.AddFace(int(face[0]), int(face[1]), int(face[2]));
    float     volume;
    JPH::Vec3 center;
    calculator.GetResult(volume, center);
    return { volume, fromJolt(center) };
}
Vec3 hullVertex(const VesselLayout& layout, unsigned hull, unsigned vertex)
{
    // Symmetric displacement hull with an immersed transom and a fine bow.
    // The same authored sections feed graphics, collision and hydrostatics.
    constexpr float widths[HullSections] = { .69f, .92f, 1, 1, .91f, .73f, .43f, .015f };
    constexpr float xs[HullSectionVertices] = { 0, -.55f, -.94f, -1, 1, .94f, .55f };
    constexpr float ys[HullSectionVertices] = { -1, -.65f, -.10f, 0, 0, -.10f, -.65f };
    unsigned        row = vertex / HullSectionVertices, corner = vertex % HullSectionVertices;
    float           halfWidth = (layout.hullSpacing > 0 ? (layout.beam - layout.hullSpacing) : layout.beam) * .5f;
    float lateralPosition = xs[corner] * widths[row] * halfWidth + (layout.hullSpacing > 0 ? (hull ? .5f : -.5f) * layout.hullSpacing : 0);
    float verticalPosition = corner == 3 || corner == 4 ? .85f : ys[corner] * layout.hullDraft;
    float longitudinalPosition = (float(row) / (HullSections - 1) - .5f) * layout.length;
    return { lateralPosition, verticalPosition, longitudinalPosition };
}
void hullTriangle(const VesselLayout& layout, unsigned hull, unsigned triangle, Vec3 out[3])
{
    constexpr unsigned sides = (HullSections - 1) * HullSectionVertices * 2;
    if (triangle < sides)
    {
        unsigned edge = triangle / 2, row = edge / HullSectionVertices, vertexA = edge % HullSectionVertices,
                 vertexB = (vertexA + 1) % HullSectionVertices;
        out[0] = hullVertex(layout, hull, row * HullSectionVertices + vertexA);
        out[1] = hullVertex(layout, hull, (row + 1) * HullSectionVertices + (triangle % 2 ? vertexB : vertexA));
        out[2] = hullVertex(layout, hull, (triangle % 2 ? row : row + 1) * HullSectionVertices + vertexB);
        // Mirror the diagonal on the opposite side. Twisted section quads
        // otherwise introduce an artificial port/starboard stability bias.
        if (vertexA >= 4)
        {
            out[0] = hullVertex(layout, hull, row * HullSectionVertices + (triangle % 2 ? vertexB : vertexA));
            out[1] = hullVertex(layout, hull, (row + 1) * HullSectionVertices + vertexA);
            out[2] = hullVertex(layout, hull, (triangle % 2 ? row + 1 : row) * HullSectionVertices + vertexB);
        }
    }
    else
    {
        unsigned cap = (triangle - sides) / (HullSectionVertices - 2), fanIndex = (triangle - sides) % (HullSectionVertices - 2) + 1;
        unsigned row = cap ? HullSections - 1 : 0;
        out[0] = hullVertex(layout, hull, row * HullSectionVertices);
        out[1] = hullVertex(layout, hull, row * HullSectionVertices + fanIndex + (cap ? 1 : 0));
        out[2] = hullVertex(layout, hull, row * HullSectionVertices + fanIndex + (cap ? 0 : 1));
    }
}
static void makeCells(VesselLayout& layout)
{
    layout.cellCount = 0;
    layout.hullVolume = 0;
    for (unsigned hull = 0; hull < (layout.hullSpacing > 0 ? 2u : 1u); ++hull)
    {
        Vec3 origin = { layout.hullSpacing > 0 ? (hull ? .5f : -.5f) * layout.hullSpacing : 0, 0, 0 };
        for (unsigned triangleIndex = 0; triangleIndex < HullTriangles; ++triangleIndex)
        {
            Vec3 points[3];
            hullTriangle(layout, hull, triangleIndex, points);
            auto volume = tetra(toJolt(origin), toJolt(points[0]), toJolt(points[1]), toJolt(points[2]));
            if (volume.volume < 1e-8f)
                continue;
            auto& cell = layout.cells[layout.cellCount++];
            cell = { { origin, points[0], points[1], points[2] }, volume.center, volume.volume };
            layout.hullVolume += volume.volume;
        }
        // Fin keel volume uses the same box as the Jolt compound collider.
        float     halfWidth = layout.hullSpacing > 0 ? .14f : .10f, keelHeight = layout.draft - layout.hullDraft;
        auto      center = toJolt(origin) + JPH::Vec3(0, -layout.hullDraft - keelHeight * .5f, 0);
        JPH::Vec3 corners[8];
        for (unsigned cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
            corners[cornerIndex] = center + JPH::Vec3((cornerIndex & 1 ? 1 : -1) * halfWidth, (cornerIndex & 2 ? 1 : -1) * keelHeight * .5f,
                                                      cornerIndex & 4 ? 1 : -1);
        constexpr unsigned faces[6][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };
        for (const auto& face : faces)
            for (unsigned faceTriangleIndex = 1; faceTriangleIndex < 3; ++faceTriangleIndex)
            {
                auto vertexA = corners[face[0]], vertexB = corners[face[faceTriangleIndex]], vertexC = corners[face[faceTriangleIndex + 1]];
                auto volume = tetra(center, vertexA, vertexB, vertexC);
                layout.cells[layout.cellCount++] = { { fromJolt(center), fromJolt(vertexA), fromJolt(vertexB), fromJolt(vertexC) },
                                                     volume.center,
                                                     volume.volume };
                layout.hullVolume += volume.volume;
            }
    }
}
void buildHullVolumes(VesselLayout& layout)
{
    // Solve the hull's immersed depth for the chosen displacement. No force
    // multiplier can hide a mismatch between geometry and displaced volume.
    float low = .08f, high = layout.draft * .95f;
    for (unsigned iteration = 0; iteration < 24; ++iteration)
    {
        layout.hullDraft = (low + high) * .5f;
        makeCells(layout);
        float volume = 0;
        for (unsigned cellIndex = 0; cellIndex < layout.cellCount; ++cellIndex)
            volume += submergedTetrahedron(layout.cells[cellIndex], { 0, 1, 0 }, 0).volume;
        if (volume * WaterDensity < layout.mass)
            low = layout.hullDraft;
        else
            high = layout.hullDraft;
    }
    // Balance fore-and-aft moments at the authored waterline as well as mass.
    SubmergedVolume submerged = {};
    for (unsigned cellIndex = 0; cellIndex < layout.cellCount; ++cellIndex)
        accumulate(submerged, submergedTetrahedron(layout.cells[cellIndex], { 0, 1, 0 }, 0));
    layout.centerOfMass.z = submerged.center.z;
}
HydrostaticState hydrostaticState(const VesselLayout& layout, float heel, float height)
{
    auto            heelRotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), heel);
    auto            normal = heelRotation.Conjugated() * JPH::Vec3::sAxisY();
    SubmergedVolume wet = {};
    for (unsigned cellIndex = 0; cellIndex < layout.cellCount; ++cellIndex)
        accumulate(wet, submergedTetrahedron(layout.cells[cellIndex], fromJolt(normal), -height));
    auto arm = heelRotation * (toJolt(wet.center) - toJolt(layout.centerOfMass));
    return { wet.volume, fromJolt(heelRotation * toJolt(wet.center) + JPH::Vec3(0, height, 0)), -arm.GetX(), height };
}
HydrostaticState equilibriumAtHeel(const VesselLayout& layout, float heel)
{
    float low = -layout.beam - layout.draft, high = layout.beam + layout.draft;
    for (unsigned iteration = 0; iteration < 30; ++iteration)
    {
        float height = (low + high) * .5f;
        auto  state = hydrostaticState(layout, heel, height);
        if (state.volume * WaterDensity > layout.mass)
            low = height;
        else
            high = height;
    }
    return hydrostaticState(layout, heel, (low + high) * .5f);
}
} // namespace mooring
