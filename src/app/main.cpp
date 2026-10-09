#include "Version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMainWindow>

#include <qwt_plot.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Modbus Control"));
    QApplication::setApplicationVersion(mb::versionString());

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smokeOption(QStringLiteral("smoke"),
                                   QStringLiteral("Construct the main window, process events once and exit."));
    parser.addOption(smokeOption);
    parser.process(app);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Modbus Control"));
    auto *plot = new QwtPlot(&window);
    plot->setAxisTitle(QwtPlot::xBottom, QStringLiteral("Time (s)"));
    plot->setAxisTitle(QwtPlot::yLeft, QStringLiteral("Value"));
    window.setCentralWidget(plot);
    window.resize(1000, 600);

    if (parser.isSet(smokeOption)) {
        window.show();
        QApplication::processEvents();
        return 0;
    }

    window.show();
    return app.exec();
}