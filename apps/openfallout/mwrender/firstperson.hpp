#ifndef OPENFALLOUT_MWRENDER_FIRSTPERSON_H
#define OPENFALLOUT_MWRENDER_FIRSTPERSON_H

namespace osg
{
    class Node;
}

namespace OFRender
{
    class RenderingManager;

    /// Makes what hangs below `root` draw as the player's own arms and weapon do in the first person: in the render bin
    /// that clears the depth buffer first, so that nothing of the world is drawn over them (they never poke into a
    /// wall)
    void setFirstPersonRenderBin(osg::Node& root);

    /// Draws what hangs below `root` with the field of view of the first person (`fov`, in degrees) instead of the one
    /// of the world, as a cull callback of the node
    void addFirstPersonFieldOfView(osg::Node& root, float fov, RenderingManager* renderingManager);
}

#endif
