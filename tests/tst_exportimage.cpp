#include <QtTest>

#include "ExportImageDialog.h"
#include "PlotCursors.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPolygonF>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <memory>

using mb::ExportImageDialog;
using mb::PlotCursors;

namespace {

bool hasNonWhitePixel(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) != qRgb(255, 255, 255))
                return true;
    return false;
}

/// Number of pixels that differ between two images within the given rectangle.
int countDifferences(const QImage &a, const QImage &b, const QRect &area)
{
    int diff = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y))
                ++diff;
    return diff;
}

} // namespace

class TstExportImage : public QObject
{
    Q_OBJECT

private:
    // Declared in this order: the curve is destroyed before the plot it is attached to.
    std::unique_ptr<QwtPlot> m_plot;
    std::unique_ptr<QwtPlotCurve> m_curve;

private slots:
    void init();
    void cleanup();

    void pngRendersAtRequestedSize();
    void readoutChangesPngOutput();
    void svgIsWritten();
    void pdfIsWritten();
    void unsupportedExtensionFails();
    void invalidSizeOrDpiFails();
    void missingDirectoryFailsWithPath();
    void nullCursorsRenderWithoutCursors();
    void cursorsObjectRenders();
    void dialogOkDisabledUntilPath();
};

void TstExportImage::init()
{
    m_plot = std::make_unique<QwtPlot>();
    m_plot->setTitle(QStringLiteral("Trend"));
    m_plot->setAxisScale(QwtPlot::xBottom, 0, 60);
    m_plot->setAxisScale(QwtPlot::yLeft, 0, 100);

    m_curve = std::make_unique<QwtPlotCurve>(QStringLiteral("Tag [unit]"));
    QPolygonF samples;
    samples << QPointF(0, 10) << QPointF(20, 70) << QPointF(40, 30) << QPointF(60, 90);
    m_curve->setSamples(samples);
    m_curve->attach(m_plot.get());

    m_plot->resize(800, 400);
    m_plot->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_plot.get()));
}

void TstExportImage::cleanup()
{
    m_curve.reset();
    m_plot.reset();
}

void TstExportImage::pngRendersAtRequestedSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.png"));

    QString error;
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(1024, 512), 96, false,
                                       QString(), &error),
             qPrintable(error));
    QVERIFY(QFile::exists(path));

    const QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(1024, 512));
    QVERIFY(hasNonWhitePixel(image));
}

void TstExportImage::readoutChangesPngOutput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plainPath = dir.filePath(QStringLiteral("plain.png"));
    const QString readoutPath = dir.filePath(QStringLiteral("readout.png"));

    QString error;
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, plainPath, QSize(1024, 512), 96,
                                       false, QString(), &error),
             qPrintable(error));
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, readoutPath, QSize(1024, 512), 96,
                                       false, QStringLiteral("C1  t = 12.0 s\nC2  t = 30.0 s"),
                                       &error),
             qPrintable(error));

    const QImage plain(plainPath);
    const QImage withReadout(readoutPath);
    QCOMPARE(withReadout.size(), plain.size());

    // The readout sits in the top-left of the canvas, inside this area.
    const int diff = countDifferences(plain, withReadout, QRect(0, 0, 400, 200));
    QVERIFY2(diff > 100, qPrintable(QStringLiteral("only %1 pixels differ").arg(diff)));
}

void TstExportImage::svgIsWritten()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.svg"));

    QString error;
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(800, 400), 96, false,
                                       QStringLiteral("C1  t = 12.0 s"), &error),
             qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.startsWith("<?xml") || data.contains("<svg"));
}

void TstExportImage::pdfIsWritten()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.pdf"));

    QString error;
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(800, 400), 96, false,
                                       QStringLiteral("C1  t = 12.0 s"), &error),
             qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(5).startsWith("%PDF"));
}

void TstExportImage::unsupportedExtensionFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.bmp"));

    QString error;
    QVERIFY(!ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(800, 400), 96, false,
                                       QString(), &error));
    QVERIFY(error.contains(QStringLiteral("Unsupported file type")));
    QVERIFY(error.contains(QStringLiteral(".bmp")));
    QVERIFY(!QFile::exists(path));
}

void TstExportImage::invalidSizeOrDpiFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.png"));

    QString error;
    QVERIFY(!ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(15, 512), 96, false,
                                       QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(1024, 20001), 96, false,
                                       QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(1024, 512), 35, false,
                                       QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(1024, 512), 1201,
                                       false, QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(path));
}

void TstExportImage::missingDirectoryFailsWithPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("no-such-folder/plot.png"));

    QString error;
    QVERIFY(!ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(800, 400), 96, false,
                                       QString(), &error));
    QVERIFY(error.contains(path));
}

void TstExportImage::nullCursorsRenderWithoutCursors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.png"));

    QString error;
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, path, QSize(640, 320), 96, false,
                                       QString(), &error),
             qPrintable(error));
    QVERIFY(QFile::exists(path));
}

void TstExportImage::cursorsObjectRenders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PlotCursors cursors(m_plot.get());

    QString error;
    const QString hidden = dir.filePath(QStringLiteral("hidden.png"));
    QVERIFY2(ExportImageDialog::render(m_plot.get(), &cursors, hidden, QSize(640, 320), 96, false,
                                       QString(), &error),
             qPrintable(error));
    const QString shown = dir.filePath(QStringLiteral("shown.png"));
    QVERIFY2(ExportImageDialog::render(m_plot.get(), &cursors, shown, QSize(640, 320), 96, true,
                                       QString(), &error),
             qPrintable(error));
    QVERIFY(QFile::exists(hidden));
    QVERIFY(QFile::exists(shown));
}

void TstExportImage::dialogOkDisabledUntilPath()
{
    ExportImageDialog dialog(m_plot.get(), nullptr, QStringLiteral("C1  t = 12.0 s"));

    auto *buttons = dialog.findChild<QDialogButtonBox *>(QStringLiteral("exportButtons"));
    QVERIFY(buttons);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    QVERIFY(ok);
    auto *path = dialog.findChild<QLineEdit *>(QStringLiteral("exportPath"));
    QVERIFY(path);

    QVERIFY(!ok->isEnabled());
    path->setText(QStringLiteral("out.png"));
    QVERIFY(ok->isEnabled());
    path->clear();
    QVERIFY(!ok->isEnabled());

    auto *width = dialog.findChild<QSpinBox *>(QStringLiteral("exportWidth"));
    auto *height = dialog.findChild<QSpinBox *>(QStringLiteral("exportHeight"));
    auto *dpi = dialog.findChild<QSpinBox *>(QStringLiteral("exportDpi"));
    QVERIFY(width && height && dpi);
    QCOMPARE(width->value(), m_plot->width());
    QCOMPARE(height->value(), m_plot->height());
    QCOMPARE(dpi->value(), 96);

    // No cursors were passed, so that option is disabled; the readout is available.
    auto *cursorsBox = dialog.findChild<QCheckBox *>(QStringLiteral("exportCursors"));
    QVERIFY(cursorsBox);
    QVERIFY(!cursorsBox->isEnabled());
    QVERIFY(!cursorsBox->isChecked());
    auto *readoutBox = dialog.findChild<QCheckBox *>(QStringLiteral("exportReadout"));
    QVERIFY(readoutBox);
    QVERIFY(readoutBox->isEnabled());
    QVERIFY(readoutBox->isChecked());
}

QTEST_MAIN(TstExportImage)
#include "tst_exportimage.moc"
