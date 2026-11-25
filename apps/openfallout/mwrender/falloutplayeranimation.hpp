#ifndef OPENFALLOUT_MWRENDER_FALLOUTPLAYERANIMATION_H
#define OPENFALLOUT_MWRENDER_FALLOUTPLAYERANIMATION_H

#include <osg/Vec3f>
#include <osg/ref_ptr>

#include "animation.hpp"

namespace osg
{
    class MatrixTransform;
}

namespace OFRender
{
    /// The animation the engine keeps for the player (RenderingManager::getAnimation of the player): what the camera
    /// follows. The player has no mesh of its own in the scene: what the games draw of it, its body and the arms of
    /// the first person, are characters of Fallout that FalloutPlayerBody makes from the look and the armor, and their
    /// skeletons carry the animations of the games. This one holds only the point of the eyes, the node `Head` at 124
    /// units above the feet (the eye height of the human skeleton of the games), which the first person camera follows.
    class FalloutPlayerAnimation final : public Animation
    {
    public:
        enum ViewMode
        {
            VM_Normal,
            VM_FirstPerson
        };

        FalloutPlayerAnimation(
            const OFWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode, Resource::ResourceSystem* resourceSystem);

        /// Whether the camera is in the player's eyes (the body and the arms show or hide by it in the renderer)
        void setViewMode(ViewMode viewMode) { mViewMode = viewMode; }
        ViewMode getViewMode() const { return mViewMode; }

        /// Moves the eyes from their place (the drop of the Camera1st node of the arms, which their sneak animations
        /// lower)
        void setFirstPersonOffset(const osg::Vec3f& offset);

        /// The eye height above the feet, in units
        static constexpr float sEyeHeight = 124.f;

    private:
        osg::ref_ptr<osg::MatrixTransform> mHead;
        ViewMode mViewMode = VM_Normal;
    };
}

#endif
