#include "AppController.h"
#include "MainWindow.h"
#include "MetaTypes.h"
#include "Version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ModbusControl"));
    QApplication::setApplicationName(QStringLiteral("Modbus Control"));
    QApplication::setApplicationVersion(mb::versionString());
    mb::registerMetaTypes();

    // Small sizes use the text-free variant of the logo (see assets/icon).
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/logo-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smokeOption(QStringLiteral("smoke"),
                                   QStringLiteral("Construct the main window, process events once and exit."));
    parser.addOption(smokeOption);
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file (.mbproj) to open."), QStringLiteral("[project]"));
    parser.process(app);

    mb::MainWindow window;
    mb::AppController controller(&window);
    if (!parser.positionalArguments().isEmpty())
        controller.openProject(parser.positionalArguments().constFirst());

    window.show();
    if (parser.isSet(smokeOption)) {
        QApplication::processEvents();
        return 0;
    }
    return app.exec();
}