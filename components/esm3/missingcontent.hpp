#ifndef OPENMW_COMPONENTS_ESM3_MISSINGCONTENT_H
#define OPENMW_COMPONENTS_ESM3_MISSINGCONTENT_H

#include <optional>
#include <string_view>

namespace ESM
{
    // What the caller said in advance about a save whose content files are not all loaded (see
    // SavedGame::getMissingContentFiles): ask the player in a box (the default), load it anyway, or do not load it.
    // The command line (--missing-content) gives it for the whole run; menu.loadGame of the Lua API for one load.
    enum class MissingContentAnswer
    {
        Ask,
        Accept,
        Refuse,
    };

    // What the engine does with such a save
    enum class MissingContentAction
    {
        Load,
        AskPlayer,
        Refuse,
    };

    // "ask", "accept" or "refuse" (lower case, nothing around them); anything else is not an answer
    std::optional<MissingContentAnswer> parseMissingContentAnswer(std::string_view text);

    std::string_view missingContentAnswerName(MissingContentAnswer answer);

    // A save with nothing missing is loaded. A hidden run (--headless) never opens the box, because nobody could answer
    // it: there "ask" loads, as hidden runs did before the answer existed. "accept" and "refuse" are kept everywhere.
    MissingContentAction decideMissingContent(MissingContentAnswer answer, bool headless, bool contentMissing);
}

#endif
