#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHDOORS_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHDOORS_H

#include "navmeshsearch.hpp"

#include <array>
#include <cstdint>
#include <functional>
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

    /// An owner as the lock gate reads one (NV 0x546EE0, 0x502550)
    struct Owner
    {
        enum class Kind
        {
            None,
            Npc, // an NPC_ base (RTTI TESNPC, 0x546F25)
            Faction, // form type 8 (0x50259F), RTTI TESFaction (0x546F61)
            Other,
        };
        Kind mKind = Kind::None;
        std::uint32_t mForm = 0;
        /// The rank a member needs (-1 in the data counts as 0, 0x567A9D)
        int mRank = 0;
    };

    /// What the search reads of a door's lock (the context of NV 0x501E80)
    struct DoorLock
    {
        /// It has lock data (XLOC, its own or its teleport partner's; 0x569160)
        bool mHasLock = false;
        bool mLocked = false; // flags bit 0x01 (0x5021A0)
        std::uint32_t mKey = 0; // +4
        /// It needs its key (the class of its level is past the hardest, 0x430B40): a leveled lock is capped at 99
        /// (0x430A7B), so only a plain one can
        bool mKeyOnly = false;
        /// The owner of the door's cell (ctx+0, 0x546A40) with that cell's rank
        Owner mCellOwner;
        /// A load door's far cell (ctx+4) and its owner
        bool mHasDestination = false;
        Owner mDestinationOwner;
        /// The door's own owner (ctx+0xC, 0x567790) and rank (ctx+0x10, 0x5679F0)
        Owner mOwner;
        /// The global of the door (ctx+0x14, 0x567960) is there and not 0 (0x5025B9 to 0x5025E8)
        bool mGlobalSet = false;
    };

    /// What the search reads of the actor it searches for (NV 0x502670)
    struct Searcher
    {
        std::uint32_t mBase = 0; // +0
        bool mNpc = false; // its base is an NPC_ (type 0x2A)
        /// Its rank in a faction, -1 when it is not a member (0x47D680)
        std::function<int(std::uint32_t faction)> mRank;
        /// It carries at least one of the item (0x4CFFE0); none when it has no inventory (+4)
        std::function<bool(std::uint32_t item)> mCarries;
        /// The class of its base has the flag 0x02 (0x5F6E60, the class DATA flags at +0x10)
        bool mGuardClass = false;
        /// Its package's type is 0x15 (+8, 0x8A61B0)
        bool mPackage15 = false;
    };

    /// NV 0x502450: the searcher passes the lock, and whether it does by picking it (with a Bobby Pin, MISC 0xA)
    bool passesLock(const DoorLock& lock, const Searcher& searcher, std::uint32_t bobbyPin, bool& byPicking);
}

#endif
