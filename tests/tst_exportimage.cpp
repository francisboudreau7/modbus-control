#include <QtTest>

#include "ExportImageDialog.h"
#include "NumberSpinBox.h"
#include "PlotCursors.h"

#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPolygonF>
#include <QPushButton>
#include <QLocale>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>

using mb::ExportImageDialog;
using mb::ExportOptions;
using mb::NumberSpinBox;
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

using Pos = ExportOptions::ReadoutPosition;

QStringList threeLines()
{
    return {QStringLiteral("C1  t = 12.0 s"), QStringLiteral("C2  t = 30.0 s"),
            QStringLiteral("dt = 18.0 s")};
}

} // namespace

class TstExportImage : public QObject
{
    Q_OBJECT

private:
    // Declared in this order: the curve is destroyed before the plot it is attached to.
    std::unique_ptr<QwtPlot> m_plot;
    std::unique_ptr<QwtPlotCurve> m_curve;

    /// Renders with `options` into a PNG in `dir` and loads it.
    QImage renderImage(const QTemporaryDir &dir, const ExportOptions &options, const QString &name,
                       PlotCursors *cursors = nullptr)
    {
        const QString path = dir.filePath(name);
        QString error;
        if (!ExportImageDialog::render(m_plot.get(), cursors, path, options, &error)) {
            qWarning("render failed: %s", qPrintable(error));
            return QImage();
        }
        return QImage(path);
    }

    static ExportOptions baseOptions()
    {
        ExportOptions o;
        o.sizePx = QSize(800, 400);
        o.dpi = 96;
        o.includeCursors = false;
        o.includeLegend = true;
        o.position = Pos::Below;
        return o;
    }

private slots:
    void initTestCase();
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

    void positionBelowGrowsImageAndKeepsPlotClear();
    void longerReadoutGrowsMore();
    void cornerPositionsKeepSize();
    void titleGrowsImage();
    void legendCanBeLeftOut();
    void bothReadoutsAreDrawn();
    void svgAndPdfWithOptions();
    void dialogReadoutChecksFollowText();
    void dialogOptionsReachTheImage();
    void dialogRemembersChoices();
    void dialogAcceptsFrenchDecimalInput();
};

void TstExportImage::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("ModbusControlTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_exportimage"));
}

void TstExportImage::init()
{
    QSettings().clear();
    m_plot = std::make_unique<QwtPlot>();
    m_plot->setTitle(QStringLiteral("Trend"));
    m_plot->setAxisScale(QwtPlot::xBottom, 0, 60);
    m_plot->setAxisScale(QwtPlot::yLeft, 0, 100);

    m_curve = std::make_unique<QwtPlotCurve>(QStringLiteral("Tag [unit]"));
    QPolygonF samples;
    samples << QPointF(0, 10) << QPointF(20, 70) << QPointF(40, 30) << QPointF(60, 90);
    m_curve->setSamples(samples);
    m_curve->attach(m_plot.get());

    m_plot->insertLegend(new QwtLegend, QwtPlot::BottomLegend);
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

    auto *width = dialog.findChild<NumberSpinBox *>(QStringLiteral("exportWidth"));
    auto *height = dialog.findChild<NumberSpinBox *>(QStringLiteral("exportHeight"));
    auto *dpi = dialog.findChild<NumberSpinBox *>(QStringLiteral("exportDpi"));
    QVERIFY(width && height && dpi);
    QCOMPARE(int(width->value()), m_plot->width());
    QCOMPARE(int(height->value()), m_plot->height());
    QCOMPARE(int(dpi->value()), 96);

    // No cursors were passed, so that option is disabled; the readout is available.
    auto *cursorsBox = dialog.findChild<QCheckBox *>(QStringLiteral("exportCursors"));
    QVERIFY(cursorsBox);
    QVERIFY(!cursorsBox->isEnabled());
    QVERIFY(!cursorsBox->isChecked());
    {
        // Any cursor counts, not only C1/C2.
        PlotCursors c3(m_plot.get());
        c3.setCursor(3, 1.0);
        ExportImageDialog d3(m_plot.get(), &c3, QString(), QString());
        QVERIFY(d3.findChild<QCheckBox *>(QStringLiteral("exportCursors"))->isEnabled());
        c3.clear();
    }
    auto *readoutBox = dialog.findChild<QCheckBox *>(QStringLiteral("cursorReadoutCheck"));
    QVERIFY(readoutBox);
    QVERIFY(readoutBox->isEnabled());
    QVERIFY(readoutBox->isChecked());
}

