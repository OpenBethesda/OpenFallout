#ifndef OPENFALLOUT_MWRENDER_CLIPCULLNODE_H
#define OPENFALLOUT_MWRENDER_CLIPCULLNODE_H

#include <osg/Group>
#include <osg/Plane>

namespace osg
{
    class ClipNode;
}

namespace OFRender
{
    /// @brief Allows to cull and clip meshes that are below a plane. Useful for reflection camera effects.
    /// Also handles flipping of the plane when the eye point goes below it.
    /// To use, simply create the scene as subgraph of this node, then do setPlane(const osg::Plane& plane);
    class ClipCullNode : public osg::Group
    {
    public:
        ClipCullNode();

        void setPlane(const osg::Plane& plane);

    private:
        osg::ref_ptr<osg::Group> mClipNodeTransform;
        osg::ref_ptr<osg::ClipNode> mClipNode;

        osg::Plane mPlane;
    };
}

#endif
