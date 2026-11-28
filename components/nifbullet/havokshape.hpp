#ifndef OPENFALLOUT_COMPONENTS_NIFBULLET_HAVOKSHAPE_HPP
#define OPENFALLOUT_COMPONENTS_NIFBULLET_HAVOKSHAPE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <osg/Matrixf>

class btCollisionShape;

namespace Nif
{
    struct bhkShape;
    struct HavokFilter;
    struct NIFFile;
}

namespace NifBullet
{
    /// The Havok data of the meshes of Oblivion, Fallout 3 and New Vegas is in Havok units, of which one is seven
    /// game units long. (Skyrim has another scale, and it is not read here.)
    constexpr float sHavokScale = 7.f;

    /// Whether what walks is stopped by a body in this layer of Fallout 3 and New Vegas. The layers that are not solid
    /// are triggers, zones, the volumes that picking and line of sight use, the boxes that are for avoiding, and the
    /// like; the collision box (layer 32) is solid, since it is the layer of barriers that are only collision, and a
    /// layer that is not known counts as solid.
    bool isSolidHavokLayer(std::uint8_t layer);

    /// Whether what walks is stopped by what has this filter: its layer is solid and it has not the flag for no
    /// collision (bit 6 of the flags of the filter).
    bool isSolidHavokFilter(const Nif::HavokFilter& filter);

    /// One convex or concave piece of a Havok shape, in game units, with the transform from its own space to the one
    /// of the body (a rotation and a translation, no scale).
    struct HavokPiece
    {
        std::unique_ptr<btCollisionShape> mShape;
        osg::Matrixf mTransform;
    };

    /// Adds the pieces that make up a Havok shape. `transform` is the one of the shape in the space of the body.
    /// The shapes that are read are boxes, spheres and capsules (as convex hulls), convex hulls, packed triangle strips
    /// and triangle strips, inside of the MOPP trees, lists, convex lists and transform shapes that hold them. Returns
    /// an empty string, or the name of the first record of a kind that is not read (then the pieces made so far mean
    /// nothing).
    std::string convertHavokShape(
        const Nif::bhkShape& shape, const osg::Matrixf& transform, std::vector<HavokPiece>& pieces);

    /// A rigid body of a model and the box of its shape in its own frame, in game units, with the model at rest (each
    /// node's own transform); what FalloutNV.exe reads of the bodies of a door's 3D (0x6C2210, 0x6C2650)
    struct HavokBodyBox
    {
        /// The layer of its filter (filter & 0x7F, NV 0x43B4D0)
        std::uint8_t mLayer = 0;
        /// From the body's frame to the model's: a turn and a translation
        osg::Matrixf mTransform;
        osg::Vec3f mMin;
        osg::Vec3f mMax;
    };

    /// Every collision object of the model's nodes that has a rigid body with a shape that is read, the first switch
    /// child only
    std::vector<HavokBodyBox> havokBodyBoxes(const Nif::NIFFile& file);

    /// The Havok material of a shape (the number that the executables' table of the sounds of steps is indexed by: 0 is
    /// stone, 2 dirt, 4 grass, 5 metal, 8 water, 9 wood, and so on): that of the first piece that has one. A packed
    /// strip names its material in its first part. Empty when none of its pieces has a material.
    std::optional<std::uint32_t> havokMaterialOf(const Nif::bhkShape& shape);
}

#endif
