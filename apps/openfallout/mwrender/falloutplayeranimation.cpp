#include "falloutplayeranimation.hpp"

#include <osg/Group>
#include <osg/MatrixTransform>

namespace OFRender
{
    FalloutPlayerAnimation::FalloutPlayerAnimation(
        const OFWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode, Resource::ResourceSystem* resourceSystem)
        : Animation(ptr, std::move(parentNode), resourceSystem)
    {
        mObjectRoot = new osg::Group;
        mObjectRoot->setName("FalloutPlayer");
        // A matrix transform: the node map of the animation (getNode, which the camera asks for "Head") holds only
        // those
        mHead = new osg::MatrixTransform(osg::Matrixf::translate(0.f, 0.f, sEyeHeight));
        mHead->setName("Head");
        mObjectRoot->addChild(mHead);
        if (mInsert != nullptr)
            mInsert->addChild(mObjectRoot);
    }

    void FalloutPlayerAnimation::setFirstPersonOffset(const osg::Vec3f& offset)
    {
        mHead->setMatrix(osg::Matrixf::translate(osg::Vec3f(0.f, 0.f, sEyeHeight) + offset));
    }
}
