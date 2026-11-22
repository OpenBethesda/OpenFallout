#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_SAYINGS_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_SAYINGS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "savestate.hpp"
#include "script.hpp"

namespace FalloutScript
{
    /// A line said to the player outside of a conversation (Say, SayTo, a radio), one after the other for a character
    struct SaidLine
    {
        std::string mVoice;
        std::string mText;
        float mSeconds = 0;
        /// The last line of what one Say or SayTo said: the script of the speaker is told when it is over
        bool mEnd = false;
        /// The topic that was said, which the script of the speaker is told of (Begin SayToDone Topic)
        FormId mTopic = 0;
        /// The response that this is the last line of: its end script runs, and the topics it adds are given, when
        /// the line is over (and before the script of the speaker is told that it has said what it was told to)
        FormId mEndInfo = 0;
        /// The record of the speaker, kept with the line: a speaker that was made while the game runs and has left
        /// the loaded cells has no reference to tell it by when the response ends
        FormId mSpeakerBase = 0;
        /// The character that says the line when it is not the one the lines were given to (0 for that one): in an
        /// exchange of two characters they take turns
        FormId mSpeaker = 0;
        /// The script asked for the text to be shown even with subtitles off
        bool mForceSubtitles = false;
        /// A line of a radio that is not the Pip-Boy's (a thing in a room, a character's): it follows the radio's
        /// volume, and a song of it is left out when the radio is set to dialogue only
        bool mRadio = false;
        bool mSong = false;
    };

    /// What a character says outside of a conversation: the lines, the one it is at, and how long that one has left
    struct Saying
    {
        FormId mReference = 0;
        std::vector<SaidLine> mLines;
        std::size_t mNext = 0;
        float mWait = 0;
        bool mStarted = false;
    };

    /// The section of a saved game that holds what characters were saying outside of a conversation when the game was
    /// saved: the line each one was at and those it had left, and the speakers and topics whose scripts were to be
    /// told at the next frame that nobody could say them. A quest that goes on from the end of a line (its end script,
    /// or the SayToDone block of the speaker's script) goes on after a load as it would have: the line that was being
    /// said is said again from its start, then the rest, and what their ends do is done when they are over. A save
    /// made before the section existed has none, and nothing is being said after its load (the game before this did
    /// the same). The lines of the radios are left out: the radios start over from what the radio section keeps.
    constexpr std::uint32_t sayingsSectionVersion = 1;

    struct SavedSayings
    {
        std::vector<Saying> mSayings;
        /// The speaker and the topic of each script that is to be told that nothing could be said
        std::vector<std::pair<FormId, FormId>> mNothingSaid;
    };

    void writeSayings(
        const std::vector<Saying>& sayings, const std::vector<std::pair<FormId, FormId>>& nothingSaid, SaveWriter& out);
    /// What the section holds, the line that was being said first, none of them started. A character whose plugin is
    /// no longer loaded is left out, and so is a line of another character of an exchange whose plugin is gone. A
    /// topic or a response of a plugin that is gone is 0: there is no end script to run for it.
    SavedSayings readSayings(SaveReader& in, std::uint32_t version);
}

#endif
