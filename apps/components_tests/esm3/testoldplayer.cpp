#include "saveloadharness.hpp"

#include <components/esm/luascripts.hpp>
#include <components/esm3/globalscript.hpp>
#include <components/esm3/player.hpp>
#include <components/esm3/stolenitems.hpp>
#include <components/falloutscript/playerstate.hpp>
#include <components/falloutscript/savestate.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

// The player of a save made before the player became a Fallout actor (step 4a): the PLAY record kept it as a Morrowind
// character. A save of this version keeps what the player needs in the "player" section of the Fallout scripts (FOSV),
// and the old record is read once to fill it. These tests write the old record in its own layout, read it as the engine
// reads it and convert it, and check the records that the engine skips now.
namespace ESM
{
    namespace
    {
        using namespace ::testing;
        using namespace ::ESM::SaveLoadTest;

        struct Esm3OldPlayerTest : public TestWithParam<FormatVersion>
        {
        };

        RefId id(std::string_view value)
        {
            return RefId::stringRefId(value);
        }

        void fillOldPlayer(Player& player)
        {
            player.mObject.blank();
            player.mObject.mRef.mRefID = id("player");
            player.mObject.mRef.mRefNum = RefNum{ 0x14, 0 };
            player.mObject.mPosition = { .pos = { 100.f, -200.f, 30.5f }, .rot = { 0.1f, 0.f, 1.5f } };
            player.mObject.mEnabled = 1;
            player.mCellId = id("Vault101a");
            player.mLastKnownExteriorPosition[0] = 4096.f;
            player.mLastKnownExteriorPosition[1] = -8192.f;
            player.mLastKnownExteriorPosition[2] = 512.f;
            player.mHasMark = false;
            player.mCurrentCrimeId = -1;
            player.mPaidCrimeId = -1;
            // The run toggle and the sneak were movement flags of the Morrowind stats (Flag_Run 4, Flag_Sneak 8), the
            // forced stance the flags 1 and 2
            player.mObject.mCreatureStats.mMovementFlags = 4 | 8 | 1;
            LuaScript script;
            script.mScriptId = 7;
            script.mData = std::string("lua\0data", 8);
            script.mTimers.push_back({ LuaTimer::Type::GAME_TIME, 12.5, "onTimer", std::string("a\0b", 3) });
            player.mObject.mLuaScripts.mScripts.push_back(script);
        }

        bool mentions(const FalloutScript::ConvertedPlayer& converted, std::string_view what)
        {
            return std::any_of(converted.mDropped.begin(), converted.mDropped.end(),
                [&](const std::string& line) { return line.find(what) != std::string::npos; });
        }

        TEST_P(Esm3OldPlayerTest, thePlayRecordOfAnOldSaveGivesThePlaceTheReferenceAndTheLuaScriptsOfThePlayer)
        {
            // (the formats before this one load the old player stats in a layout of their own, tested elsewhere)
            if (GetParam() <= MaxOldSkillsAndAttributesFormatVersion)
                return;
            Player record{};
            fillOldPlayer(record);
            Player loaded;
            saveAndLoadRecord(record, GetParam(), loaded);

            const FalloutScript::ConvertedPlayer converted = FalloutScript::convertOldPlayer(loaded);
            const FalloutScript::PlayerState& state = converted.mState;
            EXPECT_EQ(RefId::deserializeText(state.mCell), id("Vault101a"));
            EXPECT_FALSE(converted.mExterior);
            EXPECT_FLOAT_EQ(state.mPosition[0], 100.f);
            EXPECT_FLOAT_EQ(state.mPosition[1], -200.f);
            EXPECT_FLOAT_EQ(state.mPosition[2], 30.5f);
            EXPECT_FLOAT_EQ(state.mRotation[0], 0.1f);
            EXPECT_FLOAT_EQ(state.mRotation[2], 1.5f);
            EXPECT_FLOAT_EQ(state.mLastExteriorPosition[0], 4096.f);
            EXPECT_FLOAT_EQ(state.mLastExteriorPosition[1], -8192.f);
            EXPECT_FLOAT_EQ(state.mLastExteriorPosition[2], 512.f);
            EXPECT_EQ(state.mRefNum, (RefNum{ 0x14, 0 }));
            ASSERT_EQ(state.mLuaScripts.mScripts.size(), 1u);
            EXPECT_EQ(state.mLuaScripts.mScripts[0].mScriptId, 7);
            EXPECT_EQ(state.mLuaScripts.mScripts[0].mData, std::string("lua\0data", 8));
            ASSERT_EQ(state.mLuaScripts.mScripts[0].mTimers.size(), 1u);
            EXPECT_EQ(state.mLuaScripts.mScripts[0].mTimers[0].mCallbackName, "onTimer");
            EXPECT_TRUE(state.mRunning);
            EXPECT_TRUE(state.mSneaking);
        }

