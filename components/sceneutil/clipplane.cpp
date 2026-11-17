#include "clipplane.hpp"

#include <cassert>
#include <string>

#include <osg/ClipPlane>
#include <osg/StateSet>
#include <osg/Uniform>

#include <components/shader/glsles.hpp>

#include "gldialect.hpp"

namespace SceneUtil
{
    void setEyeSpaceClipPlane(
        osg::StateSet& stateSet, unsigned int index, const osg::Plane& plane, osg::StateAttribute::GLModeValue value)
    {
        if constexpr (GLDialect::sHasUserClipPlanes)
        {
            stateSet.setAttributeAndModes(new osg::ClipPlane(index, plane), value);
        }
        else
        {
            assert(index < Shader::sClipPlaneCount);
            stateSet.setMode(GL_CLIP_PLANE0 + index, value);
            const std::string name = Shader::clipPlaneUniformName(index);
            osg::Uniform* uniform = stateSet.getUniform(name);
            if (uniform == nullptr)
            {
                uniform = new osg::Uniform(osg::Uniform::FLOAT_VEC4, name);
                stateSet.addUniform(uniform, value);
            }
            uniform->set(osg::Vec4f(plane.asVec4()));
        }
    }
}
