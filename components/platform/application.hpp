#include <QApplication>
#include <QEvent>

namespace Platform
{
    class Application : public QApplication
    {
        Q_OBJECT

    public:
        Application(int& argc, char* argv[]);

    signals:
        void darkModeChanged(bool);

    private:
        bool notify(QObject* receiver, QEvent* event) override;

        std::string mCurrentStyle{};
        std::string mInitialStyle{};
        bool mDarkMode{ false };
    };
}
