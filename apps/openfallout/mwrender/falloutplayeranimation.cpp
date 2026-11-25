#include "falloutplayeranimation.hpp"

#include <osg/Group>
#include <osg/PositionAttitudeTransform>

namespace OFRender
{
    FalloutPlayerAnimation::FalloutPlayerAnimation(
        const OFWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode, Resource::ResourceSystem* resourceSystem)
        : Animation(ptr, std::move(parentNode), resourceSystem)
    {
        mObjectRoot = new osg::Group;
        mObjectRoot->setName("FalloutPlayer");
        mHead = new osg::PositionAttitudeTransform;
        mHead->setName("Head");
        mHead->setPosition(osg::Vec3f(0.f, 0.f, sEyeHeight));
        mObjectRoot->addChild(mHead);
        if (mInsert != nullptr)
            mInsert->addChild(mObjectRoot);
    }

    void FalloutPlayerAnimation::setFirstPersonOffset(const osg::Vec3f& offset)
    {
        mHead->setPosition(osg::Vec3f(0.f, 0.f, sEyeHeight) + offset);
    }
}
