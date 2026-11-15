#include <QApplication>
#include <QEvent>

namespace Platform
{
    class Application : public QApplication
    {
    public:
        Application(int& argc, char* argv[]);

    private:
        bool notify(QObject* receiver, QEvent* event) override;
    };
}
