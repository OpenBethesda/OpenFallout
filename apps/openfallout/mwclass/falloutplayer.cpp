#include "falloutplayer.hpp"

#include <cmath>

#include "../mwbase/environment.hpp"
#include "../mwphysics/actor.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwscript/falloutscripts.hpp"

#include "esm4base.hpp"

namespace OFClass
{
    void FalloutPlayer::insertObjectPhysics(const OFWorld::Ptr& ptr, const std::string& model,
        const osg::Quat& rotation, OFPhysics::PhysicsSystem& physics) const
    {
        // The body of the executables from the skeleton's bound and the size of the race (FalloutNV.exe 0xC72410)
        ESM4Impl::insertCharacterBody(ptr, physics);
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
