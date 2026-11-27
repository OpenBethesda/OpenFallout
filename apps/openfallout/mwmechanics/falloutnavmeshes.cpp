#include "falloutnavmeshes.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

#include <components/debug/debuglog.hpp>

#include <components/esm4/loadnavm.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"

namespace OFMechanics
{
    namespace NavMeshSearch = FalloutScript::NavMeshSearch;

    NavMeshSearch::Mesh toSearchMesh(const ESM4::NavMesh& record)
    {
        NavMeshSearch::Mesh mesh;
        mesh.mId = record.mId.toUint32();
        mesh.mVertices.reserve(record.mVertices.size());
        for (const ESM4::Vertex& v : record.mVertices)
            mesh.mVertices.push_back({ v.x, v.y, v.z });
        mesh.mTriangles.reserve(record.mTriangles.size());
        for (const ESM4::NavMesh::PathTriangle& t : record.mTriangles)
        {
            NavMeshSearch::Triangle triangle;
            triangle.mVertices = { t.vertices[0], t.vertices[1], t.vertices[2] };
            triangle.mEdges = { t.edges[0], t.edges[1], t.edges[2] };
            triangle.mFlags = t.flags; // NV 0x692FE7
            mesh.mTriangles.push_back(triangle);
        }
        for (const ESM4::NavMesh::ExtConnection& c : record.mExternal)
            mesh.mExternal.push_back({ c.navMesh, c.triangleIndex, c.unknown });
        const ESM4::NavMesh::TriangleGrid& g = record.mTriangleGrid;
        mesh.mGrid.mDivisor = g.divisor;
        mesh.mGrid.mCellX = g.cellX;
        mesh.mGrid.mCellY = g.cellY;
        mesh.mGrid.mMin = { g.minX, g.minY, g.minZ };
        mesh.mGrid.mMax = { g.maxX, g.maxY, g.maxZ };
        mesh.mGrid.mCells = g.cells;
        // A plugin's indices are not trusted: a triangle with a corner that is not there makes the mesh unusable, and
        // a link to a triangle or connection that is not there is no link
        for (NavMeshSearch::Triangle& triangle : mesh.mTriangles)
        {
            for (const std::uint16_t corner : triangle.mVertices)
                if (corner >= mesh.mVertices.size())
                {
                    Log(Debug::Warning) << "NavMesh " << std::hex << record.mId.toUint32() << std::dec
                                        << " has a triangle with a corner that is not there";
                    return NavMeshSearch::Mesh{ mesh.mId };
                }
            for (unsigned edge = 0; edge < 3; ++edge)
            {
                const bool external = (triangle.mFlags & (1u << edge)) != 0;
                const std::size_t count = external ? mesh.mExternal.size() : mesh.mTriangles.size();
                if (triangle.mEdges[edge] != 0xffff && triangle.mEdges[edge] >= count)
                {
                    triangle.mEdges[edge] = 0xffff;
                    triangle.mFlags &= ~(1u << edge);
                }
            }
        }
        for (std::vector<std::uint16_t>& square : mesh.mGrid.mCells)
            std::erase_if(square, [&](std::uint16_t t) { return t >= mesh.mTriangles.size(); });
        return mesh;
    }

    void FalloutNavMeshes::update(
        const OFWorld::ESMStore& store, const std::set<OFWorld::CellStore*, std::less<>>& cells)
    {
        if (!mIndexed)
        {
            for (const ESM4::NavMesh& record : store.get<ESM4::NavMesh>())
                mByCell[record.mCell].push_back(&record);
            mIndexed = true;
        }
        std::set<ESM::FormId> active;
        for (const OFWorld::CellStore* cellStore : cells)
        {
            const OFWorld::Cell& cell = *cellStore->getCell();
            const ESM::FormId* id = cell.getId().getIf<ESM::FormId>();
            if (id == nullptr)
                continue;
            active.insert(*id);
            if (mCells.contains(*id))
                continue;
            std::vector<std::uint32_t>& ids = mCells[*id];
            const auto found = mByCell.find(*id);
            if (found == mByCell.end())
                continue;
            for (const ESM4::NavMesh* record : found->second)
            {
                Loaded& loaded = mLoaded[record->mId.toUint32()];
                loaded.mMesh = toSearchMesh(*record);
                loaded.mCell = *id;
                if (cell.isExterior())
                {
                    loaded.mWorldspace = cell.getWorldSpace();
                    loaded.mGridX = cell.getGridX();
                    loaded.mGridY = cell.getGridY();
                }
                // the closed doors mark it as they mark the others (marking a mesh again adds nothing, 0x699990)
                mMeshesAdded = true;
                loaded.mDisabled = (record->mFlags & 0x800) != 0; // NV 0x440DA0
                ids.push_back(loaded.mMesh.mId);
            }
        }
        for (auto it = mCells.begin(); it != mCells.end();)
        {
            if (active.contains(it->first))
            {
                ++it;
                continue;
            }
            for (const std::uint32_t mesh : it->second)
                mLoaded.erase(mesh);
            it = mCells.erase(it);
        }
    }

