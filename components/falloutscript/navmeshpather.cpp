#include "navmeshpather.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FalloutScript::NavMeshPather
{
    namespace
    {
        using NavMeshSearch::Place;
        using NavMeshSearch::Step;
        using NavMeshSearch::Triangle;

        constexpr float maxFloat = std::numeric_limits<float>::max(); // 0x1016970
        constexpr float edgeRadius = 512.f; // fFindClosestEdgesRadius:Pathing (0x11D74DC)
        constexpr float bandBelow = -64.f; // 0x106B9F0
        constexpr float bandAbove = 180.f; // 0x104ED58
        constexpr float toMiddle = 0.1f; // 0x101E2BC
        constexpr float doorEnd = 80.f; // 0x101E33C
        constexpr float nearEnough = 512.f; // 0x1028338
        constexpr float waterMargin = 25.f; // 0x104F2F0
        constexpr float rayLift = 32.f; // the pather's ray object +0x90 (0x101E340)
        constexpr float fallHeight = 256.f; // fJumpFallHeightMin (0x11CFF94), copied at 0x6E6237
        constexpr float primeRadius = 150.f; // 0x106C948

        Point minus(const Point& a, const Point& b)
        {
            return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
        }
        Point plus(const Point& a, const Point& b)
        {
            return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
        }
        Point times(const Point& a, float s)
        {
            return { a[0] * s, a[1] * s, a[2] * s };
        }
        float dot(const Point& a, const Point& b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }
        float length(const Point& a)
        {
            return std::sqrt(dot(a, a)); // 0x457990
        }
        float length2Across(const Point& a)
        {
            return a[0] * a[0] + a[1] * a[1]; // 0x595C80
        }
        // The point of the segment from a to b nearest p (NV 0x6B9C20)
        Point nearestOn(const Point& p, const Point& a, const Point& b)
        {
            const Point ab = minus(b, a);
            const float t = dot(minus(p, a), ab);
            if (t <= 0.f)
                return a;
            const float l2 = dot(ab, ab);
            if (t >= l2)
                return b;
            return plus(a, times(ab, t / l2));
        }
        // (a - o) x (b - o) on the map
        float cross(const Point& o, const Point& a, const Point& b)
        {
            return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
        }

        bool hasTriangle(const World& world, const Spot& spot)
        {
            // NV 0x6DD6A0
            const Mesh* mesh = spot.mMesh != 0 ? world.mMeshes(spot.mMesh) : nullptr;
            return mesh != nullptr && spot.mTriangle && *spot.mTriangle < mesh->mTriangles.size();
        }

        Spot onTriangle(const World& world, const Point& point, std::uint32_t mesh, std::uint16_t triangle)
        {
            // NV 0x6DD010: a place built from a point, a mesh and a triangle
            Spot spot = world.mSpotOfMesh(mesh);
            spot.mPoint = point;
            spot.mMesh = mesh;
            spot.mTriangle = triangle;
            return spot;
        }

        // A triangle's edge with none across it (NV 0x68F200 false)
        bool border(const Triangle& triangle, unsigned edge)
        {
            return triangle.mEdges[edge] == 0xffff;
        }

        // NV 0x697010: the edge with none across it whose middle is nearest a point on the map (below the best so far,
        // which is kept as the distance in 3D) and either at a height near the point's (`below`: 180 below to 64 above;
        // else 64 below to 180 above) or within a distance of it counting the height twice
        std::optional<std::pair<std::uint16_t, unsigned>> borderEdge(
            const Mesh& mesh, const Point& point, float radius, bool below)
        {
            const float r2 = radius * radius;
            const float lo = point[2] - (below ? bandAbove : -bandBelow);
            const float hi = point[2] + (below ? -bandBelow : bandAbove);
            float best = maxFloat;
            std::optional<std::pair<std::uint16_t, unsigned>> found;
            for (std::size_t t = 0; t < mesh.mTriangles.size(); ++t)
            {
                const Triangle& triangle = mesh.mTriangles[t];
                for (unsigned e = 0; e < 3; ++e)
                {
                    if (!border(triangle, e))
                        continue;
                    const Point& a = mesh.mVertices[triangle.mVertices[e]];
                    const Point& b = mesh.mVertices[triangle.mVertices[(e + 1) % 3]];
                    const Point middle = times(plus(a, b), 0.5f); // 0x1016248
                    const Point d = minus(middle, point);
                    const float across = length2Across(d);
                    const float up = d[2] * d[2];
                    const float all = across + up;
                    if (!(across < best)) // 0x697158
                        continue;
                    const bool inBand = middle[2] < hi && lo < middle[2]; // 0x697167, 0x697176
                    if (!inBand && !(all <= r2 - up)) // 0x697188
                        continue;
                    found = std::make_pair(static_cast<std::uint16_t>(t), e);
                    best = all; // 0x69719F
                }
            }
            return found;
        }

        /// An end of a stretch put on the meshes, with the candidates of the snap (NV 0x6D4120, 0x20 bytes)
        struct Snap
        {
            std::uint32_t mMesh = 0;
            std::uint16_t mTriangle = 0;
            unsigned mEdge = 0;
            Point mPoint{}; // +0xC: the nearest point of the edge, a tenth of the way to the triangle's middle
            float mAcross = 0.f; // +0x18
            float mUp = 0.f; // +0x1C, the place's height over the nearest point
        };

        /// One stretch of the route, and how the pather goes along it
        class Pather
        {
        public:
            /// A point of the coarse route (0x30 bytes: +0 flags, +4 its place, +0x2C a door)
            struct RoutePoint
            {
                Spot mSpot;
                bool mResolved = false; // flag 1 (0x6CA830): its place has a mesh
                bool mHasPosition = false; // flag 2 (0x6CAC70)
                bool mDoor = false; // flag 4 (0x6CA0C0)
                std::uint32_t mDoorRef = 0;
            };

            Pather(const World& world, const Request& request, const Spot& start, const Spot& goal)
                : mWorld(world)
                , mRequest(request)
                , mStart(start)
                , mGoal(goal)
            {
            }

            std::vector<RoutePoint> mRoute;
            Path mPath;
            std::size_t mFirst = 0; // path +0x18
            std::size_t mLast = 0; // path +0x1C

            bool resolve(Spot& spot) const { return NavMeshPather::resolve(mWorld, spot); }

            float water(const Spot& spot) const { return mWorld.mWater(spot.mCell, spot.mPoint); }

            bool loaded(const RoutePoint& point) const { return point.mSpot.mCell != 0; } // 0x450FF0

            void append(const Spot& spot, std::uint32_t door = 0)
            {
                WayPoint point;
                point.mLocation.mPlace = { spot.mMesh, spot.mTriangle.value_or(0) };
                point.mLocation.mPoint = spot.mPoint;
                mPath.mPoints.push_back(point);
                mPath.mDoors.push_back(door);
            }
            void cut(std::size_t count)
            {
                mPath.mPoints.resize(count); // 0x6C9350
                mPath.mDoors.resize(count);
            }

            // NV 0x6D4120: none (0), the place has its triangle (1), or the nearest edges with none across them within
            // fFindClosestEdgesRadius, those at a height near the place's first, then by distance on the map (2)
            int candidates(Spot& spot, std::size_t most, std::vector<Snap>& out) const
            {
                out.clear();
                if (resolve(spot) && hasTriangle(mWorld, spot))
                    return 1; // 0x6D4161
                for (const Mesh* mesh : mWorld.mNear(spot, edgeRadius))
                {
                    for (std::size_t t = 0; t < mesh->mTriangles.size(); ++t)
                    {
                        const Triangle& triangle = mesh->mTriangles[t];
                        for (unsigned e = 0; e < 3; ++e)
                        {
                            if (!border(triangle, e)) // 0x6D4262
                                continue;
                            const Point nearest = nearestOn(spot.mPoint, mesh->mVertices[triangle.mVertices[e]],
                                mesh->mVertices[triangle.mVertices[(e + 1) % 3]]);
                            const Point v = minus(spot.mPoint, nearest);
                            const float across = length2Across(v);
                            if (!(across < edgeRadius * edgeRadius)) // 0x6D4315
                                continue;
                            Snap c;
                            c.mMesh = mesh->mId;
                            c.mTriangle = static_cast<std::uint16_t>(t);
                            c.mEdge = e;
                            const Point middle = mesh->centroid(c.mTriangle);
                            c.mPoint = plus(nearest, times(minus(middle, nearest), toMiddle)); // 0x6D4350
                            c.mAcross = across;
                            c.mUp = v[2];
                            out.push_back(c);
                        }
                    }
                }
                // 0x6D4020
                const auto inBand = [](const Snap& c) { return bandBelow < c.mUp && c.mUp < bandAbove; };
                std::stable_sort(out.begin(), out.end(), [&](const Snap& a, const Snap& b) {
                    if (inBand(a) != inBand(b))
                        return inBand(a);
                    return a.mAcross < b.mAcross;
                });
                if (out.size() > most)
                    out.resize(most);
                return out.empty() ? 0 : 2;
            }

            // NV 0x6E6F90: a step of the walk over the ground, a ray 32 up from one point to the next, then one down
            // from 32 above the next for fJumpFallHeightMin + 32: where it hits
            std::optional<Point> step(const Point& from, const Point& to) const
            {
                const Point a{ from[0], from[1], from[2] + rayLift };
                const Point b{ to[0], to[1], to[2] + rayLift };
                if (!mWorld.mClear(a, b))
                    return std::nullopt;
                return mWorld.mDown(b, fallHeight + rayLift);
            }

            // NV 0x6E6E40: the ground from one point to another, a step of the request's radius (at least 32) at a
            // time, ends within 64 of the height of the other
            bool overGround(const Point& from, const Point& to) const
            {
                const float stepLength = std::max(mRequest.mRadius, rayLift); // 0x6E6338 to 0x6E634C
                const Point d = minus(to, from);
                const float l = length(d);
                const int steps = l > 0.f ? static_cast<int>(l / stepLength) : 0;
                const Point unit = l > 0.f ? times(d, 1.f / l) : Point{};
                Point at = from;
                for (int i = 0; i < steps; ++i)
                {
                    Point next = plus(at, times(unit, stepLength));
                    next[2] = at[2];
                    const auto hit = step(at, next);
                    if (!hit)
                        return false;
                    at = *hit;
                }
                const auto hit = step(at, { to[0], to[1], at[2] });
                return hit && std::abs((*hit)[2] - to[2]) < 64.f; // 0x10240C0
            }

            // NV 0x6CB050 with 0x6E6320 modes 0 and 1: the nearest edge that the actor gets to over the ground from
            // the place (`fromPlace`, the start) or that gets to the place (the goal)
            int raySnap(Spot& spot, std::vector<Spot>& chain, bool fromPlace, std::vector<Snap>& found) const
            {
                const int r = candidates(spot, 10, found);
                if (r == 0)
                    return 2;
                if (r == 1)
                {
                    chain.push_back(spot);
                    return 1;
                }
                while (!found.empty())
                {
                    const Snap c = found.front();
                    const Mesh* mesh = mWorld.mMeshes(c.mMesh);
                    if (mesh == nullptr)
                    {
                        found.clear();
                        return 2;
                    }
                    if (fromPlace ? overGround(spot.mPoint, c.mPoint) : overGround(c.mPoint, spot.mPoint))
                    {
                        const Point middle = mesh->centroid(c.mTriangle);
                        const Point p = plus(c.mPoint, times(minus(middle, c.mPoint), toMiddle)); // 0x63C8A0
                        chain.push_back(spot);
                        chain.push_back(onTriangle(mWorld, p, c.mMesh, c.mTriangle));
                        return 1;
                    }
                    found.erase(found.begin()); // 0x6E7500
                }
                return 2;
            }

            // NV 0x6CB4C0 with 0x6E6320 mode 2: from one place to the other with rays, one straight one when both are
            // under water (0x5471E0), else over the ground; 1 when it can, 2 when not
            int rayJoin(const Spot& a, const Spot& b) const
            {
                const bool under = a.mCell != 0 && b.mCell != 0 && a.mPoint[2] < water(a) && b.mPoint[2] < water(b);
                const bool ok = under ? mWorld.mClear(a.mPoint, b.mPoint) : overGround(a.mPoint, b.mPoint);
                return ok ? 1 : 2;
            }

            // NV 0x6CDE50 (`start`) and 0x6CE2D0: where the route goes onto the mesh of `on` from `from`
            std::optional<Spot> entry(const RoutePoint& from, const RoutePoint& on, bool start) const
            {
                const std::uint32_t meshId = on.mSpot.mMesh;
                const Mesh* mesh = meshId != 0 ? mWorld.mMeshes(meshId) : nullptr;
                if (mesh == nullptr)
                    return std::nullopt; // 0x6CDEC8
                std::optional<Spot> out;
                if (on.mHasPosition)
                {
                    const NavMeshRoute::Info* info = mWorld.mInfos(meshId);
                    if (info == nullptr)
                        return std::nullopt;
                    const float radius = 2.f * length(minus(from.mSpot.mPoint, info->mCentre));
                    const auto edge = borderEdge(*mesh, from.mSpot.mPoint, radius, start); // flag 1 / 0, 0x6CE3E4
                    if (!edge)
                        return std::nullopt;
                    const Triangle& triangle = mesh->mTriangles[edge->first];
                    const Point& a = mesh->mVertices[triangle.mVertices[edge->second]];
                    const Point& b = mesh->mVertices[triangle.mVertices[(edge->second + 1) % 3]];
                    // the middle of the edge a tenth of the way to the middle of the triangle (0x104E2B8, 0x101E2BC)
                    const Point p = plus(times(plus(a, b), 0.45f), times(mesh->centroid(edge->first), toMiddle));
                    out = onTriangle(mWorld, p, meshId, edge->first);
                }
                else
                {
                    // NV 0x697590: the triangles with an edge to the other's mesh; the one whose middle is nearest the
                    // request's start (or goal)
                    const Point& to = start ? mStart.mPoint : mGoal.mPoint;
                    float best = maxFloat;
                    for (std::size_t t = 0; t < mesh->mTriangles.size(); ++t)
                    {
                        const Triangle& triangle = mesh->mTriangles[t];
                        bool linked = false;
                        for (unsigned e = 0; e < 3; ++e)
                            if ((triangle.mFlags & (1u << e)) != 0 && triangle.mEdges[e] < mesh->mExternal.size()
                                && mesh->mExternal[triangle.mEdges[e]].mMesh == from.mSpot.mMesh)
                                linked = true;
                        if (!linked)
                            continue;
                        if (mRequest.mRadius > primeRadius
                            && (triangle.mFlags & NavMeshSearch::Flag_NoLargeActors) != 0)
                            continue;
                        const Point middle = mesh->centroid(static_cast<std::uint16_t>(t));
                        const float key = length(minus(middle, to));
                        if (key < best)
                        {
                            best = key;
                            out = onTriangle(mWorld, middle, meshId, static_cast<std::uint16_t>(t));
                        }
                    }
                }
                // 0x6CE4BE to 0x6CE4E4, 0x6CE6EA to 0x6CE722: the goal's side is not under the water
                if (out && !start)
                {
                    const float surface = water(*out);
                    if (out->mPoint[2] < surface + waterMargin)
                        out->mPoint[2] = surface;
                }
                return out;
            }

            // NV 0x6CAA40: the start of the stretch from route point `i` on the meshes; 1, or 2 when it is not
            int snapStart(std::size_t i, std::vector<Spot>& chain, std::vector<Snap>& found)
            {
                RoutePoint& p = mRoute[i];
                if (i != 0) // byte +0x10, set only by a path that goes on past a door, is clear (0x6CAA87)
                {
                    const RoutePoint& previous = mRoute[i - 1];
                    if (i > mFirst ? previous.mHasPosition : !p.mHasPosition) // 0x6CAAB6, 0x6CAB2F
                        if (const auto at = entry(previous, p, true))
                        {
                            chain.push_back(*at);
                            return 1;
                        }
                }
                if (!p.mHasPosition)
                    return 2; // 0x6CAC4B
                if (!resolve(p.mSpot))
                    return 2; // 0x6CABCD
                if (hasTriangle(mWorld, p.mSpot))
                {
                    chain.push_back(p.mSpot);
                    return 1;
                }
                const int r = candidates(p.mSpot, 10, found);
                if (r == 1)
                {
                    chain.push_back(p.mSpot);
                    return 1;
                }
                if (r == 2)
                    return raySnap(p.mSpot, chain, true, found); // 0x6CAC3B
                return 2;
            }

            // NV 0x6CAC90: the goal of the stretch at route point `i`
            int snapGoal(std::size_t i, std::vector<Spot>& chain, std::vector<Snap>& found)
            {
                RoutePoint& p = mRoute[i];
                if (i + 1 != mRoute.size())
                {
                    const RoutePoint& next = mRoute[i + 1];
                    if (i < mLast ? next.mHasPosition : !p.mDoor) // 0x6CAD16, 0x6CADB2
                        if (const auto at = entry(next, p, false))
                        {
                            // the route point takes the place (0x6CAD66, 0x6CADF9)
                            p.mSpot = *at;
                            chain.push_back(*at);
                            return 1;
                        }
                }
                if (!p.mHasPosition)
                    return 2; // 0x6CB02F
                if (!resolve(p.mSpot))
                    return 2; // 0x6CAE6C
                if (hasTriangle(mWorld, p.mSpot))
                {
                    chain.push_back(p.mSpot);
                    return 1;
                }
                const int r = candidates(p.mSpot, 10, found);
                if (r == 1)
                {
                    chain.push_back(p.mSpot);
                    return 1;
                }
                if (r != 2)
                    return 2; // 0x6CB026
                // 0x6CAEE4 to 0x6CB006: the first one near the goal
                const float r2 = mRequest.mGoalRadius * mRequest.mGoalRadius;
                for (const Snap& c : found)
                {
                    const Point v = minus(p.mSpot.mPoint, c.mPoint);
                    if (length2Across(v) < r2 && v[2] > bandBelow && v[2] < bandAbove)
                    {
                        chain.push_back(onTriangle(mWorld, c.mPoint, c.mMesh, c.mTriangle));
                        return 1;
                    }
                }
                return raySnap(p.mSpot, chain, false, found); // 0x6CB01F
            }

            // NV 0x6CB6A0: a goal off the meshes is near enough the nearest edge
            bool offMeshGoal(std::size_t last, const std::vector<Snap>& found, std::vector<Spot>& chain) const
            {
                const Snap& c = found.front();
                const Mesh* mesh = mWorld.mMeshes(c.mMesh);
                if (mesh == nullptr)
                    return false;
                const Triangle& triangle = mesh->mTriangles[c.mTriangle];
                const Point& at = mRoute[last].mSpot.mPoint;
                Point nearest = nearestOn(at, mesh->mVertices[triangle.mVertices[c.mEdge]],
                    mesh->mVertices[triangle.mVertices[(c.mEdge + 1) % 3]]);
                const Point v = minus(nearest, at);
                if (!(length2Across(v) < mRequest.mGoalRadius * mRequest.mGoalRadius) || !(v[2] > bandBelow)
                    || !(v[2] < bandAbove)) // 0x6CB7B1 to 0x6CB810
                    return false;
                nearest = plus(nearest, times(minus(mesh->centroid(c.mTriangle), nearest), toMiddle));
                chain.clear(); // 0x6AC010
                chain.push_back(onTriangle(mWorld, nearest, c.mMesh, c.mTriangle));
                return true;
            }

            // NV 0x6DD0D0: a place at a point by another place: on that one's mesh when it has a triangle under the
            // point, else a place of its own to be resolved
            Spot near(const Point& point, const Spot& base) const
            {
                if (const Mesh* mesh = base.mMesh != 0 ? mWorld.mMeshes(base.mMesh) : nullptr)
                {
                    Spot probe = base;
                    probe.mPoint = point;
                    if (const auto t = NavMeshSearch::triangleAt(*mesh, point, water(probe)))
                    {
                        probe.mTriangle = t;
                        return probe;
                    }
                }
                return mWorld.mSpotAt(point, base);
            }

            // NV 0x6D7350 with 0x6D74C0: a straight line on the map from one place to the other crosses edge after
            // edge to the goal's triangle; `strict`: not into water, a door, a mark of a stuck actor or, for a large
            // actor, a triangle for small ones (the edge test 0x6CD5C0)
            bool straight(const Spot& from, const Spot& to, bool strict) const
            {
                Spot goal = to;
                Spot start = from;
                if (!resolve(goal))
                    return !resolve(start); // 0x6D7391 to 0x6D73B2
                const bool goalOn = hasTriangle(mWorld, goal);
                if (!resolve(start))
                    return !goalOn; // 0x6D750B
                if (!hasTriangle(mWorld, start))
                    return false; // 0x6D7545
                const bool large = mRequest.mRadius > mRequest.mLargeActorRadius; // 0x6CD22E
                Place place{ start.mMesh, *start.mTriangle };
                Point a = start.mPoint;
                const Point& b = to.mPoint;
                int skip = -1;
                // shortcut: the exe walks with no limit (0x6D74C0); a cycle of float errors would hang the game, so
                // the walk gives up after more crossings than any loaded mesh has triangles, never in the data
                for (int guard = 0; guard < 65536; ++guard)
                {
                    const Mesh& mesh = *mWorld.mMeshes(place.mMesh);
                    const Triangle& triangle = mesh.mTriangles[place.mTriangle];
                    // NV 0x698060 with 0x6B9E90: the first edge the line crosses properly, past the one it came in by
                    int exit = -1;
                    Point x{};
                    for (int e = 0; e < 3 && exit < 0; ++e)
                    {
                        if (e == skip)
                            continue;
                        const Point& p0 = mesh.mVertices[triangle.mVertices[e]];
                        const Point& p1 = mesh.mVertices[triangle.mVertices[(e + 1) % 3]];
                        const float c4 = cross(b, p0, p1);
                        const float c8 = cross(a, p0, p1);
                        if (!(c4 * c8 < 0.f)) // 0x6B9F06
                            continue;
                        const float c10 = cross(p0, a, b);
                        float c12 = c10 + c8 - c4;
                        if (!(std::abs(c12) >= 0.001f)) // 0x6B9F67
                            c12 = cross(p1, a, b);
                        if (!(c10 * c12 < 0.f)) // 0x6B9FAF
                            continue;
                        const float t = c10 / (c10 - c12);
                        x = { p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, 0.f };
                        exit = e;
                    }
                    if (exit < 0)
                        return !goalOn || place == Place{ goal.mMesh, *goal.mTriangle }; // 0x6D75FD, 0x6D78BB
                    const auto next = NavMeshSearch::across(mWorld.mMeshes, place, static_cast<unsigned>(exit));
                    if (!next)
                        return false;
                    if (strict)
                    {
                        const std::uint32_t flags
                            = mWorld.mMeshes(next->first.mMesh)->mTriangles[next->first.mTriangle].mFlags;
                        if ((flags & NavMeshSearch::Flag_Water) != 0
                            || (large && (flags & NavMeshSearch::Flag_NoLargeActors) != 0)
                            || (flags & NavMeshSearch::Flag_Stuck) != 0 || (flags & NavMeshSearch::Flag_Door) != 0)
                            return false;
                    }
                    if ((triangle.mFlags & (1u << exit)) != 0 && mesh.mExternal[triangle.mEdges[exit]].mType == 1)
                        return false; // 0x6D76D8
                    if (next->first.mMesh != place.mMesh)
                    {
                        // NV 0x6BA3D0: where the line meets the edge it came in by, or that edge's nearer end
                        const Mesh& other = *mWorld.mMeshes(next->first.mMesh);
                        const Triangle& in = other.mTriangles[next->first.mTriangle];
                        const Point& e0 = other.mVertices[in.mVertices[next->second]];
                        const Point& e1 = other.mVertices[in.mVertices[(next->second + 1) % 3]];
                        const float cc = cross(e1, x, b);
                        const float c1c = cross(e0, x, b);
                        const float c14 = cross(x, e0, e1);
                        const float c10 = c14 + c1c - cc;
                        const float t = c14 / (c14 - c10);
                        const Point hit{ x[0] + (b[0] - x[0]) * t, x[1] + (b[1] - x[1]) * t, 0.f };
                        if (cc * c1c <= 0.f)
                            x = hit;
                        else
                        {
                            const Point d0 = minus(hit, e0);
                            const Point d1 = minus(hit, e1);
                            x = length2Across(d1) <= length2Across(d0) ? e1 : e0; // 0x6BA502
                        }
                    }
                    place = next->first;
                    skip = next->second;
                    a = x;
                }
                return false;
            }

            // NV 0x6CD1F0: the straight way, and the two lines beside it a radius to each side
            bool straightWay(const Spot& from, const Spot& to) const
            {
                if (!straight(from, to, true))
                    return false;
                if (from.mPoint[2] < water(from) + waterMargin || to.mPoint[2] < water(to) + waterMargin)
                    return true; // 0x6CD298 to 0x6CD331
                const Point d = minus(to.mPoint, from.mPoint);
                Point side{ d[1], -d[0], 0.f }; // d x (0, 0, 1), 0x53D1A0
                const float l = length(side);
                side = l > 1e-6f ? times(side, mRequest.mRadius / l) : Point{};
                return straight(near(minus(from.mPoint, side), from), near(minus(to.mPoint, side), to), false)
                    && straight(near(plus(from.mPoint, side), from), near(plus(to.mPoint, side), to), false);
            }

            // The end of a stretch is near enough its last route point (NV 0x6CCA87 to 0x6CCAF5)
            bool closeToEnd(std::size_t last, const Point& at) const
            {
                const Point v = minus(mRoute[last].mSpot.mPoint, at);
                const float r = mRoute[last].mDoor ? doorEnd : mRequest.mGoalRadius;
                return length2Across(v) < r * r && std::abs(v[2]) < bandAbove;
            }

            // NV 0x6CC5E0: 0 the way is found, 1 found short of the goal, 2 none
            int passes(const Spot& start, const Spot& goal, std::size_t last)
            {
                // 0x6CC60E: bUseStraightLineCheckFirst:Pathfinding (0x11D73D8) is on and +0xA2 is never set
                if (mRequest.mAvoidObjects.empty() && !mRequest.mHasHeading && straightWay(start, goal))
                {
                    append(start);
                    return 0;
                }
                const int count = std::clamp(mRequest.mPasses, 1, 10);
                // 0x6CC6E2: the meshes of the whole route that are loaded and usable (0x6DD610)
                std::vector<std::uint32_t> filter;
                for (const RoutePoint& p : mRoute)
                {
                    const NavMeshRoute::Info* info = p.mSpot.mMesh != 0 ? mWorld.mInfos(p.mSpot.mMesh) : nullptr;
                    if (info != nullptr && (info->mFlags & NavMeshRoute::InfoFlag_Unusable) == 0
                        && mWorld.mMeshes(p.mSpot.mMesh) != nullptr)
                        filter.push_back(p.mSpot.mMesh);
                }
                Request request = mRequest;
                request.mAvoid.clear(); // list A, empty at first (0x6CF9A0)
                bool partial = false;
                for (int pass = 0; pass < count; ++pass)
                {
                    if (pass == 1)
                        for (const std::uint32_t mesh : mWorld.mLoaded()) // 0x6CC79B to 0x6CC883
                            if (std::find(filter.begin(), filter.end(), mesh) == filter.end())
                                filter.push_back(mesh);
                    Spot s = start;
                    Spot g = goal;
                    std::vector<Step> corridor;
                    std::optional<std::vector<Step>> found;
                    if (hasTriangle(mWorld, s) && hasTriangle(mWorld, g)) // 0x6CD6C2, 0x6CD72E
                        found = NavMeshSearch::search(mWorld.mMeshes, { s.mMesh, *s.mTriangle },
                            { g.mMesh, *g.mTriangle }, request, mWorld.mDoors, &filter, &corridor);
                    if (found)
                        corridor = std::move(*found);
                    else
                    {
                        // 0x6CC90D to 0x6CCC9D
                        if (corridor.empty())
                        {
                            if (pass >= 1)
                                return 2;
                            continue;
                        }
                        const Place end = corridor.back().mPlace;
                        const Spot reached = onTriangle(
                            mWorld, mWorld.mMeshes(end.mMesh)->centroid(end.mTriangle), end.mMesh, end.mTriangle);
                        if (mLast != last)
                            partial = mPath.mPartial = true;
                        else if (closeToEnd(last, reached.mPoint))
                        {
                            if (!mRequest.mPartial)
                                return 2; // 0x6CCB0F
                            if (!mRoute[last].mDoor)
                                partial = mPath.mPartial = true;
                        }
                        else if (pass == 0)
                            continue;
                        else
                            partial = mPath.mPartial = true;
                        if (!mRequest.mPartial)
                            return 2; // 0x6CCC32
                        g = reached;
                    }
                    // 0x6CCCBE: the smoother (0x6CD8A0 with the default bUseOldPathSmoothing 0)
                    // shortcut: a request radius over 150 takes NavMeshPOVSearch (0x6CD8D2) in the exe, not built yet;
                    // it takes the smoother here
                    const std::size_t before = mPath.mPoints.size();
                    NavMeshSearch::Smoothed way = NavMeshSearch::smooth(mWorld.mMeshes, request, corridor, s.mPoint,
                        g.mPoint, [this](std::uint32_t mesh, const Point& p) {
                            Spot at = mWorld.mSpotOfMesh(mesh);
                            at.mPoint = p;
                            return at.mCell != 0 ? water(at) : maxFloat; // no cell: as the game (0x6AD7D0)
                        });
                    for (const WayPoint& point : way.mPoints)
                    {
                        mPath.mPoints.push_back(point);
                        mPath.mDoors.push_back(0);
                    }
                    if (way.mFound)
                        break;
                    for (const auto& edge : way.mNarrow)
                        request.mAvoid.insert(edge);
                    if (pass == count - 1 && mRequest.mKeepTriangles) // 0x6CCD08
                    {
                        partial = mPath.mPartial = true;
                        for (const Step& step : corridor)
                            append(
                                onTriangle(mWorld, mWorld.mMeshes(step.mPlace.mMesh)->centroid(step.mPlace.mTriangle),
                                    step.mPlace.mMesh, step.mPlace.mTriangle));
                        break;
                    }
                    const bool any = !mPath.mPoints.empty();
                    if (any && mLast == last && closeToEnd(last, mPath.mPoints.back().mLocation.mPoint))
                        break; // 0x6CCEEB
                    if (request.mAvoid.empty() || pass == count - 1) // 0x6CCF26
                    {
                        if (!any)
                            return 2; // 0x6CD05A
                        partial = mPath.mPartial = true;
                        if (mRequest.mPartial)
                            return 1; // 0x6CCF79
                        cut(before);
                        return 2;
                    }
                    cut(before);
                }
                return partial ? 1 : 0;
            }

            // NV 0x6CEC40: the route points as they are, a mesh's middle for one with no place of its own
            void copy(std::size_t first, std::size_t last)
            {
                for (std::size_t i = first; i <= last; ++i)
                {
                    Spot spot = mRoute[i].mSpot;
                    if (!mRoute[i].mHasPosition)
                        if (const NavMeshRoute::Info* info = mWorld.mInfos(spot.mMesh))
                            spot.mPoint = info->mCentre;
                    append(spot);
                }
                tag(last);
            }

            // NV 0x6CC355 to 0x6CC45D: the door at the end of a stretch tags its last point (0x6CC560); the hop to the
            // next route point in the same cell (0x6CC416) is never taken here, the range ending at a door or the end
            void tag(std::size_t last)
            {
                if (mRoute[last].mDoor && !mPath.mPoints.empty())
                {
                    mPath.mDoors.back() = mRoute[last].mDoorRef;
                    mPath.mPoints.back().mLocation.mPoint = mRoute[last].mSpot.mPoint; // its near marker, 0x568FA0
                }
            }

            // NV 0x6CB950: one stretch of resolved points
            bool stretch(std::size_t first, std::size_t last)
            {
                std::vector<Spot> startChain;
                std::vector<Spot> goalChain;
                std::vector<Snap> startFound;
                std::vector<Snap> goalFound;
                const int r30 = snapStart(first, startChain, startFound);
                const int r34 = snapGoal(last, goalChain, goalFound);
                if (r30 == 0 || r34 == 0)
                    return false;
                const Point& from = mRoute[first].mSpot.mPoint;
                const Point& to = mRoute[last].mSpot.mPoint;
                const auto across
                    = [](const Point& a, const Point& b) { return std::sqrt(length2Across(minus(a, b))); };
                const float ds = startFound.empty() ? 0.f : across(startFound.front().mPoint, from);
                const float dg = goalFound.empty() ? 0.f : across(goalFound.front().mPoint, to);
                const float distance = across(from, to);
                const bool shortcut
                    = distance < (ds + dg) * 2.f || ((r30 == 2 || r34 == 2) && distance < nearEnough); // 0x6CBB88
                if (shortcut && rayJoin(mRoute[first].mSpot, mRoute[last].mSpot) == 1)
                {
                    append(mRoute[first].mSpot);
                    append(mRoute[last].mSpot);
                    tag(last);
                    return true;
                }
                if (r30 == 2)
                    return false;
                if (r34 == 2)
                {
                    if (goalFound.empty())
                        return false;
                    if (!offMeshGoal(last, goalFound, goalChain))
                        mPath.mPartial = true; // 0x6CBF3B
                    const Snap& c = goalFound.front();
                    goalChain.push_back(onTriangle(mWorld, c.mPoint, c.mMesh, c.mTriangle)); // 0x6CBF6A
                }
                for (std::size_t m = 0; m + 1 < startChain.size(); ++m)
                    append(startChain[m]);
                const int rc = passes(startChain.back(), goalChain.back(), last);
                if (rc == 0)
                {
                    for (auto it = goalChain.rbegin(); it != goalChain.rend(); ++it)
                        append(*it);
                    tag(last);
                    return !mPath.mPartial;
                }
                if (rc == 1)
                {
                    mPath.mPartial = true;
                    return false;
                }
                // 0x6CC03D
                if (distance < nearEnough && mRequest.mRadius < mRequest.mLargeActorRadius)
                {
                    if (straight(mRoute[first].mSpot, mRoute[last].mSpot, false))
                        return false; // 0x6CC0C8
                    if (rayJoin(mRoute[first].mSpot, mRoute[last].mSpot) == 1)
                    {
                        mPath.mPartial = false; // 0x6CC1D5
                        append(mRoute[first].mSpot);
                        append(mRoute[last].mSpot);
                        return true;
                    }
                }
                return false;
            }

            // NV 0x6CA500 and 0x6CA850: the range in stretches, a run of resolved points whose meshes follow on from
            // each other searched, a run of the others copied
            bool stretches()
            {
                struct Segment
                {
                    std::size_t mFirst;
                    std::size_t mLast;
                    bool mSearched;
                };
                std::vector<Segment> segments;
                std::size_t i = mFirst;
                while (i <= mLast)
                {
                    std::size_t j = i;
                    while (j <= mLast && !mRoute[j].mResolved)
                        ++j;
                    if (j > mLast)
                    {
                        segments.push_back({ i, mLast, false });
                        break;
                    }
                    std::size_t k = j;
                    while (k + 1 <= mLast && mRoute[k + 1].mResolved)
                        ++k;
                    if (j > i)
                        segments.push_back({ i, j - 1, false });
                    std::size_t from = j;
                    for (std::size_t m = j; m < k; ++m)
                    {
                        const NavMeshRoute::Info* a = mWorld.mInfos(mRoute[m].mSpot.mMesh);
                        const NavMeshRoute::Info* b = mWorld.mInfos(mRoute[m + 1].mSpot.mMesh);
                        if (a == nullptr || b == nullptr)
                            return false; // 0x6CA705 to 0x6CA72D
                        const auto listed = [&](const std::vector<std::uint32_t>& list) {
                            return std::find(list.begin(), list.end(), b->mId) != list.end();
                        };
                        if (a != b && !listed(a->mNeighbours) && !listed(a->mNeighboursToo))
                        {
                            segments.push_back({ from, m, true });
                            from = m + 1;
                        }
                    }
                    segments.push_back({ from, k, true });
                    i = k + 1;
                }
                for (std::size_t s = 0; s < segments.size(); ++s)
                {
                    const Segment& segment = segments[s];
                    bool ok = false;
                    if (segment.mSearched)
                    {
                        if (s > 0 && segments[s - 1].mSearched && !mRequest.mStraightWhenLost)
                        {
                            mPath.mPartial = true; // 0x6CA955
                            return false;
                        }
                        ok = stretch(segment.mFirst, segment.mLast);
                        if (!ok && !mRequest.mStraightWhenLost)
                            return false; // 0x6CA9B2
                    }
                    if (!ok)
                        copy(segment.mFirst, segment.mLast);
                }
                return true;
            }

        private:
            const World& mWorld;
            const Request& mRequest;
            const Spot& mStart;
            const Spot& mGoal;
        };
    }

    bool resolve(const World& world, Spot& spot)
    {
        const float water = world.mWater(spot.mCell, spot.mPoint);
        if (spot.mMesh != 0)
        {
            // NV 0x6DD74C to 0x6DD7D6: a place that has its mesh keeps it, with or without a triangle under it
            const NavMeshRoute::Info* info = world.mInfos(spot.mMesh);
            if (info != nullptr && (info->mFlags & NavMeshRoute::InfoFlag_Unusable) != 0)
                return false; // 0x68F320 at 0x6DD75F
            if (!spot.mTriangle)
                if (const Mesh* mesh = world.mMeshes(spot.mMesh))
                    spot.mTriangle = NavMeshSearch::triangleAt(*mesh, spot.mPoint, water); // 0x6DD7BB
            return true;
        }
        const std::vector<NavMeshSearch::Candidate> candidates = world.mCandidates(spot.mCell);
        if (candidates.empty())
            return false; // 0x6DD740
        if (candidates.size() == 1)
        {
            // the place of a cell with one mesh has that mesh from the start (0x6DD280)
            spot.mMesh = candidates.front().mMesh->mId;
            return resolve(world, spot);
        }
        // NV 0x6DD80C to 0x6DD8CD: the first mesh that is not disabled and has a triangle under the place
        for (const NavMeshSearch::Candidate& candidate : candidates)
            if (!candidate.mDisabled)
                if (const auto triangle = NavMeshSearch::triangleAt(*candidate.mMesh, spot.mPoint, water))
                {
                    spot.mMesh = candidate.mMesh->mId;
                    spot.mTriangle = triangle;
                    return true;
                }
        // NV 0x6DD90A to 0x6DD94C: the nearest mesh by 0x6DDC00, but the place keeps its point and has no triangle
        const auto nearest = NavMeshSearch::locateNearest(candidates, spot.mPoint, water);
        if (!nearest)
            return false;
        spot.mMesh = nearest->first.mMesh;
        spot.mTriangle.reset();
        return true;
    }

    std::optional<Path> findPath(const World& world, const Request& request, Spot start, Spot goal)
    {
        // NV 0x6C8E2C: the triangles of the request's places are found again (0x6DDBE0)
        start.mTriangle.reset();
        goal.mTriangle.reset();
        Pather pather(world, request, start, goal);
        using Kind = NavMeshRoute::RoutePoint::Kind;
        const bool startOn = resolve(world, start);
        const bool goalOn = resolve(world, goal);
        const NavMeshRoute::Info* a = startOn ? world.mInfos(start.mMesh) : nullptr;
        const NavMeshRoute::Info* b = goalOn ? world.mInfos(goal.mMesh) : nullptr;
        const auto place = [&](const Spot& spot) {
            Pather::RoutePoint p;
            p.mSpot = spot;
            p.mResolved = spot.mMesh != 0;
            p.mHasPosition = true;
            return p;
        };
        // NV 0x6C8F50
        if (a != nullptr && b != nullptr)
        {
            if (a == b)
                pather.mRoute = { place(start), place(goal) };
            else
            {
                const NavMeshRoute::Found found = NavMeshRoute::search(
                    world.mInfos, world.mRouteDoors, *a, start.mPoint, *b, goal.mPoint, !request.mNoDoors, false);
                const bool sameSpace
                    = start.mWorld != 0 ? start.mWorld == goal.mWorld : start.mCell != 0 && start.mCell == goal.mCell;
                if (!found.mReached && !sameSpace)
                    return std::nullopt; // shortcut: TeleportDoorSearch (0x6C9B20) is the load door step, not built yet
                const std::vector<NavMeshRoute::RoutePoint> route
                    = NavMeshRoute::route(found, world.mRouteDoors, *a, start.mPoint, *b, goal.mPoint);
                for (const NavMeshRoute::RoutePoint& point : route)
                {
                    Pather::RoutePoint p;
                    switch (point.mKind)
                    {
                        case Kind::Location:
                            p = place(pather.mRoute.empty() ? start : goal);
                            break;
                        case Kind::Mesh:
                            p.mSpot = world.mSpotOfMesh(point.mInfo->mId);
                            p.mSpot.mPoint = point.mPosition;
                            p.mSpot.mMesh = point.mInfo->mId;
                            p.mResolved = true;
                            break;
                        case Kind::Door:
                        case Kind::Landing:
                            p.mSpot = world.mSpotOfMesh(point.mInfo->mId);
                            p.mSpot.mPoint = point.mPosition;
                            p.mSpot.mMesh = point.mInfo->mId;
                            p.mResolved = true;
                            p.mHasPosition = true;
                            p.mDoor = point.mKind == Kind::Door;
                            p.mDoorRef = point.mDoor;
                            break;
                    }
                    pather.mRoute.push_back(p);
                }
            }
        }
        else
            return std::nullopt; // shortcut: an end on no mesh goes to TeleportDoorSearch (0x6C909A), the load door
                                 // step

        // NV 0x6CA0E0 with 0x6C9FC0: the run of route points in loaded cells from the start, up to the first door
        if (!pather.loaded(pather.mRoute.front()))
            return Path{}; // 0x6CA149
        std::size_t last = 1;
        while (last < pather.mRoute.size() && pather.loaded(pather.mRoute[last]))
        {
            if (pather.mRoute[last].mDoor)
            {
                ++last;
                break;
            }
            ++last;
        }
        pather.mFirst = 0;
        pather.mLast = last - 1;
        const bool ok = pather.stretches();
        // the actor mover keeps a path that stops short when the request lets it (0x9DD84C to 0x9DD884)
        if (ok || (pather.mPath.mPartial && request.mPartial && !pather.mPath.mPoints.empty()))
            return pather.mPath;
        return std::nullopt;
    }
}
