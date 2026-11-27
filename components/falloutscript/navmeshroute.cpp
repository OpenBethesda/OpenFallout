#include "navmeshroute.hpp"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>

namespace FalloutScript::NavMeshRoute
{
    namespace
    {
        float distance(const Point& a, const Point& b)
        {
            const float x = a[0] - b[0];
            const float y = a[1] - b[1];
            const float z = a[2] - b[2];
            return std::sqrt(x * x + y * y + z * z); // 0x439EF0, 0x457990
        }

        struct Node
        {
            float mG = 0.f; // +0
            float mH = 0.f; // +4
            const Info* mInfo = nullptr; // +8
            std::uint32_t mDoor = 0; // +0xC
            Node* mParent = nullptr; // +0x10
            bool mOpen = false;
        };
    }

    Found search(const Infos& infos, const Doors& doors, const Info& start, const Point& startPosition,
        const Info& goal, const Point& goalPosition, bool withDoors, bool ignoreLocks)
    {
        constexpr float estimateWeight = 0.1f; // the double at 0x101FFA0
        constexpr float doorPenalty = 409600.f; // 0x106C238
        constexpr float firstListFactor = 3.f; // the double at 0x1021928

        // Both in one world space (0x6B8CCF): only then the estimate is a distance
        const bool sameWorld = start.mWorld != 0 && start.mWorld == goal.mWorld;
        const auto position = [&](const Info* info) -> const Point& {
            if (info == &start)
                return startPosition;
            if (info == &goal)
                return goalPosition;
            return info->mCentre;
        };

        std::map<const Info*, Node> nodes;
        // One list in the order of g + h; a node goes before the first that costs as much or more (0x6B94E8 to
        // 0x6B94F0), and the head is taken (0x996CE0)
        std::list<Node*> open;
        const auto insert = [&](Node* node) {
            const float f = node->mG + node->mH;
            auto at
                = std::find_if(open.begin(), open.end(), [&](const Node* other) { return f <= other->mG + other->mH; });
            open.insert(at, node);
            node->mOpen = true;
        };
        const auto remove = [&](Node* node) {
            if (node->mOpen)
                open.remove(node);
            node->mOpen = false;
        };
        // 0x6B8FE0: a node of a mesh met before keeps its cost unless the new one is lower (0x6B9029); 2 when it is
        // not, 1 when it is lowered, 0 for a new node
        const auto reach = [&](const Info* info, float g, Node*& out) -> int {
            auto [it, inserted] = nodes.try_emplace(info);
            out = &it->second;
            if (!inserted)
            {
                if (!(g < out->mG))
                    return 2;
                remove(out);
                out->mG = g;
                return 1;
            }
            out->mG = g;
            return 0;
        };

        Node* first = &nodes[&start];
        first->mInfo = &start;
        first->mH = sameWorld ? distance(goalPosition, start.mCentre) * estimateWeight : 1.f; // 0x6B8CF8 to 0x6B8D45
        insert(first);

        Node* best = nullptr;
        Node* reached = nullptr;
        std::vector<Node*> children;
        while (!open.empty())
        {
            Node* current = open.front();
            open.pop_front();
            current->mOpen = false;
            if (best == nullptr)
                best = current; // 0x6B9210
            if (current->mInfo == &goal) // 0x6B8C20
            {
                reached = current;
                best = current;
                break;
            }

            // Expand, NV 0x6B8490
            children.clear();
            const Info& info = *current->mInfo;
            const float scale = std::clamp(1.f - 100.f * info.mCost, 0.1f, 1.f); // 0x6B84B3 to 0x6B84D9
            const auto estimate = [&](const Info* to) {
                return sameWorld ? distance(goalPosition, to->mCentre) * estimateWeight : 1.f; // 0x6B8634
            };
            const auto neighbours = [&](const std::vector<std::uint32_t>& list, float factor) {
                for (const std::uint32_t id : list)
                {
                    const Info* to = infos(id);
                    if (to == nullptr)
                        continue;
                    float cost = distance(position(&info), position(to)) * scale;
                    cost = cost * factor + current->mG; // 0x6B85EB to 0x6B85F4; the second list has no factor
                    Node* node = nullptr;
                    const int result = reach(to, cost, node);
                    if (result == 2)
                        continue;
                    if (result == 0)
                    {
                        node->mInfo = to;
                        node->mH = estimate(to);
                    }
                    node->mDoor = 0;
                    children.push_back(node);
                }
            };
            neighbours(info.mNeighbours, firstListFactor);
            neighbours(info.mNeighboursToo, 1.f); // 0x6B87A9
            if (withDoors || ignoreLocks) // 0x6B885D to 0x6B8873
            {
                for (const std::uint32_t id : info.mDoors)
                {
                    if (id == 0)
                        continue;
                    const std::optional<Door> door = doors(id);
                    if (!door)
                        continue;
                    float penalty = 0.f;
                    if (!door->mPasses && !ignoreLocks)
                        continue; // mode 0 (0x6B8958); mode 1 costs nothing more
                    if (door->mMinimalUse)
                        penalty = doorPenalty; // 0x6B8973
                    const Info* to = infos(door->mTarget);
                    if (to == nullptr || (to->mFlags & InfoFlag_Unusable) != 0) // 0x6B8A09, 0x6B8A1A
                        continue;
                    float cost = distance(door->mNear, position(&info)) + distance(position(to), door->mFar);
                    cost = cost * scale * firstListFactor + current->mG + penalty; // 0x6B8B36 to 0x6B8B7B
                    Node* node = nullptr;
                    const int result = reach(to, cost, node);
                    if (result == 2)
                        continue;
                    if (result == 0)
                    {
                        node->mInfo = to;
                        node->mH = sameWorld ? current->mH + current->mH : 1.f; // 0x6B8BD8 to 0x6B8BF3
                    }
                    node->mDoor = id;
                    children.push_back(node);
                }
            }

            // NV 0x6B92CF to 0x6B935C
            bool abort = false;
            for (Node* child : children)
            {
                remove(child);
                if (child == current->mParent)
                {
                    abort = true; // 0x6B92F7: the search ends without the goal
                    break;
                }
                child->mParent = current;
                insert(child);
                if (child->mH < best->mH)
                    best = child;
            }
            if (abort)
                break;
        }

        // 0x6B8F40 with a partial route, and 0x996AE0
        Found found;
        found.mReached = reached != nullptr;
        for (const Node* node = best; node != nullptr; node = node->mParent)
        {
            found.mInfos.push_back(node->mInfo);
            if (node->mParent != nullptr)
                found.mDoors.push_back(node->mDoor);
        }
        std::reverse(found.mInfos.begin(), found.mInfos.end());
        std::reverse(found.mDoors.begin(), found.mDoors.end());
        return found;
    }

