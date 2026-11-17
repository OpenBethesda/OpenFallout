#include <osg/Notify>
#include <osg/ref_ptr>

#include <gtest/gtest.h>

#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // Keeps every message the handler is given
    class Collect : public osg::NotifyHandler
    {
    public:
        void notify(osg::NotifySeverity severity, const char* message) override
        {
            const std::lock_guard lock(mMutex);
            if (*message != '\0')
                mMessages.emplace_back(std::to_string(severity) + " " + message);
        }

        std::mutex mMutex;
        std::vector<std::string> mMessages;
    };

    // Several threads write OpenSceneGraph log lines at once. The Metal build's draw thread writes many (a state that
    // OpenGL ES does not have, on every frame it is used), while other threads load; one stream shared by all threads
    // turned the lines to garbage and could crash the engine
    TEST(OsgNotifyThreadsTest, everyLineOfEveryThreadArrivesWholeWithItsSeverity)
    {
#ifndef OPENFALLOUT_METAL
        GTEST_SKIP() << "the desktop OpenGL build uses an OpenSceneGraph without the project's patches";
#endif
        const osg::ref_ptr<osg::NotifyHandler> before = osg::getNotifyHandler();
        const osg::NotifySeverity level = osg::getNotifyLevel();
        osg::setNotifyLevel(osg::INFO);
        osg::ref_ptr<Collect> collect = new Collect;
        osg::setNotifyHandler(collect);

        constexpr int threads = 8;
        constexpr int lines = 2000;
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([t] {
                // Two severities, so a change of severity in one thread meets the lines of another
                const osg::NotifySeverity severity = t % 2 == 0 ? osg::WARN : osg::INFO;
                for (int i = 0; i < lines; ++i)
                    osg::notify(severity) << "thread " << t << " line " << i << std::endl;
            });
        for (std::thread& worker : workers)
            worker.join();

        osg::setNotifyHandler(before.get());
        osg::setNotifyLevel(level);

        std::set<std::string> expected;
        for (int t = 0; t < threads; ++t)
            for (int i = 0; i < lines; ++i)
                expected.insert(std::to_string(t % 2 == 0 ? osg::WARN : osg::INFO) + " thread " + std::to_string(t)
                    + " line " + std::to_string(i) + "\n");
        std::size_t whole = 0;
        std::string firstBroken;
        for (const std::string& message : collect->mMessages)
        {
            if (expected.erase(message) == 1)
                ++whole;
            else if (firstBroken.empty())
                firstBroken = message;
        }
        EXPECT_EQ(whole, static_cast<std::size_t>(threads * lines)) << "first broken line: " << firstBroken;
        EXPECT_EQ(collect->mMessages.size(), static_cast<std::size_t>(threads * lines));
    }
}
