#ifndef OPENFALLOUT_COMPONENTS_SCENEUTIL_CLIPPLANE_H
#define OPENFALLOUT_COMPONENTS_SCENEUTIL_CLIPPLANE_H

#include <osg/Plane>
#include <osg/StateAttribute>

namespace osg
{
    class StateSet;
}

namespace SceneUtil
{
    // Sets user clip plane `index` for what is drawn with a state set: what lies where the plane is negative is cut
    // away. The plane is in eye space, which is where the shaders put gl_ClipVertex.
    //
    // Desktop OpenGL keeps it as an osg::ClipPlane, applied while the model-view matrix is the identity. OpenGL ES has
    // no user clip planes (OpenSceneGraph's OpenGL ES profile cannot apply an osg::ClipPlane), so the Metal build
    // switches on the clip distance of the same number and gives the plane to the converted shaders as a uniform
    // (Shader::clipPlaneUniformName). A state set that is drawn is changed only where its last frame cannot be drawing:
    // in the draw traversal, or one of a pair of state sets taken in turn by the cull traversal.
    void setEyeSpaceClipPlane(osg::StateSet& stateSet, unsigned int index, const osg::Plane& plane,
        osg::StateAttribute::GLModeValue value = osg::StateAttribute::ON);
}

#endif
