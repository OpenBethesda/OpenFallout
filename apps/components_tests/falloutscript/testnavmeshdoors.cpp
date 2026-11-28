#include <components/falloutscript/navmeshdoors.hpp>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    using namespace FalloutScript;
    using NavMeshDoors::Axes;
    using NavMeshSearch::Point;

    // Three squares of 100 in a row along x, two triangles each, in one square of the grid
    NavMeshSearch::Mesh row()
    {
        NavMeshSearch::Mesh mesh;
        mesh.mId = 1;
        for (float y : { 0.f, 100.f })
            for (float x : { 0.f, 100.f, 200.f, 300.f })
                mesh.mVertices.push_back({ x, y, 0.f });
        for (std::uint16_t i = 0; i < 3; ++i)
        {
            NavMeshSearch::Triangle a;
            a.mVertices = { i, static_cast<std::uint16_t>(i + 1), static_cast<std::uint16_t>(i + 5) };
            NavMeshSearch::Triangle b;
            b.mVertices = { i, static_cast<std::uint16_t>(i + 5), static_cast<std::uint16_t>(i + 4) };
            mesh.mTriangles.push_back(a);
            mesh.mTriangles.push_back(b);
        }
        mesh.mGrid.mDivisor = 1;
        mesh.mGrid.mCellX = 300.f;
        mesh.mGrid.mCellY = 100.f;
        mesh.mGrid.mMin = { 0.f, 0.f, 0.f };
        mesh.mGrid.mMax = { 300.f, 100.f, 0.f };
        mesh.mGrid.mCells = { { 0, 1, 2, 3, 4, 5 } };
        return mesh;
    }

    const Axes upright{ Point{ 1.f, 0.f, 0.f }, Point{ 0.f, 1.f, 0.f }, Point{ 0.f, 0.f, 1.f } };

    TEST(FalloutScriptNavMeshDoorsTest, the_box_of_a_body_stands_upright_in_a_frame_of_its_own)
    {
        const NavMeshDoors::Shape shape
            = NavMeshDoors::shapeOf(upright, { 100.f, 50.f, 0.f }, { -4.f, -50.f, 0.f }, { 4.f, 50.f, 120.f });
        // 0x6C2AA7: across is minus the body's x when the body stands upright
        EXPECT_FLOAT_EQ(shape.mFrame[0][0], -1.f);
        EXPECT_FLOAT_EQ(shape.mFrame[1][1], 1.f);
        EXPECT_FLOAT_EQ(shape.mMin[0], -4.f);
        EXPECT_FLOAT_EQ(shape.mMax[1], 50.f);
        EXPECT_FLOAT_EQ(shape.mWorldMin[0], 96.f);
        EXPECT_FLOAT_EQ(shape.mWorldMax[2], 120.f);
    }

    TEST(FalloutScriptNavMeshDoorsTest, a_corner_by_the_side_of_a_cell_goes_thirty_on)
    {
        const NavMeshDoors::Shape shape
            = NavMeshDoors::shapeOf(upright, { 4100.f, 50.f, 0.f }, { -10.f, -5.f, 0.f }, { 10.f, 5.f, 100.f });
        // fmod leaves 14 of the corners at 4110, under 15 (0x6C2F26): they go to 4140. It leaves 4090 of those at
        // 4090, which stay
        EXPECT_FLOAT_EQ(shape.mWorldMin[0], 4090.f);
        EXPECT_FLOAT_EQ(shape.mWorldMax[0], 4140.f);
    }

    TEST(FalloutScriptNavMeshDoorsTest, a_closed_door_marks_the_triangles_under_it_until_it_opens)
    {
        NavMeshSearch::Mesh mesh = row();
        const NavMeshDoors::Shape shape
            = NavMeshDoors::shapeOf(upright, { 100.f, 50.f, 0.f }, { -4.f, -50.f, 0.f }, { 4.f, 50.f, 120.f });
        NavMeshDoors::close(mesh, 0x77, shape);
        // the body's origin is on the side of the first triangle (0x697730)
        EXPECT_NE(mesh.mTriangles[0].mFlags & NavMeshSearch::Flag_Door, 0u);
        // the third square is far from it
        EXPECT_EQ(mesh.mTriangles[4].mFlags & NavMeshSearch::Flag_Door, 0u);
        EXPECT_EQ(mesh.mTriangles[5].mFlags & NavMeshSearch::Flag_Door, 0u);
        const std::size_t records = mesh.mDoors.size();
        ASSERT_GT(records, 0u);
        EXPECT_EQ(mesh.mDoors.front().second, 0x77u);
        // closing it again adds nothing (0x699990)
        NavMeshDoors::close(mesh, 0x77, shape);
        EXPECT_EQ(mesh.mDoors.size(), records);

        NavMeshDoors::open(mesh, 0x77);
        EXPECT_TRUE(mesh.mDoors.empty());
        for (const NavMeshSearch::Triangle& triangle : mesh.mTriangles)
            EXPECT_EQ(triangle.mFlags & NavMeshSearch::Flag_Door, 0u);
    }

    TEST(FalloutScriptNavMeshDoorsTest, a_locked_door_lets_through_its_owners_and_who_has_its_key_or_a_pin)
    {
        using NavMeshDoors::Owner;
        constexpr std::uint32_t pin = 0xA;
        NavMeshDoors::DoorLock lock;
        lock.mHasLock = true;
        lock.mLocked = true;
        lock.mKey = 0x50;
        std::vector<std::uint32_t> items;
        NavMeshDoors::Searcher searcher;
        searcher.mBase = 0x700;
        searcher.mNpc = true;
        searcher.mRank = [](std::uint32_t faction) { return faction == 0x900 ? 2 : -1; };
        searcher.mCarries
            = [&](std::uint32_t item) { return std::find(items.begin(), items.end(), item) != items.end(); };
        bool picking = false;
        EXPECT_FALSE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        items = { pin };
        EXPECT_TRUE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        EXPECT_TRUE(picking);
        lock.mKeyOnly = true; // a pin does not open a lock that needs its key (0x5024F5)
        EXPECT_FALSE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        items = { 0x50 };
        EXPECT_TRUE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        EXPECT_FALSE(picking);
        items.clear();
        // a member of the owning faction with its rank (0x502550), but not one below it
        lock.mOwner = { Owner::Kind::Faction, 0x900, 2 };
        EXPECT_TRUE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        lock.mOwner.mRank = 3;
        EXPECT_FALSE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        // another owner lets it through only with a global that is set
        lock.mOwner = { Owner::Kind::Npc, 0x800, 0 };
        EXPECT_FALSE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        lock.mGlobalSet = true;
        EXPECT_TRUE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        // the owner of the cell passes, and then the door's own owner is not asked (0x5023A9)
        lock.mGlobalSet = false;
        lock.mCellOwner = { Owner::Kind::Npc, 0x700, 0 };
        EXPECT_TRUE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        lock.mHasDestination = true;
        lock.mDestinationOwner = { Owner::Kind::Npc, 0x800, 0 };
        EXPECT_FALSE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
        // a lock that is not locked, or no lock, lets everyone through
        lock.mLocked = false;
        EXPECT_TRUE(NavMeshDoors::passesLock(lock, searcher, pin, picking));
    }
}
