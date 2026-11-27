#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHSEARCH_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHSEARCH_H

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

/// The path search of the executables over the navmeshes of Fallout 3 and New Vegas (NavMeshSearch, FalloutNV.exe
/// 0x6A6B80 to 0x6A7AC9 and the driver 0x6A1CF0): A* from triangle to triangle, with the costs of 0x6A6FA0
namespace FalloutScript::NavMeshSearch
{
    using Point = std::array<float, 3>;

    /// The flags of a triangle that the search tests (NV 0x691140)
    enum TriangleFlag : std::uint32_t
    {
        /// Edge 0, 1 or 2 leads to another mesh: its number is the external connection (NV 0x68F1D0)
        Flag_External0 = 0x1,
        Flag_External1 = 0x2,
        Flag_External2 = 0x4,
        /// Only actors up to fPathingLargeActorRadius go there (NV 0x6A72E3)
        Flag_NoLargeActors = 0x10,
        /// A step into it costs a hundredth (NV 0x693B70, 0x6A74A4)
        Flag_Preferred = 0x40,
        /// Water (NV 0x6A5EC0)
        Flag_Water = 0x200,
        /// A door is on it, the door links of the mesh say which (NV 0x6A71A8)
        Flag_Door = 0x1000,
    };

    struct Triangle
    {
        std::array<std::uint16_t, 3> mVertices{};
        /// The triangle across each edge, or the external connection; 0xffff: none (NV 0x68F200)
        std::array<std::uint16_t, 3> mEdges{ 0xffff, 0xffff, 0xffff };
        std::uint32_t mFlags = 0;
    };

    /// An external connection: a triangle of another mesh (NVEX)
    struct Link
    {
        std::uint32_t mMesh = 0;
        std::uint16_t mTriangle = 0;
    };

    struct Mesh
    {
        std::uint32_t mId = 0;
        std::vector<Point> mVertices;
        std::vector<Triangle> mTriangles;
        std::vector<Link> mExternal;
        /// The door on each door triangle (NVDP), the triangle first
        std::vector<std::pair<std::uint16_t, std::uint32_t>> mDoors;

        /// The middle of a triangle, the mean of its corners (NV 0x558220)
        Point centroid(std::uint16_t triangle) const;
    };

    /// A triangle of a mesh
    struct Place
    {
        std::uint32_t mMesh = 0;
        std::uint16_t mTriangle = 0;
        bool operator==(const Place& other) const = default;
        bool operator<(const Place& other) const
        {
            return std::tie(mMesh, mTriangle) < std::tie(other.mMesh, other.mTriangle);
        }
    };

    /// What a door triangle on the way is to the searcher (NV 0x6A71A8 to 0x6A7299)
    enum class Door
    {
        Open,
        Closed, // the search does not go through it
        Costly, // a step onto it costs a hundred times as much (the door's base has the flag Minimal Use, 0x518000)
    };

    /// The request of a path (NV 0x6E2420 sets the defaults)
    struct Request
    {
        /// 0.6 of the width of the actor's bounds (NV 0x6E2A55, 0x885140); the default is 35
        float mRadius = 35.f;
        /// fPathingLargeActorRadius (NV 0xF87750: 80): above it, triangles without large actors are closed
        float mLargeActorRadius = 80.f;
        /// +0x74: how near the goal a searcher with a radius over 100 has to come (NV 0x6E24C6: the float at 0x119E4BC,
        /// 25)
        float mGoalRadius = 25.f;
        /// +0x9D and +0x9E: the actor may go into the water (NV 0x6A7322 to 0x6A7386); +0x9D is on by default
        bool mWater = true;
        bool mWaterToo = false;
        /// +0x98: no door at all (NV 0x73CB80, 0x6A71C4)
        bool mNoDoors = false;
        /// Edges to stay away from, each a triangle and one of its edges: a step over one costs a hundred times as much
        /// (the list at NavMeshSearch +0x2084, NV 0x6A738E)
        std::set<std::pair<Place, unsigned>> mAvoid;
    };

    /// One triangle of a path, and the edge of the one before that leads to it (0xffff for the first)
    struct Step
    {
        Place mPlace;
        std::uint16_t mEdge = 0xffff;
    };

    /// The meshes that are loaded, by their ids (a link to one that is not is no way, NV 0x68F4F2)
    using Meshes = std::function<const Mesh*(std::uint32_t)>;
    using DoorRule = std::function<Door(std::uint32_t door)>;

    /// The triangle across edge `edge` of `place`, and its edge that leads back (NV 0x68F460); nothing when the edge
    /// leads nowhere or the link is one way
    std::optional<std::pair<Place, std::uint16_t>> across(const Meshes& meshes, const Place& place, unsigned edge);

    /// The way from `start` to `goal`, triangle after triangle, or nothing when there is none
    std::optional<std::vector<Step>> search(
        const Meshes& meshes, const Place& start, const Place& goal, const Request& request, const DoorRule& doors);
}

#endif
