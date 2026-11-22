#include "sayings.hpp"

#include <algorithm>
#include <optional>

namespace FalloutScript
{
    namespace
    {
        constexpr std::uint8_t lineEnds = 1;
        constexpr std::uint8_t lineForcesSubtitles = 2;

        /// A saved line takes at least: two empty texts, the seconds, four forms and the flags
        constexpr std::size_t lineBytes = 4 + 4 + 4 + 4 * 4 + 1;
        /// A saved character: its reference and the count of its lines
        constexpr std::size_t sayingBytes = 4 + 4;

        /// The lines that a save keeps of what a character says: those it has not said to the end, from the one it is
        /// at, without those of radios
        std::vector<const SaidLine*> kept(const Saying& saying)
        {
            std::vector<const SaidLine*> lines;
            for (std::size_t i = saying.mNext; i < saying.mLines.size(); ++i)
                if (!saying.mLines[i].mRadio)
                    lines.push_back(&saying.mLines[i]);
            return lines;
        }
    }

    std::uint64_t SayingQueue::push(FormId reference, std::vector<SaidLine> lines)
    {
        const std::lock_guard lock(mMutex);
        const std::uint64_t ticket = mNext++;
        mEntries.push_back({ ticket, Entry{ reference, std::move(lines) } });
        return ticket;
    }

    std::optional<SayingQueue::Entry> SayingQueue::take(std::uint64_t ticket)
    {
        const std::lock_guard lock(mMutex);
        const auto found = std::find_if(mEntries.begin(), mEntries.end(),
            [ticket](const std::pair<std::uint64_t, Entry>& entry) { return entry.first == ticket; });
        if (found == mEntries.end())
            return std::nullopt;
        Entry entry = std::move(found->second);
        mEntries.erase(found);
        return entry;
    }

    std::vector<SayingQueue::Entry> SayingQueue::takeAll()
    {
        const std::lock_guard lock(mMutex);
        std::vector<Entry> all;
        all.reserve(mEntries.size());
        for (auto& [ticket, entry] : mEntries)
            all.push_back(std::move(entry));
        mEntries.clear();
        return all;
    }

    void SayingQueue::clear()
    {
        const std::lock_guard lock(mMutex);
        mEntries.clear();
    }

    void writeSayings(
        const std::vector<Saying>& sayings, const std::vector<std::pair<FormId, FormId>>& nothingSaid, SaveWriter& out)
    {
        std::vector<std::pair<FormId, std::vector<const SaidLine*>>> written;
        for (const Saying& saying : sayings)
            if (std::vector<const SaidLine*> lines = kept(saying); !lines.empty())
                written.emplace_back(saying.mReference, std::move(lines));
        out.count(written.size());
        for (const auto& [reference, lines] : written)
        {
            out.form(reference);
            out.count(lines.size());
            for (const SaidLine* line : lines)
            {
                out.string(line->mVoice);
                out.string(line->mText);
                out.f32(line->mSeconds);
                out.form(line->mTopic);
                out.form(line->mEndInfo);
                out.form(line->mSpeakerBase);
                out.form(line->mSpeaker);
                out.u8(static_cast<std::uint8_t>(
                    (line->mEnd ? lineEnds : 0) | (line->mForceSubtitles ? lineForcesSubtitles : 0)));
            }
        }
        out.count(nothingSaid.size());
        for (const auto& [speaker, topic] : nothingSaid)
        {
            out.form(speaker);
            out.form(topic);
        }
    }

    SavedSayings readSayings(SaveReader& in, std::uint32_t)
    {
        SavedSayings saved;
        const std::size_t sayings = in.count(sayingBytes);
        for (std::size_t i = 0; i < sayings; ++i)
        {
            const std::optional<FormId> reference = in.form();
            Saying saying;
            saying.mReference = reference.value_or(0);
            const std::size_t lines = in.count(lineBytes);
            for (std::size_t j = 0; j < lines; ++j)
            {
                SaidLine line;
                line.mVoice = in.string();
                line.mText = in.string();
                line.mSeconds = in.f32();
                line.mTopic = in.form().value_or(0);
                line.mEndInfo = in.form().value_or(0);
                line.mSpeakerBase = in.form().value_or(0);
                const std::optional<FormId> speaker = in.form();
                const std::uint8_t flags = in.u8();
                line.mEnd = (flags & lineEnds) != 0;
                line.mForceSubtitles = (flags & lineForcesSubtitles) != 0;
                // The other character of an exchange whose plugin is gone says nothing. What the end of its line does
                // is still done, in its place among the lines: the line is kept with no words, no voice and no time
                // (and is said by the character the lines were given to), so the end script of its response runs and
                // the script of that character is told that it is done when it would have been
                if (!speaker)
                {
                    if (!line.mEnd && line.mEndInfo == 0)
                        continue;
                    line.mVoice.clear();
                    line.mText.clear();
                    line.mSeconds = 0;
                    line.mForceSubtitles = false;
                    line.mSpeaker = 0;
                }
                else
                    line.mSpeaker = *speaker;
                saying.mLines.push_back(std::move(line));
            }
            if (reference && !saying.mLines.empty())
                saved.mSayings.push_back(std::move(saying));
        }
        const std::size_t nothing = in.count(8);
        for (std::size_t i = 0; i < nothing; ++i)
        {
            const std::optional<FormId> speaker = in.form();
            const std::optional<FormId> topic = in.form();
            if (speaker && topic)
                saved.mNothingSaid.emplace_back(*speaker, *topic);
        }
        return saved;
    }
}