        TEST_P(Esm3OldPlayerTest, anExteriorCellOfAnOldSaveIsKeptAsAnExteriorCell)
        {
            if (GetParam() <= MaxOldSkillsAndAttributesFormatVersion)
                return;
            Player record{};
            fillOldPlayer(record);
            record.mCellId = RefId::esm3ExteriorCell(-3, 7);
            Player loaded;
            saveAndLoadRecord(record, GetParam(), loaded);

            const FalloutScript::ConvertedPlayer converted = FalloutScript::convertOldPlayer(loaded);
            EXPECT_TRUE(converted.mExterior);
            EXPECT_EQ(RefId::deserializeText(converted.mState.mCell), RefId::esm3ExteriorCell(-3, 7));
        }

        TEST_P(Esm3OldPlayerTest, whatTheOldRecordHadOfMorrowindIsNamedAsDropped)
        {
            if (GetParam() <= MaxOldSkillsAndAttributesFormatVersion)
                return;
            Player record{};
            fillOldPlayer(record);
            record.mHasMark = true;
            record.mMarkedCell = id("Balmora");
            record.mBirthsign = id("wombburned");
            record.mSaveSkills = { { Skill::Acrobatics, 55.f } };
            Player loaded;
            saveAndLoadRecord(record, GetParam(), loaded);

            const FalloutScript::ConvertedPlayer converted = FalloutScript::convertOldPlayer(loaded);
            EXPECT_TRUE(mentions(converted, "Morrowind stats"));
            EXPECT_TRUE(mentions(converted, "draw state"));
            EXPECT_TRUE(mentions(converted, "Morrowind inventory"));
            EXPECT_TRUE(mentions(converted, "Mark"));
            EXPECT_TRUE(mentions(converted, "birthsign"));
            EXPECT_TRUE(mentions(converted, "werewolf"));
        }

        TEST_P(Esm3OldPlayerTest, aRecordWithoutTheMorrowindExtrasNamesOnlyWhatEveryRecordHad)
        {
            if (GetParam() <= MaxOldSkillsAndAttributesFormatVersion)
                return;
            Player record{};
            fillOldPlayer(record);
            Player loaded;
            saveAndLoadRecord(record, GetParam(), loaded);

            const FalloutScript::ConvertedPlayer converted = FalloutScript::convertOldPlayer(loaded);
            EXPECT_FALSE(mentions(converted, "Mark"));
            EXPECT_FALSE(mentions(converted, "birthsign"));
            EXPECT_FALSE(mentions(converted, "werewolf"));
            EXPECT_EQ(converted.mDropped.size(), 4u);
        }

        // The records that the Morrowind mechanics wrote, which the engine no longer writes and skips on load: each is
        // written in the layout of the old save, with a record after it that must still load.
        template <class WriteOldRecord>
        void skipTheOldRecordAndLoadTheNext(FormatVersion format, std::uint32_t oldRecord, WriteOldRecord&& writeOld)
        {
            GlobalScript next;
            next.mId = id("nextscript");
            next.mRunning = true;

            ESMWriter writer;
            auto stream = std::make_unique<std::stringstream>();
            writer.setFormatVersion(format);
            writer.save(*stream);
            writer.startRecord(oldRecord);
            writeOld(writer);
            writer.endRecord(oldRecord);
            writer.startRecord(fakeRecordId);
            next.save(writer);
            writer.endRecord(fakeRecordId);

            ESMReader reader;
            reader.open(std::move(stream), "stream");
            ASSERT_TRUE(reader.hasMoreRecs());
            ASSERT_EQ(reader.getRecName().toInt(), oldRecord);
            reader.getRecHeader();
            reader.skipRecord();

            ASSERT_TRUE(reader.hasMoreRecs());
            ASSERT_EQ(reader.getRecName().toInt(), fakeRecordId);
            reader.getRecHeader();
            GlobalScript result;
            result.load(reader);
            EXPECT_EQ(result.mId, next.mId);
            EXPECT_TRUE(result.mRunning);
            EXPECT_FALSE(reader.hasMoreRecs());
        }

