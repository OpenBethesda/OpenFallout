#include "dialoguemanagerimp.hpp"

#include <components/debug/debuglog.hpp>

#include "../mwworld/cellref.hpp"
#include "../mwworld/ptr.hpp"

namespace OFDialogue
{
    bool DialogueManager::startDialogue(const OFWorld::Ptr& actor, ResponseCallback* /*callback*/)
    {
        // A character of Fallout talks through OFScript::FalloutScripts and never comes here
        Log(Debug::Warning) << "Warning: " << actor.getCellRef().getRefId().toDebugString()
                            << " has a Morrowind conversation, which OpenFallout does not run";
        return false;
    }
}
