#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHPATHER_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHPATHER_H

#include "navmeshroute.hpp"
#include "navmeshsmoother.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

/// The pather of the executables (FalloutNV.exe, the object of 0x6C8D30, run from 0x6C8E10): from a path request to
/// the points an actor walks. The coarse route over the navigation data (NavMeshRoute), then the stretches of it in
/// loaded cells, each with its ends put on the meshes, a straight look first, the search over the triangles (up to
/// the request's number of times, each time with more meshes and the narrow edges of the last try avoided) and the
/// smoother along them
namespace FalloutScript::NavMeshPather
{
    using NavMeshSearch::Candidate;
    using NavMeshSearch::Mesh;
    using NavMeshSearch::Point;
    using NavMeshSearch::Request;
    using NavMeshSearch::WayPoint;

    /// A place as the pather carries it (NV PathingLocation, 0x28 bytes)
    struct Spot
    {
        Point mPoint{}; // +4
        /// +0x1C: its cell (for a place outside, the exterior cell under it); 0 when that is not loaded
        std::uint32_t mCell = 0;
        /// The world space of a place outside, 0 inside (0x6DD4F0)
        std::uint32_t mWorld = 0;
        /// +0x10: the mesh it is on (its NavMeshInfo); 0 while not known
        std::uint32_t mMesh = 0;
        /// +0x24, none for 0xFFFF
        std::optional<std::uint16_t> mTriangle;
    };

    /// What the pather reads of the world
    struct World
    {
        NavMeshSearch::Meshes mMeshes;
        NavMeshRoute::Infos mInfos;
        /// The meshes of a loaded cell (NV PathingLocation 0x6DD280)
        std::function<std::vector<Candidate>(std::uint32_t cell)> mCandidates;
        /// A place at a point, with its cell and world space and no mesh (NV 0x6DD280), `near` a place it is by
        std::function<Spot(const Point& point, const Spot& near)> mSpotAt;
        /// The cell and world space of a mesh, the cell 0 when it is not loaded
        std::function<Spot(std::uint32_t mesh)> mSpotOfMesh;
        /// The meshes of the cells around a place within a distance (NV 0x6D8A00: an interior's own; outside, the
        /// cells of the 3 by 3 around it whose square meets the place's square of that distance)
        std::function<std::vector<const Mesh*>(const Spot& spot, float distance)> mNear;
        /// Every mesh of the loaded cells (NV 0x6D91D0)
        std::function<std::vector<std::uint32_t>()> mLoaded;
        /// The height of the water at a point of a cell (NV 0x45CBC0)
        std::function<float(std::uint32_t cell, const Point& point)> mWater;
        /// A ray of the pather (NV 0x6E6C00, the filter 0x00030026) from one point to another hits nothing
        std::function<bool(const Point& from, const Point& to)> mClear;
        /// Where a ray of the pather straight down from a point for a length hits, if it does
        std::function<std::optional<Point>(const Point& from, float length)> mDown;
        NavMeshSearch::DoorRule mDoors;
        NavMeshRoute::Doors mRouteDoors;
    };

    /// The points of a path (NV path object +0x20, 0x3C bytes each), with the door each one is tagged with (+0x38,
    /// 0x6CC560; 0 for none)
    struct Path
    {
        std::vector<WayPoint> mPoints;
        std::vector<std::uint32_t> mDoors;
        /// +0x40: the path stops short of the goal (setter 0x6C9FA0)
        bool mPartial = false;
    };

    /// NV 0x6DD6F0: the mesh of a place and, when there is one, its triangle under the place; false when it is on no
    /// usable mesh
    bool resolve(const World& world, Spot& spot);

    /// NV 0x6C8E10 with 0x6C8F50 and 0x6CA0E0: the path from `start` to `goal`, or nothing
    std::optional<Path> findPath(const World& world, const Request& request, Spot start, Spot goal);
}

#endif
