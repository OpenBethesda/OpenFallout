#ifndef MAINDIALOG_H
#define MAINDIALOG_H

#ifndef Q_MOC_RUN
#include <components/process/processinvoker.hpp>

#include <components/config/gamesettings.hpp>
#include <components/config/launchersettings.hpp>

#endif
#include "ui_mainwindow.h"

class QListWidgetItem;
class QStackedWidget;
class QStringListModel;
class QString;

namespace Files
{
    struct ConfigurationManager;
}

namespace Launcher
{
    class GraphicsPage;
    class DataFilesPage;
    class SettingsPage;

    enum FirstRunDialogResult
    {
        FirstRunDialogResultFailure,
        FirstRunDialogResultContinue
    };

    class MainDialog : public QMainWindow, private Ui::MainWindow
    {
        Q_OBJECT

    public:
        explicit MainDialog(const Files::ConfigurationManager& configurationManager, QWidget* parent = nullptr);
        ~MainDialog() override;

        FirstRunDialogResult showFirstRunDialog();

        bool reloadSettings();
        bool writeSettings();

    public slots:
        void enableDataPage();
        void enableGraphicsPage();
        void enableSettingsPage();
        void play();
        void help();

    protected:
        bool event(QEvent* event) override;

    private:
        bool setup();

        void createIcons();
        void createPages();

        bool setupLauncherSettings();
        bool setupGameSettings();
        bool setupGraphicsSettings();
        bool setupGameData();

        void setVersionLabel();

        void loadSettings();
        void saveSettings();

        inline bool startProgram(const QString& name, bool detached = false)
        {
            return startProgram(name, QStringList(), detached);
        }
        bool startProgram(const QString& name, const QStringList& arguments, bool detached = false);

        void closeEvent(QCloseEvent* event) override;

        GraphicsPage* mGraphicsPage;
        DataFilesPage* mDataFilesPage;
        SettingsPage* mSettingsPage;

        Process::ProcessInvoker* mGameInvoker;

        const Files::ConfigurationManager& mCfgMgr;

        Config::GameSettings mGameSettings;
        Config::LauncherSettings mLauncherSettings;
    };
}
#endif
