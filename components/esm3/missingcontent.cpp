#include "missingcontent.hpp"

namespace ESM
{
    std::optional<MissingContentAnswer> parseMissingContentAnswer(std::string_view text)
    {
        if (text == "ask")
            return MissingContentAnswer::Ask;
        if (text == "accept")
            return MissingContentAnswer::Accept;
        if (text == "refuse")
            return MissingContentAnswer::Refuse;
        return std::nullopt;
    }

    std::string_view missingContentAnswerName(MissingContentAnswer answer)
    {
        switch (answer)
        {
            case MissingContentAnswer::Accept:
                return "accept";
            case MissingContentAnswer::Refuse:
                return "refuse";
            case MissingContentAnswer::Ask:
                break;
        }
        return "ask";
    }

    MissingContentAction decideMissingContent(MissingContentAnswer answer, bool headless, bool contentMissing)
    {
        if (!contentMissing)
            return MissingContentAction::Load;
        switch (answer)
        {
            case MissingContentAnswer::Accept:
                return MissingContentAction::Load;
            case MissingContentAnswer::Refuse:
                return MissingContentAction::Refuse;
            case MissingContentAnswer::Ask:
                break;
        }
        return headless ? MissingContentAction::Load : MissingContentAction::AskPlayer;
    }
}
