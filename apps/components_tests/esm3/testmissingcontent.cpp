#include <components/esm3/missingcontent.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

namespace ESM
{
    namespace
    {
        constexpr MissingContentAnswer allAnswers[]
            = { MissingContentAnswer::Ask, MissingContentAnswer::Accept, MissingContentAnswer::Refuse };

        // A save whose content files are all loaded is loaded, whatever the caller said and however the engine runs
        TEST(ESMMissingContentTest, aSaveWithNothingMissingIsLoaded)
        {
            for (const MissingContentAnswer answer : allAnswers)
                for (const bool headless : { false, true })
                    EXPECT_EQ(decideMissingContent(answer, headless, false), MissingContentAction::Load);
        }

        // The player's default is unchanged: the engine asks
        TEST(ESMMissingContentTest, theDefaultAsksThePlayer)
        {
            EXPECT_EQ(decideMissingContent(MissingContentAnswer::Ask, false, true), MissingContentAction::AskPlayer);
        }

        // A hidden run never opens the box: nobody could answer it. It loads, as it did before the answer existed
        TEST(ESMMissingContentTest, aHeadlessRunThatWasNotToldLoads)
        {
            EXPECT_EQ(decideMissingContent(MissingContentAnswer::Ask, true, true), MissingContentAction::Load);
        }

        TEST(ESMMissingContentTest, acceptLoadsWithoutABox)
        {
            EXPECT_EQ(decideMissingContent(MissingContentAnswer::Accept, false, true), MissingContentAction::Load);
            EXPECT_EQ(decideMissingContent(MissingContentAnswer::Accept, true, true), MissingContentAction::Load);
        }

        // The caller's refusal wins over the headless default
        TEST(ESMMissingContentTest, refuseDoesNotLoadAndOpensNoBox)
        {
            EXPECT_EQ(decideMissingContent(MissingContentAnswer::Refuse, false, true), MissingContentAction::Refuse);
            EXPECT_EQ(decideMissingContent(MissingContentAnswer::Refuse, true, true), MissingContentAction::Refuse);
        }

        TEST(ESMMissingContentTest, theThreeWordsAreRead)
        {
            EXPECT_EQ(parseMissingContentAnswer("ask"), MissingContentAnswer::Ask);
            EXPECT_EQ(parseMissingContentAnswer("accept"), MissingContentAnswer::Accept);
            EXPECT_EQ(parseMissingContentAnswer("refuse"), MissingContentAnswer::Refuse);
        }

        TEST(ESMMissingContentTest, anyOtherWordIsNotAnAnswer)
        {
            for (const std::string_view text : { "", "yes", "no", "Accept", "REFUSE", " ask", "accept ", "1" })
                EXPECT_EQ(parseMissingContentAnswer(text), std::nullopt) << "'" << text << "'";
        }

        TEST(ESMMissingContentTest, eachAnswerHasTheNameItIsReadFrom)
        {
            for (const MissingContentAnswer answer : allAnswers)
                EXPECT_EQ(parseMissingContentAnswer(missingContentAnswerName(answer)), answer);
            EXPECT_EQ(missingContentAnswerName(MissingContentAnswer::Accept), "accept");
        }
    }
}
