#include <components/sceneutil/clipplane.hpp>
#include <components/sceneutil/gldialect.hpp>
#include <components/shader/glsles.hpp>

#include <osg/ClipPlane>
#include <osg/StateSet>
#include <osg/Uniform>

#include <gtest/gtest.h>

namespace
{
    // What cuts away the part of a drawing on the negative side of an eye-space plane, in each build: desktop OpenGL's
    // user clip plane, or (OpenGL ES has none) the switch of the clip distance of the same number and the plane in the
    // uniform the converted vertex shaders write that distance from
    osg::Vec4d clipPlaneOf(const osg::StateSet& stateSet, unsigned int index)
    {
        if constexpr (SceneUtil::GLDialect::sHasUserClipPlanes)
        {
            const auto* clipPlane
                = dynamic_cast<const osg::ClipPlane*>(stateSet.getAttribute(osg::StateAttribute::CLIPPLANE, index));
            return clipPlane != nullptr ? clipPlane->getClipPlane() : osg::Vec4d();
        }
        else
        {
            const osg::Uniform* uniform = stateSet.getUniform(Shader::clipPlaneUniformName(index));
            osg::Vec4f value;
            if (uniform == nullptr || !uniform->get(value))
                return osg::Vec4d();
            return osg::Vec4d(value);
        }
    }

    TEST(SceneUtilClipPlaneTest, a_plane_set_on_a_state_set_cuts_with_that_plane)
    {
        osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet;
        const osg::Plane plane(0.0, 0.0, 1.0, -120.0);

        SceneUtil::setEyeSpaceClipPlane(*stateSet, 0, plane);

        EXPECT_EQ(clipPlaneOf(*stateSet, 0), plane.asVec4());
        EXPECT_EQ(stateSet->getMode(GL_CLIP_PLANE0), static_cast<unsigned int>(osg::StateAttribute::ON));
    }

    // The water's reflection camera gives a new plane every frame, the transparent objects at the water surface one for
    // each side of it: setting the plane again on the same state set replaces it
    TEST(SceneUtilClipPlaneTest, setting_a_plane_again_replaces_it)
    {
        osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet;
        SceneUtil::setEyeSpaceClipPlane(*stateSet, 1, osg::Plane(0.0, 0.0, 1.0, -4.0),
            osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED);
        const osg::Plane otherSide(0.0, 0.0, -1.0, 4.0);
        SceneUtil::setEyeSpaceClipPlane(*stateSet, 1, otherSide,
            osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED);

        EXPECT_EQ(clipPlaneOf(*stateSet, 1), otherSide.asVec4());
        EXPECT_EQ(stateSet->getMode(GL_CLIP_PLANE1),
            static_cast<unsigned int>(
                osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED));
        // plane 0 is untouched
        EXPECT_EQ(clipPlaneOf(*stateSet, 0), osg::Vec4d());
    }
}
