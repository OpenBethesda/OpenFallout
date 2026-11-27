#include <components/falloutscript/navmeshroute.hpp>

#include <gtest/gtest.h>

#include <map>

namespace
{
    using namespace FalloutScript::NavMeshRoute;

    // Meshes 1 to 3 in a row in a world space, 1 also next to 4; door 100 on mesh 3 leads into mesh 9, an interior
    struct World
    {
        std::map<std::uint32_t, Info> mInfos;
        std::map<std::uint32_t, Door> mDoors;

        World()
        {
            const auto add = [&](std::uint32_t id, float x, std::uint32_t world, std::vector<std::uint32_t> next) {
                Info& info = mInfos[id];
                info.mId = id;
                info.mWorld = world;
                info.mCentre = { x, 0.f, 0.f };
                info.mNeighbours = std::move(next);
            };
            add(1, 0.f, 50, { 2, 4 });
            add(2, 100.f, 50, { 1, 3 });
            add(3, 200.f, 50, { 2 });
            add(4, -100.f, 50, { 1 });
            add(9, 5000.f, 0, {});
            mInfos[3].mDoors = { 100 };
            Door door;
            door.mTarget = 9;
            door.mNear = { 210.f, 0.f, 0.f };
            door.mFar = { 5000.f, 10.f, 0.f };
            door.mPasses = true;
            mDoors[100] = door;
        }

        Infos infos() const
        {
            return [this](std::uint32_t id) -> const Info* {
                const auto found = mInfos.find(id);
                return found != mInfos.end() ? &found->second : nullptr;
            };
        }

        Doors doors() const
        {
            return [this](std::uint32_t id) -> std::optional<Door> {
                const auto found = mDoors.find(id);
                return found != mDoors.end() ? std::optional<Door>(found->second) : std::nullopt;
            };
        }

        const Info& at(std::uint32_t id) const { return mInfos.at(id); }
    };

    TEST(FalloutScriptNavMeshRouteTest, the_route_goes_over_the_meshes_between)
    {
        const World world;
        const Found found
            = search(world.infos(), world.doors(), world.at(1), { 0, 0, 0 }, world.at(3), { 200, 0, 0 }, true, false);
        ASSERT_TRUE(found.mReached);
        ASSERT_EQ(found.mInfos.size(), 3u);
        EXPECT_EQ(found.mInfos[1]->mId, 2u);
        EXPECT_EQ(found.mDoors, (std::vector<std::uint32_t>{ 0, 0 }));
        const auto points = route(found, world.doors(), world.at(1), { 0, 0, 0 }, world.at(3), { 200, 0, 0 });
        ASSERT_EQ(points.size(), 3u);
        EXPECT_EQ(points[1].mKind, RoutePoint::Kind::Mesh);
        EXPECT_FLOAT_EQ(points[1].mPosition[0], 100.f);
    }

    TEST(FalloutScriptNavMeshRouteTest, a_route_through_a_door_stands_at_it_and_comes_out_on_the_far_side)
    {
        World world;
        const Found found
            = search(world.infos(), world.doors(), world.at(1), { 0, 0, 0 }, world.at(9), { 5000, 50, 0 }, true, false);
        ASSERT_TRUE(found.mReached);
        ASSERT_EQ(found.mInfos.size(), 4u);
        EXPECT_EQ(found.mDoors.back(), 100u);
        const auto points = route(found, world.doors(), world.at(1), { 0, 0, 0 }, world.at(9), { 5000, 50, 0 });
        ASSERT_EQ(points.size(), 5u); // the start, mesh 2, the door, where it comes out, the goal
        EXPECT_EQ(points[2].mKind, RoutePoint::Kind::Door);
        EXPECT_FLOAT_EQ(points[2].mPosition[0], 210.f);
        EXPECT_EQ(points[2].mDoor, 100u);
        EXPECT_EQ(points[3].mKind, RoutePoint::Kind::Landing);
        EXPECT_FLOAT_EQ(points[3].mPosition[1], 10.f);

        // a door the searcher cannot pass is no way, unless the request ignores locks
        world.mDoors[100].mPasses = false;
        EXPECT_FALSE(
            search(world.infos(), world.doors(), world.at(1), { 0, 0, 0 }, world.at(9), { 5000, 50, 0 }, true, false)
                .mReached);
        EXPECT_TRUE(
            search(world.infos(), world.doors(), world.at(1), { 0, 0, 0 }, world.at(9), { 5000, 50, 0 }, false, true)
                .mReached);
        // a request without doors does not go through
        world.mDoors[100].mPasses = true;
        EXPECT_FALSE(
            search(world.infos(), world.doors(), world.at(1), { 0, 0, 0 }, world.at(9), { 5000, 50, 0 }, false, false)
                .mReached);
    }

    TEST(FalloutScriptNavMeshRouteTest, a_goal_out_of_reach_gives_the_route_to_the_mesh_nearest_it)
    {
        World world;
        world.mInfos[3].mWorld = 50;
        Info& lone = world.mInfos[7];
        lone.mId = 7;
        lone.mWorld = 50;
        lone.mCentre = { 600.f, 0.f, 0.f };
        const Found found
            = search(world.infos(), world.doors(), world.at(1), { 0, 0, 0 }, world.at(7), { 600, 0, 0 }, true, false);
        EXPECT_FALSE(found.mReached);
        ASSERT_FALSE(found.mInfos.empty());
        EXPECT_EQ(found.mInfos.back()->mId, 3u); // 0x6B9354: the lowest estimate
    }
}
