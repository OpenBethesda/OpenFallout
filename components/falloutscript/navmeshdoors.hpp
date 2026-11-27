#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHDOORS_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHDOORS_H

#include "navmeshsearch.hpp"

#include <array>
#include <cstdint>
#include <vector>

/// The in-cell doors on the navmeshes, as the obstacle manager of the executables marks them (FalloutNV.exe
/// ObstacleTaskData kind 0x30 for a closed door, 0x6997E0; kind 0x40 for an open one, 0x699A30): the triangles under a
/// closed door get the flag 0x1000 and a record of the door, which the search reads (0x6A71A8)
namespace FalloutScript::NavMeshDoors
{
    using NavMeshSearch::Mesh;
    using NavMeshSearch::Point;
    /// Three directions, the columns of a turn
    using Axes = std::array<Point, 3>;

    /// The box of one collision body of a door (the shape of 0x8C bytes, NV 0x6C23C0, filled by 0x6C2650)
    struct Shape
    {
        Point mOrigin{}; // +0x10, the origin of the body on the map
        Axes mFrame{}; // +0x1C: across, along and up
        Point mMin{}; // +0x40, in the frame from the origin
        Point mMax{}; // +0x4C
        Point mWorldMin{}; // +0x58, the box on the map
        Point mWorldMax{}; // +0x64
    };

    /// NV 0x6C2650: the box of a body that is turned by `body` (its columns) and stands at `origin`, from the box of
    /// its collision shape in its own frame (shape vfunc 0x1C)
    Shape shapeOf(const Axes& body, const Point& origin, const Point& localMin, const Point& localMax);

    /// NV 0x6D93D0: two boxes overlap, the sides not counting
    bool overlaps(const Point& aMin, const Point& aMax, const Point& bMin, const Point& bMax);

    /// NV 0x6987D0: the triangles of the mesh under the box, which `offset` grows (or, below zero, shrinks) on each
    /// side
    std::vector<std::uint16_t> trianglesUnder(const Mesh& mesh, const Shape& shape, const Point& offset);

    /// NV 0x6997E0: the door is closed over the mesh; its box is made thinner by 0.49 of its thickness on each side
    void close(Mesh& mesh, std::uint32_t door, const Shape& shape);

    /// NV 0x699A30: the door is open; every triangle it was on loses the flag
    void open(Mesh& mesh, std::uint32_t door);
}

#endif
