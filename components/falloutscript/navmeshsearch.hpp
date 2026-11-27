#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHSEARCH_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_NAVMESHSEARCH_H

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
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
        /// Set while the game runs by the obstacle task (NV 0x6BC6FC, from 0x6C82F9); the lookup of the triangle under
        /// a point passes over it (NV 0x696B2D)
        Flag_Covered = 0x20,
        /// A step into it costs a hundredth (NV 0x693B70, 0x6A74A4)
        Flag_Preferred = 0x40,
        /// Water (NV 0x6A5EC0)
        Flag_Water = 0x200,
        /// A door is on it, the door links of the mesh say which (NV 0x6A71A8)
        Flag_Door = 0x1000,
        /// An actor got stuck there: the mesh's obstacles say how much a step into it costs (set by NV 0x691570 from
        /// 0x691510, tested at 0x6A7519; never cleared while the mesh is loaded)
        Flag_Stuck = 0x80000000,
    };

    /// A mark an actor that got stuck left on a triangle (NV 0x691590, 0x28 bytes, in the list at NavMesh +0xF4)
    struct Obstacle
    {
        Point mPosition{};
        float mRadius = 0.f; // +0x18
        float mCost = 1.f; // +0x1C
        std::uint16_t mTriangle = 0; // +0x24
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

    /// The triangles of a mesh by squares of the map (NVGD, read by NV 0x69DA40)
    struct Grid
    {
        std::uint32_t mDivisor = 0; // squares on a side
        float mCellX = 0.f;
        float mCellY = 0.f;
        Point mMin{};
        Point mMax{};
        std::vector<std::vector<std::uint16_t>> mCells; // a row of squares after another, by y then x (NV 0x6967C0)
    };

    struct Mesh
    {
        std::uint32_t mId = 0;
        std::vector<Point> mVertices;
        std::vector<Triangle> mTriangles;
        std::vector<Link> mExternal;
        /// The door on each door triangle (NVDP), the triangle first
        std::vector<std::pair<std::uint16_t, std::uint32_t>> mDoors;
        Grid mGrid;
        /// Added while the game runs, only by the stuck marker (NV 0x691510, its one caller 0x9E540D); removed only
        /// with the mesh (NV 0x68EE72)
        std::vector<Obstacle> mObstacles;

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

    /// Something a path keeps away from, put on the request by the actor avoidance (NV 0x6DC780 reads it): `mType` 0
    /// and 2 are a ball around `mA`, 1 the stretch from `mA` to `mB`
    struct AvoidObject
    {
        Point mA{}; // +0x0
        Point mB{}; // +0xC
        float mRadius = 0.f; // +0x18
        float mCost = 0.f; // +0x1C
        int mType = 0; // +0x20
    };

    /// What `object` adds to the cost of a step to `point` (NV 0x6DC780): (1 - d² / r²) times its cost, d the distance
    /// of the point from the ball's middle or, for a stretch, from its line where the point is beside it; nothing
    /// beyond the radius
    float avoidCost(const AvoidObject& object, const Point& point);

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
        /// +0x94 (getter NV 0x69E390): a step to a triangle's middle costs 1 + the sum of their avoidCost there more
        /// (NV 0x6A74AD to 0x6A7516, the sum 0x6DC990)
        std::vector<AvoidObject> mAvoidObjects;
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

    /// How far the mesh is above (positive) or below `point` where the point is inside `triangle` on the map, or
    /// nothing when it is outside (NV 0x697730, 0x6B9CF0)
    std::optional<float> heightAbove(const Mesh& mesh, std::uint16_t triangle, const Point& point);

    /// The triangle of `mesh` under `point` (NV 0x696A50): one of the triangles of the point's square of the grid that
    /// has the point inside it on the map and is at most 64 above it, and, unless the point is no more than 50 above
    /// `landHeight` (the height of the land there, lowest() in an interior), at most 180 below it; the first that is
    /// within 40, otherwise the nearest
    std::optional<std::uint16_t> triangleAt(
        const Mesh& mesh, const Point& point, float landHeight = std::numeric_limits<float>::lowest());

    /// The triangle of `mesh` nearest `point` (NV 0x696CF0): the first of the point's square of the grid (of all the
    /// triangles when the point is outside the grid) that has it inside on the map, at most 180 above it and 64 below,
    /// within 40, otherwise the nearest such; `inside` false when none has the point inside and this is the triangle
    /// with the nearest middle
    struct Nearest
    {
        std::uint16_t mTriangle = 0;
        bool mInside = false;
    };
    std::optional<Nearest> nearestTriangle(const Mesh& mesh, const Point& point);

    /// Where the line from the middle of `triangle` to `point` leaves the triangle, nothing when it does not (NV
    /// 0x698160 with 0x6BA240), and the point 0.9 of the way from the middle to there (NV 0x6DDD18 to 0x6DDDC1)
    Point moveOnto(const Mesh& mesh, std::uint16_t triangle, const Point& point);

    /// A mesh where a point may be: one of the cell's, or of the exterior cell under it (NV PathingLocation 0x6DD280)
    struct Candidate
    {
        const Mesh* mMesh = nullptr;
        bool mDisabled = false; // the record has the flag 0x800 (NV 0x440DA0)
    };

    /// The triangle of a point in a loaded cell, and the point, as the game finds them first (NV 0x6DD6F0): with one
    /// mesh, its triangle under the point (triangleAt) or nothing; with more, the first not disabled that has one,
    /// otherwise locateNearest
    std::optional<std::pair<Place, Point>> locate(
        const std::vector<Candidate>& candidates, const Point& point, float landHeight);

    /// The triangle nearest a point and the point moved onto it, which the game uses when locate finds nothing and for
    /// the start of a path (NV 0x6DDC00, its callers 0x8E1A37 and 0x6D0C36): with one mesh, its nearestTriangle; with
    /// more, the first mesh's triangle that has the point inside, or else the one with the nearest middle among those
    /// the point is less than 64 below and, unless it is below `landHeight` + 25, at most 180 above, or else the
    /// nearest at all
    std::optional<std::pair<Place, Point>> locateNearest(
        const std::vector<Candidate>& candidates, const Point& point, float landHeight);

    /// The mark of an actor stuck on its way to `waypoint` on `triangle` (NV 0x9E5282 to 0x9E540D): each mark of the
    /// triangle that has the waypoint inside it (in 3D) doubles its radius while that is at most 128, otherwise ten
    /// times its cost; when none has, a new mark of `radius` (the request's) and cost 1 (NV 0x691510)
    void markStuck(Mesh& mesh, std::uint16_t triangle, const Point& waypoint, float radius);

    /// The way from `start` to `goal`, triangle after triangle, or nothing when there is none
    std::optional<std::vector<Step>> search(
        const Meshes& meshes, const Place& start, const Place& goal, const Request& request, const DoorRule& doors);
}

#endif
