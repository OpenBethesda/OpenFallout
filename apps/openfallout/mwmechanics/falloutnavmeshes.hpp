#ifndef GAME_MWMECHANICS_FALLOUTNAVMESHES_H
#define GAME_MWMECHANICS_FALLOUTNAVMESHES_H

#include <components/esm/formid.hpp>
#include <components/esm/refid.hpp>
#include <components/falloutscript/navmeshdoors.hpp>
#include <components/falloutscript/navmeshpather.hpp>
#include <components/falloutscript/navmeshsmoother.hpp>

#include <limits>
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
        /// The height of the water of a loaded cell (NV 0x45CBC0), lowest() when it has none or is not loaded
        float waterOf(ESM::FormId cell) const;

        /// The navigation data of every mesh, loaded or not (NAVI, NV 0x6B5FAD), for the coarse route
        FalloutScript::NavMeshRoute::Infos infos() const;
        /// The cell and world space of a loaded mesh, the cell 0 when it is not loaded
        FalloutScript::NavMeshPather::Spot placeOf(std::uint32_t mesh) const;
        /// NV 0x6D8A00: the meshes of a place's interior cell, or outside those of the loaded cells of the 3 by 3
        /// around its cell whose square meets the square of `distance` round the place (0x6D8B64 to 0x6D8C41)
        std::vector<const FalloutScript::NavMeshSearch::Mesh*> near(
            const FalloutScript::NavMeshPather::Spot& spot, float distance) const;
        /// Every loaded mesh (NV 0x6D91D0)
        std::vector<std::uint32_t> loadedMeshes() const;

        /// An in-cell door over the meshes (NV 0x6C3640): one that is not open marks the triangles under the boxes of
        /// its bodies (0x6C5190, 0x6997E0), an open one takes its marks off (0x6C4FB0, 0x699A30). `worldspace` is that
        /// of the door's cell, empty in an interior.
        void setDoor(std::uint32_t door, ESM::FormId cell, const ESM::RefId& worldspace,
            const std::vector<FalloutScript::NavMeshDoors::Shape>& shapes, bool closed);
        /// Whether the door's marks are on the meshes that are loaded: not when a mesh was loaded since the doors were
        /// last marked
        bool doorMarked(std::uint32_t door) const { return !mMeshesAdded && mDoorMarks.contains(door); }
        /// Every closed door has marked the meshes that are loaded
        void doorsMarked() { mMeshesAdded = false; }

    private:
        struct Loaded
        {
            FalloutScript::NavMeshSearch::Mesh mMesh;
            ESM::FormId mCell;
            ESM::RefId mWorldspace; // empty in an interior
            bool mDisabled = false;
            int mGridX = 0; // of an exterior cell
            int mGridY = 0;
            float mWater = std::numeric_limits<float>::lowest(); // the cell's height of water (NV 0x5471E0)
            std::uint32_t mWorld = 0; // the world space's form, 0 in an interior
        };
        std::map<std::uint32_t, FalloutScript::NavMeshRoute::Info> mInfos;
        /// The meshes each closed door marked
        std::map<std::uint32_t, std::vector<std::uint32_t>> mDoorMarks;
        bool mMeshesAdded = false;
        std::unordered_map<ESM::FormId, std::vector<const ESM4::NavMesh*>> mByCell;
        bool mIndexed = false;
        std::map<std::uint32_t, Loaded> mLoaded;
        std::map<ESM::FormId, std::vector<std::uint32_t>> mCells;
    };

    /// The mesh of a NAVM record as the search reads it (NV 0x692950)
    FalloutScript::NavMeshSearch::Mesh toSearchMesh(const ESM4::NavMesh& record);
}

#endif
