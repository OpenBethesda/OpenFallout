#include <components/falloutscript/navmeshpather.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using namespace FalloutScript;
    using NavMeshPather::Spot;
    using NavMeshSearch::Mesh;
    using NavMeshSearch::Point;
    using NavMeshSearch::Triangle;

    // A grid of squares of 100 units, two triangles each, in one square of the mesh's grid
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
        mesh.mGrid.mDivisor = 1;
        mesh.mGrid.mCellX = width * 100.f;
        mesh.mGrid.mCellY = height * 100.f;
        mesh.mGrid.mMin = { 0.f, 0.f, 0.f };
        mesh.mGrid.mMax = { width * 100.f, height * 100.f, 0.f };
        mesh.mGrid.mCells.resize(1);
        for (std::size_t t = 0; t < mesh.mTriangles.size(); ++t)
            mesh.mGrid.mCells[0].push_back(static_cast<std::uint16_t>(t));
        return mesh;
    }

    Spot spotAt(const Point& point)
    {
        Spot spot;
        spot.mPoint = point;
        spot.mCell = 1;
        return spot;
    }

    // One cell (1) with one mesh on flat ground
    struct Fixture
    {
        Mesh mMesh;
        NavMeshRoute::Info mInfo;
        NavMeshPather::World mWorld;

        explicit Fixture(Mesh mesh)
            : mMesh(std::move(mesh))
        {
            mInfo.mId = mMesh.mId;
            mWorld.mMeshes = [this](std::uint32_t id) { return id == mMesh.mId ? &mMesh : nullptr; };
            mWorld.mInfos = [this](std::uint32_t id) { return id == mMesh.mId ? &mInfo : nullptr; };
            mWorld.mCandidates = [this](std::uint32_t cell) {
                return cell == 1 ? std::vector<NavMeshSearch::Candidate>{ { &mMesh, false } }
                                 : std::vector<NavMeshSearch::Candidate>{};
            };
            mWorld.mSpotAt = [](const Point& point, const Spot&) { return spotAt(point); };
            mWorld.mSpotOfMesh = [](std::uint32_t) { return spotAt({}); };
            mWorld.mNear = [this](const Spot&, float) { return std::vector<const Mesh*>{ &mMesh }; };
            mWorld.mLoaded = [this] { return std::vector<std::uint32_t>{ mMesh.mId }; };
            mWorld.mWater = [](std::uint32_t, const Point&) { return std::numeric_limits<float>::lowest(); };
            mWorld.mClear = [](const Point&, const Point&) { return true; };
            mWorld.mDown = [](const Point& from, float) { return Point{ from[0], from[1], 0.f }; };
            mWorld.mDoors = [](std::uint32_t) { return NavMeshSearch::Door::Open; };
            mWorld.mRouteDoors = [](std::uint32_t) { return std::optional<NavMeshRoute::Door>{}; };
        }

        Spot at(float x, float y) const { return spotAt({ x, y, 0.f }); }
    };

    TEST(FalloutScriptNavMeshPatherTest, a_place_off_the_one_mesh_of_its_cell_keeps_the_mesh_with_no_triangle)
    {
        Fixture fixture(grid(1, 3, 1));
        Spot off = fixture.at(150.f, 150.f);
        // 0x6DD7D6: true, the triangle stays 0xFFFF
        EXPECT_TRUE(NavMeshPather::resolve(fixture.mWorld, off));
        EXPECT_EQ(off.mMesh, 1u);
        EXPECT_FALSE(off.mTriangle);
        Spot on = fixture.at(150.f, 50.f);
        EXPECT_TRUE(NavMeshPather::resolve(fixture.mWorld, on));
        EXPECT_TRUE(on.mTriangle);
    }

    TEST(FalloutScriptNavMeshPatherTest, a_clear_straight_line_is_the_path)
    {
        Fixture fixture(grid(1, 3, 1));
        const auto path = NavMeshPather::findPath(fixture.mWorld, {}, fixture.at(20.f, 50.f), fixture.at(280.f, 50.f));
        ASSERT_TRUE(path);
        // the start (0x6CD542) and the goal chain (0x6CC318)
        ASSERT_EQ(path->mPoints.size(), 2u);
        EXPECT_FLOAT_EQ(path->mPoints[0].mLocation.mPoint[0], 20.f);
        EXPECT_FLOAT_EQ(path->mPoints[1].mLocation.mPoint[0], 280.f);
        EXPECT_FALSE(path->mPartial);
    }

    TEST(FalloutScriptNavMeshPatherTest, a_marked_triangle_on_the_line_sends_it_to_the_search_and_the_smoother)
    {
        Fixture fixture(grid(1, 3, 3));
        // the straight check does not go over a mark of a stuck actor (0x6CD60A)
        for (const std::uint16_t t : { 8, 9 })
            fixture.mMesh.mTriangles[t].mFlags |= NavMeshSearch::Flag_Stuck;
        const auto path
            = NavMeshPather::findPath(fixture.mWorld, {}, fixture.at(20.f, 150.f), fixture.at(280.f, 150.f));
        ASSERT_TRUE(path);
        ASSERT_GE(path->mPoints.size(), 2u);
        EXPECT_FLOAT_EQ(path->mPoints.front().mLocation.mPoint[0], 20.f);
        EXPECT_FLOAT_EQ(path->mPoints.back().mLocation.mPoint[0], 280.f);
        EXPECT_FALSE(path->mPartial);
    }

    TEST(FalloutScriptNavMeshPatherTest, a_goal_just_off_the_mesh_ends_on_the_nearest_edge)
    {
        Fixture fixture(grid(1, 3, 1));
        // an actor thin enough for the smoother to get that near the edge
        NavMeshSearch::Request thin;
        thin.mRadius = 2.f;
        const auto path
            = NavMeshPather::findPath(fixture.mWorld, thin, fixture.at(20.f, 50.f), fixture.at(280.f, 110.f));
        ASSERT_TRUE(path);
        // 0x6CAEE4: the first candidate within the goal radius (25), a tenth of the way to its triangle's middle
        const Point& end = path->mPoints.back().mLocation.mPoint;
        EXPECT_LT(end[1], 100.f);
        EXPECT_LT(std::hypot(end[0] - 280.f, end[1] - 110.f), 25.f);
    }

    TEST(FalloutScriptNavMeshPatherTest, a_goal_far_off_the_mesh_is_reached_over_the_ground_when_the_rays_allow)
    {
        Fixture fixture(grid(1, 3, 1));
        const auto path = NavMeshPather::findPath(fixture.mWorld, {}, fixture.at(20.f, 50.f), fixture.at(250.f, 300.f));
        ASSERT_TRUE(path);
        // 0x6CB050 with 0x6E6320 mode 1: the goal chain is the goal and the snapped edge point, appended backwards
        EXPECT_FLOAT_EQ(path->mPoints.back().mLocation.mPoint[1], 300.f);
        // with every ray blocked there is no way
        fixture.mWorld.mClear = [](const Point&, const Point&) { return false; };
        EXPECT_FALSE(NavMeshPather::findPath(fixture.mWorld, {}, fixture.at(20.f, 50.f), fixture.at(250.f, 300.f)));
    }

    TEST(FalloutScriptNavMeshPatherTest, the_start_of_a_path_gets_a_tangent_along_the_start_heading)
    {
        Fixture fixture(grid(1, 3, 1));
        NavMeshSearch::Request request;
        request.mHasStartHeading = true;
        request.mStartHeading = std::acos(0.f); // east
        request.mHasHeading = true;
        request.mHeading = request.mStartHeading;
        const auto path
            = NavMeshPather::findPath(fixture.mWorld, request, fixture.at(20.f, 50.f), fixture.at(280.f, 50.f));
        ASSERT_TRUE(path);
        ASSERT_GE(path->mPoints.size(), 2u);
        // 0x6D99A0: 256 along the heading, less 1.2 radii (35)
        EXPECT_NEAR(path->mPoints.front().mTangent[0], 256.f - 42.f, 0.01f);
        EXPECT_NEAR(path->mPoints.front().mTangent[1], 0.f, 0.01f);
        // 0x6DA420 never runs here: the smoother's last arrival point (0x6B03CF) and the goal chain (0x6CC318) both
        // write the goal, so the last two points are not 64 apart (0x6CA408)
        EXPECT_FLOAT_EQ(path->mPoints[path->mPoints.size() - 2].mLocation.mPoint[0], 280.f);
        EXPECT_FLOAT_EQ(path->mPoints.back().mTangent[0], 0.f);
        // a heading back past the second point gives none (0x6DA156: the dot under -0.7)
        request.mHasHeading = false;
        request.mStartHeading = -request.mStartHeading;
        const auto back
            = NavMeshPather::findPath(fixture.mWorld, request, fixture.at(20.f, 50.f), fixture.at(280.f, 50.f));
        ASSERT_TRUE(back);
        EXPECT_FLOAT_EQ(back->mPoints.front().mTangent[0], 0.f);
    }
}
