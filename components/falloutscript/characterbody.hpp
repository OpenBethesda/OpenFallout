#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_CHARACTERBODY_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_CHARACTERBODY_H

#include <vector>

namespace FalloutScript::CharacterBody
{
    struct Point
    {
        double mX = 0;
        double mY = 0;
        double mZ = 0;
    };

    /// The BSBound of a skeleton: its middle and half extents (units, the frame of the character: y ahead, z up)
    struct Bound
    {
        Point mCentre;
        Point mHalfExtents;
    };

    /// The size the executables give a character that has no 3D (FalloutNV.exe 0xC72683, Fallout3.exe 0x8EEC7F):
    /// half extents 23, 17.5 and 64, not scaled
    Bound noModelBound();

    struct Capsule
    {
        Point mA;
        Point mB;
        double mRadius = 0;
    };

    enum class Kind
    {
        Upright, // the 18-point hull
        Lying, // one capsule along the length
        LyingWithLegs, // a capsule for the body and two short ones (balls) for the legs
    };

    /// The body of a character controller, as FalloutNV.exe builds it (0xC72410; Fallout3.exe 0x8EE9E0). Points are in
    /// the frame of the character with the feet at 0 (the executables put the bottom of the shape on the reference,
    /// SetPosition 0xC6E390), and the shape turns with the character.
    struct Body
    {
        Kind mKind = Kind::Upright;
        /// Upright: tip, ring of 8, ring of 8, tip (0xC70DE0; Fallout3.exe 0x8EC700)
        std::vector<Point> mHull;
        /// Lying: the capsules (one, or the body then the two legs); the shape is their union (a list shape)
        std::vector<Capsule> mCapsules;
        /// From the feet to the top
        double mHeight = 0;
        /// Half the width across (x) and along (y), from the middle of the shape
        double mHalfWidth = 0;
        double mHalfLength = 0;
        /// How high an edge the body walks up: the height of the lowest point where its side is upright (the low ring
        /// of the hull, the middle of the lowest ball of a capsule). Below it the shape slopes, and the character
        /// proxy slides up whatever it touches there: FalloutNV.exe leaves the proxy's slope limit at its default of
        /// 90 degrees (hkpCharacterProxyCinfo +0x64 = pi/2 at 0xC6CE69, not set again at 0xC6DA62), and its 47
        /// degrees only decide whether a surface holds the character up (the support test 0xC739B8 against the
        /// cosine at controller +0x430, set at 0xC742B1). The exe has no step setting.
        double mStepHeight = 0;
    };

    /// The body from the bound of the skeleton and the scale of the character (the scale of the reference times the
    /// height of the race, or the base scale of a creature; FalloutNV.exe 0x567400, kept in the controller info at
    /// +0xAC by 0x931033):
    /// - half extents h and middle c are those of the bound times the scale;
    /// - the body is upright when h.z is at least h.x and h.y, else it lies (0xC726D1);
    /// - upright: radius r0 = (h.x + h.y) / 2, an 18-point hull from the feet to 2 h.z: a tip at the feet, a ring of
    ///   radius r0 at the height of the foot plus 0.1 Havok units (0.6999 units), a ring at 2 h.z - r0 / 2, a tip at
    ///   2 h.z; the foot is max(31 times the scale, 0.75 r0) (0xC72D30, 0xC73F2B, 31 at 0xC6DADE);
    /// - lying along y (h.x <= h.y): r = min(h.x, h.z), m = max(h.x, h.z), the capsule from (c.x, -(c.y + h.y - r))
    ///   to (c.x, c.y + h.y - r); along x the same with x and y swapped round (the legs stay off y);
    /// - when 0.75 m <= r one capsule of radius r with its line r over the feet (0xC72D72); else (0xC72834) with
    ///   d = m - r the body's capsule at 2 d + r and a leg 8 units in from each end, a capsule from d - 1 to d + 1 of
    ///   radius d - 1;
    /// - the whole shape is moved by c.x and c.y (the offset of the controller, 0xC72C6F).
    Body body(const Bound& bound, double scale);

    /// The radius of a character for the distance of a blow (FalloutNV.exe 0x8852E0, the radius 0x8BE420 of
    /// 0x9A6770): the middle of the bound plus its half extent, ahead (y), times the scale
    double meleeRadius(const Bound& bound, double scale);

    /// How far apart two characters' middles may be for one to strike the other (FalloutNV.exe 0x9A6830; Fallout3.exe
    /// 0x7B8E70): the two radii and half the reach, and never less than 0. The melee procedure walks its fighter to
    /// within this of the target (the radius of its path goal, 0x9CE7D1). The reach is that of the weapon times the
    /// scale of the fighter (0x9A69C0; Fallout3.exe 0x7B7810).
    double meleeDistance(double radiusA, double radiusB, double reach);
}

#endif
