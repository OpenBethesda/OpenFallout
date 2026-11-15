#include "debugging.hpp"

#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>

#include <boost/iostreams/stream.hpp>

#include <components/crashcatcher/crashcatcher.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/misc/strings/lower.hpp>

#include <SDL_messagebox.h>

namespace Debug
{
    static LogListener logListener;
    void setLogListener(LogListener listener)
    {
        logListener = std::move(listener);
    }

    namespace
    {
        class DebugOutputBase : public boost::iostreams::sink
        {
        public:
            virtual std::streamsize write(const char* str, std::streamsize size)
            {
                if (size <= 0)
                    return size;
                std::string_view msg{ str, static_cast<size_t>(size) };

                // Skip debug level marker
                Level level = All;
                if (Log::sWriteLevel)
                {
                    level = getLevelMarker(msg[0]);
                    msg = msg.substr(1);
                }

                char prefix[32];
                std::size_t prefixSize;
                {
                    prefix[0] = '[';
                    const auto now = std::chrono::system_clock::now();
                    const auto time = std::chrono::system_clock::to_time_t(now);
                    tm timeInfo{};
                    (void)localtime_r(&time, &timeInfo);
                    prefixSize = std::strftime(prefix + 1, sizeof(prefix) - 1, "%T", &timeInfo) + 1;
                    char levelLetter = " EWIVD*"[int(level)];
                    const auto ms
                        = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                    prefixSize += snprintf(prefix + prefixSize, sizeof(prefix) - prefixSize, ".%03u %c] ",
                        static_cast<unsigned>(ms % 1000), levelLetter);
                }

                while (!msg.empty())
                {
                    if (msg[0] == 0)
                        break;
                    size_t lineSize = 1;
                    while (lineSize < msg.size() && msg[lineSize - 1] != '\n')
                        lineSize++;
                    writeImpl(prefix, prefixSize, level);
                    writeImpl(msg.data(), lineSize, level);
                    if (logListener)
                        logListener(
                            level, std::string_view(prefix, prefixSize), std::string_view(msg.data(), lineSize));
                    msg = msg.substr(lineSize);
                }

                return size;
            }

            virtual ~DebugOutputBase() = default;

        protected:
            static Level getLevelMarker(char marker)
            {
                if (0 <= marker && static_cast<unsigned>(marker) < static_cast<unsigned>(All))
                    return static_cast<Level>(marker);
                return All;
            }

            virtual std::streamsize writeImpl(const char* str, std::streamsize size, Level debugLevel)
            {
                return size;
            }
        };

        struct Record
        {
            std::string mValue;
            Level mLevel;
        };

        std::deque<Record> globalBuffer;

        Color getColor(Level level)
        {
            switch (level)
            {
                case Error:
                    return Red;
                case Warning:
                    return Yellow;
                case Info:
                    return Reset;
                case Verbose:
                    return DarkGray;
                case Debug:
                    return DarkGray;
                case All:
                    return Reset;
            }
            return Reset;
        }

        bool useColoredOutput()
        {
            return std::getenv("TERM") != nullptr && std::getenv("NO_COLOR") == nullptr && isatty(fileno(stderr));
        }

        class Identity
        {
        public:
            explicit Identity(std::ostream& stream)
                : mStream(stream)
            {
            }

            void write(const char* str, std::streamsize size, Level /*level*/)
            {
                mStream.write(str, size);
                mStream.flush();
            }

        private:
            std::ostream& mStream;
        };

        class Coloured
        {
        public:
            explicit Coloured(std::ostream& stream)
                : mStream(stream)
                // TODO: check which stream is stderr?
                , mUseColor(useColoredOutput())
            {
            }

            void write(const char* str, std::streamsize size, Level level)
            {
                if (mUseColor)
                    mStream << "\033[0;" << getColor(level) << 'm';
                mStream.write(str, size);
                if (mUseColor)
                    mStream << "\033[0;" << Reset << 'm';
                mStream.flush();
            }

        private:
            std::ostream& mStream;
            bool mUseColor;
        };

        class Buffer
        {
        public:
            explicit Buffer(std::size_t capacity, std::deque<Record>& buffer)
                : mCapacity(capacity)
                , mBuffer(buffer)
            {
            }

            void write(const char* str, std::streamsize size, Level debugLevel)
            {
                while (mBuffer.size() >= mCapacity)
                    mBuffer.pop_front();
                mBuffer.push_back(Record{ std::string(str, size), debugLevel });
            }

        private:
            std::size_t mCapacity;
            std::deque<Record>& mBuffer;
        };

        template <class First, class Second>
        class Tee : public DebugOutputBase
        {
        public:
            explicit Tee(First first, Second second)
                : mFirst(first)
                , mSecond(second)
            {
            }

