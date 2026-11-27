#ifndef GAME_MWMECHANICS_FALLOUTNAVMESHES_H
#define GAME_MWMECHANICS_FALLOUTNAVMESHES_H

#include <components/esm/formid.hpp>
#include <components/esm/refid.hpp>
#include <components/falloutscript/navmeshdoors.hpp>
#include <components/falloutscript/navmeshsmoother.hpp>

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace ESM4
{
    struct NavMesh;
}

namespace OFWorld
{
    class CellStore;
    class ESMStore;
}

namespace OFMechanics
{
    /// The navmeshes of the cells that are loaded, as the path search of the executables reads them (NavMesh, NV
    /// 0x692950). A mesh keeps what the game adds to it while it runs (the marks where actors got stuck, the doors on
    /// it) for as long as its cell is loaded, and loses it with the cell (NV 0x68EE72).
    class FalloutNavMeshes
    {
    public:
        void update(const OFWorld::ESMStore& store, const std::set<OFWorld::CellStore*, std::less<>>& cells);

        FalloutScript::NavMeshSearch::Meshes meshes() const;
        FalloutScript::NavMeshSearch::Mesh* find(std::uint32_t id);
        /// The meshes of a loaded cell, where a point in it may be (NV PathingLocation 0x6DD280)
        std::vector<FalloutScript::NavMeshSearch::Candidate> candidates(ESM::FormId cell) const;
        /// The height of the land under a point of a mesh's cell (NV 0x45CBC0): lowest() in an interior
        FalloutScript::NavMeshSearch::LandHeight land() const;

        /// An in-cell door over the meshes (NV 0x6C3640): one that is not open marks the triangles under the boxes of
        /// its bodies (0x6C5190, 0x6997E0), an open one takes its marks off (0x6C4FB0, 0x699A30). `worldspace` is that
        /// of the door's cell, empty in an interior.
        void setDoor(std::uint32_t door, ESM::FormId cell, const ESM::RefId& worldspace,
            const std::vector<FalloutScript::NavMeshDoors::Shape>& shapes, bool closed);
        /// Whether the door's marks are on the meshes that are loaded (a mesh that is loaded again has none)
        bool doorMarked(std::uint32_t door) const { return mDoorMarks.contains(door); }

    private:
        struct Loaded
        {
            FalloutScript::NavMeshSearch::Mesh mMesh;
            ESM::FormId mCell;
            ESM::RefId mWorldspace; // empty in an interior
            bool mDisabled = false;
            int mGridX = 0; // of an exterior cell
            int mGridY = 0;
        };
        /// The meshes each closed door marked
        std::map<std::uint32_t, std::vector<std::uint32_t>> mDoorMarks;
        std::unordered_map<ESM::FormId, std::vector<const ESM4::NavMesh*>> mByCell;
        bool mIndexed = false;
        std::map<std::uint32_t, Loaded> mLoaded;
        std::map<ESM::FormId, std::vector<std::uint32_t>> mCells;
    };

    /// The mesh of a NAVM record as the search reads it (NV 0x692950)
    FalloutScript::NavMeshSearch::Mesh toSearchMesh(const ESM4::NavMesh& record);
}

#endif
