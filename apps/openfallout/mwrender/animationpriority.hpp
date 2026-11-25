#ifndef GAME_RENDER_ANIMATIONPRIORITY_H
#define GAME_RENDER_ANIMATIONPRIORITY_H

#include "blendmask.hpp"
#include "bonegroup.hpp"

namespace OFRender
{
    /// Holds an animation priority value for each BoneGroup.
    struct AnimPriority
    {
        /// Convenience constructor, initialises all priorities to the same value.
        AnimPriority(int priority)
        {
            for (unsigned int i = 0; i < sNumBlendMasks; ++i)
                mPriority[i] = priority;
        }

        bool operator==(const AnimPriority& other) const
        {
            for (unsigned int i = 0; i < sNumBlendMasks; ++i)
                if (other.mPriority[i] != mPriority[i])
                    return false;
            return true;
        }

        int& operator[](BoneGroup n) { return mPriority[n]; }

        const int& operator[](BoneGroup n) const { return mPriority[n]; }

        bool contains(int priority) const
        {
            for (unsigned int i = 0; i < sNumBlendMasks; ++i)
                if (priority == mPriority[i])
                    return true;
            return false;
        }

        int mPriority[sNumBlendMasks];
    };
}

namespace OFMechanics
{
    /// The priorities of the animation groups that play at once on one skeleton: a group of a higher priority takes
    /// the bones it moves from one of a lower. Scripted (PlayGroup, the Lua animation interface) is above all.
    enum Priority
    {
        Priority_Default,
        Priority_WeaponLowerBody,
        Priority_SneakIdleLowerBody,
        Priority_SwimIdle,
        Priority_Jump,
        Priority_Movement,
        Priority_Hit,
        Priority_Weapon,
        Priority_Block,
        Priority_Knockdown,
        Priority_Torch,
        Priority_Storm,
        Priority_Death,
        Priority_Scripted,

        Num_Priorities
    };
}
#endif
