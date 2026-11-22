#ifndef GAME_MWBASE_DIALOGUEMANAGER_H
#define GAME_MWBASE_DIALOGUEMANAGER_H

#include <list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ESM
{
    class RefId;
}

namespace OFWorld
{
    class Ptr;
}

namespace OFBase
{
    /// \brief What the dialogue window of Morrowind asks about a Morrowind conversation (implemented in OFDialogue)
    ///
    /// The conversations of Fallout run in OFScript::FalloutScripts and the interface of the Lua scripts. The window
    /// of Morrowind (topics, persuasion, services) is still built and still holds the GM_Dialogue mode of a Fallout
    /// conversation, so it keeps asking these questions; the engine has no Morrowind conversation to answer them with.
    class DialogueManager
    {
        DialogueManager(const DialogueManager&);
        ///< not implemented

        DialogueManager& operator=(const DialogueManager&);
        ///< not implemented

    public:
        class ResponseCallback
        {
        public:
            virtual ~ResponseCallback() = default;
            virtual void addResponse(std::string_view title, std::string_view text) = 0;
        };

        DialogueManager() {}

        virtual ~DialogueManager() {}

        virtual bool isInChoice() const = 0;

        virtual bool startDialogue(const OFWorld::Ptr& actor, ResponseCallback* callback) = 0;

        virtual const std::vector<std::pair<std::string, int>>& getChoices() const = 0;

        virtual bool isGoodbye() const = 0;

        virtual void keywordSelected(std::string_view keyword, ResponseCallback* callback) = 0;
        virtual void goodbyeSelected() = 0;
        virtual void questionAnswered(int answer, ResponseCallback* callback) = 0;

        enum TopicType
        {
            Specific = 1,
            Exhausted = 2
        };

        enum ServiceType
        {
            Any = -1,
            Barter = 1,
            Repair = 2,
            Spells = 3,
            Training = 4,
            Travel = 5,
            Spellmaking = 6,
            Enchanting = 7
        };

        virtual std::list<std::string> getAvailableTopics() = 0;
        virtual int getTopicFlag(const ESM::RefId&) const = 0;

        virtual bool checkServiceRefused(ResponseCallback* callback, ServiceType service = ServiceType::Any) = 0;

        virtual void persuade(int type, ResponseCallback* callback) = 0;
    };
}

#endif
