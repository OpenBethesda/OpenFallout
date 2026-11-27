#include "navmeshsmoother.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>

namespace FalloutScript::NavMeshSearch
{
    namespace
    {
        constexpr float pi = 3.14159265f;
        constexpr float twoPi = 6.2831855f; // 0x101FF48
        constexpr float maxFloat = std::numeric_limits<float>::max(); // 0x1016970
        // fPOVSmootherAvoidNodeCost:Pathfinding (NV 0x11D7084)
        constexpr float avoidNodeCost = 7.f;

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
            return std::sqrt(dot(a, a));
        }
        float length2Across(const Point& a)
        {
            return a[0] * a[0] + a[1] * a[1];
        }
        // NV 0x457910 / 0x4A0C10: a vector too short to have a way is the zero vector
        Point unit(const Point& a)
        {
            const float l = length(a);
            if (l <= 1e-6f) // 0x4A0C10
                return {};
            return times(a, 1.f / l);
        }
        // (p - o) x (q - o) on the map
        float cross(const Point& o, const Point& p, const Point& q)
        {
            return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0]);
        }
        // The angle from one direction on the map to another, -pi to pi (NV 0x698620 with 0x6A0840)
        float turn(const float a[2], const float b[2])
        {
            return std::atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]);
        }

        std::pair<const Point*, const Point*> edgeEnds(const Mesh& mesh, std::uint16_t triangle, unsigned edge)
        {
            const Triangle& t = mesh.mTriangles[triangle];
            return { &mesh.mVertices[t.mVertices[edge]], &mesh.mVertices[t.mVertices[(edge + 1) % 3]] };
        }

        // The edge of the triangle by which the line from `s` to `t` leaves it, other than `entered`, or -1 when `t` is
        // in it (NV 0x698320)
        int exitEdge(const Mesh& mesh, std::uint16_t triangle, int entered, const Point& s, const Point& t)
        {
            for (int edge = 0; edge < 3; ++edge)
            {
                if (edge == entered)
                    continue;
                const auto [a, b] = edgeEnds(mesh, triangle, edge);
                const float c1 = cross(t, *a, *b);
                const float c2 = cross(s, *a, *b);
                const float c3 = cross(*a, s, t);
                const float c4 = cross(*b, s, t);
                if (c1 * c2 > 0.f || c3 * c4 > 0.f || c1 == 0.f || c4 == 0.f)
                    continue;
                return edge;
            }
            return -1;
        }

        // Where the line from `s` to `t` crosses the edge a b (NV 0x6BA240), in 3D along the edge
        std::optional<Point> crossing(const Point& s, const Point& t, const Point& a, const Point& b)
        {
            const float ad = cross(t, a, b);
            const float ac = cross(s, a, b);
            const float ca = cross(a, s, t);
            const float cb = cross(b, s, t);
            if (ad * ac > 0.f || ca * cb > 0.f || ad == 0.f || cb == 0.f)
                return std::nullopt;
            return plus(a, times(minus(b, a), ca / (ca - cb)));
        }

        // The point of the line a b that the line p q crosses, or the end of a b nearer it when p q does not cross a b
        // between them (NV 0x6BA550)
        Point lineCrossing(const Point& a, const Point& b, const Point& p, const Point& q)
        {
            if (a == b)
                return a;
            const float c1 = (p[0] - b[0]) * (q[1] - b[1]) - (p[1] - b[1]) * (q[0] - b[0]);
            const float c2 = (p[0] - a[0]) * (q[1] - a[1]) - (p[1] - a[1]) * (q[0] - a[0]);
            const Point x = plus(a, times(minus(b, a), c2 / (c2 - c1)));
            if (c1 * c2 <= 0.f)
                return x;
            return dot(minus(x, b), minus(x, b)) <= dot(minus(x, a), minus(x, a)) ? b : a;
        }

        // Where the edge a b and the line p q cross, on the edge in 3D, when they cross strictly (NV 0x6BA100)
        std::optional<Point> edgeCrossing(const Point& a, const Point& b, const Point& p, const Point& q)
        {
            const float sq = cross(q, a, b);
            const float sp = cross(p, a, b);
            if (sq * sp > 0.f) // 0x6BA181
                return std::nullopt;
            const float sa = cross(a, p, q);
            const float sb = sa + sp - sq;
            if (!(sa * sb < 0.f)) // 0x6BA1D9
                return std::nullopt;
            return plus(a, times(minus(b, a), sa / (sa - sb)));
        }

        // The unit normal of a triangle, nothing when it has no area (NV 0x6971E0 with 0x53D1A0)
        Point normalOf(const Mesh& mesh, std::uint16_t triangle)
        {
            const Triangle& t = mesh.mTriangles[triangle];
            const Point& v0 = mesh.mVertices[t.mVertices[0]];
            const Point e1 = minus(mesh.mVertices[t.mVertices[1]], v0);
            const Point e2 = minus(mesh.mVertices[t.mVertices[2]], v0);
            const Point n{ e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0] };
            const float l = length(n);
            if (l <= 1e-6f) // 0x1017CF8
                return {};
            return times(n, 1.f / l);
        }

        // An arc of a circle: from one unit normal to another, anticlockwise (0) or clockwise (1) (NV 0x6A05E0)
        struct Arc
        {
            float mFrom[2]{};
            float mTo[2]{};
            int mSide = 0;
        };

        // `v` in the frame of the unit vector `x`: (cos, sin) of the angle from x to v (NV 0x698620)
        std::array<float, 2> inFrameOf(const float x[2], const float v[2])
        {
            return { v[0] * x[0] + v[1] * x[1], v[1] * x[0] - v[0] * x[1] };
        }

        // The quarter a vector is in, anticlockwise from +x; 0 counts as positive (NV 0x698680)
        int quarter(const std::array<float, 2>& v)
        {
            int q = 0;
            if (v[1] < 0.f)
                q ^= 3;
            if (v[0] < 0.f)
                q ^= 1;
            return q;
        }

        // `q` is not past the end of the arc (NV 0x6A0660, 0x6A06F0): q and the end in the frame of the start, by
        // their quarters, and in the same quarter by their sines
        bool within(const Arc& arc, const float q[2])
        {
            const float* x = arc.mSide == 0 ? arc.mFrom : arc.mTo;
            const float* y = arc.mSide == 0 ? arc.mTo : arc.mFrom;
            const std::array<float, 2> end = inFrameOf(x, y);
            const std::array<float, 2> at = inFrameOf(x, q);
            const int a = quarter(at);
            const int b = quarter(end);
            if (a != b)
                return a < b;
            if (a == 0 || a == 3)
                return end[1] >= at[1]; // 0x6A0747
            return end[1] <= at[1]; // 0x6A0773
        }

        // NV 0x6A07A0
        float arcLength(const Arc& arc, float radius)
        {
            const float th = turn(arc.mFrom, arc.mTo);
            if (arc.mSide == 0)
                return (th < 0.f ? th + twoPi : th) * radius; // 0x6A07DB
            return (th < 0.f ? -th : twoPi - th) * radius; // 0x6A0806
        }

        Point onCircle(const Point& centre, float radius, const float n[2])
        {
            return { centre[0] + radius * n[0], centre[1] + radius * n[1], centre[2] };
        }

        // Walks around the circle from where `from` is to the end of the arc, a step of 15 degrees after another, at
        // most 24 (NV 0x6B1E50)
        std::optional<Location> walkArc(const Meshes& meshes, const Request& request, const Location& from,
            const Point& centre, float radius, const Arc& arc)
        {
            const float step = arc.mSide == 0 ? 0.2617994f : -0.2617994f; // 0x1064F64, 0x106B3A8
            const float c = std::cos(step);
            const float s = std::sin(step);
            float q[2] = { arc.mFrom[0] * c - arc.mFrom[1] * s, arc.mFrom[0] * s + arc.mFrom[1] * c };
            Location at = from;
            int steps = 0;
            while (within(arc, q) && steps < 24)
            {
                const Walk w = walk(meshes, request, at, onCircle(centre, radius, q));
                if (!w.mArrived)
                    return std::nullopt;
                at = w.mEnd;
                const float x = q[0] * c - q[1] * s;
                q[1] = q[0] * s + q[1] * c;
                q[0] = x;
                ++steps;
            }
            if (steps == 24)
                return std::nullopt;
            const Walk w = walk(meshes, request, at, onCircle(centre, radius, arc.mTo));
            if (!w.mArrived)
                return std::nullopt;
            return w.mEnd;
        }

        // The arc on two circles 0.9 of the actor's radius inside and outside it, so that all of the actor fits (NV
        // 0x6B23D0)
        bool walkWideArc(const Meshes& meshes, const Request& request, const Location& from, const Point& centre,
            float radius, const Arc& arc)
        {
            const float r9 = request.mRadius * 0.9f; // 0x106B9E8
            for (const float r :
                { arc.mSide == 0 ? radius - r9 : radius + r9, arc.mSide == 0 ? radius + r9 : radius - r9 })
            {
                const Walk w = walk(meshes, request, from, onCircle(centre, r, arc.mFrom));
                if (!w.mArrived || !walkArc(meshes, request, w.mEnd, centre, r, arc))
                    return false;
            }
            return true;
        }

        // A straight leg, and in the full mode the lines beside it either side of the actor (NV 0x6B25F0)
        Walk walkLeg(const Meshes& meshes, const Request& request, bool simple, const Location& from, const Point& to,
            bool fromStart, bool toEnd)
        {
            Walk result = walk(meshes, request, from, to);
            if (!result.mArrived)
                return result;
            if (simple)
            {
                result.mEnd.mPoint[2] = to[2];
                return result;
            }
            const Point d = minus(to, from.mPoint);
            const Point u = unit(d);
            const float s = 0.9f * std::min(length(d), request.mRadius); // 0x106B9E8
            const Point w{ -u[1], u[0], 0.f }; // up (0x11A9484) x u
            const float side = 0.9f * request.mRadius; // 0x10177E8
            const Point wa = fromStart ? plus(times(w, 0.5f * s), times(u, 0.5f * s)) : times(w, side);
            const Point wc = fromStart ? plus(times(w, -0.5f * s), times(u, 0.5f * s)) : times(w, -side);
            const Point wb = toEnd ? minus(times(w, 0.5f * s), times(u, 0.5f * s)) : times(w, side);
            const Point wd = toEnd ? minus(times(w, -0.5f * s), times(u, 0.5f * s)) : times(w, -side);
            for (const auto& [first, second] : { std::pair{ wa, wb }, std::pair{ wc, wd } })
            {
                const Walk a = walk(meshes, request, from, plus(from.mPoint, first));
                if (!a.mArrived)
                    return a;
                const Walk b = walk(meshes, request, a.mEnd, plus(to, second));
                if (!b.mArrived)
                    return b;
            }
            return result;
        }

        // A circle against a line: where the line from a to b goes in and out of it, as shares of the way (NV 0x6BAC20,
        // on the map, the circle made the unit circle)
        struct Cut
        {
            int mCode = 0; // 0 and 3 none, 1 starts inside and leaves at mT1, 2 goes in at mT1 and ends inside, 4 in
                           // and out, 5 all inside
            float mT1 = 0.f;
            float mT2 = 0.f;
        };
        Cut cut(const Point& centre, float radius, const Point& a, const Point& b)
        {
            Cut result;
            if (!(radius > 0.f))
                return result;
            constexpr float eps = 0.001f; // 0x1017D00
            const float ax = (a[0] - centre[0]) / radius;
            const float ay = (a[1] - centre[1]) / radius;
            const float dx = (b[0] - centre[0]) / radius - ax;
            const float dy = (b[1] - centre[1]) / radius - ay;
            const bool xMajor = (dy == 0.f && dx != 0.f) ? true
                : (dy != 0.f && dx == 0.f)               ? false
                                                         : std::abs(dy) < std::abs(dx); // 0x6BAD37
            // the line as y = m x + c (or x = m y + c), its points on the unit circle
            const float m = xMajor ? dy / dx : dx / dy;
            const float c = xMajor ? ay - m * ax : ax - m * ay;
            const float q = m * m + 1.f;
            const float disc = q - c * c;
            if (disc < -eps) // 0x6BADA7
                return result;
            const float k = 1.f / q;
            const auto t = [&](float major) { return xMajor ? (major - ax) / dx : (major - ay) / dy; };
            const auto inside = [&](float v) { return v >= -eps && v <= 1.f + eps; };
            if (disc < eps)
            {
                result.mCode = 3;
                result.mT1 = t(-c * m * k);
                return result;
            }
            const float s = std::sqrt(disc);
            float t1 = t((-c * m + s) * k);
            float t2 = t((-c * m - s) * k);
            if ((xMajor ? dx : dy) > 0.f)
                std::swap(t1, t2);
            const bool in1 = inside(t1);
            const bool in2 = inside(t2);
            if (in1 && in2)
                result = { 4, t1, t2 };
            else if (in1)
                result = { 2, t1, 0.f };
            else if (in2)
                result = { 1, t2, 0.f };
            else
                result.mCode = ax * ax + ay * ay < 1.f ? 5 : 0;
            return result;
        }

        // NV 0x6B9B10: the distance from p to the stretch a b
        float distanceToStretch(const Point& p, const Point& a, const Point& b)
        {
            const Point v = minus(b, a);
            const float d = dot(minus(p, a), v);
            if (d <= 0.f)
                return length(minus(p, a));
            const float l2 = dot(v, v);
            if (d >= l2)
                return length(minus(p, b));
            return length(minus(p, plus(a, times(v, d / l2))));
        }

        // A point of visibility (NV 0x6AE120, 0x4C bytes)
        struct Pov
        {
            int mType = -1; // 0 left corner, 1 right corner, 2 start, 3 end, 4 circle, 5 small circle
            Point mPoint{};
            float mRadius = 0.f;
            Point mA{};
            Point mB{};
            float mCost = 1.f;
            unsigned mIndex = 0xffffffff; // +0x3C
            unsigned mPartner = 0xffffffff; // +0x40
        };

        struct Node
        {
            float mG = 0.f;
            float mH = 0.f;
            std::size_t mPov = 0;
            int mSide = 0;
            float mA[2]{}; // where it leaves the circle before it
            float mB[2]{}; // where it comes onto its own circle
            Location mDeparture;
            Location mArrival;
            int mParent = -1;
            std::uint64_t mVersion = 0;
        };

        class Smoother
        {
        public:
            Smoother(const Meshes& meshes, const Request& request, const std::vector<Step>& corridor, Point start,
                Point end, const LandHeight& land)
                : mMeshes(meshes)
                , mRequest(request)
                , mCorridor(corridor)
                , mStart(start)
                , mEnd(end)
                , mLand(land)
            {
            }

            Smoothed run();

        private:
            const Meshes& mMeshes;
            const Request& mRequest;
            const std::vector<Step>& mCorridor;
            Point mStart;
            Point mEnd;
            const LandHeight& mLand;
            bool mSimple = false;
            std::vector<Pov> mPovs;
            std::vector<std::size_t> mLeft;
            std::vector<std::size_t> mRight;
            std::vector<std::size_t> mCircles;
            std::size_t mStartPov = 0;
            std::size_t mEndPov = 0;
            std::vector<Node> mNodes;
            std::map<std::pair<std::size_t, int>, int> mKeys;
            std::vector<int> mChildren;
            std::uint64_t mOrder = 0;
            using Entry = std::tuple<float, std::uint64_t, int, std::uint64_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> mOpen;

            const Mesh& mesh(const Place& place) const { return *mMeshes(place.mMesh); }
            void corners();
            void corner(const std::vector<Point>& chain, std::size_t i, bool left, std::vector<unsigned>& kept);
            void avoidCircles();
            void wallCircles();
            bool link(int from, std::size_t to);
            bool cost(const Pov& a, std::size_t b, const Point& departure, const Point& arrival, float& length) const;
            void open(int node);
            void fallback(std::vector<int>& path);
            void write(const std::vector<int>& path, bool failed, Smoothed& out) const;
            void slopes(const Location& from, const Point& to, Smoothed& out) const;
        };

        void Smoother::open(int node)
        {
            Node& n = mNodes[node];
            n.mVersion = ++mOrder;
            mOpen.emplace(n.mG + n.mH, mOrder, node, n.mVersion);
        }

        // NV 0x6AE370: the corners of the corridor, the ends of the edges it crosses, on its left and its right
        void Smoother::corners()
        {
            // a vertex: its mesh and number; an element of a chain: the vertex and the crossing it came from
            using Vertex = std::pair<std::uint32_t, std::uint16_t>;
            struct Element
            {
                Vertex mVertex;
                Place mPlace;
                unsigned mOther = 0; // the length of the other chain when it was added (+0x14)
                unsigned mCorner = 0xffffffff; // +0x1C
            };
            std::vector<Element> chains[2]; // left, right
            const auto add = [&](int side, const Vertex& v, const Place& place) {
                chains[side].push_back({ v, place, static_cast<unsigned>(chains[1 - side].size()) });
            };
            for (std::size_t i = 0; i + 1 < mCorridor.size(); ++i)
            {
                const Place& place = mCorridor[i].mPlace;
                const unsigned edge = mCorridor[i + 1].mEdge;
                const Mesh& m = mesh(place);
                const Triangle& t = m.mTriangles[place.mTriangle];
                const Vertex a{ place.mMesh, t.mVertices[edge] };
                const Vertex b{ place.mMesh, t.mVertices[(edge + 1) % 3] };
                if (i == 0)
                {
                    const Point& pa = m.mVertices[a.second];
                    const Point& pb = m.mVertices[b.second];
                    // (A - B) x (A - start), 0x6AE527
                    const Point u = minus(pa, pb);
                    const Point w = minus(pa, mStart);
                    if (u[0] * w[1] - u[1] * w[0] > 0.f)
                    {
                        add(0, b, place);
                        add(1, a, place);
                    }
                    else
                    {
                        add(0, a, place);
                        add(1, b, place);
                    }
                    continue;
                }
                const Vertex& lastLeft = chains[0].back().mVertex;
                const Vertex& lastRight = chains[1].back().mVertex;
                if (a == lastLeft)
                    add(1, b, place);
                else if (a == lastRight)
                    add(0, b, place);
                else if (b == lastLeft)
                    add(1, a, place);
                else if (b == lastRight)
                    add(0, a, place);
                else
                {
                    // NV 0x6AE66E: the crossing before, seen from this triangle
                    const auto back = across(mMeshes, mCorridor[i - 1].mPlace, mCorridor[i].mEdge);
                    if (!back)
                        continue;
                    const Triangle& n = mesh(back->first).mTriangles[back->first.mTriangle];
                    const Vertex c0{ place.mMesh, n.mVertices[back->second] };
                    const Vertex c1{ place.mMesh, n.mVertices[(back->second + 1) % 3] };
                    if (a == c0)
                        add(1, b, back->first);
                    else if (a == c1)
                        add(0, b, back->first);
                    else if (b == c0)
                        add(1, a, back->first);
                    else if (b == c1)
                        add(0, a, back->first);
                }
            }
            for (int side = 0; side < 2; ++side)
            {
                std::vector<Point> chain;
                for (const Element& e : chains[side])
                    chain.push_back(mMeshes(e.mVertex.first)->mVertices[e.mVertex.second]);
                std::vector<unsigned> kept(chain.size(), 0xffffffff);
                for (std::size_t k = 0; k < chain.size(); ++k)
                    corner(chain, k, side == 0, kept);
                for (std::size_t k = 0; k < chain.size(); ++k)
                    chains[side][k].mCorner = kept[k];
            }
            // NV 0x6AE890: each corner's first kept corner of the other side added at or after it
            for (int side = 0; side < 2; ++side)
            {
                const std::vector<std::size_t>& list = side == 0 ? mLeft : mRight;
                for (const Element& e : chains[side])
                {
                    if (e.mCorner == 0xffffffff)
                        continue;
                    for (std::size_t j = e.mOther; j < chains[1 - side].size(); ++j)
                        if (chains[1 - side][j].mCorner != 0xffffffff)
                        {
                            mPovs[list[e.mCorner]].mPartner = chains[1 - side][j].mCorner;
                            break;
                        }
                }
            }
        }

        // NV 0x6AEBA0: a corner is kept where the corridor turns round it (and at the ends of a chain)
        void Smoother::corner(const std::vector<Point>& chain, std::size_t i, bool left, std::vector<unsigned>& kept)
        {
            const Point& v = chain[i];
            const Point& prev = i == 0 ? mStart : chain[i - 1];
            const Point& next = i + 1 == chain.size() ? mEnd : chain[i + 1];
            Point a = minus(prev, v);
            a[2] = 0.f;
            a = unit(a);
            Point b = minus(next, v);
            b[2] = 0.f;
            b = unit(b);
            const float cz = left ? a[0] * b[1] - a[1] * b[0] : b[0] * a[1] - b[1] * a[0];
            const float d = dot(a, b);
            const bool end = i == 0 || i + 1 == chain.size();
            if (!end && !(d > 0.f ? cz < 0.05f : cz < -0.05f)) // 0x103C7C8, 0x106B9D8
                return;
            std::vector<std::size_t>& list = left ? mLeft : mRight;
            Pov pov;
            pov.mType = left ? 0 : 1;
            pov.mPoint = v;
            pov.mRadius = mRequest.mRadius * 1.2f; // 0x10290F0
            pov.mA = a;
            pov.mB = b;
            pov.mIndex = static_cast<unsigned>(list.size());
            kept[i] = pov.mIndex;
            list.push_back(mPovs.size());
            mPovs.push_back(pov);
        }

        // NV 0x6AEFC0 and 0x6AF2B0: circles round the things the actor keeps away from and round where actors got stuck
        void Smoother::avoidCircles()
        {
            const float extra = mRequest.mRadius * 1.2f; // 0x10290F0
            for (const AvoidObject& object : mRequest.mAvoidObjects)
            {
                Pov pov;
                pov.mType = 4;
                pov.mPoint = object.mA;
                pov.mRadius = object.mRadius + extra;
                mCircles.push_back(mPovs.size());
                mPovs.push_back(pov);
                if (object.mType == 2)
                {
                    pov.mType = 5;
                    pov.mCost = 10.f; // 0x1017B78
                    pov.mRadius = 0.25f * object.mRadius + extra; // 0x10290B0
                    mCircles.push_back(mPovs.size());
                    mPovs.push_back(pov);
                }
            }
            std::vector<std::uint32_t> seen;
            for (const Step& step : mCorridor)
            {
                if (std::find(seen.begin(), seen.end(), step.mPlace.mMesh) != seen.end())
                    continue;
                seen.push_back(step.mPlace.mMesh);
                for (const Obstacle& obstacle : mesh(step.mPlace).mObstacles)
                {
                    Pov pov;
                    pov.mType = 4;
                    pov.mPoint = obstacle.mPosition;
                    pov.mRadius = obstacle.mRadius + extra;
                    mCircles.push_back(mPovs.size());
                    mPovs.push_back(pov);
                }
            }
        }

        // NV 0x6AF500 with the wall query 0x6D4570: where the start or the end is nearer a wall than the actor's
        // radius, a circle either side of it along the wall
        void Smoother::wallCircles()
        {
            const float r = mRequest.mRadius;
            for (const auto& [point, place] :
                { std::pair{ mStart, mCorridor.front().mPlace }, std::pair{ mEnd, mCorridor.back().mPlace } })
            {
                std::vector<Place> flood{ place };
                std::optional<Point> nearest;
                float best = maxFloat;
                for (std::size_t i = 0; i < flood.size(); ++i)
                {
                    const Mesh& m = mesh(flood[i]);
                    for (unsigned edge = 0; edge < 3; ++edge)
                    {
                        const auto [a, b] = edgeEnds(m, flood[i].mTriangle, edge);
                        if (!(distanceToStretch(point, *a, *b) < r))
                            continue;
                        if (m.mTriangles[flood[i].mTriangle].mEdges[edge] == 0xffff)
                        {
                            // NV 0x6B9C20, 0x6D4B52: the nearest point of the nearest wall
                            const Point v = minus(*b, *a);
                            const float t
                                = std::clamp(dot(minus(point, *a), v) / std::max(dot(v, v), 1e-12f), 0.f, 1.f);
                            const Point q = plus(*a, times(v, t));
                            const float d2 = dot(minus(q, point), minus(q, point));
                            if (d2 < best)
                            {
                                best = d2;
                                nearest = q;
                            }
                            continue;
                        }
                        const auto next = across(mMeshes, flood[i], edge);
                        if (next && std::find(flood.begin(), flood.end(), next->first) == flood.end())
                            flood.push_back(next->first);
                    }
                }
                if (!nearest)
                    continue;
                const Point d = minus(point, *nearest);
                const Point n = unit(Point{ -d[1], d[0], 0.f });
                for (const float s : { 1.f, -1.f })
                {
                    Pov pov;
                    pov.mType = 4;
                    pov.mPoint = plus(point, times(n, s * r * 1.01f)); // 0x106B9E0
                    pov.mRadius = r;
                    mCircles.push_back(mPovs.size());
                    mPovs.push_back(pov);
                }
            }
        }

        // NV 0x6B2C00: the length of a straight leg, and more for the circles it goes through
        bool Smoother::cost(const Pov& a, std::size_t b, const Point& departure, const Point& arrival, float& out) const
        {
            out = length(minus(arrival, departure));
            if (length2Across(minus(arrival, departure)) < 0.001f) // 0x1016978
                return true;
            for (const std::size_t index : mCircles)
            {
                const Pov& circle = mPovs[index];
                // NV 0x6BAB00: the circle's middle is nearer the stretch than its radius
                if (!(distanceToStretch(circle.mPoint, departure, arrival) < circle.mRadius))
                    continue;
                const Cut c = cut(circle.mPoint, circle.mRadius, departure, arrival);
                if (c.mCode == 0 || c.mCode == 3)
                    continue;
                if (circle.mType == 5 && index != b)
                {
                    if (mRequest.mRefuseSmallCircles && a.mType != 2)
                        return false;
                    out *= 10.f; // 0x1020758
                }
                const Point way = minus(arrival, departure);
                Point in = departure;
                Point outPoint = arrival;
                if (c.mCode == 4)
                {
                    in = plus(departure, times(way, c.mT1));
                    outPoint = plus(departure, times(way, c.mT2));
                }
                else if (c.mCode == 1)
                    outPoint = plus(departure, times(way, c.mT1));
                else if (c.mCode == 2)
                    in = plus(departure, times(way, c.mT1));
                const float chord = length(minus(outPoint, in));
                const float d = distanceToStretch(circle.mPoint, in, outPoint);
                const float pen = std::max((circle.mRadius - d) / circle.mRadius, 0.f);
                out += (1.f + pen * avoidNodeCost) * chord;
            }
            return true;
        }

        // NV 0x6B0F40: the ways from the node to the point of visibility `to`, each a child of the node
        bool Smoother::link(int from, std::size_t to)
        {
            const Node a = mNodes[from];
            const Pov& pa = mPovs[a.mPov];
            const Pov& pb = mPovs[to];
            // copied: a new node can move the nodes
            const bool hasParent = a.mParent >= 0;
            const std::size_t parentPov = hasParent ? mNodes[a.mParent].mPov : 0;
            if (to == a.mPov || (hasParent && to == parentPov))
                return true;
            const Point d = minus(pb.mPoint, pa.mPoint);
            if (!mSimple && d[2] * d[2] > 1.44f * length2Across(d)) // 0x10290F0 squared
                return true;
            std::vector<Tangent> entries = tangents(pa.mPoint, pa.mRadius, pb.mPoint, pb.mRadius, a.mSide);
            if (!hasParent
                && dot(minus(pa.mPoint, pb.mPoint), minus(pa.mPoint, pb.mPoint)) < pb.mRadius * pb.mRadius) // 0x6B109A
            {
                // NV 0x6B10A3: the start is inside the circle
                Point v = minus(pa.mPoint, pb.mPoint);
                v[2] = 0.f;
                const float len = length(v);
                entries.assign(2, Tangent{});
                entries[1].mSide = 1;
                Point n0;
                Point n1;
                if (len < 0.5f * pb.mRadius)
                    n0 = n1 = len > 0.f ? times(v, 1.f / len) : Point{};
                else
                {
                    const Point vn = times(v, 1.f / len);
                    const Point perp{ -vn[1], vn[0], 0.f };
                    const float s = std::sqrt(pb.mRadius * pb.mRadius - len * len);
                    n0 = unit(plus(times(vn, len), times(perp, s)));
                    n1 = unit(minus(times(vn, len), times(perp, s)));
                }
                for (int i = 0; i < 2; ++i)
                {
                    const Point& n = i == 0 ? n0 : n1;
                    entries[i].mA[0] = entries[i].mB[0] = n[0];
                    entries[i].mA[1] = entries[i].mB[1] = n[1];
                }
            }
            if (entries.empty())
                return true;
            int candidates = 0;
            int failures = 0;
            for (const Tangent& entry : entries)
            {
                if ((pb.mType == 0 && entry.mSide == 1) || (pb.mType == 1 && entry.mSide == 0))
                    continue;
                const Arc arc{ { a.mB[0], a.mB[1] }, { entry.mA[0], entry.mA[1] }, a.mSide };
                if (hasParent && (mPovs[parentPov].mType == 0 || mPovs[parentPov].mType == 1))
                {
                    // NV 0x6B1316: no arc of more than 200 degrees
                    float th = turn(a.mB, entry.mA);
                    if (a.mSide == 0)
                    {
                        if (th < 0.f) // 0x6B138F
                            th += twoPi;
                        if (th > 3.4906585f) // 0x106BA00
                            continue;
                    }
                    else
                    {
                        if (th > 0.f)
                            th -= twoPi;
                        if (th < -3.4906585f) // 0x106B9F8, 0x6B1404
                            continue;
                    }
                }
                ++candidates;
                Location departure;
                if (!hasParent)
                    departure = Location{ mCorridor.front().mPlace, -1, mStart };
                else
                {
                    const auto around = walkArc(mMeshes, mRequest, a.mArrival, pa.mPoint, pa.mRadius, arc);
                    if (!around)
                    {
                        ++failures;
                        continue;
                    }
                    departure = *around;
                    if (!(std::abs(departure.mPoint[2] - pa.mPoint[2]) < 180.f)) // 0x104ED58
                    {
                        ++failures;
                        continue;
                    }
                }
                const Point target = onCircle(pb.mPoint, pb.mRadius, entry.mB);
                const Walk leg = walkLeg(mMeshes, mRequest, mSimple, departure, target, pa.mType == 2, pb.mType == 3);
                if (!leg.mArrived)
                {
                    ++failures;
                    continue;
                }
                if (!mSimple && hasParent && !walkWideArc(mMeshes, mRequest, a.mArrival, pa.mPoint, pa.mRadius, arc))
                {
                    ++failures;
                    continue;
                }
                // NV 0x6B1691 to 0x6B1755: where it arrives is near the height of the circle's middle
                const Point& p = leg.mEnd.mPoint;
                const Mesh& arrivalMesh = mesh(leg.mEnd.mPlace);
                const float z = heightOn(arrivalMesh, leg.mEnd.mPlace.mTriangle, p, maxFloat, maxFloat).value_or(p[2]);
                const float dz = z - pb.mPoint[2];
                if ((p[2] >= mLand(leg.mEnd.mPlace.mMesh, p) && dz <= -64.f) || dz >= 180.f) // 0x106B9F0, 0x104ED58
                {
                    ++failures;
                    continue;
                }
                float straight = 0.f;
                if (!mSimple && !cost(pa, to, departure.mPoint, p, straight))
                    continue;
                const float ratio = std::clamp(leg.mRatio * 0.9f, 0.f, 1.f); // 0x106B9E8
                float g = a.mG;
                if (hasParent)
                    g += arcLength(arc, pa.mRadius) * pb.mCost * (1.f - ratio);
                g += straight * pb.mCost * (1.f - ratio);
                // NV 0x6B3420: a node a point of visibility and a way round it
                const auto key = std::make_pair(to, entry.mSide);
                const auto found = mKeys.find(key);
                int child;
                if (found == mKeys.end())
                {
                    child = static_cast<int>(mNodes.size());
                    mKeys.emplace(key, child);
                    Node node;
                    node.mPov = to;
                    node.mSide = entry.mSide;
                    node.mH = 0.1f * length(minus(pb.mPoint, mEnd)); // 0x6B1DF0, 0x101FFA0
                    mNodes.push_back(node);
                }
                else
                {
                    child = found->second;
                    if (!(mNodes[child].mG > g))
                        continue;
                }
                Node& node = mNodes[child];
                node.mG = g;
                node.mDeparture = departure;
                node.mArrival = leg.mEnd;
                node.mA[0] = entry.mA[0];
                node.mA[1] = entry.mA[1];
                node.mB[0] = entry.mB[0];
                node.mB[1] = entry.mB[1];
                node.mParent = from;
                open(child);
                mChildren.push_back(child);
            }
            return !(candidates > 0 && failures >= candidates);
        }

        Smoothed Smoother::run()
        {
            Smoothed out;
            if (mCorridor.empty())
                return out;
            // NV 0x6AD7D0 to 0x6AD9B1: an end high above the land is brought down onto the mesh, and the smoother is
            // simple only when neither needed it
            bool simple = true;
            for (const bool isStart : { true, false })
            {
                Point& p = isStart ? mStart : mEnd;
                const Place& place = isStart ? mCorridor.front().mPlace : mCorridor.back().mPlace;
                if (p[2] < mLand(place.mMesh, p) + 50.f) // 0x101E2C0
                    continue;
                simple = false;
                const Mesh& m = mesh(place);
                if (const auto z = heightOn(m, place.mTriangle, p, 180.f, 64.f)) // 0x106B158, 0x101E6EC
                    p[2] = *z;
            }
            mSimple = simple;

            // NV 0x6ADCB0
            Pov start;
            start.mType = 2;
            start.mPoint = mStart;
            mStartPov = mPovs.size();
            mPovs.push_back(start);
            corners();
            Pov end;
            end.mType = 3;
            end.mPoint = mEnd;
            mEndPov = mPovs.size();
            mPovs.push_back(end);
            avoidCircles();
            wallCircles();

            // NV 0x6B32F0, 0x6B3A30
            Node first;
            first.mPov = mStartPov;
            first.mH = 0.75f * length(minus(mEnd, mStart)); // 0x101DE30
            mNodes.push_back(first);
            mKeys.emplace(std::make_pair(mStartPov, 0), 0);
            open(0);
            int best = -1;
            int found = -1;
            while (!mOpen.empty())
            {
                const auto [f, unused, index, version] = mOpen.top();
                mOpen.pop();
                if (mNodes[index].mVersion != version)
                    continue;
                if (best < 0)
                    best = index;
                if (mPovs[mNodes[index].mPov].mType == 3)
                {
                    found = index;
                    break;
                }
                // NV 0x6B0AD0
                const Pov pov = mPovs[mNodes[index].mPov];
                mChildren.clear();
                const auto lists = [&](const std::vector<std::size_t>& first, unsigned from1,
                                       const std::vector<std::size_t>& second, unsigned from2, bool capped) {
                    for (const auto& [list, from] : { std::pair{ &first, from1 }, std::pair{ &second, from2 } })
                    {
                        int failed = 0;
                        for (std::size_t i = from; i < list->size(); ++i)
                            if (!link(index, (*list)[i]) && capped && ++failed == 4)
                                break;
                    }
                };
                link(index, mEndPov);
                if (pov.mType == 0)
                    lists(mLeft, pov.mIndex + 1, mRight, pov.mPartner, true);
                else if (pov.mType == 1)
                    lists(mRight, pov.mIndex + 1, mLeft, pov.mPartner, true);
                else if (pov.mType == 2)
                    lists(mRight, 0, mLeft, 0, true);
                else
                    lists(mRight, 0, mLeft, 0, false);
                for (const std::size_t circle : mCircles)
                    link(index, circle);
                // NV 0x6B3A30: the best estimate so far, for a way that does not get there
                for (const int child : mChildren)
                    if (mNodes[child].mH < mNodes[best].mH) // 0x6B9130
                        best = child;
            }
            const int last = found >= 0 ? found : best;
            std::vector<int> path;
            for (int n = last; n >= 0; n = mNodes[n].mParent)
                path.push_back(n);
            std::reverse(path.begin(), path.end());
            out.mFound = found >= 0;
            if (!out.mFound)
                fallback(path);
            write(path, !out.mFound, out);
            // NV 0x6AD770 at its end: the doors on the triangles of the way
            for (const Step& step : mCorridor)
            {
                const Mesh& m = mesh(step.mPlace);
                if ((m.mTriangles[step.mPlace.mTriangle].mFlags & Flag_Door) == 0)
                    continue;
                for (const auto& [triangle, door] : m.mDoors)
                    if (triangle == step.mPlace.mTriangle)
                    {
                        out.mDoors.push_back(door);
                        break;
                    }
            }
            return out;
        }

        // NV 0x6B1970: a way that did not get there goes on from its last circle straight to the end, if it can
        void Smoother::fallback(std::vector<int>& path)
        {
            if (path.size() < 2)
                return;
            const Node last = mNodes[path.back()];
            const Pov& pov = mPovs[last.mPov];
            const Pov& end = mPovs[mEndPov];
            const std::vector<Tangent> entries = tangents(pov.mPoint, pov.mRadius, end.mPoint, end.mRadius, last.mSide);
            if (entries.empty())
                return;
            const Tangent& entry = entries.front();
            const Arc arc{ { last.mB[0], last.mB[1] }, { entry.mA[0], entry.mA[1] }, entry.mSide };
            const auto departure = walkArc(mMeshes, mRequest, last.mArrival, pov.mPoint, pov.mRadius, arc);
            if (!departure || !walkWideArc(mMeshes, mRequest, last.mArrival, pov.mPoint, pov.mRadius, arc))
                return;
            const Point target = onCircle(end.mPoint, end.mRadius, entry.mB);
            // NV 0x6B1A9D: an actor of a radius of 100 or more walks the full leg (0x1017A40)
            const Walk leg = mRequest.mRadius >= 100.f
                ? walkLeg(mMeshes, mRequest, false, *departure, target, false, true)
                : walk(mMeshes, mRequest, *departure, target);
            float length = 0.f;
            if (!leg.mArrived || !cost(pov, mEndPov, departure->mPoint, leg.mEnd.mPoint, length))
                return;
            Location arrival = leg.mEnd;
            if (mSimple)
                arrival.mPoint[2] = std::max(arrival.mPoint[2], mLand(arrival.mPlace.mMesh, arrival.mPoint));
            // it stops two radii short of the end
            arrival.mPoint
                = minus(arrival.mPoint, times(unit(minus(target, departure->mPoint)), 2.f * mRequest.mRadius));
            Node node;
            node.mPov = mEndPov;
            node.mSide = entry.mSide;
            node.mA[0] = entry.mA[0];
            node.mA[1] = entry.mA[1];
            node.mB[0] = entry.mB[0];
            node.mB[1] = entry.mB[1];
            node.mDeparture = *departure;
            node.mArrival = arrival;
            node.mParent = path.back();
            path.push_back(static_cast<int>(mNodes.size()));
            mNodes.push_back(node);
        }

        // NV 0x6AFCC0: the points of the way, where each straight leg starts and ends
        void Smoother::write(const std::vector<int>& path, bool failed, Smoothed& out) const
        {
            (void)failed;
            const auto perpendicular = [](const float n[2], int side) {
                return side == 0 ? Point{ -n[1], n[0], 0.f } : Point{ n[1], -n[0], 0.f }; // 0x6B0A60
            };
            const auto scaled = [](Point v, float s, float limit) {
                v = times(v, s * 3.f); // 0x1017718
                if (dot(v, v) > limit * limit)
                    v = times(unit(v), limit);
                return v;
            };
            for (std::size_t i = 1; i < path.size(); ++i)
            {
                const Node& node = mNodes[path[i]];
                const Node& before = mNodes[path[i - 1]];
                const float r = mPovs[before.mPov].mRadius;
                Location departure = node.mDeparture;
                Location arrival = node.mArrival;
                const float l = length(minus(arrival.mPoint, departure.mPoint));
                if (mSimple)
                    departure.mPoint[2]
                        = std::max(departure.mPoint[2], mLand(departure.mPlace.mMesh, departure.mPoint));
                bool emit = true;
                float th = 0.f;
                if (i > 1)
                {
                    th = turn(before.mB, node.mA);
                    const Arc window{ { 0.f, -1.f }, { 0.f, 1.f }, 0 };
                    const float q[2] = { std::cos(th), std::sin(th) };
                    if (r * th < 50.f && within(window, q)) // 0x101E2C0
                        emit = false;
                }
                if (emit)
                {
                    const float s = i > 1 ? std::abs(r * th) : r;
                    out.mPoints.push_back({ departure, scaled(perpendicular(node.mA, before.mSide), s, l) });
                }
                if (mSimple && (i + 1 == path.size() || failed))
                    arrival.mPoint[2] = std::max(arrival.mPoint[2], mLand(arrival.mPlace.mMesh, arrival.mPoint));
                slopes(departure, arrival.mPoint, out); // 0x6B028B
                const float rNext = mPovs[node.mPov].mRadius;
                float thNext = 0.f;
                if (i + 1 < path.size())
                    thNext = turn(node.mB, mNodes[path[i + 1]].mA);
                const float sNext = i + 1 < path.size() ? std::abs(rNext * thNext) : rNext;
                out.mPoints.push_back({ arrival, scaled(perpendicular(node.mB, node.mSide), sNext, l) });
            }
            if (mRequest.mHasHeading && !out.mPoints.empty())
            {
                // NV 0x6AFCC0 at its end: an end within 90 degrees of the heading asked for is turned to face it
                // (0x6B04CC falls through to the snap only when |diff| < pi / 2)
                Point& v = out.mPoints.back().mTangent;
                float diff = mRequest.mHeading - std::atan2(v[0], v[1]);
                while (diff > pi)
                    diff -= twoPi;
                while (diff < -pi)
                    diff += twoPi;
                if (std::abs(diff) < pi / 2.f) // 0x6B04CC: 0x1064F50
                {
                    const float l = std::sqrt(length2Across(v));
                    v = { std::sin(mRequest.mHeading) * l, std::cos(mRequest.mHeading) * l, 0.f };
                }
            }
        }
    }

    namespace
    {
        // NV 0x6B05F0: where the leg from `from` to `to` goes over an edge between two triangles that slope apart (the
        // dot of their normals under 0.9), a point more than 32 from both ends and from the point before, its tangent
        // the way of the leg by the request's radius; none before the first point
        void Smoother::slopes(const Location& from, const Point& to, Smoothed& out) const
        {
            const Point way = unit(minus(to, from.mPoint));
            Point ray = from.mPoint;
            Place at = from.mPlace;
            int entered = -1;
            const auto far = [](const Point& a, const Point& b) {
                return dot(minus(a, b), minus(a, b)) > 1024.f; // 0x101ECB8
            };
            for (int pass = 0; pass < 1000; ++pass) // 0x3E8
            {
                const Mesh* here = mMeshes(at.mMesh);
                if (here == nullptr)
                    return;
                const int edge = exitEdge(*here, at.mTriangle, entered, ray, to);
                if (edge < 0)
                    return;
                const auto next = across(mMeshes, at, static_cast<unsigned>(edge));
                if (!next)
                    return;
                const Mesh& there = *mMeshes(next->first.mMesh);
                if (next->first.mMesh != at.mMesh)
                {
                    // 0x6B0714 to 0x6B07C5: as the walk, on the edge a hundredth of the way to its middle
                    const auto [a, b] = edgeEnds(there, next->first.mTriangle, next->second);
                    const Point middle = times(plus(*a, *b), 0.5f);
                    ray = lineCrossing(*a, *b, from.mPoint, to);
                    ray = plus(ray, times(minus(middle, ray), 0.01f));
                }
                if (dot(normalOf(*here, at.mTriangle), normalOf(there, next->first.mTriangle)) < 0.9f) // 0x6B0806
                {
                    const auto [a, b] = edgeEnds(*here, at.mTriangle, static_cast<unsigned>(edge));
                    if (const auto x = edgeCrossing(*a, *b, from.mPoint, to); x && far(*x, from.mPoint) && far(*x, to)
                        && !out.mPoints.empty() && far(*x, out.mPoints.back().mLocation.mPoint))
                    {
                        Point point = *x;
                        // 0x6B0956 to 0x6B09A9: not below the land, by the cell of the point before
                        if (mSimple)
                            point[2] = std::max(point[2], mLand(out.mPoints.back().mLocation.mPlace.mMesh, point));
                        out.mPoints.push_back({ Location{ at, -1, point }, times(way, mRequest.mRadius) });
                    }
                }
                at = next->first;
                entered = next->second;
            }
        }
    }

    std::optional<float> heightOn(
        const Mesh& mesh, std::uint16_t triangle, const Point& p, float maxAbove, float maxBelow)
    {
        const Triangle& t = mesh.mTriangles[triangle];
        const Point& v0 = mesh.mVertices[t.mVertices[0]];
        const Point& v1 = mesh.mVertices[t.mVertices[1]];
        const Point& v2 = mesh.mVertices[t.mVertices[2]];
        const float det = (v1[0] - v0[0]) * (v2[1] - v0[1]) - (v2[0] - v0[0]) * (v1[1] - v0[1]);
        if (det == 0.f) // 0x697A27
            return std::nullopt;
        const float inv = 1.f / det;
        const Point a = minus(v1, p);
        const Point b = minus(v2, p);
        const Point c = minus(v0, p);
        const float w0 = (a[0] * b[1] - b[0] * a[1]) * inv;
        if (w0 < 0.f) // 0x697A94
            return std::nullopt;
        const float w1 = (b[0] * c[1] - c[0] * b[1]) * inv;
        if (w1 < 0.f) // 0x697ADB
            return std::nullopt;
        const float w2 = (c[0] * a[1] - a[0] * c[1]) * inv;
        if (w2 < 0.f) // 0x697B06
            return std::nullopt;
        const float z = w0 * v0[2] + w1 * v1[2] + w2 * v2[2];
        const float dz = p[2] - z;
        if (dz > maxAbove || dz < -maxBelow) // 0x697B4B, 0x697B62
            return std::nullopt;
        return z;
    }

    Walk walk(const Meshes& meshes, const Request& request, const Location& from, const Point& to)
    {
        Walk result;
        Location at = from;
        Point ray = from.mPoint;
        int visits = 0;
        int steps = 0;
        while (steps < 1000) // 0x3E8
        {
            ++steps;
            const Mesh* mesh = meshes(at.mPlace.mMesh);
            if (mesh == nullptr)
                break;
            const Triangle& triangle = mesh->mTriangles[at.mPlace.mTriangle];
            if ((triangle.mFlags & Flag_Preferred) != 0) // 0x693B70
                ++visits;
            const int edge = exitEdge(*mesh, at.mPlace.mTriangle, at.mEntered, ray, to);
            if (edge < 0)
            {
                Point end = to;
                if (const auto z = heightOn(*mesh, at.mPlace.mTriangle, to, maxFloat, maxFloat))
                    end[2] = *z;
                result.mArrived = true;
                result.mEnd = Location{ at.mPlace, at.mEntered, end };
                result.mRatio = static_cast<float>(visits) / steps;
                return result;
            }
            auto next = across(meshes, at.mPlace, edge);
            // NV 0x6B21E6 to 0x6B224B: a connection of type 1, and a triangle that large actors may not go on
            if (next && (triangle.mFlags & (1u << edge)) != 0 && mesh->mExternal[triangle.mEdges[edge]].mType == 1)
                next.reset();
            if (next && request.mRadius > request.mLargeActorRadius
                && (meshes(next->first.mMesh)->mTriangles[next->first.mTriangle].mFlags & Flag_NoLargeActors) != 0)
                next.reset();
            if (!next)
            {
                const auto [a, b] = edgeEnds(*mesh, at.mPlace.mTriangle, edge);
                // shortcut: 0x6BA240 leaves its point unset when the lines do not cross, which the exit edge rules out
                // but for the start of the line after a change of mesh; the start of the line is taken then
                result.mEnd = Location{ at.mPlace, at.mEntered, crossing(from.mPoint, to, *a, *b).value_or(ray) };
                result.mRatio = static_cast<float>(visits) / steps;
                return result;
            }
            if (next->first.mMesh != at.mPlace.mMesh)
            {
                // NV 0x6B2267 to 0x6B2303: the line starts again on the edge, a hundredth of the way to its middle
                const Mesh& other = *meshes(next->first.mMesh);
                const auto [a, b] = edgeEnds(other, next->first.mTriangle, next->second);
                const Point middle = times(plus(*a, *b), 0.5f); // 0x1016248
                ray = lineCrossing(*a, *b, from.mPoint, to);
                ray = plus(ray, times(minus(middle, ray), 0.01f)); // 0x1013EA4
            }
            at.mPlace = next->first;
            at.mEntered = next->second;
        }
        result.mEnd = Location{ at.mPlace, at.mEntered, to };
        result.mRatio = steps > 0 ? static_cast<float>(visits) / steps : 0.f;
        return result;
    }

    std::vector<Tangent> tangents(const Point& pa, float ra, const Point& pb, float rb, int side)
    {
        std::vector<Tangent> result;
        float dx = pb[0] - pa[0];
        float dy = pb[1] - pa[1];
        float a = ra;
        float b = rb;
        if (rb != 0.f)
        {
            a = ra / rb;
            b = 1.f;
            dx /= rb;
            dy /= rb;
        }
        else if (ra != 0.f)
        {
            b = rb / ra;
            a = 1.f;
            dx /= ra;
            dy /= ra;
        }
        const float l2 = dx * dx + dy * dy;
        if (l2 == 0.f)
            return result;
        const float inv = 1.f / l2;
        // NV 0x6BB63F: the tangent that keeps to the same side; 0x6BB768: the one that crosses between them
        for (const bool crossing : { false, true })
        {
            const float c = crossing ? a + b : a - b;
            if (c * c > l2)
                continue;
            const float s = std::sqrt(l2 - c * c);
            Tangent t;
            const bool first = side == 0;
            t.mSide = crossing ? 1 - side : side;
            t.mA[0] = (first ? c * dx + dy * s : c * dx - dy * s) * inv;
            t.mA[1] = (first ? c * dy - dx * s : c * dy + dx * s) * inv;
            t.mB[0] = crossing ? -t.mA[0] : t.mA[0];
            t.mB[1] = crossing ? -t.mA[1] : t.mA[1];
            result.push_back(t);
        }
        return result;
    }

    Smoothed smooth(const Meshes& meshes, const Request& request, const std::vector<Step>& corridor, Point start,
        Point end, const LandHeight& land)
    {
        Smoother smoother(meshes, request, corridor, start, end, land);
        return smoother.run();
    }
}