void TstExportImage::positionBelowGrowsImageAndKeepsPlotClear()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QImage plain = renderImage(dir, baseOptions(), QStringLiteral("plain.png"));
    QCOMPARE(plain.size(), QSize(800, 400));

    ExportOptions below = baseOptions();
    below.cursorReadout = threeLines().join(QLatin1Char('\n'));
    const QImage withBox = renderImage(dir, below, QStringLiteral("below.png"));
    QVERIFY(!withBox.isNull());
    QCOMPARE(withBox.width(), 800);
    QVERIFY2(withBox.height() > 400, "the image must grow by the readout height");

    // The plot part is untouched: no readout pixel lies inside the plot rectangle.
    QCOMPARE(countDifferences(plain, withBox, QRect(0, 0, 800, 400)), 0);
    // The box is drawn in the added strip.
    bool inkBelow = false;
    for (int y = 400; y < withBox.height() && !inkBelow; ++y)
        for (int x = 0; x < withBox.width(); ++x)
            if (withBox.pixel(x, y) != qRgb(255, 255, 255)) {
                inkBelow = true;
                break;
            }
    QVERIFY(inkBelow);
}

void TstExportImage::longerReadoutGrowsMore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ExportOptions one = baseOptions();
    one.cursorReadout = QStringLiteral("one line");
    ExportOptions many = baseOptions();
    many.cursorReadout = threeLines().join(QLatin1Char('\n'));
    const QImage a = renderImage(dir, one, QStringLiteral("a.png"));
    const QImage b = renderImage(dir, many, QStringLiteral("b.png"));
    QVERIFY(a.height() > 400);
    QVERIFY(b.height() > a.height());
}

void TstExportImage::cornerPositionsKeepSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage plain = renderImage(dir, baseOptions(), QStringLiteral("plain.png"));

    QHash<int, QImage> images;
    for (Pos pos : {Pos::TopLeft, Pos::TopRight, Pos::BottomLeft, Pos::BottomRight}) {
        ExportOptions o = baseOptions();
        o.cursorReadout = threeLines().join(QLatin1Char('\n'));
        o.position = pos;
        const QImage image = renderImage(dir, o, QStringLiteral("corner%1.png").arg(int(pos)));
        QCOMPARE(image.size(), plain.size());
        QVERIFY(countDifferences(plain, image, QRect(0, 0, 800, 400)) > 100);
        images.insert(int(pos), image);
    }
    // The boxes land in different corners.
    QVERIFY(countDifferences(plain, images[int(Pos::TopLeft)], QRect(0, 0, 400, 200)) > 100);
    QCOMPARE(countDifferences(plain, images[int(Pos::TopLeft)], QRect(500, 250, 300, 150)), 0);
    QVERIFY(countDifferences(plain, images[int(Pos::BottomRight)], QRect(500, 200, 300, 200)) > 100);
    QCOMPARE(countDifferences(plain, images[int(Pos::BottomRight)], QRect(0, 0, 300, 100)), 0);
    QVERIFY(countDifferences(plain, images[int(Pos::TopRight)], QRect(500, 0, 300, 200)) > 100);
    QVERIFY(countDifferences(plain, images[int(Pos::BottomLeft)], QRect(0, 200, 300, 200)) > 100);
}

void TstExportImage::titleGrowsImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage plain = renderImage(dir, baseOptions(), QStringLiteral("plain.png"));

    ExportOptions o = baseOptions();
    o.title = QStringLiteral("Step response");
    const QImage titled = renderImage(dir, o, QStringLiteral("titled.png"));
    QCOMPARE(titled.width(), 800);
    const int extra = titled.height() - 400;
    QVERIFY(extra > 10);

    // The title band holds the text; the plot below it is the same picture, moved down.
    bool ink = false;
    for (int y = 0; y < extra && !ink; ++y)
        for (int x = 0; x < titled.width(); ++x)
            if (titled.pixel(x, y) != qRgb(255, 255, 255)) {
                ink = true;
                break;
            }
    QVERIFY(ink);
    int diff = 0;
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 800; ++x)
            if (plain.pixel(x, y) != titled.pixel(x, y + extra))
                ++diff;
    QCOMPARE(diff, 0);

    // A blank title adds nothing.
    o.title = QStringLiteral("   ");
    QCOMPARE(renderImage(dir, o, QStringLiteral("blank.png")).size(), QSize(800, 400));
}

