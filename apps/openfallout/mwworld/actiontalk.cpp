#include "actiontalk.hpp"

#include "../mwbase/environment.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwscript/falloutscripts.hpp"

#include <components/esm/defs.hpp>

namespace OFWorld
{
    ActionTalk::ActionTalk(const Ptr& actor)
        : Action(false, actor)
    {
    }

    void ActionTalk::executeImp(const Ptr& actor)
    {
        if (actor != OFMechanics::getPlayer())
            return;
        // A character of Fallout talks with the dialogue of the games it is from, and a companion gives the wheel of
        // its commands first (it has "Talk To" among them). A character of Morrowind has no conversation: its dialogue
        // window is gone
        if (ESM::isESM4Rec(static_cast<ESM::RecNameInts>(getTarget().getType())))
        {
            const auto scripts = OFBase::Environment::get().getFalloutScripts();
            if (!scripts->isCompanion(getTarget()) || !scripts->openCompanionWheel(getTarget()))
                scripts->talkTo(getTarget());
        }
    }
}
