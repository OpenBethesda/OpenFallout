#include <components/falloutscript/navmeshsmoother.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>

namespace
{
    using namespace FalloutScript::NavMeshSearch;

    // A grid of squares of 100 units, two triangles each, as in testnavmeshsearch.cpp
    Mesh grid(std::uint32_t id, int width, int height)
    {
        Mesh mesh;
        mesh.mId = id;
        const auto corner = [&](int x, int y) { return static_cast<std::uint16_t>(y * (width + 1) + x); };
        for (int y = 0; y <= height; ++y)
            for (int x = 0; x <= width; ++x)
                mesh.mVertices.push_back({ x * 100.f, y * 100.f, 0.f });
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

    // Outdoors on low land: the smoother is simple
    const LandHeight lowLand = [](std::uint32_t, const Point&) { return 0.f; };
    // An interior: the full smoother
    const LandHeight interior = [](std::uint32_t, const Point&) { return std::numeric_limits<float>::lowest(); };

    TEST(FalloutScriptNavMeshSmootherTest, the_tangents_of_two_circles)
    {
        // 0x6BB570: the same side first, then the crossing one; unit normals where they touch
        const auto t = tangents({ 0, 0, 0 }, 1.f, { 10, 0, 0 }, 1.f, 0);
        ASSERT_EQ(t.size(), 2u);
        EXPECT_EQ(t[0].mSide, 0);
        EXPECT_NEAR(t[0].mA[0], 0.f, 1e-5f);
        EXPECT_NEAR(t[0].mA[1], -1.f, 1e-5f);
        EXPECT_NEAR(t[0].mB[1], -1.f, 1e-5f);
        EXPECT_EQ(t[1].mSide, 1);
        EXPECT_NEAR(t[1].mA[0], 0.2f, 1e-5f);
        EXPECT_NEAR(t[1].mA[1], -0.9797959f, 1e-5f);
        EXPECT_NEAR(t[1].mB[0], -0.2f, 1e-5f);
        // a point and a circle
        EXPECT_EQ(tangents({ 0, 0, 0 }, 0.f, { 10, 0, 0 }, 1.f, 1).size(), 2u);
        EXPECT_TRUE(tangents({ 0, 0, 0 }, 0.f, { 0, 0, 0 }, 0.f, 0).empty());
    }

    TEST(FalloutScriptNavMeshSmootherTest, a_straight_line_is_walked_on_the_mesh)
    {
        World world;
        world.mMeshes[1] = grid(1, 3, 1);
        const Request request;
        const Walk there = walk(world.meshes(), request, { { 1, 1 }, -1, { 20, 50, 0 } }, { 280, 40, 7 });
        EXPECT_TRUE(there.mArrived);
        EXPECT_EQ(there.mEnd.mPlace, (Place{ 1, 4 }));
        EXPECT_FLOAT_EQ(there.mEnd.mPoint[2], 0.f); // the height of the mesh
        // off the side of the mesh: it stops on the edge
        const Walk off = walk(world.meshes(), request, { { 1, 1 }, -1, { 20, 50, 0 } }, { 20, 150, 0 });
        EXPECT_FALSE(off.mArrived);
        EXPECT_NEAR(off.mEnd.mPoint[1], 100.f, 1e-3f);
        // preferred triangles on the way
        world.mMeshes[1].mTriangles[2].mFlags |= Flag_Preferred;
        world.mMeshes[1].mTriangles[3].mFlags |= Flag_Preferred;
        const Walk ratio = walk(world.meshes(), request, { { 1, 1 }, -1, { 20, 50, 0 } }, { 280, 40, 0 });
        EXPECT_GT(ratio.mRatio, 0.f);
        EXPECT_LT(ratio.mRatio, 1.f);
    }

    TEST(FalloutScriptNavMeshSmootherTest, the_height_on_a_triangle)
    {
        Mesh mesh = grid(1, 1, 1);
        mesh.mVertices[3][2] = 30.f;
        // 0x697980: inside or on the edge (a weight of 0 is kept, 0x697A94)
        EXPECT_TRUE(heightOn(mesh, 0, { 90, 10, 0 }, 180.f, 64.f));
        EXPECT_TRUE(heightOn(mesh, 0, { 50, 0, 0 }, 180.f, 64.f));
        EXPECT_FALSE(heightOn(mesh, 0, { 10, 90, 0 }, 180.f, 64.f));
        // too far below it
        EXPECT_FALSE(heightOn(mesh, 0, { 90, 10, -80 }, 180.f, 64.f));
    }

    TEST(FalloutScriptNavMeshSmootherTest, a_straight_corridor_is_one_straight_leg)
    {
        World world;
        world.mMeshes[1] = grid(1, 3, 1);
        const std::vector<Step> corridor{ { { 1, 1 }, 0xffff }, { { 1, 0 }, 0 }, { { 1, 3 }, 1 }, { { 1, 2 }, 0 },
            { { 1, 5 }, 1 }, { { 1, 4 }, 0 } };
        for (const LandHeight* land : { &lowLand, &interior })
        {
            const Smoothed way = smooth(world.meshes(), Request{}, corridor, { 20, 50, 0 }, { 280, 50, 0 }, *land);
            ASSERT_TRUE(way.mFound);
            ASSERT_EQ(way.mPoints.size(), 2u);
            EXPECT_NEAR(way.mPoints[0].mLocation.mPoint[0], 20.f, 1e-3f);
            EXPECT_NEAR(way.mPoints[1].mLocation.mPoint[0], 280.f, 1e-3f);
            // the start and the end have no circle, so the way on from them has no length (0x6AFCC0: the radius times
            // 3)
            EXPECT_FLOAT_EQ(way.mPoints[0].mTangent[0], 0.f);
        }
    }

    TEST(FalloutScriptNavMeshSmootherTest, a_leg_over_a_ridge_has_a_point_where_the_floor_bends)
    {
        // the edge at x 200 is 60 up: the floor bends at x 100, 200 and 300 (a dot of the normals under 0.9)
        World world;
        world.mMeshes[1] = grid(1, 3, 1);
        world.mMeshes[1].mVertices[2][2] = 60.f;
        world.mMeshes[1].mVertices[6][2] = 60.f;
        const std::vector<Step> corridor{ { { 1, 1 }, 0xffff }, { { 1, 0 }, 0 }, { { 1, 3 }, 1 }, { { 1, 2 }, 0 },
            { { 1, 5 }, 1 }, { { 1, 4 }, 0 } };
        Request request;
        request.mRadius = 20.f;
        const Smoothed way = smooth(world.meshes(), request, corridor, { 20, 50, 0 }, { 280, 50, 0 }, interior);
        ASSERT_TRUE(way.mFound);
        // 0x6B05F0: the start, the bends at 100 and 200 (300 is the end of the mesh), the end
        ASSERT_EQ(way.mPoints.size(), 4u);
        EXPECT_NEAR(way.mPoints[1].mLocation.mPoint[0], 100.f, 1e-3f);
        EXPECT_NEAR(way.mPoints[2].mLocation.mPoint[0], 200.f, 1e-3f);
        EXPECT_NEAR(way.mPoints[2].mLocation.mPoint[2], 60.f, 1e-3f);
        // the tangent is the way of the leg in 3D (the end is 12 up) by the request's radius
        EXPECT_NEAR(std::sqrt(way.mPoints[1].mTangent[0] * way.mPoints[1].mTangent[0]
                        + way.mPoints[1].mTangent[2] * way.mPoints[1].mTangent[2]),
            20.f, 1e-3f);
    }

    TEST(FalloutScriptNavMeshSmootherTest, a_way_round_a_corner_keeps_away_from_it)
    {
        // an L: the squares 0 and 1 of the bottom row and square 0 of the top row (square 1 of the top row is cut off)
        World world;
        Mesh mesh = grid(1, 2, 2);
        mesh.mTriangles[3].mEdges[1] = 0xffff; // the top of square 1
        mesh.mTriangles[5].mEdges[1] = 0xffff; // the right of square 2
        mesh.mTriangles[6].mEdges = { 0xffff, 0xffff, 0xffff };
        mesh.mTriangles[7].mEdges = { 0xffff, 0xffff, 0xffff };
        world.mMeshes[1] = mesh;
        // from square 1 to square 2: 2 -> 3 (diagonal) -> 1 (left of 3) -> 0 -> 5 (top of 1... ) by the search's edges
        const std::vector<Step> corridor{ { { 1, 2 }, 0xffff }, { { 1, 3 }, 2 }, { { 1, 0 }, 2 }, { { 1, 1 }, 2 },
            { { 1, 4 }, 1 }, { { 1, 5 }, 0 } };
        Request request;
        request.mRadius = 20.f;
        const Smoothed way = smooth(world.meshes(), request, corridor, { 170, 30, 0 }, { 30, 170, 0 }, lowLand);
        ASSERT_TRUE(way.mFound);
        ASSERT_GE(way.mPoints.size(), 3u);
        // every point is on the mesh, and none is in the square that is cut off
        for (const WayPoint& point : way.mPoints)
            EXPECT_FALSE(point.mLocation.mPoint[0] > 101.f && point.mLocation.mPoint[1] > 101.f);
        // the way goes round the corner at (100, 100), 1.2 radii from it
        bool near = false;
        for (const WayPoint& point : way.mPoints)
        {
            const float dx = point.mLocation.mPoint[0] - 100.f;
            const float dy = point.mLocation.mPoint[1] - 100.f;
            near = near || std::abs(std::sqrt(dx * dx + dy * dy) - 24.f) < 0.5f;
        }
        EXPECT_TRUE(near);
    }
}
