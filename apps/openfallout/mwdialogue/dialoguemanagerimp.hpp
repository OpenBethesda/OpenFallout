#ifndef GAME_MWDIALOGUE_DIALOGUEMANAGERIMP_H
#define GAME_MWDIALOGUE_DIALOGUEMANAGERIMP_H

#include "../mwbase/dialoguemanager.hpp"

namespace OFDialogue
{
    /// \brief The answer of an engine that has no Morrowind conversation: nobody starts one, so there are no topics,
    /// choices or services, and what the window asks to do with them does nothing
    class DialogueManager : public OFBase::DialogueManager
    {
    public:
        bool isInChoice() const override { return false; }

        bool startDialogue(const OFWorld::Ptr& actor, ResponseCallback* callback) override;

        const std::vector<std::pair<std::string, int>>& getChoices() const override { return mNoChoices; }

        bool isGoodbye() const override { return false; }

        void keywordSelected(std::string_view /*keyword*/, ResponseCallback* /*callback*/) override {}
        void goodbyeSelected() override {}
        void questionAnswered(int /*answer*/, ResponseCallback* /*callback*/) override {}

        std::list<std::string> getAvailableTopics() override { return {}; }
        int getTopicFlag(const ESM::RefId& /*topic*/) const override { return 0; }

        bool checkServiceRefused(ResponseCallback* /*callback*/, ServiceType /*service*/) override { return true; }

        void persuade(int /*type*/, ResponseCallback* /*callback*/) override {}

    private:
        const std::vector<std::pair<std::string, int>> mNoChoices;
    };
}

#endif
