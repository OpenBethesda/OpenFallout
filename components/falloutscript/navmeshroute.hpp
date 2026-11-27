#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHROUTE_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHROUTE_H

#include "navmeshsearch.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

/// The coarse route of a path, over the navigation meshes as the navigation data (NAVI) joins them, before the search
/// over their triangles: NavMeshInfoSearch of the executables (FalloutNV.exe vtable 0x106C22C, run 0x6B8C50, called by
/// 0x6C94C0)
namespace FalloutScript::NavMeshRoute
{
    using NavMeshSearch::Point;

    /// One navigation mesh in the navigation data (the object of 0x5C bytes of NV 0x6B46A0)
    struct Info
    {
        std::uint32_t mId = 0; // +0, the mesh's form
        std::uint32_t mFlags = 0; // +8
        /// The world space it is in, 0 in an interior (0x690800 of +0x1C)
        std::uint32_t mWorld = 0;
        Point mCentre{}; // +0x10
        float mCost = 0.f; // +0x20
        std::vector<std::uint32_t> mNeighbours; // +0x24
        std::vector<std::uint32_t> mNeighboursToo; // +0x34
        std::vector<std::uint32_t> mDoors; // +0x44, teleport doors
    };

    /// Not to be used (NV 0x68F320)
    constexpr std::uint32_t InfoFlag_Unusable = 0x10;

    using Infos = std::function<const Info*(std::uint32_t mesh)>;

    /// A teleport door of a mesh, as the search reads it (NV 0x6B88B9 to 0x6B8B31)
    struct Door
    {
        /// The mesh the linked door is on: the form of its XNDP (0x6B89CD to 0x6B89FD)
        std::uint32_t mTarget = 0;
        /// Where one stands on this side: the teleport marker of the linked door (0x568FA0, 0x6B8AAA)
        Point mNear{};
        /// Where one comes out on the far side: the door's own teleport marker (0x6B8B10)
        Point mFar{};
        /// Its base has the flag Minimal Use (0x518000)
        bool mMinimalUse = false;
        /// The searcher passes its lock without picking it (0x502450 true with its out flag clear, 0x6B891B)
        bool mPasses = false;
    };

    /// Nothing for a door that is disabled (form flag 0x800, 0x6B88C4) or has no teleport, no linked door or no XNDP
    /// on the linked door
    using Doors = std::function<std::optional<Door>(std::uint32_t door)>;

    struct Found
    {
        /// The goal was reached; else the route goes to the mesh nearest the goal by the estimate (0x6B8F7E)
        bool mReached = false;
        /// The meshes from the start's to the last (0x996AE0)
        std::vector<const Info*> mInfos;
        /// The door taken from each mesh to the next, 0 for none
        std::vector<std::uint32_t> mDoors;
    };

    /// NV 0x6B8C50 as 0x6C94C0 runs it. `doors`: the request lets the searcher through doors (its actor information,
    /// 0 when request +0x98 is set, 0x6C9555). `ignoreLocks`: request +0xA4 (mode 1, 0x6C9577)
    Found search(const Infos& infos, const Doors& doors, const Info& start, const Point& startPosition,
        const Info& goal, const Point& goalPosition, bool withDoors, bool ignoreLocks);

    /// A point of the coarse route (the points of 0x30 bytes, NV 0x6F4D00 to 0x6F4F90)
    struct RoutePoint
    {
        enum class Kind
        {
            Location, // the start or the goal (flag 2)
            Mesh, // the centre of a mesh on the way (flag 1)
            Landing, // where one comes out of a door (flag 3)
            Door, // where one stands to use a door (flag 7)
        };
        Kind mKind = Kind::Location;
        Point mPosition{};
        const Info* mInfo = nullptr;
        std::uint32_t mDoor = 0;
    };

    /// The route that NV 0x6C94C0 makes of a search (0x6C9611 to 0x6C9899)
    std::vector<RoutePoint> route(const Found& found, const Doors& doors, const Info& start, const Point& startPosition,
        const Info& goal, const Point& goalPosition);
}

#endif
