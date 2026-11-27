#include <components/falloutscript/navmeshsearch.hpp>

#include <gtest/gtest.h>

#include <map>

namespace
{
    using namespace FalloutScript::NavMeshSearch;

    // A grid of `width` by `height` squares of `size` units, two triangles each: the first (corners bottom left,
    // bottom right, top right) has its bottom, its right side and the diagonal as edges 0, 1 and 2, the second
    // (bottom left, top right, top left) the diagonal, its top and its left side
    Mesh grid(std::uint32_t id, int width, int height, float size = 100.f)
    {
        Mesh mesh;
        mesh.mId = id;
        const auto corner = [&](int x, int y) { return static_cast<std::uint16_t>(y * (width + 1) + x); };
        for (int y = 0; y <= height; ++y)
            for (int x = 0; x <= width; ++x)
                mesh.mVertices.push_back({ x * size, y * size, 0.f });
        const auto first = [&](int x, int y) { return static_cast<std::uint16_t>(2 * (y * width + x)); };
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                Triangle a;
                a.mVertices = { corner(x, y), corner(x + 1, y), corner(x + 1, y + 1) };
                a.mEdges = { y > 0 ? static_cast<std::uint16_t>(first(x, y - 1) + 1) : std::uint16_t(0xffff),
                    x + 1 < width ? static_cast<std::uint16_t>(first(x + 1, y) + 1) : std::uint16_t(0xffff),
                    static_cast<std::uint16_t>(first(x, y) + 1) };
                Triangle b;
                b.mVertices = { corner(x, y), corner(x + 1, y + 1), corner(x, y + 1) };
                b.mEdges = { first(x, y), y + 1 < height ? first(x, y + 1) : std::uint16_t(0xffff),
                    x > 0 ? first(x - 1, y) : std::uint16_t(0xffff) };
                mesh.mTriangles.push_back(a);
                mesh.mTriangles.push_back(b);
            }
        return mesh;
    }

    struct World
    {
        std::map<std::uint32_t, Mesh> mMeshes;
        Meshes meshes() const
        {
            return [this](std::uint32_t id) -> const Mesh* {
                const auto found = mMeshes.find(id);
                return found != mMeshes.end() ? &found->second : nullptr;
            };
        }
    };

    std::vector<std::uint16_t> triangles(const std::vector<Step>& path)
    {
        std::vector<std::uint16_t> result;
        for (const Step& step : path)
            result.push_back(step.mPlace.mTriangle);
        return result;
    }

    TEST(FalloutScriptNavMeshSearchTest, the_edges_of_a_triangle_lead_to_the_triangle_across_and_back)
    {
        World world;
        world.mMeshes[1] = grid(1, 2, 1);
        // the first triangle of square 0: its right side leads to the second triangle of square 1, by that one's left
        const auto right = across(world.meshes(), { 1, 0 }, 1);
        ASSERT_TRUE(right);
        EXPECT_EQ(right->first, (Place{ 1, 3 }));
        EXPECT_EQ(right->second, 2);
        EXPECT_FALSE(across(world.meshes(), { 1, 0 }, 0)); // the bottom of the grid
        // a triangle whose neighbour does not lead back is no way (0x68F64E)
        world.mMeshes[1].mTriangles[3].mEdges[2] = 0xffff;
        EXPECT_FALSE(across(world.meshes(), { 1, 0 }, 1));
    }

    TEST(FalloutScriptNavMeshSearchTest, a_path_goes_from_the_start_to_the_goal_over_the_edges)
    {
        World world;
        world.mMeshes[1] = grid(1, 4, 1);
        const auto path = search(world.meshes(), { 1, 0 }, { 1, 7 }, Request{}, nullptr);
        ASSERT_TRUE(path);
        EXPECT_EQ(path->front().mPlace, (Place{ 1, 0 }));
        EXPECT_EQ(path->front().mEdge, 0xffff);
        EXPECT_EQ(path->back().mPlace, (Place{ 1, 7 }));
        for (std::size_t i = 1; i < path->size(); ++i)
        {
            const auto next = across(world.meshes(), (*path)[i - 1].mPlace, (*path)[i].mEdge);
            ASSERT_TRUE(next);
            EXPECT_EQ(next->first, (*path)[i].mPlace);
        }
        // there is no way to a triangle that is cut off
        world.mMeshes[1].mTriangles[6].mEdges[2] = 0xffff;
        world.mMeshes[1].mTriangles[7].mEdges[0] = 0xffff;
        world.mMeshes[1].mTriangles[7].mEdges[2] = 0xffff;
        world.mMeshes[1].mTriangles[4].mEdges[1] = 0xffff;
        EXPECT_FALSE(search(world.meshes(), { 1, 0 }, { 1, 7 }, Request{}, nullptr));
    }

    TEST(FalloutScriptNavMeshSearchTest, large_actors_and_actors_that_do_not_swim_go_around)
    {
        // Two rows: the bottom row's middle square is closed to large actors, then water
        World world;
        world.mMeshes[1] = grid(1, 3, 2);
        Mesh& mesh = world.mMeshes[1];
        mesh.mTriangles[2].mFlags |= Flag_NoLargeActors;
        mesh.mTriangles[3].mFlags |= Flag_NoLargeActors;
        const Place start{ 1, 0 };
        const Place goal{ 1, 5 };
        const auto direct = search(world.meshes(), start, goal, Request{}, nullptr);
        ASSERT_TRUE(direct);
        EXPECT_EQ(triangles(*direct), (std::vector<std::uint16_t>{ 0, 3, 2, 5 }));

        // 0.6 of a bound 140 wide is 84, above fPathingLargeActorRadius
        Request large;
        large.mRadius = 84.f;
        const auto around = search(world.meshes(), start, goal, large, nullptr);
        ASSERT_TRUE(around);
        for (const Step& step : *around)
            EXPECT_TRUE(step.mPlace.mTriangle != 2 && step.mPlace.mTriangle != 3);
        EXPECT_EQ(around->back().mPlace, goal);

        // An actor that may not go into the water does not; one that may does
        mesh.mTriangles[2].mFlags = Flag_Water;
        mesh.mTriangles[3].mFlags = Flag_Water;
        Request dry;
        dry.mWater = false;
        const auto dryPath = search(world.meshes(), start, goal, dry, nullptr);
        ASSERT_TRUE(dryPath);
        for (const Step& step : *dryPath)
            EXPECT_TRUE(step.mPlace.mTriangle != 2 && step.mPlace.mTriangle != 3);
        // a swimmer goes round too: into and out of the water costs five times, in it four times (0x6A7361)
        const auto wet = search(world.meshes(), start, goal, Request{}, nullptr);
        ASSERT_TRUE(wet);
        EXPECT_EQ(wet->back().mPlace, goal);
    }

    TEST(FalloutScriptNavMeshSearchTest, preferred_triangles_cost_a_hundredth)
    {
        // A long way of preferred triangles beats the short way (0x6A74A4)
        World world;
        world.mMeshes[1] = grid(1, 3, 2);
        Mesh& mesh = world.mMeshes[1];
        for (const std::uint16_t triangle : { 6, 7, 8, 9, 10, 11 })
            mesh.mTriangles[triangle].mFlags |= Flag_Preferred;
        const auto path = search(world.meshes(), { 1, 0 }, { 1, 5 }, Request{}, nullptr);
        ASSERT_TRUE(path);
        bool upper = false;
        for (const Step& step : *path)
            upper = upper || step.mPlace.mTriangle >= 6;
        EXPECT_TRUE(upper);
    }

    TEST(FalloutScriptNavMeshSearchTest, a_path_goes_on_into_another_mesh_by_its_external_connections)
    {
        World world;
        Mesh left = grid(1, 1, 1);
        Mesh right = grid(2, 1, 1);
        for (auto& vertex : right.mVertices)
            vertex[0] += 100.f;
        // the right side of the left square is the left side of the right one
        left.mTriangles[0].mEdges[1] = 0;
        left.mTriangles[0].mFlags |= Flag_External1;
        left.mExternal.push_back({ 2, 1 });
        right.mTriangles[1].mEdges[2] = 0;
        right.mTriangles[1].mFlags |= Flag_External2;
        right.mExternal.push_back({ 1, 0 });
        world.mMeshes[1] = left;
        world.mMeshes[2] = right;
        const auto path = search(world.meshes(), { 1, 1 }, { 2, 0 }, Request{}, nullptr);
        ASSERT_TRUE(path);
        ASSERT_EQ(path->size(), 4u);
        EXPECT_EQ((*path)[1].mPlace, (Place{ 1, 0 }));
        EXPECT_EQ((*path)[2].mPlace, (Place{ 2, 1 }));
        // a mesh that is not loaded is no way (0x68F4F2)
        world.mMeshes.erase(2);
        EXPECT_TRUE(search(world.meshes(), { 1, 1 }, { 1, 0 }, Request{}, nullptr));
        EXPECT_FALSE(across(world.meshes(), { 1, 0 }, 1));
    }

    TEST(FalloutScriptNavMeshSearchTest, the_triangle_under_a_point)
    {
        // Two floors of one square, the upper one 100 higher, both in the one square of the grid
        Mesh mesh = grid(1, 1, 1);
        const std::size_t ground = mesh.mVertices.size();
        for (std::size_t i = 0; i < ground; ++i)
            mesh.mVertices.push_back({ mesh.mVertices[i][0], mesh.mVertices[i][1], 100.f });
        for (std::uint16_t t = 0; t < 2; ++t)
        {
            Triangle upper = mesh.mTriangles[t];
            for (auto& corner : upper.mVertices)
                corner = static_cast<std::uint16_t>(corner + ground);
            upper.mEdges = { 0xffff, 0xffff, 0xffff };
            mesh.mTriangles.push_back(upper);
        }
        mesh.mGrid.mDivisor = 1;
        mesh.mGrid.mCellX = mesh.mGrid.mCellY = 100.f;
        mesh.mGrid.mMin = { 0.f, 0.f, 0.f };
        mesh.mGrid.mMax = { 100.f, 100.f, 100.f };
        mesh.mGrid.mCells = { { 0, 1, 2, 3 } };

        // Below the right side of the diagonal: triangle 0 on the ground, 2 above
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 10.f }), std::optional<std::uint16_t>(0));
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 95.f }), std::optional<std::uint16_t>(2));
        EXPECT_EQ(triangleAt(mesh, { 20.f, 70.f, 95.f }), std::optional<std::uint16_t>(3));
        // Up to 64 above the point, so nothing from far below (0x10240C0)
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, -70.f }), std::nullopt);
        // 150 above the upper floor: more than 40 off, so the nearest, but nothing more than 180 below (0x106B160)
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 250.f }), std::optional<std::uint16_t>(2));
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 400.f }), std::nullopt);
        // ...unless the point is near the land
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 400.f }, 360.f), std::optional<std::uint16_t>(2));
        // Outside the box of the grid
        EXPECT_EQ(triangleAt(mesh, { 120.f, 20.f, 10.f }), std::nullopt);
        // A covered triangle is passed over (0x696B2D)
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 40.f }), std::optional<std::uint16_t>(0));
        mesh.mTriangles[0].mFlags |= Flag_Covered;
        EXPECT_EQ(triangleAt(mesh, { 70.f, 20.f, 40.f }), std::optional<std::uint16_t>(2));
        EXPECT_EQ(heightAbove(mesh, 2, { 70.f, 20.f, 10.f }), std::optional<float>(90.f));
        EXPECT_EQ(heightAbove(mesh, 1, { 70.f, 20.f, 10.f }), std::nullopt);
    }

    TEST(FalloutScriptNavMeshSearchTest, a_point_off_the_mesh_is_moved_onto_the_nearest_triangle)
    {
        Mesh mesh = grid(1, 1, 1);
        mesh.mGrid.mDivisor = 1;
        mesh.mGrid.mCellX = mesh.mGrid.mCellY = 100.f;
        mesh.mGrid.mMax = { 100.f, 100.f, 0.f };
        mesh.mGrid.mCells = { { 0, 1 } };
        // On the mesh: inside, nothing moves
        const auto inside = nearestTriangle(mesh, { 70.f, 20.f, 30.f });
        ASSERT_TRUE(inside);
        EXPECT_EQ(inside->mTriangle, 0);
        EXPECT_TRUE(inside->mInside);
        // 200 above is beyond the 180 of 0x106B158: the nearest middle, not inside
        const auto high = nearestTriangle(mesh, { 70.f, 20.f, 200.f });
        ASSERT_TRUE(high);
        EXPECT_FALSE(high->mInside);
        // Off the side of the grid: every triangle, the nearest middle is that of triangle 0 (66.7, 33.3)
        const auto off = nearestTriangle(mesh, { 120.f, 50.f, 0.f });
        ASSERT_TRUE(off);
        EXPECT_EQ(off->mTriangle, 0);
        EXPECT_FALSE(off->mInside);
        // ...and the point goes 0.9 of the way from that middle to where the line to it leaves the triangle, x 100
        const Point moved = moveOnto(mesh, 0, { 120.f, 50.f, 0.f });
        EXPECT_NEAR(moved[0], 0.1f * 200.f / 3.f + 90.f, 1e-3f);
        EXPECT_NEAR(moved[1], 0.1f * 100.f / 3.f + 0.9f * 43.75f, 1e-3f);
        ASSERT_TRUE(triangleAt(mesh, moved));

        // One mesh: locate gives up where locateNearest moves the point (0x6DD74C, 0x6DDC00)
        const std::vector<Candidate> one{ { &mesh, false } };
        EXPECT_FALSE(locate(one, { 120.f, 50.f, 0.f }, std::numeric_limits<float>::lowest()));
        const auto nearest = locateNearest(one, { 120.f, 50.f, 0.f }, std::numeric_limits<float>::lowest());
        ASSERT_TRUE(nearest);
        EXPECT_EQ(nearest->first, (Place{ 1, 0 }));
        // Two meshes: the second one's triangle under the point wins, the first is disabled
        Mesh other = grid(2, 1, 1);
        for (auto& vertex : other.mVertices)
            vertex[0] += 100.f;
        other.mGrid = mesh.mGrid;
        other.mGrid.mMin[0] = 100.f;
        other.mGrid.mMax[0] = 200.f;
        const std::vector<Candidate> two{ { &mesh, true }, { &other, false } };
        const auto found = locate(two, { 120.f, 50.f, 0.f }, std::numeric_limits<float>::lowest());
        ASSERT_TRUE(found);
        EXPECT_EQ(found->first, (Place{ 2, 1 }));
    }

    TEST(FalloutScriptNavMeshSearchTest, a_real_mesh_of_new_vegas)
    {
        // NAVM 0014F130 of FalloutNV.esm: triangle 0 (corners 0 1 2, edges 1 none 0, flags 0x804) and triangle 1
        // (corners 3 1 0, edges none 0 none, flags 0xc00: its door, NVDP), the edge of triangle 0 that has the bit
        // 0x4 is its external connection
        World world;
        Mesh mesh;
        mesh.mId = 0x14f130;
        mesh.mVertices = { { 25256.f, -32768.f, 8063.558f }, { 25396.f, -32872.f, 8063.495f },
            { 25412.f, -32768.f, 8063.308f }, { 25232.f, -32844.f, 8063.914f } };
        mesh.mTriangles = { Triangle{ { 0, 1, 2 }, { 1, 0xffff, 0 }, 0x804 },
            Triangle{ { 3, 1, 0 }, { 0xffff, 0, 0xffff }, 0xc00 } };
        mesh.mExternal = { { 0x152d81, 0 } };
        world.mMeshes[mesh.mId] = mesh;
        const auto path = search(world.meshes(), { 0x14f130, 0 }, { 0x14f130, 1 }, Request{}, nullptr);
        ASSERT_TRUE(path);
        ASSERT_EQ(path->size(), 2u);
        EXPECT_EQ((*path)[1].mEdge, 0);
        // its external connection leads to a mesh that is not loaded
        EXPECT_FALSE(across(world.meshes(), { 0x14f130, 0 }, 2));
    }
}
