#include "application.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/scalableicon.hpp>

namespace Platform
{
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
    {
        Debug::Level level;
        switch (type)
        {
            case QtDebugMsg:
                level = Debug::Debug;
                break;
            case QtInfoMsg:
                level = Debug::Info;
                break;
            case QtWarningMsg:
                level = Debug::Warning;
                break;
            case QtCriticalMsg:
                level = Debug::Error;
                break;
            case QtFatalMsg:
                level = Debug::Error;
                break;
            default:
                level = Debug::Debug;
                break;
        }

        Log(level) << qUtf8Printable(qFormatLogMessage(type, context, msg));
    }

    Application::Application(int& argc, char* argv[])
        : QApplication(argc, argv)
    {
        qInstallMessageHandler(qtMessageHandler);
    }

    bool Application::notify(QObject* receiver, QEvent* event)
    {
        try
        {
            if (event->type() == QEvent::ThemeChange || event->type() == QEvent::PaletteChange)
            {
                Misc::ScalableIcon::updateAllIcons();
            }

            return QApplication::notify(receiver, event);
        }
        catch (const std::exception& exception)
        {
            Log(Debug::Error) << "An exception has been caught: " << exception.what();
        }

        return false;
    }
}
