#include "falloutplayer.hpp"

#include <cmath>

#include "../mwbase/environment.hpp"
#include "../mwphysics/actor.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwscript/falloutscripts.hpp"

namespace OFClass
{
    void FalloutPlayer::insertObjectPhysics(const OFWorld::Ptr& ptr, const std::string& model,
        const osg::Quat& rotation, OFPhysics::PhysicsSystem& physics) const
    {
        // The size of the race (the reference scale is the physics' own, OFPhysics::Actor::updateScaleUnsafe)
        double height = 1.0;
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
        {
            const double scale = ptr.getCellRef().getScale();
            if (scale > 0.0)
                height = scripts->sizeOf(ptr) / scale;
        }
        if (!std::isfinite(height) || height <= 0.0)
            height = 1.0;
        OFPhysics::CharacterShape shape;
        shape.mRadius *= static_cast<float>(height);
        shape.mHalfHeight *= static_cast<float>(height);
        physics.addCharacter(ptr, shape);
    }

    std::string_view FalloutPlayer::getName(const OFWorld::ConstPtr& ptr) const
    {
        // The name the player gave the character (the name screen of the making of the character), else the record's
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            if (const std::string_view given = scripts->playerName(); !given.empty())
                return given;
        return ptr.get<ESM4::Npc>()->mBase->mFullName;
    }
}