    NavMeshSearch::Meshes FalloutNavMeshes::meshes() const
    {
        return [this](std::uint32_t id) -> const NavMeshSearch::Mesh* {
            const auto found = mLoaded.find(id);
            return found != mLoaded.end() ? &found->second.mMesh : nullptr;
        };
    }

    NavMeshSearch::Mesh* FalloutNavMeshes::find(std::uint32_t id)
    {
        const auto found = mLoaded.find(id);
        return found != mLoaded.end() ? &found->second.mMesh : nullptr;
    }

    std::vector<NavMeshSearch::Candidate> FalloutNavMeshes::candidates(ESM::FormId cell) const
    {
        std::vector<NavMeshSearch::Candidate> result;
        const auto found = mCells.find(cell);
        if (found == mCells.end())
            return result;
        for (const std::uint32_t id : found->second)
        {
            const Loaded& loaded = mLoaded.at(id);
            result.push_back({ &loaded.mMesh, loaded.mDisabled });
        }
        return result;
    }

    NavMeshSearch::LandHeight FalloutNavMeshes::land() const
    {
        return [this](std::uint32_t mesh, const NavMeshSearch::Point& p) {
            const auto found = mLoaded.find(mesh);
            if (found == mLoaded.end())
                return std::numeric_limits<float>::max(); // no cell: as the game (0x6AD7D0)
            if (found->second.mWorldspace.empty())
                return std::numeric_limits<float>::lowest(); // an interior (0x45CBC0, 0x1015F5C)
            return OFBase::Environment::get().getWorld()->getTerrainHeightAt(
                osg::Vec3f(p[0], p[1], p[2]), found->second.mWorldspace);
        };
    }

    void FalloutNavMeshes::setDoor(std::uint32_t door, ESM::FormId cell, const ESM::RefId& worldspace,
        const std::vector<FalloutScript::NavMeshDoors::Shape>& shapes, bool closed)
    {
        namespace NavMeshDoors = FalloutScript::NavMeshDoors;
        const auto marked = mDoorMarks.find(door);
        if (!closed)
        {
            if (marked == mDoorMarks.end())
                return;
            for (const std::uint32_t id : marked->second)
                if (NavMeshSearch::Mesh* mesh = find(id))
                    NavMeshDoors::open(*mesh, door);
            mDoorMarks.erase(marked);
            return;
        }
        std::vector<std::uint32_t>& meshes = mDoorMarks[door];
        constexpr float cellSize = 4096.f; // 0x1017A10
        constexpr float big = std::numeric_limits<float>::max();
        for (const NavMeshDoors::Shape& shape : shapes)
        {
            // NV 0x6C1EA0: an interior's own cell; outside, the loaded cells around the one under the body that its
            // box is over
            const int gx = static_cast<int>(shape.mOrigin[0]) >> 12;
            const int gy = static_cast<int>(shape.mOrigin[1]) >> 12;
            for (auto& [id, loaded] : mLoaded)
            {
                if (worldspace.empty() ? loaded.mCell != cell : loaded.mWorldspace != worldspace)
                    continue;
                if (!worldspace.empty())
                {
                    if (std::abs(loaded.mGridX - gx) > 1 || std::abs(loaded.mGridY - gy) > 1)
                        continue;
                    const NavMeshSearch::Point low{ loaded.mGridX * cellSize, loaded.mGridY * cellSize, -big };
                    const NavMeshSearch::Point high{ low[0] + cellSize, low[1] + cellSize, big };
                    if (!NavMeshDoors::overlaps(low, high, shape.mWorldMin, shape.mWorldMax))
                        continue;
                }
                // NV 0x6C2050: the meshes whose grid's box, 128 higher, it overlaps
                NavMeshSearch::Point top = loaded.mMesh.mGrid.mMax;
                top[2] += 128.f; // 0x102E430
                if (!NavMeshDoors::overlaps(loaded.mMesh.mGrid.mMin, top, shape.mWorldMin, shape.mWorldMax))
                    continue;
                NavMeshDoors::close(loaded.mMesh, door, shape);
                if (std::find(meshes.begin(), meshes.end(), id) == meshes.end())
                    meshes.push_back(id);
            }
        }
    }
}
