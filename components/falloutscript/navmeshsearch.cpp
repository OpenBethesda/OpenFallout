#include "navmeshsearch.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

namespace FalloutScript::NavMeshSearch
{
    namespace
    {
        Point minus(const Point& a, const Point& b)
        {
            return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
        }

        // NV 0x589850: the length on the map, without the height
        float lengthAcross(const Point& v)
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1]);
        }

        // NV 0x457990
        float length(const Point& v)
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        // The weight of the distance to the goal in the estimate of a triangle (NV 0x6A6F7A, the double at 0x1016408)
        constexpr float goalWeight = 0.01f;
        // A step into a preferred triangle costs this share (NV 0x6A74A4, the same double)
        constexpr float preferredShare = 0.01f;
        // Water: a step into it or out of it, and a step in it (NV 0x6A736D 0x101712C, 0x6A7380 0x101E570)
        constexpr float waterEdgeFactor = 5.f;
        constexpr float waterFactor = 4.f;
        // An edge to avoid, a costly door (NV 0x6A7299 and 0x6A73.., the float at 0x1016410)
        constexpr float avoidFactor = 100.f;
        // An edge narrower than this costs more to cross (NV 0x6A763D, the float at 0x10231A0)
        constexpr float wideEdge = 512.f;

        struct Node
        {
            Place mPlace;
            float mG = 0.f;
            float mH = 0.f;
            Point mPoint{};
            Place mParent;
            std::uint16_t mParentEdge = 0xffff;
            bool mHasParent = false;
            bool mClosed = false;
            std::uint64_t mVersion = 0;
        };
    }

    Point Mesh::centroid(std::uint16_t triangle) const
    {
        const Triangle& t = mTriangles[triangle];
        Point sum{};
        for (const std::uint16_t corner : t.mVertices)
            for (int i = 0; i < 3; ++i)
                sum[i] += mVertices[corner][i];
        return { sum[0] / 3.f, sum[1] / 3.f, sum[2] / 3.f };
    }

    std::optional<std::pair<Place, std::uint16_t>> across(const Meshes& meshes, const Place& place, unsigned edge)
    {
        const Mesh* mesh = meshes(place.mMesh);
        if (mesh == nullptr || place.mTriangle >= mesh->mTriangles.size())
            return std::nullopt;
        const Triangle& triangle = mesh->mTriangles[place.mTriangle];
        const std::uint16_t value = triangle.mEdges[edge];
        if (value == 0xffff)
            return std::nullopt;
        if ((triangle.mFlags & (1u << edge)) == 0)
        {
            // In the mesh: the triangle across, and its edge whose triangle is this one (NV 0x68F5DC to 0x68F660)
            if (value >= mesh->mTriangles.size())
                return std::nullopt;
            const Triangle& other = mesh->mTriangles[value];
            for (std::uint16_t back = 0; back < 3; ++back)
                if ((other.mFlags & (1u << back)) == 0 && other.mEdges[back] == place.mTriangle)
                    return std::make_pair(Place{ place.mMesh, value }, back);
            return std::nullopt;
        }
        // To another mesh, which has to be loaded, by a connection that it has back (NV 0x68F4BF to 0x68F5C9)
        if (value >= mesh->mExternal.size())
            return std::nullopt;
        const Link& link = mesh->mExternal[value];
        const Mesh* target = meshes(link.mMesh);
        if (target == nullptr || link.mTriangle >= target->mTriangles.size())
            return std::nullopt;
        const Triangle& other = target->mTriangles[link.mTriangle];
        for (std::uint16_t back = 0; back < 3; ++back)
        {
            if ((other.mFlags & (1u << back)) == 0 || other.mEdges[back] >= target->mExternal.size())
                continue;
            const Link& returning = target->mExternal[other.mEdges[back]];
            if (returning.mMesh == place.mMesh && returning.mTriangle == place.mTriangle)
                return std::make_pair(Place{ link.mMesh, link.mTriangle }, back);
        }
        return std::nullopt;
    }

    std::optional<std::vector<Step>> search(
        const Meshes& meshes, const Place& start, const Place& goal, const Request& request, const DoorRule& doors)
    {
        const Mesh* startMesh = meshes(start.mMesh);
        const Mesh* goalMesh = meshes(goal.mMesh);
        if (startMesh == nullptr || goalMesh == nullptr || start.mTriangle >= startMesh->mTriangles.size()
            || goal.mTriangle >= goalMesh->mTriangles.size())
            return std::nullopt;
        // The points of the search are the middles of the triangles, the start's and the goal's too (NV 0x6A6C0F,
        // 0x6A6C84, 0x6A6F23)
        const Point goalPoint = goalMesh->centroid(goal.mTriangle);

        std::map<Place, Node> nodes;
        // The open list orders by the cost so far and the estimate (NV 0x6B9710, 0x6F4790); the first of two equal
        // ones comes first
        using Entry = std::tuple<float, std::uint64_t, Place, std::uint64_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        std::uint64_t order = 0;

        const auto push = [&](Node& node) {
            node.mVersion = ++order;
            open.emplace(node.mG + node.mH, order, node.mPlace, node.mVersion);
        };

        Node& first = nodes[start];
        first.mPlace = start;
        first.mPoint = startMesh->centroid(start.mTriangle);
        first.mH = length(minus(first.mPoint, goalPoint)) * goalWeight;
        push(first);

        while (!open.empty())
        {
            const auto [f, unused, place, version] = open.top();
            open.pop();
            Node& current = nodes[place];
            if (current.mVersion != version || current.mClosed)
                continue;
            current.mClosed = true;

            // NavMeshSearch's goal (NV 0x6A79C0): the triangle of the goal; for a searcher with a radius over 100,
            // also a point within half the goal radius on the map that is farther on the map than up or down
            const Point toGoal = minus(goalPoint, current.mPoint);
            const float across2 = toGoal[0] * toGoal[0] + toGoal[1] * toGoal[1];
            const bool nearEnough = request.mRadius > 100.f
                && across2 < 0.25f * request.mGoalRadius * request.mGoalRadius && across2 > toGoal[2] * toGoal[2];
            if (place == goal || nearEnough)
            {
                std::vector<Step> path;
                for (const Node* node = &current;;)
                {
                    path.push_back(Step{ node->mPlace, node->mParentEdge });
                    if (!node->mHasParent)
                        break;
                    node = &nodes[node->mParent];
                }
                std::reverse(path.begin(), path.end());
                return path;
            }

            const Mesh& mesh = *meshes(place.mMesh);
            const Triangle& here = mesh.mTriangles[place.mTriangle];
            for (unsigned edge = 0; edge < 3; ++edge)
            {
                const auto next = across(meshes, place, edge);
                if (!next)
                    continue;
                const Place& to = next->first;
                const std::uint16_t back = next->second;
                const Mesh& toMesh = *meshes(to.mMesh);
                const Triangle& there = toMesh.mTriangles[to.mTriangle];

                float doorFactor = 1.f;
                if ((there.mFlags & Flag_Door) != 0)
                {
                    // NV 0x6A71A8 to 0x6A7299
                    if (request.mNoDoors)
                        continue;
                    const auto found = std::find_if(toMesh.mDoors.begin(), toMesh.mDoors.end(),
                        [&](const auto& door) { return door.first == to.mTriangle; });
                    if (found != toMesh.mDoors.end() && doors)
                    {
                        const Door door = doors(found->second);
                        if (door == Door::Closed)
                            continue;
                        if (door == Door::Costly)
                            doorFactor = avoidFactor;
                    }
                }
                // A large actor does not go where large actors may not (NV 0x6A72AE to 0x6A72EF)
                if (request.mRadius > request.mLargeActorRadius && (there.mFlags & Flag_NoLargeActors) != 0)
                    continue;

                // Water (NV 0x6A72F4 to 0x6A7386)
                float waterMultiplier = 1.f;
                const bool waterThere = (there.mFlags & Flag_Water) != 0;
                const bool waterHere = (here.mFlags & Flag_Water) != 0;
                if (!request.mWater && !request.mWaterToo)
                {
                    if (waterThere)
                        continue;
                }
                else if (waterThere != waterHere)
                    waterMultiplier = waterEdgeFactor;
                else if (waterThere)
                    waterMultiplier = waterFactor;

                // NV 0x6A738E
                const float avoidMultiplier = request.mAvoid.count({ place, edge }) != 0 ? avoidFactor : 1.f;

                // The step: from the point of this triangle to the middle of the next on the map (NV 0x6A7439 to
                // 0x6A7483)
                const Point point = toMesh.centroid(to.mTriangle);
                float step = lengthAcross(minus(point, current.mPoint));
                if ((there.mFlags & Flag_Preferred) != 0)
                    step *= preferredShare;
                // A narrow edge costs up to twice what a wide one does (NV 0x6A75E5 to 0x6A768C): the edge of the next
                // triangle that leads back
                const Triangle& toTriangle = toMesh.mTriangles[to.mTriangle];
                const Point& a = toMesh.mVertices[toTriangle.mVertices[back]];
                const Point& b = toMesh.mVertices[toTriangle.mVertices[(back + 1) % 3]];
                const float narrow = std::clamp(1.f - lengthAcross(minus(a, b)) / wideEdge, 0.f, 1.f);
                step *= narrow * 0.5f + 0.5f;
                step *= waterMultiplier * avoidMultiplier * doorFactor;

                const float g = current.mG + step;
                auto [it, inserted] = nodes.try_emplace(to);
                Node& node = it->second;
                if (!inserted && !(g < node.mG))
                    continue; // NV 0x6A7E45: only a cheaper way changes it
                if (inserted)
                {
                    node.mPlace = to;
                    node.mPoint = point;
                    node.mH = length(minus(point, goalPoint)) * goalWeight;
                }
                node.mG = g;
                node.mParent = place;
                node.mParentEdge = static_cast<std::uint16_t>(edge);
                node.mHasParent = true;
                node.mClosed = false;
                push(node);
            }
        }
        return std::nullopt;
    }
}