    std::vector<RoutePoint> route(const Found& found, const Doors& doors, const Info& start, const Point& startPosition,
        const Info& goal, const Point& goalPosition)
    {
        using Kind = RoutePoint::Kind;
        std::vector<RoutePoint> points;
        points.push_back({ Kind::Location, startPosition, &start, 0 });
        if (found.mInfos.size() > 1) // 0x6C9611
        {
            const auto crossing = [&](std::size_t i) {
                const std::uint32_t id = found.mDoors[i];
                const std::optional<Door> door = doors(id);
                const Point nearSide = door ? door->mNear : Point{}; // the zero vector 0x11F426C
                const Point farSide = door ? door->mFar : Point{};
                points.push_back({ Kind::Door, nearSide, found.mInfos[i], id }); // 0x6C9A70
                points.push_back({ Kind::Landing, farSide, found.mInfos[i + 1], 0 }); // 0x6C99C0
            };
            if (found.mDoors[0] != 0) // 0x6C9677
                crossing(0);
            for (std::size_t i = 1; i + 1 < found.mInfos.size(); ++i) // 0x6C9748 to 0x6C9887
            {
                if (found.mDoors[i] != 0)
                    crossing(i);
                else
                    points.push_back({ Kind::Mesh, found.mInfos[i]->mCentre, found.mInfos[i], 0 }); // 0x6C9920
            }
        }
        points.push_back({ Kind::Location, goalPosition, &goal, 0 }); // 0x6C9899
        return points;
    }
}