void TstExportImage::legendCanBeLeftOut()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ExportOptions with = baseOptions();
    ExportOptions without = baseOptions();
    without.includeLegend = false;
    const QImage a = renderImage(dir, with, QStringLiteral("with.png"));
    const QImage b = renderImage(dir, without, QStringLiteral("without.png"));
    QCOMPARE(a.size(), b.size());
    QVERIFY(countDifferences(a, b, QRect(0, 0, 800, 400)) > 50);
}

void TstExportImage::bothReadoutsAreDrawn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ExportOptions cursorOnly = baseOptions();
    cursorOnly.cursorReadout = QStringLiteral("C1  t = 1.0 s");
    ExportOptions both = cursorOnly;
    both.analysisReadout = QStringLiteral("K = 2.5\nTau = 4 s");
    ExportOptions analysisOnly = baseOptions();
    analysisOnly.analysisReadout = both.analysisReadout;

    const QImage a = renderImage(dir, cursorOnly, QStringLiteral("a.png"));
    const QImage b = renderImage(dir, both, QStringLiteral("b.png"));
    const QImage c = renderImage(dir, analysisOnly, QStringLiteral("c.png"));
    QVERIFY(b.height() > a.height());
    QVERIFY(b.height() > c.height());
    QVERIFY(c.height() > 400);
}

void TstExportImage::svgAndPdfWithOptions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ExportOptions o = baseOptions();
    o.title = QStringLiteral("Title");
    o.cursorReadout = QStringLiteral("C1  t = 1.0 s");
    o.analysisReadout = QStringLiteral("K = 2.5");
    QString error;
    const QString svg = dir.filePath(QStringLiteral("o.svg"));
    const QString pdf = dir.filePath(QStringLiteral("o.pdf"));
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, svg, o, &error), qPrintable(error));
    QVERIFY2(ExportImageDialog::render(m_plot.get(), nullptr, pdf, o, &error), qPrintable(error));
    QFile s(svg);
    QVERIFY(s.open(QIODevice::ReadOnly));
    QVERIFY(s.readAll().contains("<svg"));
    QFile p(pdf);
    QVERIFY(p.open(QIODevice::ReadOnly));
    QVERIFY(p.read(5).startsWith("%PDF"));
}

void TstExportImage::dialogReadoutChecksFollowText()
{
    {
        ExportImageDialog d(m_plot.get(), nullptr, QStringLiteral("C1"), QString());
        auto *cur = d.findChild<QCheckBox *>(QStringLiteral("cursorReadoutCheck"));
        auto *ana = d.findChild<QCheckBox *>(QStringLiteral("analysisReadoutCheck"));
        auto *pos = d.findChild<QComboBox *>(QStringLiteral("positionCombo"));
        QVERIFY(cur && ana && pos);
        QVERIFY(cur->isEnabled() && cur->isChecked());
        QVERIFY(!ana->isEnabled() && !ana->isChecked());
        QVERIFY(pos->isEnabled());
        QCOMPARE(pos->currentIndex(), 0); // Below the graph
        QCOMPARE(pos->count(), 5);
        cur->setChecked(false);
        QVERIFY(!pos->isEnabled());
    }
    {
        ExportImageDialog d(m_plot.get(), nullptr, QString(), QStringLiteral("K = 1"));
        auto *cur = d.findChild<QCheckBox *>(QStringLiteral("cursorReadoutCheck"));
        auto *ana = d.findChild<QCheckBox *>(QStringLiteral("analysisReadoutCheck"));
        QVERIFY(!cur->isEnabled() && !cur->isChecked());
        QVERIFY(ana->isEnabled() && ana->isChecked());
    }
    {
        ExportImageDialog d(m_plot.get(), nullptr, QString(), QString());
        auto *pos = d.findChild<QComboBox *>(QStringLiteral("positionCombo"));
        QVERIFY(!pos->isEnabled());
        auto *legend = d.findChild<QCheckBox *>(QStringLiteral("legendCheck"));
        QVERIFY(legend && legend->isChecked());
        auto *title = d.findChild<QLineEdit *>(QStringLiteral("titleEdit"));
        QVERIFY(title && title->text().isEmpty());
    }
}

