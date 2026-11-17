#include "screencapture.hpp"

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/sceneutil/workqueue.hpp>

#include <osg/Image>
#include <osg/ref_ptr>
#include <osgDB/ReaderWriter>
#include <osgDB/Registry>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
    class ScreenCaptureWorkItem : public SceneUtil::WorkItem
    {
    public:
        ScreenCaptureWorkItem(const osg::ref_ptr<osgViewer::ScreenCaptureHandler::CaptureOperation>& impl,
            const osg::Image& image, unsigned int contextId)
            : mImpl(impl)
            , mImage(new osg::Image(image))
            , mContextId(contextId)
        {
            assert(mImpl != nullptr);
        }

        void doWork() override
        {
            if (mAborted)
                return;

            try
            {
                (*mImpl)(*mImage, mContextId);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "ScreenCaptureWorkItem exception: " << e.what();
            }
        }

        void abort() override { mAborted = true; }

    private:
        const osg::ref_ptr<osgViewer::ScreenCaptureHandler::CaptureOperation> mImpl;
        const osg::ref_ptr<const osg::Image> mImage;
        const unsigned int mContextId;
        std::atomic_bool mAborted{ false };
    };
}

namespace SceneUtil
{
    osg::ref_ptr<const osg::Image> withoutAlpha(const osg::Image& image)
    {
        if (image.getPixelFormat() != GL_RGBA || image.getDataType() != GL_UNSIGNED_BYTE)
            return &image;
        osg::ref_ptr<osg::Image> rgb = new osg::Image;
        rgb->allocateImage(image.s(), image.t(), image.r(), GL_RGB, GL_UNSIGNED_BYTE);
        rgb->setOrigin(image.getOrigin());
        for (int r = 0; r < image.r(); ++r)
            for (int t = 0; t < image.t(); ++t)
            {
                const unsigned char* from = image.data(0, t, r);
                unsigned char* to = rgb->data(0, t, r);
                for (int s = 0; s < image.s(); ++s, from += 4, to += 3)
                    std::copy(from, from + 3, to);
            }
        return rgb;
    }

    std::filesystem::path writeScreenshotToFile(
        const std::filesystem::path& screenshotPath, const std::string& screenshotFormat, const osg::Image& image)
    {
        // Count screenshots.
        int shotCount = 0;

        // Find the first unused filename with a do-while
        std::ostringstream stream;
        std::string lastFileName;
        std::filesystem::path lastFilePath;
        do
        {
            // Reset the stream
            stream.str("");
            stream.clear();

            stream << "screenshot" << std::setw(3) << std::setfill('0') << shotCount++ << "." << screenshotFormat;

            lastFileName = stream.str();
            lastFilePath = screenshotPath / lastFileName;

        } while (std::filesystem::exists(lastFilePath));

        std::ofstream outStream;
        outStream.open(lastFilePath, std::ios::binary);

        osgDB::ReaderWriter* readerwriter = osgDB::Registry::instance()->getReaderWriterForExtension(screenshotFormat);
        if (!readerwriter)
        {
            Log(Debug::Error) << "Error: Can't write screenshot, no '" << screenshotFormat << "' readerwriter found";
            return std::string();
        }

        osgDB::ReaderWriter::WriteResult result = readerwriter->writeImage(*withoutAlpha(image), outStream);
        if (!result.success())
        {
            Log(Debug::Error) << "Error: Can't write screenshot: " << result.message() << " code " << result.status();
            return std::string();
        }

        return lastFileName;
    }

    WriteScreenshotToFileOperation::WriteScreenshotToFileOperation(const std::filesystem::path& screenshotPath,
        const std::string& screenshotFormat, std::function<void(std::string)> callback)
        : mScreenshotPath(screenshotPath)
        , mScreenshotFormat(screenshotFormat)
        , mCallback(std::move(callback))
    {
    }

    void WriteScreenshotToFileOperation::operator()(const osg::Image& image, const unsigned int /*contextId*/)
    {
        std::filesystem::path fileName;
        try
        {
            fileName = writeScreenshotToFile(mScreenshotPath, mScreenshotFormat, image);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to write screenshot to file with path=\"" << mScreenshotPath << "\", format=\""
                              << mScreenshotFormat << "\": " << e.what();
        }
        if (fileName.empty())
            mCallback(std::string());
        else
        {
            mCallback(Files::pathToUnicodeString(fileName));
            Log(Debug::Info) << mScreenshotPath / fileName << " has been saved";
        }
    }

    AsyncScreenCaptureOperation::AsyncScreenCaptureOperation(
        std::shared_ptr<WorkQueue> queue, osg::ref_ptr<CaptureOperation> impl)
        : mQueue(std::move(queue))
        , mImpl(std::move(impl))
    {
        assert(mQueue != nullptr);
        assert(mImpl != nullptr);
    }

    AsyncScreenCaptureOperation::~AsyncScreenCaptureOperation()
    {
        stop();
    }

    void AsyncScreenCaptureOperation::stop()
    {
        for (const std::shared_ptr<SceneUtil::WorkItem>& item : *mWorkItems.lockConst())
            item->abort();

        for (const std::shared_ptr<SceneUtil::WorkItem>& item : *mWorkItems.lockConst())
            item->waitTillDone();
    }

    void AsyncScreenCaptureOperation::operator()(const osg::Image& image, const unsigned int contextId)
    {
        std::shared_ptr<SceneUtil::WorkItem> item = std::make_shared<ScreenCaptureWorkItem>(mImpl, image, contextId);
        Log(Debug::Info) << "Screenshot captured, queued to be written";
        mQueue->addWorkItem(item);
        const auto isDone = [](const std::shared_ptr<SceneUtil::WorkItem>& v) { return v->isDone(); };
        const auto workItems = mWorkItems.lock();
        workItems->erase(std::remove_if(workItems->begin(), workItems->end(), isDone), workItems->end());
        workItems->emplace_back(std::move(item));
    }
}
