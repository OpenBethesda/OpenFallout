#include "navmeshdoors.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FalloutScript::NavMeshDoors
{
    namespace
    {
        Point minus(const Point& a, const Point& b)
        {
            return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
        }

        Point plus(const Point& a, const Point& b)
        {
            return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
        }

        float dot(const Point& a, const Point& b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }

        // NV 0x53D1A0 and 0x4B3800
        Point cross(const Point& a, const Point& b)
        {
            return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        }

        // `p` in the frame: its parts along each column (NV 0x4B3AE0)
        Point into(const Axes& frame, const Point& p)
        {
            return { dot(frame[0], p), dot(frame[1], p), dot(frame[2], p) };
        }

        // A point given in the frame, on the map
        Point outOf(const Axes& frame, const Point& p)
        {
            Point out{};
            for (int i = 0; i < 3; ++i)
                out[i] = frame[0][i] * p[0] + frame[1][i] * p[1] + frame[2][i] * p[2];
            return out;
        }

        float cross2(float ax, float ay, float bx, float by)
        {
            return ax * by - ay * bx;
        }

        // NV 0x6BA010: the segment a-b crosses c-d, c and d on both sides of the line of a and b or on it, a and b
        // strictly on both sides of the line of c and d
        bool crosses(const Point& a, const Point& b, const float c[2], const float d[2])
        {
            const float u = cross2(a[0] - d[0], a[1] - d[1], b[0] - d[0], b[1] - d[1]);
            const float w = cross2(a[0] - c[0], a[1] - c[1], b[0] - c[0], b[1] - c[1]);
            if (!(u * w <= 0.f)) // 0x6BA08E
                return false;
            const float s = cross2(c[0] - a[0], c[1] - a[1], d[0] - a[0], d[1] - a[1]);
            const float t = s + w - u;
            return s * t < 0.f;
        }

        // NV 0x6912A0: the square of the grid a point is in, clamped to the grid on both sides
        std::uint32_t square(float offset, float size, std::uint32_t divisor)
        {
            const float index = std::floor(offset / size);
            if (!(index > 0.f))
                return 0;
            return std::min(static_cast<std::uint32_t>(index), divisor - 1);
        }
    }

    Shape shapeOf(const Axes& body, const Point& origin, const Point& localMin, const Point& localMax)
    {
        Shape shape;
        shape.mOrigin = origin; // 0x6C2750
        // 0x6C27E6 to 0x6C2A36: the axis of the body nearest up, and the one after it
        const auto up = [](const Point& v) { return std::abs(v[2]); };
        Point vertical;
        Point other;
        if (!(up(body[1]) >= up(body[0])))
        {
            if (!(up(body[2]) >= up(body[0])))
            {
                vertical = body[0];
                other = body[1];
            }
            else
            {
                vertical = body[2];
                other = body[0];
            }
        }
        else if (!(up(body[2]) >= up(body[1])))
        {
            vertical = body[1];
            other = body[2];
        }
        else
        {
            vertical = body[2];
            other = body[0];
        }
        if (!(vertical[2] >= 0.f)) // 0x6C2A4A
            other = { -other[0], -other[1], -other[2] };
        // 0x6C2AA7: the box stands upright, turned with the body about the up of the map (0x11A9484)
        const Point z{ 0.f, 0.f, 1.f };
        Point y = cross(z, other);
        const float length = std::sqrt(dot(y, y));
        y = length > 1e-6f ? Point{ y[0] / length, y[1] / length, y[2] / length } : Point{}; // 0x53D227
        Point x = cross(y, z);
        x = { -x[0], -x[1], -x[2] }; // 0x4A0BD0
        shape.mFrame = { x, y, z };

        // 0x6C2BA6 to 0x6C2EF7: the corners of the shape's box, in the frame and on the map
        std::array<Point, 8> inFrame;
        std::array<Point, 8> onMap;
        for (int i = 0; i < 8; ++i)
        {
            const Point corner{ (i & 1) ? localMax[0] : localMin[0], (i & 2) ? localMax[1] : localMin[1],
                (i & 4) ? localMax[2] : localMin[2] };
            inFrame[i] = into(shape.mFrame, outOf(body, corner));
            onMap[i] = plus(outOf(shape.mFrame, inFrame[i]), origin);
        }
        // 0x6C2F26 to 0x6C30B4: a corner within 15 of the side of a cell (4096) goes 30 on
        for (int i = 0; i < 8; ++i)
        {
            for (int axis = 0; axis < 2; ++axis)
                if (std::abs(std::fmod(onMap[i][axis], 4096.f)) < 15.f)
                    onMap[i][axis] += 30.f;
            inFrame[i] = into(shape.mFrame, minus(onMap[i], origin));
        }
        constexpr float big = std::numeric_limits<float>::max();
        shape.mMin = shape.mWorldMin = { big, big, big };
        shape.mMax = shape.mWorldMax = { -big, -big, -big };
        for (int i = 0; i < 8; ++i)
            for (int axis = 0; axis < 3; ++axis)
            {
                shape.mMin[axis] = std::min(shape.mMin[axis], inFrame[i][axis]);
                shape.mMax[axis] = std::max(shape.mMax[axis], inFrame[i][axis]);
                shape.mWorldMin[axis] = std::min(shape.mWorldMin[axis], onMap[i][axis]);
                shape.mWorldMax[axis] = std::max(shape.mWorldMax[axis], onMap[i][axis]);
            }
        return shape;
    }

    bool overlaps(const Point& aMin, const Point& aMax, const Point& bMin, const Point& bMax)
    {
        for (int axis = 0; axis < 3; ++axis)
            if (!(aMax[axis] > bMin[axis] && aMin[axis] < bMax[axis]))
                return false;
        return true;
    }

    std::vector<std::uint16_t> trianglesUnder(const Mesh& mesh, const Shape& shape, const Point& offset)
    {
        constexpr float aboveTop = 128.f; // 0x102E430, 0x101E704
        std::vector<std::uint16_t> out;
        const NavMeshSearch::Grid& grid = mesh.mGrid;
        if (grid.mDivisor == 0 || grid.mCells.size() != std::size_t(grid.mDivisor) * grid.mDivisor)
            return out;
        // 0x698807 to 0x698841: the squares under the box on the map (the offset not counted)
        const std::uint32_t x0 = square(shape.mWorldMin[0] - grid.mMin[0], grid.mCellX, grid.mDivisor);
        const std::uint32_t x1 = square(shape.mWorldMax[0] - grid.mMin[0], grid.mCellX, grid.mDivisor);
        const std::uint32_t y0 = square(shape.mWorldMin[1] - grid.mMin[1], grid.mCellY, grid.mDivisor);
        const std::uint32_t y1 = square(shape.mWorldMax[1] - grid.mMin[1], grid.mCellY, grid.mDivisor);
        // 0x698858 to 0x698A05: the box in its frame, grown by the offset
        const float corners[4][2] = {
            { shape.mMin[0] - offset[0], shape.mMin[1] - offset[1] },
            { shape.mMin[0] - offset[0], shape.mMax[1] + offset[1] },
            { shape.mMax[0] + offset[0], shape.mMax[1] + offset[1] },
            { shape.mMax[0] + offset[0], shape.mMin[1] - offset[1] },
        };
        const float height = shape.mWorldMax[2] - shape.mWorldMin[2]; // 0x698A08
        const Point boxMin = minus(shape.mWorldMin, offset);
        const Point boxMax = plus(shape.mWorldMax, offset);
        for (std::uint32_t y = y0; y <= y1; ++y)
            for (std::uint32_t x = x0; x <= x1; ++x)
                for (const std::uint16_t t : grid.mCells[y * grid.mDivisor + x])
                {
                    if (t >= mesh.mTriangles.size() || std::find(out.begin(), out.end(), t) != out.end())
                        continue;
                    const NavMeshSearch::Triangle& triangle = mesh.mTriangles[t];
                    if ((triangle.mFlags & NavMeshSearch::Flag_Covered) != 0) // 0x698AE7
                        continue;
                    constexpr float big = std::numeric_limits<float>::max();
                    Point low{ big, big, big };
                    Point high{ -big, -big, -big };
                    for (const std::uint16_t v : triangle.mVertices)
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            low[axis] = std::min(low[axis], mesh.mVertices[v][axis]);
                            high[axis] = std::max(high[axis], mesh.mVertices[v][axis]);
                        }
                    high[2] += aboveTop;
                    if (!overlaps(boxMin, boxMax, low, high)) // 0x698C80
                        continue;
                    // 0x6C3570: its middle is in the box, from the height of the box below the body to 128 above
                    const Point middle = into(shape.mFrame, minus(mesh.centroid(t), shape.mOrigin));
                    bool keep = middle[2] >= -height && middle[2] <= aboveTop && middle[0] >= shape.mMin[0]
                        && middle[0] <= shape.mMax[0] && middle[1] >= shape.mMin[1] && middle[1] <= shape.mMax[1];
                    // 0x697730: the body's origin is over the triangle
                    if (!keep)
                        if (const std::optional<float> dz = NavMeshSearch::heightAbove(mesh, t, shape.mOrigin))
                            keep = *dz <= height && *dz >= -aboveTop;
                    // 0x698D2F to 0x698EEF: a side of the triangle crosses a side of the box
                    for (int e = 0; e < 3 && !keep; ++e)
                    {
                        const Point a = into(shape.mFrame, minus(mesh.mVertices[triangle.mVertices[e]], shape.mOrigin));
                        const Point b
                            = into(shape.mFrame, minus(mesh.mVertices[triangle.mVertices[(e + 1) % 3]], shape.mOrigin));
                        for (int k = 0; k < 4 && !keep; ++k)
                            keep = crosses(a, b, corners[k], corners[(k + 1) % 4]);
                    }
                    if (keep)
                        out.push_back(t);
                }
        return out;
    }

    void close(Mesh& mesh, std::uint32_t door, const Shape& shape)
    {
        // 0x699839 to 0x699918: the thinnest side of the box, 0.49 of it off each side
        constexpr float share = 0.49f; // 0x106B168
        const Point d = minus(shape.mMax, shape.mMin);
        Point offset{};
        const auto size = [&](int axis) { return std::abs(d[axis]); };
        if (!(size(1) <= size(0)))
        {
            if (size(2) <= size(0))
                offset[2] = -size(2) * share;
            else
                offset[0] = -size(0) * share;
        }
        else if (size(2) <= size(1))
            offset[2] = -size(2) * share;
        else
            offset[1] = -size(1) * share;
        for (const std::uint16_t t : trianglesUnder(mesh, shape, offset))
        {
            // 0x699990 to 0x6999F7: unless the first record of the triangle is this door's
            const auto found = std::find_if(
                mesh.mDoors.begin(), mesh.mDoors.end(), [&](const auto& record) { return record.first == t; });
            if (found != mesh.mDoors.end() && found->second == door)
                continue;
            mesh.mTriangles[t].mFlags |= NavMeshSearch::Flag_Door;
            mesh.mDoors.emplace_back(t, door);
        }
    }

    void open(Mesh& mesh, std::uint32_t door)
    {
        // 0x699A30 to 0x699AD8
        for (auto it = mesh.mDoors.begin(); it != mesh.mDoors.end();)
        {
            if (it->second != door)
            {
                ++it;
                continue;
            }
            if (it->first < mesh.mTriangles.size())
                mesh.mTriangles[it->first].mFlags &= ~static_cast<std::uint32_t>(NavMeshSearch::Flag_Door);
            it = mesh.mDoors.erase(it);
        }
    }
}
