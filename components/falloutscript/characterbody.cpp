#include "characterbody.hpp"

#include <algorithm>
#include <cmath>

namespace FalloutScript::CharacterBody
{
    namespace
    {
        // Havok units to game units: 1 / 0.1428767293691635 (Fallout3.exe 0xDBE7C0; FalloutNV.exe 6.9991 at 0x11B0194)
        constexpr double havokUnit = 1.0 / 0.1428767293691635;
        // The foot of the controller (cinfo +0xA8: 31 game units, FalloutNV.exe 0xC6DADE; Fallout3.exe 4.4292 Havok
        // units at 0xE336B4)
        constexpr double footHeight = 31.0;
        // The ring cosines and sines of the hull (FalloutNV.exe 0x11B0154 and 0x11B0174): 45 degree steps from +x
        constexpr double ringCos[8] = { 1, 0.70710677, 0, -0.70710677, -1, -0.70710677, 0, 0.70710677 };
        constexpr double ringSin[8] = { 0, 0.70710677, 1, 0.70710677, 0, -0.70710677, -1, -0.70710677 };

        Point shifted(Point point, double x, double y, double z)
        {
            point.mX += x;
            point.mY += y;
            point.mZ += z;
            return point;
        }
    }

    Bound noModelBound()
    {
        return Bound{ {}, { 23.0, 17.5, 64.0 } };
    }

    Body body(const Bound& bound, double scale)
    {
        const double cx = bound.mCentre.mX * scale;
        const double cy = bound.mCentre.mY * scale;
        const double hx = bound.mHalfExtents.mX * scale;
        const double hy = bound.mHalfExtents.mY * scale;
        const double hz = bound.mHalfExtents.mZ * scale;

        Body result;
        if (hz >= hx && hz >= hy)
        {
            result.mKind = Kind::Upright;
            const double r0 = (hx + hy) * 0.5;
            const double foot = std::max(footHeight * scale, 0.75 * r0);
            result.mHeight = 2 * hz;
            result.mHull.push_back({ cx, cy, 0 });
            const double low = foot + 0.1 * havokUnit;
            for (int i = 0; i < 8; ++i)
                result.mHull.push_back({ cx + r0 * ringCos[i], cy + r0 * ringSin[i], low });
            const double high = 2 * hz - r0 * 0.5;
            for (int i = 0; i < 8; ++i)
                result.mHull.push_back({ cx + r0 * ringCos[i], cy + r0 * ringSin[i], high });
            result.mHull.push_back({ cx, cy, 2 * hz });
            result.mHalfWidth = r0;
            result.mHalfLength = r0;
            result.mStepHeight = low;
            return result;
        }

        const bool alongY = hx <= hy;
        const double side = alongY ? hx : hy;
        const double r = std::min(side, hz);
        const double m = std::max(side, hz);
        Point a = alongY ? Point{ cx, cy + hy - r, 0 } : Point{ cx + hx - r, cy, 0 };
        Point b = alongY ? Point{ cx, -(cy + hy - r), 0 } : Point{ -(cx + hx - r), cy, 0 };
        // the offset of the controller moves the whole shape by the middle of the bound (0xC72C6F)
        a = shifted(a, cx, cy, 0);
        b = shifted(b, cx, cy, 0);
        if (0.75 * m <= r)
        {
            result.mKind = Kind::Lying;
            result.mCapsules.push_back({ shifted(a, 0, 0, r), shifted(b, 0, 0, r), r });
            result.mHeight = 2 * r;
            result.mStepHeight = r;
        }
        else
        {
            result.mKind = Kind::LyingWithLegs;
            const double d = m - r;
            result.mCapsules.push_back({ shifted(a, 0, 0, 2 * d + r), shifted(b, 0, 0, 2 * d + r), r });
            result.mCapsules.push_back({ shifted(a, 0, -8, d + 1), shifted(a, 0, -8, d - 1), d - 1 });
            result.mCapsules.push_back({ shifted(b, 0, 8, d + 1), shifted(b, 0, 8, d - 1), d - 1 });
            result.mHeight = 2 * m;
            // the legs touch the ground: the middle of their lower balls
            result.mStepHeight = d - 1;
        }
        double halfWidth = 0;
        double halfLength = 0;
        for (const Capsule& capsule : result.mCapsules)
            for (const Point& end : { capsule.mA, capsule.mB })
            {
                halfWidth = std::max(halfWidth, std::abs(end.mX) + capsule.mRadius);
                halfLength = std::max(halfLength, std::abs(end.mY) + capsule.mRadius);
            }
        result.mHalfWidth = halfWidth;
        result.mHalfLength = halfLength;
        return result;
    }

    double meleeRadius(const Bound& bound, double scale)
    {
        return (bound.mCentre.mY + bound.mHalfExtents.mY) * scale;
    }

    double meleeDistance(double radiusA, double radiusB, double reach)
    {
        return std::max(0.0, radiusA + radiusB + 0.5 * reach);
    }
}