            std::streamsize writeImpl(const char* str, std::streamsize size, Level debugLevel) override
            {
                mFirst.write(str, size, debugLevel);
                mSecond.write(str, size, debugLevel);
                return size;
            }

        private:
            First mFirst;
            Second mSecond;
        };

        Level toLevel(std::string_view value)
        {
            if (value == "ERROR")
                return Error;
            if (value == "WARNING")
                return Warning;
            if (value == "INFO")
                return Info;
            if (value == "VERBOSE")
                return Verbose;
            if (value == "DEBUG")
                return Debug;

            return Verbose;
        }

        static std::unique_ptr<std::ostream> rawStdout = nullptr;
        static std::unique_ptr<std::ostream> rawStderr = nullptr;
        static std::unique_ptr<std::mutex> rawStderrMutex = nullptr;
        static std::ofstream logfile;

        static boost::iostreams::stream_buffer<Tee<Identity, Coloured>> standardOut;
        static boost::iostreams::stream_buffer<Tee<Identity, Coloured>> standardErr;
        static boost::iostreams::stream_buffer<Tee<Buffer, Coloured>> bufferedOut;
        static boost::iostreams::stream_buffer<Tee<Buffer, Coloured>> bufferedErr;
    }

    std::ostream& getRawStdout()
    {
        return rawStdout ? *rawStdout : std::cout;
    }

    std::ostream& getRawStderr()
    {
        return rawStderr ? *rawStderr : std::cerr;
    }

    Misc::Locked<std::ostream&> getLockedRawStderr()
    {
        return Misc::Locked<std::ostream&>(*rawStderrMutex, getRawStderr());
    }

    Level getDebugLevel()
    {
        if (const char* env = getenv("OPENFALLOUT_DEBUG_LEVEL"))
            return toLevel(env);

        return Verbose;
    }

    Level getRecastMaxLogLevel()
    {
        if (const char* env = getenv("OPENFALLOUT_RECAST_MAX_LOG_LEVEL"))
            return toLevel(env);

        return Error;
    }

    void setupLogging(const std::filesystem::path& logDir, std::string_view appName)
    {
        Log::sMinDebugLevel = getDebugLevel();
        Log::sWriteLevel = true;

        const std::string logName = Misc::StringUtils::lowerCase(appName) + ".log";
        logfile.open(logDir / logName, std::ios::out);

        Identity log(logfile);

        for (const Record& v : globalBuffer)
            log.write(v.mValue.data(), v.mValue.size(), v.mLevel);

        globalBuffer.clear();

        standardOut.open(Tee(log, Coloured(*rawStdout)));
        standardErr.open(Tee(log, Coloured(*rawStderr)));

        std::cout.rdbuf(&standardOut);
        std::cerr.rdbuf(&standardErr);
    }

    int wrapApplication(
        int (*innerApplication)(int argc, char* argv[]), int argc, char* argv[], std::string_view appName)
    {
        rawStdout = std::make_unique<std::ostream>(std::cout.rdbuf());
        rawStderr = std::make_unique<std::ostream>(std::cerr.rdbuf());
        rawStderrMutex = std::make_unique<std::mutex>();

        constexpr std::size_t bufferCapacity = 1024;

        bufferedOut.open(Tee(Buffer(bufferCapacity, globalBuffer), Coloured(*rawStdout)));
        bufferedErr.open(Tee(Buffer(bufferCapacity, globalBuffer), Coloured(*rawStderr)));

        std::cout.rdbuf(&bufferedOut);
        std::cerr.rdbuf(&bufferedErr);

        int ret = 0;
        try
        {
            if (const auto env = std::getenv("OPENFALLOUT_DISABLE_CRASH_CATCHER");
                env == nullptr || Misc::StringUtils::toNumeric<int>(env, 0) == 0)
            {
                const std::string crashLogName = Misc::StringUtils::lowerCase(appName) + "-crash.log";
                // install the crash handler as soon as possible.
                crashCatcherInstall(argc, argv, std::filesystem::temp_directory_path() / crashLogName);
                ret = innerApplication(argc, argv);
            }
            else
                ret = innerApplication(argc, argv);
        }
        catch (const std::exception& e)
        {
#if (defined(__APPLE__) || defined(__linux) || defined(__unix) || defined(__posix))
            if (!isatty(fileno(stdin)))
#endif
                SDL_ShowSimpleMessageBox(0, (std::string(appName) + ": Fatal error").c_str(), e.what(), nullptr);

            Log(Debug::Error) << "Fatal error: " << e.what();

            ret = 1;
        }

        // Restore cout and cerr
        std::cout.rdbuf(rawStdout->rdbuf());
        std::cerr.rdbuf(rawStderr->rdbuf());

        Log::sMinDebugLevel = All;
        Log::sWriteLevel = false;

        return ret;
    }
}
