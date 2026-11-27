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

    float avoidCost(const AvoidObject& object, const Point& point)
    {
        const auto dot = [](const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        const float r2 = object.mRadius * object.mRadius;
        float d2 = std::numeric_limits<float>::max(); // NV 0x6DC798, the float at 0x106CCD0
        if (object.mType == 0 || object.mType == 2)
        {
            const Point d = minus(object.mA, point);
            d2 = dot(d, d);
        }
        else if (object.mType == 1)
        {
            // NV 0x6DC7DE to 0x6DC858: only beside the stretch, not past its ends
            const Point s = minus(object.mB, object.mA);
            const Point v = minus(point, object.mA);
            const float l2 = dot(s, s);
            const float t = dot(v, s);
            if (t >= 0.f && l2 >= t) // 0x6DC81E to 0x6DC838
                d2 = dot(v, v) - t * t / l2;
        }
        if (!(d2 < r2))
            return 0.f; // NV 0x6DC868
        return (1.f - d2 / r2) * object.mCost;
    }

    void markStuck(Mesh& mesh, std::uint16_t triangle, const Point& waypoint, float radius)
    {
        bool add = true;
        for (Obstacle& obstacle : mesh.mObstacles)
        {
            if (obstacle.mTriangle != triangle)
                continue;
            const Point d = minus(obstacle.mPosition, waypoint);
            if (!(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < obstacle.mRadius * obstacle.mRadius))
                continue; // NV 0x9E5348
            if (obstacle.mRadius < 128.f) // NV 0x9E5360, the double at 0x102E430
                obstacle.mRadius *= 2.f;
            else
                obstacle.mCost *= 10.f; // NV 0x9E5397, the double at 0x1020758
            add = false;
        }
        if (!add)
            return;
        mesh.mObstacles.push_back({ waypoint, radius, 1.f, triangle });
        mesh.mTriangles[triangle].mFlags |= Flag_Stuck; // NV 0x69154F
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

    std::optional<float> heightAbove(const Mesh& mesh, std::uint16_t triangle, const Point& p)
    {
        const Triangle& t = mesh.mTriangles[triangle];
        const Point& a = mesh.mVertices[t.mVertices[0]];
        const Point& b = mesh.mVertices[t.mVertices[1]];
        const Point& c = mesh.mVertices[t.mVertices[2]];
        // NV 0x6B9CF0: the weights of the corners at the point; one below zero is outside (0x6B9DA6)
        const float det = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if (det == 0.f)
            return std::nullopt;
        const float inverse = 1.f / det;
        const float wa = ((b[0] - p[0]) * (c[1] - p[1]) - (c[0] - p[0]) * (b[1] - p[1])) * inverse;
        if (!(wa >= 0.f))
            return std::nullopt;
        const float wb = ((c[0] - p[0]) * (a[1] - p[1]) - (a[0] - p[0]) * (c[1] - p[1])) * inverse;
        if (!(wb >= 0.f))
            return std::nullopt;
        const float wc = ((a[0] - p[0]) * (b[1] - p[1]) - (b[0] - p[0]) * (a[1] - p[1])) * inverse;
        if (!(wc >= 0.f))
            return std::nullopt;
        // between the limits of 0x697730, the largest floats
        const float height = wa * a[2] + wb * b[2] + wc * c[2] - p[2];
        if (!(std::abs(height) < std::numeric_limits<float>::max())) // 0x6B9E59, 0x6B9E70
            return std::nullopt;
        return height;
    }

    std::optional<std::uint16_t> triangleAt(const Mesh& mesh, const Point& point, float landHeight)
    {
        // The square of the grid (NV 0x69D370): none outside the box of the grid, the last one on the far sides
        const Grid& grid = mesh.mGrid;
        if (grid.mDivisor == 0 || grid.mCells.size() != std::size_t(grid.mDivisor) * grid.mDivisor
            || point[0] < grid.mMin[0] || point[0] > grid.mMax[0] || point[1] < grid.mMin[1] || point[1] > grid.mMax[1])
            return std::nullopt;
        const auto square = [&](float offset, float size) {
            const float index = std::floor(offset / size);
            return index < float(grid.mDivisor) ? std::uint32_t(index) : grid.mDivisor - 1;
        };
        const std::uint32_t x = square(point[0] - grid.mMin[0], grid.mCellX);
        const std::uint32_t y = square(point[1] - grid.mMin[1], grid.mCellY);

        // NV 0x696A50
        constexpr float aboveLand = 50.f; // 0x101E2C0
        constexpr float lowest = -180.f; // 0x106B160
        constexpr float highest = 64.f; // 0x10240C0
        constexpr float closeEnough = 40.f; // 0x1035810
        const bool onLand = point[2] < landHeight + aboveLand; // 0x696AE4
        std::optional<std::uint16_t> best;
        float bestHeight = std::numeric_limits<float>::max();
        for (const std::uint16_t triangle : grid.mCells[y * grid.mDivisor + x])
        {
            if (triangle >= mesh.mTriangles.size() || (mesh.mTriangles[triangle].mFlags & Flag_Covered) != 0)
                continue;
            const std::optional<float> height = heightAbove(mesh, triangle, point);
            if (!height || (!onLand && *height < lowest) || *height > highest) // 0x696B85, 0x696B9A
                continue;
            const float distance = std::abs(*height);
            if (distance >= bestHeight)
                continue;
            if (distance < closeEnough) // 0x696BD0, 0x696E01
                return triangle;
            best = triangle;
            bestHeight = distance;
        }
        return best;
    }

    std::optional<Nearest> nearestTriangle(const Mesh& mesh, const Point& point)
    {
        constexpr float above = 180.f; // 0x106B158
        constexpr float below = 64.f; // 0x101E6EC
        constexpr float closeEnough = 40.f; // 0x1035810
        std::optional<std::uint16_t> inside;
        float insideHeight = std::numeric_limits<float>::max();
        std::optional<std::uint16_t> nearest;
        float nearestDistance = std::numeric_limits<float>::max();
        const auto consider = [&](std::uint16_t triangle) -> bool {
            // the test of 0x697730 with these limits: a point outside them counts as outside the triangle
            const std::optional<float> height = heightAbove(mesh, triangle, point);
            if (height && *height <= above && *height >= -below) // 0x697730 with 0x6B9E59, 0x6B9E70
            {
                const float distance = std::abs(*height);
                if (distance < insideHeight)
                {
                    if (distance < closeEnough) // 0x696BD0, 0x696E01
                    {
                        inside = triangle;
                        return true;
                    }
                    inside = triangle;
                    insideHeight = distance;
                }
                return false;
            }
            const Point middle = mesh.centroid(triangle);
            const float distance = (middle[0] - point[0]) * (middle[0] - point[0])
                + (middle[1] - point[1]) * (middle[1] - point[1]) + (middle[2] - point[2]) * (middle[2] - point[2]);
            if (distance < nearestDistance)
            {
                nearest = triangle;
                nearestDistance = distance;
            }
            return false;
        };
        const Grid& grid = mesh.mGrid;
        const bool inGrid = grid.mDivisor != 0 && grid.mCells.size() == std::size_t(grid.mDivisor) * grid.mDivisor
            && point[0] >= grid.mMin[0] && point[0] <= grid.mMax[0] && point[1] >= grid.mMin[1]
            && point[1] <= grid.mMax[1];
        if (inGrid)
        {
            const auto square = [&](float offset, float size) {
                const float index = std::floor(offset / size);
                return index < float(grid.mDivisor) ? std::uint32_t(index) : grid.mDivisor - 1;
            };
            const std::uint32_t x = square(point[0] - grid.mMin[0], grid.mCellX);
            const std::uint32_t y = square(point[1] - grid.mMin[1], grid.mCellY);
            for (const std::uint16_t triangle : grid.mCells[y * grid.mDivisor + x])
                if (triangle < mesh.mTriangles.size() && (mesh.mTriangles[triangle].mFlags & Flag_Covered) == 0
                    && consider(triangle))
                    break;
        }
        else
        {
            // every triangle, the covered ones too (NV 0x696E9F)
            for (std::size_t triangle = 0; triangle < mesh.mTriangles.size(); ++triangle)
                if (consider(static_cast<std::uint16_t>(triangle)))
                    break;
        }
        if (inside)
            return Nearest{ *inside, true };
        if (nearest)
            return Nearest{ *nearest, false };
        return std::nullopt;
    }

    Point moveOnto(const Mesh& mesh, std::uint16_t triangle, const Point& point)
    {
        const Triangle& t = mesh.mTriangles[triangle];
        const Point from = mesh.centroid(triangle);
        const auto cross = [](const Point& o, const Point& p, const Point& q) {
            return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0]);
        };
        for (unsigned edge = 0; edge < 3; ++edge)
        {
            const Point& a = mesh.mVertices[t.mVertices[edge]];
            const Point& b = mesh.mVertices[t.mVertices[(edge + 1) % 3]];
            if (a == b)
                continue;
            // NV 0x6BA240: the edge a b and the line from the middle to the point cross on the map
            const float ad = cross(point, a, b);
            const float ac = cross(from, a, b);
            const float ca = cross(a, from, point);
            const float cb = cross(b, from, point);
            if (ad * ac > 0.f || ca * cb > 0.f || ad == 0.f || cb == 0.f)
                continue;
            const float along = ca / (ca - cb);
            const Point hit{ a[0] + (b[0] - a[0]) * along, a[1] + (b[1] - a[1]) * along, a[2] + (b[2] - a[2]) * along };
            // 0x101E2BC and 0x10177E8
            return { from[0] * 0.1f + hit[0] * 0.9f, from[1] * 0.1f + hit[1] * 0.9f, from[2] * 0.1f + hit[2] * 0.9f };
        }
        return point;
    }

    std::optional<std::pair<Place, Point>> locate(
        const std::vector<Candidate>& candidates, const Point& point, float landHeight)
    {
        // NV 0x6DD74C: the one mesh of the place, whatever its flags; the point is not moved
        if (candidates.size() == 1)
        {
            const auto triangle = triangleAt(*candidates.front().mMesh, point, landHeight);
            if (!triangle)
                return std::nullopt;
            return std::make_pair(Place{ candidates.front().mMesh->mId, *triangle }, point);
        }
        // NV 0x6DD80C
        for (const Candidate& candidate : candidates)
            if (!candidate.mDisabled)
                if (const auto triangle = triangleAt(*candidate.mMesh, point, landHeight))
                    return std::make_pair(Place{ candidate.mMesh->mId, *triangle }, point);
        return locateNearest(candidates, point, landHeight);
    }

    std::optional<std::pair<Place, Point>> locateNearest(
        const std::vector<Candidate>& candidates, const Point& point, float landHeight)
    {
        // NV 0x6DDC00
        if (candidates.size() == 1)
        {
            const Mesh& mesh = *candidates.front().mMesh;
            const auto nearest = nearestTriangle(mesh, point);
            if (!nearest)
                return std::nullopt;
            return std::make_pair(Place{ mesh.mId, nearest->mTriangle },
                nearest->mInside ? point : moveOnto(mesh, nearest->mTriangle, point));
        }
        constexpr float nearLand = 25.f; // 0x104F2F0
        constexpr float above = 180.f; // 0x104ED58
        constexpr float below = -64.f; // 0x106B9F0
        const bool low = point[2] < landHeight + nearLand;
        const Mesh* bestMesh = nullptr;
        std::uint16_t bestTriangle = 0;
        float bestDistance = std::numeric_limits<float>::max();
        bool bestFits = false;
        for (const Candidate& candidate : candidates)
        {
            const Mesh& mesh = *candidate.mMesh;
            const auto nearest = nearestTriangle(mesh, point);
            if (!nearest)
                continue;
            if (nearest->mInside)
                return std::make_pair(Place{ mesh.mId, nearest->mTriangle }, point);
            const Point middle = mesh.centroid(nearest->mTriangle);
            const float distance
                = (point[0] - middle[0]) * (point[0] - middle[0]) + (point[1] - middle[1]) * (point[1] - middle[1]);
            const float height = point[2] - middle[2];
            const bool fits = (low || height < above) && height > below; // 0x6DE014 to 0x6DE03C
            if (bestFits ? fits && distance < bestDistance : fits || distance < bestDistance)
            {
                bestMesh = &mesh;
                bestTriangle = nearest->mTriangle;
                bestDistance = distance;
                bestFits = bestFits || fits;
            }
        }
        if (bestMesh == nullptr)
            return std::nullopt;
        return std::make_pair(Place{ bestMesh->mId, bestTriangle }, moveOnto(*bestMesh, bestTriangle, point));
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
        // The open list orders by the cost so far and the estimate (NV 0x6B9710, 0x6F4790); a node goes before the
        // first one that costs as much or more (0x6A8438 to 0x6A8440), so of two equal ones the last comes first
        using Entry = std::tuple<float, std::uint64_t, Place, std::uint64_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        std::uint64_t order = 0;

        const auto push = [&](Node& node) {
            node.mVersion = ++order;
            open.emplace(node.mG + node.mH, ~order, node.mPlace, node.mVersion);
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
                // An external connection of type 1 is no way (NV 0x6A70B0 to 0x6A70C6)
                if ((here.mFlags & (1u << edge)) != 0 && mesh.mExternal[here.mEdges[edge]].mType == 1)
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
                // The things the actor keeps away from, at the middle of the next triangle (NV 0x6A74AD to 0x6A7516)
                float avoidSum = 0.f;
                for (const AvoidObject& object : request.mAvoidObjects)
                    avoidSum += avoidCost(object, point);
                if (avoidSum > 0.f)
                    step *= 1.f + avoidSum;
                // Where an actor got stuck: the sum of the costs of its marks (NV 0x6A7519 to 0x6A75D0)
                if ((there.mFlags & Flag_Stuck) != 0)
                {
                    float stuckSum = 0.f;
                    bool any = false;
                    for (const Obstacle& obstacle : toMesh.mObstacles)
                        if (obstacle.mTriangle == to.mTriangle)
                        {
                            stuckSum += obstacle.mCost;
                            any = true;
                        }
                    if (any)
                        step *= stuckSum;
                }
                // A narrow edge costs up to twice what a wide one does (NV 0x6A75E5 to 0x6A768C): the edge of the next
                // triangle that leads back
                const Triangle& toTriangle = toMesh.mTriangles[to.mTriangle];
                const Point& a = toMesh.mVertices[toTriangle.mVertices[back]];
                const Point& b = toMesh.mVertices[toTriangle.mVertices[(back + 1) % 3]];
                const float narrow = std::clamp(1.f - lengthAcross(minus(a, b)) / wideEdge, 0.f, 1.f);
                step *= narrow * 0.5f + 0.5f;
                step *= waterMultiplier; // one at a time, as 0x6A768F to 0x6A76A4
                step *= avoidMultiplier;
                step *= doorFactor;

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