void TstExportImage::dialogOptionsReachTheImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto exportWith = [&](bool cursorOn, bool analysisOn, const QString &title,
                                const QString &name) {
        ExportImageDialog d(m_plot.get(), nullptr, QStringLiteral("C1  t = 1 s"),
                            QStringLiteral("K = 2\nTau = 3 s"));
        d.findChild<QCheckBox *>(QStringLiteral("cursorReadoutCheck"))->setChecked(cursorOn);
        d.findChild<QCheckBox *>(QStringLiteral("analysisReadoutCheck"))->setChecked(analysisOn);
        d.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(title);
        d.findChild<QLineEdit *>(QStringLiteral("exportPath"))->setText(dir.filePath(name));
        d.accept();
        return QImage(dir.filePath(name));
    };
    const QImage none = exportWith(false, false, QString(), QStringLiteral("none.png"));
    const QImage cursor = exportWith(true, false, QString(), QStringLiteral("cursor.png"));
    const QImage analysis = exportWith(false, true, QString(), QStringLiteral("analysis.png"));
    const QImage both = exportWith(true, true, QString(), QStringLiteral("both.png"));
    const QImage titled = exportWith(false, false, QStringLiteral("T"), QStringLiteral("t.png"));
    QCOMPARE(none.size(), m_plot->size());
    QVERIFY(cursor.height() > none.height());
    QVERIFY(analysis.height() > none.height());
    QVERIFY(both.height() > cursor.height());
    QVERIFY(both.height() > analysis.height());
    QVERIFY(titled.height() > none.height());
}

void TstExportImage::dialogRemembersChoices()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        ExportImageDialog d(m_plot.get(), nullptr, QStringLiteral("C1"), QStringLiteral("K = 1"));
        d.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->setText(QStringLiteral("My graph"));
        d.findChild<QCheckBox *>(QStringLiteral("legendCheck"))->setChecked(false);
        d.findChild<QCheckBox *>(QStringLiteral("analysisReadoutCheck"))->setChecked(false);
        d.findChild<QComboBox *>(QStringLiteral("positionCombo"))->setCurrentIndex(3);
        d.findChild<QLineEdit *>(QStringLiteral("exportPath"))
            ->setText(dir.filePath(QStringLiteral("x.png")));
        d.accept();
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("x.png"))));
    }
    ExportImageDialog again(m_plot.get(), nullptr, QStringLiteral("C1"), QStringLiteral("K = 1"));
    QCOMPARE(again.findChild<QLineEdit *>(QStringLiteral("titleEdit"))->text(),
             QStringLiteral("My graph"));
    QVERIFY(!again.findChild<QCheckBox *>(QStringLiteral("legendCheck"))->isChecked());
    QVERIFY(!again.findChild<QCheckBox *>(QStringLiteral("analysisReadoutCheck"))->isChecked());
    QVERIFY(again.findChild<QCheckBox *>(QStringLiteral("cursorReadoutCheck"))->isChecked());
    QCOMPARE(again.findChild<QComboBox *>(QStringLiteral("positionCombo"))->currentIndex(), 3);

    // A check box that is disabled this time keeps the stored choice for later.
    ExportImageDialog noAnalysis(m_plot.get(), nullptr, QStringLiteral("C1"), QString());
    noAnalysis.findChild<QLineEdit *>(QStringLiteral("exportPath"))
        ->setText(dir.filePath(QStringLiteral("y.png")));
    noAnalysis.accept();
    QVERIFY(!QSettings().value(QStringLiteral("exportImage/analysisReadout"), true).toBool());
}

void TstExportImage::dialogAcceptsFrenchDecimalInput()
{
    QLocale::setDefault(QLocale(QLocale::French, QLocale::Canada));
    {
        ExportImageDialog d(m_plot.get(), nullptr, QString());
        auto *dpi = d.findChild<NumberSpinBox *>(QStringLiteral("exportDpi"));
        auto *width = d.findChild<NumberSpinBox *>(QStringLiteral("exportWidth"));
        QVERIFY(dpi && width);
        dpi->selectAll();
        QTest::keyClicks(dpi, QStringLiteral("150"));
        dpi->interpretText();
        QCOMPARE(dpi->value(), 150.0);
        // The same class with a decimal place: both marks parse.
        width->setDecimals(1);
        width->selectAll();
        QTest::keyClicks(width, QStringLiteral("640,5"));
        width->interpretText();
        QCOMPARE(width->value(), 640.5);
        width->selectAll();
        QTest::keyClicks(width, QStringLiteral("700.5"));
        width->interpretText();
        QCOMPARE(width->value(), 700.5);
    }
    QLocale::setDefault(QLocale::c());
}

QTEST_MAIN(TstExportImage)
#include "tst_exportimage.moc"
