#include <components/debug/debugging.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>

namespace
{
    // Sets or removes OPENFALLOUT_HEADLESS for one test and puts back what was there
    struct HeadlessEnvironment
    {
        std::optional<std::string> mBefore;

        explicit HeadlessEnvironment(const char* value)
        {
            if (const char* before = std::getenv("OPENFALLOUT_HEADLESS"))
                mBefore = before;
            if (value == nullptr)
                unsetenv("OPENFALLOUT_HEADLESS");
            else
                setenv("OPENFALLOUT_HEADLESS", value, 1);
        }

        ~HeadlessEnvironment()
        {
            if (mBefore)
                setenv("OPENFALLOUT_HEADLESS", mBefore->c_str(), 1);
            else
                unsetenv("OPENFALLOUT_HEADLESS");
        }
    };

    TEST(DebugHeadlessRun, aRunIsHeadlessWhenTheEnvironmentSaysOne)
    {
        const HeadlessEnvironment environment("1");
        EXPECT_TRUE(Debug::isHeadlessRun());
    }

    TEST(DebugHeadlessRun, aRunIsNotHeadlessWithoutTheVariable)
    {
        const HeadlessEnvironment environment(nullptr);
        EXPECT_FALSE(Debug::isHeadlessRun());
    }

    TEST(DebugHeadlessRun, anyOtherValueIsNotHeadless)
    {
        for (const char* value : { "", "0", "true", "11" })
        {
            const HeadlessEnvironment environment(value);
            EXPECT_FALSE(Debug::isHeadlessRun()) << "value '" << value << "'";
        }
    }
}
