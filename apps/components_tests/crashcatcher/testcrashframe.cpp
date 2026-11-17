#include <components/crashcatcher/crashframe.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{
    std::string format(const char* label, std::uintptr_t address, const char* image, std::uintptr_t imageBase,
        const char* symbol, std::uintptr_t symbolAddress, std::size_t room = 512)
    {
        std::array<char, 512> out{};
        const std::size_t written = CrashCatcher::formatFrame(
            std::span<char>(out.data(), room), label, address, image, imageBase, symbol, symbolAddress);
        const std::string text(out.data());
        EXPECT_EQ(written, text.size());
        return text;
    }

    TEST(CrashCatcherFrameTest, namesTheImageByItsFileNameAndTheOffsetInIt)
    {
        EXPECT_EQ(format("#2", 0x104a3c120, "/some/folder/libosg.162.dylib", 0x104a00000, "_ZN3osg5State5applyEv",
                      0x104a3c000),
            "#2 0x104a3c120 libosg.162.dylib+0x3c120 _ZN3osg5State5applyEv+0x120\n");
    }

    TEST(CrashCatcherFrameTest, leavesOutTheSymbolWhenItIsNotKnown)
    {
        EXPECT_EQ(format("lr", 0x100004010, "/a/openfallout", 0x100000000, nullptr, 0),
            "lr 0x100004010 openfallout+0x4010\n");
    }

    TEST(CrashCatcherFrameTest, saysWhenNoImageHoldsTheAddress)
    {
        // A call through a null function pointer: the program counter is 0
        EXPECT_EQ(format("pc", 0, nullptr, 0, nullptr, 0), "pc 0x0 (no image)\n");
    }

    TEST(CrashCatcherFrameTest, cutsTheLineToTheRoomThereIs)
    {
        const std::string text = format("#3", 0x104a3c120, "/x/libosg.162.dylib", 0x104a00000, nullptr, 0, 10);
        EXPECT_EQ(text, "#3 0x104a");
    }

    TEST(CrashCatcherFrameTest, writesNothingWithoutRoom)
    {
        std::array<char, 1> out{ 'x' };
        EXPECT_EQ(CrashCatcher::formatFrame(std::span<char>(out.data(), 0), "pc", 1, nullptr, 0, nullptr, 0), 0u);
        EXPECT_EQ(out[0], 'x');
    }
}