        TEST_P(Esm3OldPlayerTest, theDeathCounterOfAnOldSaveIsSkippedAndTheRecordAfterItStillLoads)
        {
            // DCOU: how many of each Morrowind character died (the deaths of Fallout are in FOSV)
            skipTheOldRecordAndLoadTheNext(GetParam(), static_cast<std::uint32_t>(REC_DCOU), [](ESMWriter& writer) {
                writer.writeHNRefId("ID__", id("dagoth ur"));
                writer.writeHNT("COUN", std::int32_t{ 3 });
            });
        }

        TEST_P(Esm3OldPlayerTest, theStolenItemsOfAnOldSaveAreSkippedAndTheRecordAfterItStillLoads)
        {
            // STLN: the Morrowind items the player had stolen, by owner (Fallout judges thefts in its own records)
            skipTheOldRecordAndLoadTheNext(GetParam(), static_cast<std::uint32_t>(REC_STLN), [](ESMWriter& writer) {
                StolenItems items;
                items.mStolenItems[id("gold_001")][{ id("fargoth"), false }] = 2;
                items.write(writer);
            });
        }

        TEST_P(Esm3OldPlayerTest, thePlayRecordIsSkippedOnceItWasRead)
        {
            // PLAY: read to fill the player section when the save has none, and not kept
            if (GetParam() <= MaxOldSkillsAndAttributesFormatVersion)
                return;
            skipTheOldRecordAndLoadTheNext(GetParam(), static_cast<std::uint32_t>(REC_PLAY),
                [](ESMWriter& writer) {
                    Player record{};
                    fillOldPlayer(record);
                    record.save(writer);
                });
        }

        INSTANTIATE_TEST_SUITE_P(FormatVersions, Esm3OldPlayerTest, ValuesIn(getFormats()));
    }

    namespace
    {
        TEST(FalloutScriptPlayerStateTest, thePlayerSectionKeepsEverythingItHas)
        {
            FalloutScript::PlayerState state;
            state.mCell = RefId::esm3ExteriorCell(2, -5).serializeText();
            state.mPosition = { 1.f, 2.f, 3.f };
            state.mRotation = { -0.5f, 0.f, 3.f };
            state.mLastExteriorPosition = { 9.f, 8.f, 7.f };
            state.mRefNum = RefNum{ 0x14, 0 };
            state.mRunning = true;
            LuaScript script;
            script.mScriptId = 3;
            script.mData = std::string("x\0y", 3);
            script.mTimers.push_back({ LuaTimer::Type::REAL_TIME, 1.25, "tick", "" });
            state.mLuaScripts.mScripts.push_back(script);

            FalloutScript::SaveWriter out;
            FalloutScript::writePlayerState(state, out);
            FalloutScript::SaveReader in(out.bytes());
            FalloutScript::PlayerState result;
            FalloutScript::readPlayerState(result, in, FalloutScript::playerSectionVersion);

            EXPECT_EQ(result.mCell, state.mCell);
            EXPECT_EQ(result.mPosition, state.mPosition);
            EXPECT_EQ(result.mRotation, state.mRotation);
            EXPECT_EQ(result.mLastExteriorPosition, state.mLastExteriorPosition);
            EXPECT_EQ(result.mRefNum, state.mRefNum);
            EXPECT_TRUE(result.mRunning);
            EXPECT_FALSE(result.mSneaking);
            ASSERT_EQ(result.mLuaScripts.mScripts.size(), 1u);
            EXPECT_EQ(result.mLuaScripts.mScripts[0].mData, std::string("x\0y", 3));
            ASSERT_EQ(result.mLuaScripts.mScripts[0].mTimers.size(), 1u);
            EXPECT_EQ(result.mLuaScripts.mScripts[0].mTimers[0].mType, LuaTimer::Type::REAL_TIME);
            EXPECT_DOUBLE_EQ(result.mLuaScripts.mScripts[0].mTimers[0].mTime, 1.25);
        }

        TEST(FalloutScriptPlayerStateTest, aPlayerWithNoCellAndNoScriptsKeepsThatToo)
        {
            FalloutScript::SaveWriter out;
            FalloutScript::writePlayerState(FalloutScript::PlayerState(), out);
            FalloutScript::SaveReader in(out.bytes());
            FalloutScript::PlayerState result;
            result.mRunning = true;
            FalloutScript::readPlayerState(result, in, FalloutScript::playerSectionVersion);
            EXPECT_TRUE(result.mCell.empty());
            EXPECT_FALSE(result.mRunning);
            EXPECT_TRUE(result.mLuaScripts.mScripts.empty());
        }
    }
}
