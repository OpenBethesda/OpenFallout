#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHSMOOTHER_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHSMOOTHER_H

#include "navmeshsearch.hpp"

#include <functional>
#include <vector>

/// The smoother of the executables, PathSmootherPOVSearch (FalloutNV.exe vtable 0x106B9CC, run 0x6AD770), with
/// bUseOldPathSmoothing 0: from the triangles a search found, the way an actor walks, as arcs around corners and things
/// to keep away from joined by straight lines, every one of them walked on the mesh
namespace FalloutScript::NavMeshSearch
{
    /// A point on a mesh: its triangle, the edge it was entered by (-1: none) and the point (NV location, 0x18 bytes)
    struct Location
    {
        Place mPlace;
        int mEntered = -1;
        Point mPoint{};
    };

    /// Where a straight line from `from` to `to` on the mesh ends (NV 0x6B2060): `to` (at the height of the mesh) when
    /// it gets there through at most 1000 triangles, otherwise where it met an edge it cannot cross; `mRatio`: the
    /// share of the triangles on the way that are preferred
    struct Walk
    {
        bool mArrived = false;
        Location mEnd;
        float mRatio = 0.f;
    };
    Walk walk(const Meshes& meshes, const Request& request, const Location& from, const Point& to);

    /// The height of the mesh at the point when the point is inside the triangle on the map and not `maxAbove` or more
    /// above it nor `maxBelow` or more below it (NV 0x697980)
    std::optional<float> heightOn(
        const Mesh& mesh, std::uint16_t triangle, const Point& point, float maxAbove, float maxBelow);

    /// The tangents of two circles (NV 0x6BB570): `side` 0 goes around the first anticlockwise, 1 clockwise; each is
    /// the way it goes around the second, and the unit normals of the circles where the tangent meets them
    struct Tangent
    {
        int mSide = 0;
        float mA[2]{};
        float mB[2]{};
    };
    std::vector<Tangent> tangents(const Point& a, float ra, const Point& b, float rb, int side);

    /// A point of the way the smoother gives: where it is and the way it goes on from there (NV 0x6E2280, 0x3C bytes)
    struct WayPoint
    {
        Location mLocation;
        Point mTangent{};
    };

    struct Smoothed
    {
        bool mFound = false;
        std::vector<WayPoint> mPoints;
        /// The doors on the triangles of the way (NV 0x6AD770 at its end, output +0x30)
        std::vector<std::uint32_t> mDoors;
    };

    /// The height of the land under a point of a mesh's cell (NV 0x45CBC0): lowest() in an interior, max() when the
    /// mesh has no cell
    using LandHeight = std::function<float(std::uint32_t mesh, const Point& point)>;

    /// The way along `corridor` (a search's triangles) from `start` to `end`
    Smoothed smooth(const Meshes& meshes, const Request& request, const std::vector<Step>& corridor, Point start,
        Point end, const LandHeight& land);
}

#endif
