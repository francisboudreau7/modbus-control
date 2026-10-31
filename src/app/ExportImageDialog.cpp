#include "ExportImageDialog.h"

#include "NumberSpinBox.h"
#include "PlotCursors.h"

#include <qwt_plot.h>
#include <qwt_plot_renderer.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSettings>
#include <QComboBox>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QSvgGenerator>
#include <QtMath>

namespace mb {

namespace {

constexpr int kMinPx = 16;
constexpr int kMaxPx = 20000;
constexpr int kMinDpi = 36;
constexpr int kMaxDpi = 1200;
const char kDirSetting[] = "export/imageDir";

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

/// Hides the cursor markers for the lifetime of the guard and always shows them again.
class MarkerHideGuard
{
public:
    explicit MarkerHideGuard(PlotCursors *cursors) : m_cursors(cursors)
    {
        if (m_cursors)
            m_cursors->setMarkersVisible(false);
    }
    ~MarkerHideGuard()
    {
        if (m_cursors)
            m_cursors->setMarkersVisible(true);
    }
    MarkerHideGuard(const MarkerHideGuard &) = delete;
    MarkerHideGuard &operator=(const MarkerHideGuard &) = delete;

private:
    PlotCursors *m_cursors;
};

/// Font of the readout box.
QFont readoutFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(9.0);
    return font;
}

/// Font of the title line.
QFont titleFont()
{
    QFont font = QGuiApplication::font();
    font.setPointSizeF(12.0);
    font.setBold(true);
    return font;
}

/// Sizes of the extra parts for one device resolution. Metrics come from a one-pixel image with
/// the same resolution as the target, so the sizes match what the painter will draw.
struct ExtraSizes {
    qreal titleHeight = 0.0; ///< band above the plot; 0 when there is no title
    qreal boxWidth = 0.0;    ///< framed text box: widest line + paddings
    qreal boxHeight = 0.0;   ///< framed text box height; 0 when there is no readout
    qreal margin = 0.0;
    qreal pad = 0.0;
    qreal lineHeight = 0.0;
};

ExtraSizes measureExtras(int dpi, const QString &title, const QStringList &readoutLines)
{
    QImage probe(1, 1, QImage::Format_ARGB32);
    const int dotsPerMeter = qRound(dpi / 0.0254);
    probe.setDotsPerMeterX(dotsPerMeter);
    probe.setDotsPerMeterY(dotsPerMeter);

    ExtraSizes e;
    const qreal scale = dpi / 96.0;
    e.margin = 10.0 * scale;
    e.pad = 6.0 * scale;
    if (!title.isEmpty()) {
        const QFontMetricsF fm(titleFont(), &probe);
        e.titleHeight = fm.lineSpacing() + 2 * e.pad;
    }
    if (!readoutLines.isEmpty()) {
        const QFontMetricsF fm(readoutFont(), &probe);
        qreal textWidth = 0.0;
        for (const QString &line : readoutLines)
            textWidth = qMax(textWidth, fm.horizontalAdvance(line));
        e.lineHeight = fm.lineSpacing();
        e.boxWidth = textWidth + 2 * e.pad;
        e.boxHeight = e.lineHeight * readoutLines.size() + 2 * e.pad;
    }
    return e;
}

/// Draws a bold title centred in `band`.
void paintTitle(QPainter *p, const QRectF &band, const QString &title)
{
    p->save();
    p->setFont(titleFont());
    p->setPen(Qt::black);
    p->setRenderHint(QPainter::TextAntialiasing, true);
    p->drawText(band, Qt::AlignCenter, title);
    p->restore();
}

/// Draws the readout lines as a framed text box with its top-left corner at `topLeft`.
void paintReadoutBox(QPainter *p, const QPointF &topLeft, const ExtraSizes &e,
                     const QStringList &lines)
{
    p->save();
    p->setFont(readoutFont());
    const QFontMetricsF fm(readoutFont(), p->device());
    const QRectF box(topLeft, QSizeF(e.boxWidth, e.boxHeight));

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);
    p->setPen(QPen(QColor(80, 80, 80), 1));
    p->setBrush(QColor(255, 255, 255, 217)); // about 85 % opaque white
    p->drawRect(box);

    p->setPen(Qt::black);
    qreal baseline = box.top() + e.pad + fm.ascent();
    for (const QString &line : lines) {
        p->drawText(QPointF(box.left() + e.pad, baseline), line);
        baseline += e.lineHeight;
    }
    p->restore();
}

/// Where the parts go inside the total image.
struct Layout {
    QSizeF total;
    QRectF plot;
    QRectF titleBand;
    QPointF belowPos; ///< top-left of the readout box for Below
};

Layout makeLayout(const QSize &plotPx, const ExtraSizes &e, bool below)
{
    Layout l;
    const qreal belowHeight = below && e.boxHeight > 0 ? e.boxHeight + 2 * e.margin : 0.0;
    l.total = QSizeF(plotPx.width(), plotPx.height() + e.titleHeight + belowHeight);
    l.titleBand = QRectF(0, 0, plotPx.width(), e.titleHeight);
    l.plot = QRectF(0, e.titleHeight, plotPx.width(), plotPx.height());
    l.belowPos = QPointF(e.margin, l.plot.bottom() + e.margin);
    return l;
}

/// Everything drawn around and on top of the rendered plot.
void paintExtras(QPainter *p, const QwtPlot *plot, const Layout &l, const ExtraSizes &e,
                 const ExportOptions &o, const QStringList &lines)
{
    if (e.titleHeight > 0)
        paintTitle(p, l.titleBand, o.title);
    if (lines.isEmpty())
        return;
    if (o.position == ExportOptions::ReadoutPosition::Below) {
        ExtraSizes capped = e; // never wider than the image
        capped.boxWidth = qMin(e.boxWidth, l.plot.width() - 2 * e.margin);
        paintReadoutBox(p, l.belowPos, capped, lines);
        return;
    }
    // Inside the canvas area: its position comes from the on-screen plot, scaled to the target.
    const qreal sx = plot->width() > 0 ? l.plot.width() / plot->width() : 1.0;
    const qreal sy = plot->height() > 0 ? l.plot.height() / plot->height() : 1.0;
    const QRect c = plot->canvas()->geometry();
    const QRectF canvas(l.plot.left() + c.left() * sx, l.plot.top() + c.top() * sy, c.width() * sx,
                        c.height() * sy);
    const bool right = o.position == ExportOptions::ReadoutPosition::TopRight
        || o.position == ExportOptions::ReadoutPosition::BottomRight;
    const bool bottom = o.position == ExportOptions::ReadoutPosition::BottomLeft
        || o.position == ExportOptions::ReadoutPosition::BottomRight;
    const qreal x = right ? canvas.right() - e.margin - e.boxWidth : canvas.left() + e.margin;
    const qreal y = bottom ? canvas.bottom() - e.margin - e.boxHeight : canvas.top() + e.margin;
    paintReadoutBox(p, QPointF(x, y), e, lines);
}

QString extensionFromFilter(const QString &selectedFilter)
{
    if (selectedFilter.contains(QLatin1String("*.svg")))
        return QStringLiteral(".svg");
    if (selectedFilter.contains(QLatin1String("*.pdf")))
        return QStringLiteral(".pdf");
    return QStringLiteral(".png");
}

} // namespace

ExportImageDialog::ExportImageDialog(QwtPlot *plot, PlotCursors *cursors, const QString &readoutText,
                                     QWidget *parent)
    : ExportImageDialog(plot, cursors, readoutText, QString(), parent)
{
}

ExportImageDialog::ExportImageDialog(QwtPlot *plot, PlotCursors *cursors, const QString &cursorReadout,
                                     const QString &analysisReadout, QWidget *parent)
    : QDialog(parent), m_plot(plot), m_cursors(cursors), m_readout(cursorReadout),
      m_analysisReadout(analysisReadout)
{
    setWindowTitle(tr("Export graph"));
    const QSettings settings;

    auto *pathEdit = new QLineEdit(this);
    pathEdit->setObjectName(QStringLiteral("exportPath"));
    auto *browse = new QPushButton(tr("Browse..."), this);
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(pathEdit, 1);
    fileRow->addWidget(browse);

    const QSize defaultSize = plot ? plot->size() : QSize(800, 400);

    auto *widthSpin = new NumberSpinBox(this);
    widthSpin->setObjectName(QStringLiteral("exportWidth"));
    widthSpin->setDecimals(0);
    widthSpin->setRange(kMinPx, kMaxPx);
    widthSpin->setSuffix(tr(" px"));
    widthSpin->setValue(qBound(kMinPx, defaultSize.width(), kMaxPx));

    auto *heightSpin = new NumberSpinBox(this);
    heightSpin->setObjectName(QStringLiteral("exportHeight"));
    heightSpin->setDecimals(0);
    heightSpin->setRange(kMinPx, kMaxPx);
    heightSpin->setSuffix(tr(" px"));
    heightSpin->setValue(qBound(kMinPx, defaultSize.height(), kMaxPx));

    auto *dpiSpin = new NumberSpinBox(this);
    dpiSpin->setObjectName(QStringLiteral("exportDpi"));
    dpiSpin->setDecimals(0);
    dpiSpin->setRange(kMinDpi, kMaxDpi);
    dpiSpin->setSuffix(tr(" dpi"));
    dpiSpin->setValue(qBound(kMinDpi, settings.value(QStringLiteral("exportImage/dpi"), 96).toInt(),
                             kMaxDpi));

    auto *titleEdit = new QLineEdit(this);
    titleEdit->setObjectName(QStringLiteral("titleEdit"));
    titleEdit->setPlaceholderText(tr("(none)"));
    titleEdit->setText(settings.value(QStringLiteral("exportImage/title")).toString());

    auto *legendCheck = new QCheckBox(tr("Include legend"), this);
    legendCheck->setObjectName(QStringLiteral("legendCheck"));
    legendCheck->setChecked(settings.value(QStringLiteral("exportImage/legend"), true).toBool());

    const bool cursorsAvailable = cursors && !cursors->cursorNumbers().isEmpty();
    auto *cursorsBox = new QCheckBox(tr("Include cursors"), this);
    cursorsBox->setObjectName(QStringLiteral("exportCursors"));
    cursorsBox->setEnabled(cursorsAvailable);
    cursorsBox->setChecked(cursorsAvailable
                           && settings.value(QStringLiteral("exportImage/cursors"), true).toBool());

    auto *cursorReadoutCheck = new QCheckBox(tr("Include cursor readout"), this);
    cursorReadoutCheck->setObjectName(QStringLiteral("cursorReadoutCheck"));
    cursorReadoutCheck->setEnabled(!cursorReadout.isEmpty());
    cursorReadoutCheck->setChecked(
        !cursorReadout.isEmpty()
        && settings.value(QStringLiteral("exportImage/cursorReadout"), true).toBool());

    auto *analysisReadoutCheck = new QCheckBox(tr("Include Analysis results"), this);
    analysisReadoutCheck->setObjectName(QStringLiteral("analysisReadoutCheck"));
    analysisReadoutCheck->setEnabled(!analysisReadout.isEmpty());
    analysisReadoutCheck->setChecked(
        !analysisReadout.isEmpty()
        && settings.value(QStringLiteral("exportImage/analysisReadout"), true).toBool());

    auto *positionCombo = new QComboBox(this);
    positionCombo->setObjectName(QStringLiteral("positionCombo"));
    positionCombo->addItem(tr("Below the graph"), int(ExportOptions::ReadoutPosition::Below));
    positionCombo->addItem(tr("Top left"), int(ExportOptions::ReadoutPosition::TopLeft));
    positionCombo->addItem(tr("Top right"), int(ExportOptions::ReadoutPosition::TopRight));
    positionCombo->addItem(tr("Bottom left"), int(ExportOptions::ReadoutPosition::BottomLeft));
    positionCombo->addItem(tr("Bottom right"), int(ExportOptions::ReadoutPosition::BottomRight));
    {
        const int stored = settings.value(QStringLiteral("exportImage/position"),
                                          int(ExportOptions::ReadoutPosition::Below)).toInt();
        const int index = positionCombo->findData(stored);
        positionCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    const auto updatePosition = [cursorReadoutCheck, analysisReadoutCheck, positionCombo] {
        positionCombo->setEnabled(cursorReadoutCheck->isChecked()
                                  || analysisReadoutCheck->isChecked());
    };
    connect(cursorReadoutCheck, &QCheckBox::toggled, this, updatePosition);
    connect(analysisReadoutCheck, &QCheckBox::toggled, this, updatePosition);
    updatePosition();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("exportButtons"));
    QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(false); // until a path is entered

    auto *form = new QFormLayout(this);
    form->addRow(tr("File:"), fileRow);
    form->addRow(tr("Width:"), widthSpin);
    form->addRow(tr("Height:"), heightSpin);
    form->addRow(tr("Resolution:"), dpiSpin);
    form->addRow(tr("Title:"), titleEdit);
    form->addRow(legendCheck);
    form->addRow(cursorsBox);
    form->addRow(cursorReadoutCheck);
    form->addRow(analysisReadoutCheck);
    form->addRow(tr("Readout position:"), positionCombo);
    form->addRow(buttons);

    connect(pathEdit, &QLineEdit::textChanged, this,
            [pathEdit, okButton] { okButton->setEnabled(!pathEdit->text().trimmed().isEmpty()); });

    connect(browse, &QPushButton::clicked, this, [this, pathEdit] {
        const QString startDir =
            QSettings()
                .value(QString::fromLatin1(kDirSetting),
                       QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                .toString();
        const QString current = pathEdit->text().trimmed();
        const QString initial =
            current.isEmpty() ? QDir(startDir).filePath(QStringLiteral("trend.png")) : current;
        const QString filters = tr("PNG image (*.png);;SVG image (*.svg);;PDF document (*.pdf)");
        QString selectedFilter;
        QString file = QFileDialog::getSaveFileName(this, tr("Export graph"), initial, filters,
                                                    &selectedFilter);
        if (file.isEmpty())
            return;
        const QString suffix = QFileInfo(file).suffix().toLower();
        if (suffix != QLatin1String("png") && suffix != QLatin1String("svg")
            && suffix != QLatin1String("pdf"))
            file += extensionFromFilter(selectedFilter);
        pathEdit->setText(QDir::toNativeSeparators(file));
    });

    connect(buttons, &QDialogButtonBox::accepted, this, &ExportImageDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool ExportImageDialog::render(QwtPlot *plot, PlotCursors *cursors, const QString &path,
                               const QSize &sizePx, int dpi, bool includeCursors,
                               const QString &readout, QString *error)
{
    ExportOptions options;
    options.sizePx = sizePx;
    options.dpi = dpi;
    options.includeCursors = includeCursors;
    options.cursorReadout = readout;
    options.position = ExportOptions::ReadoutPosition::TopLeft; // the old behaviour
    return render(plot, cursors, path, options, error);
}

bool ExportImageDialog::render(QwtPlot *plot, PlotCursors *cursors, const QString &path,
                               const ExportOptions &options, QString *error)
{
    const QSize sizePx = options.sizePx;
    const int dpi = options.dpi;
    if (!plot) {
        setError(error, QStringLiteral("Cannot export '%1': no plot").arg(path));
        return false;
    }
    if (path.trimmed().isEmpty()) {
        setError(error, QStringLiteral("No file name given"));
        return false;
    }

    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (suffix != QLatin1String("png") && suffix != QLatin1String("svg")
        && suffix != QLatin1String("pdf")) {
        const QString shown =
            suffix.isEmpty() ? QStringLiteral("(none)") : QLatin1Char('.') + suffix;
        setError(error, QStringLiteral("Unsupported file type '%1' (use .png, .svg or .pdf)")
                            .arg(shown));
        return false;
    }
    if (sizePx.width() < kMinPx || sizePx.width() > kMaxPx || sizePx.height() < kMinPx
        || sizePx.height() > kMaxPx) {
        setError(error, QStringLiteral("Invalid image size %1 x %2 px (each side must be %3..%4)")
                            .arg(sizePx.width())
                            .arg(sizePx.height())
                            .arg(kMinPx)
                            .arg(kMaxPx));
        return false;
    }
    if (dpi < kMinDpi || dpi > kMaxDpi) {
        setError(error, QStringLiteral("Invalid resolution %1 dpi (must be %2..%3)")
                            .arg(dpi)
                            .arg(kMinDpi)
                            .arg(kMaxDpi));
        return false;
    }
    if (info.isDir() || !info.absoluteDir().exists()) {
        setError(error,
                 QStringLiteral("Cannot write '%1': folder does not exist or path is a folder")
                     .arg(path));
        return false;
    }

    // Restored on every exit path, including the failures below.
    MarkerHideGuard markerGuard(options.includeCursors ? nullptr : cursors);

    QwtPlotRenderer renderer;
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardBackground, false);
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardCanvasFrame, true);
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardLegend, !options.includeLegend);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);

    // The readouts form one box: cursor readout, an empty line, the Analysis readout.
    QString readout = options.cursorReadout;
    if (!options.analysisReadout.isEmpty())
        readout += (readout.isEmpty() ? QString() : QStringLiteral("\n\n")) + options.analysisReadout;
    const QStringList lines =
        readout.isEmpty() ? QStringList() : readout.split(QLatin1Char('\n'));
    const QString title = options.title.trimmed();

    const bool below = options.position == ExportOptions::ReadoutPosition::Below;
    const ExtraSizes extras = measureExtras(dpi, title, lines);
    const Layout layout = makeLayout(sizePx, extras, below);
    const QSize totalPx(qCeil(layout.total.width()), qCeil(layout.total.height()));
    const QRectF target(0, 0, totalPx.width(), totalPx.height());

    ExportOptions drawn = options;
    drawn.title = title;

    if (suffix == QLatin1String("png")) {
        QImage image(totalPx, QImage::Format_ARGB32);
        const int dotsPerMeter = qRound(dpi / 0.0254);
        image.setDotsPerMeterX(dotsPerMeter);
        image.setDotsPerMeterY(dotsPerMeter);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            if (!painter.isActive()) {
                setError(error, QStringLiteral("Cannot paint image for '%1'").arg(path));
                return false;
            }
            renderer.render(plot, &painter, layout.plot);
            paintExtras(&painter, plot, layout, extras, drawn, lines);
        }
        if (!image.save(path, "PNG")) {
            setError(error, QStringLiteral("Cannot write PNG file '%1'").arg(path));
            return false;
        }
        return true;
    }

    if (suffix == QLatin1String("svg")) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(totalPx);
        generator.setViewBox(QRect(QPoint(0, 0), totalPx));
        generator.setResolution(dpi);
        generator.setTitle(QStringLiteral("Modbus Control graph"));
        QPainter painter;
        if (!painter.begin(&generator)) {
            setError(error, QStringLiteral("Cannot write SVG file '%1'").arg(path));
            return false;
        }
        painter.fillRect(target, Qt::white);
        renderer.render(plot, &painter, layout.plot);
        paintExtras(&painter, plot, layout, extras, drawn, lines);
        painter.end();
        return true;
    }

    // PDF: one page of totalPx at dpi. QPdfWriter is used directly because
    // QwtPlotRenderer::renderDocument() cannot draw the title and the readout.
    const qreal widthMm = totalPx.width() * 25.4 / dpi;
    const qreal heightMm = totalPx.height() * 25.4 / dpi;
    QPdfWriter writer(path);
    writer.setResolution(dpi);
    writer.setPageLayout(QPageLayout(QPageSize(QSizeF(widthMm, heightMm), QPageSize::Millimeter,
                                               QString(), QPageSize::ExactMatch),
                                     QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                                     QPageLayout::Millimeter));
    QPainter painter;
    if (!painter.begin(&writer)) {
        setError(error, QStringLiteral("Cannot write PDF file '%1'").arg(path));
        return false;
    }
    // Device units of the page can differ slightly from the rounded pixel size: scale the layout.
    const QRectF page(0, 0, writer.width(), writer.height());
    painter.fillRect(page, Qt::white);
    painter.scale(page.width() / target.width(), page.height() / target.height());
    renderer.render(plot, &painter, layout.plot);
    paintExtras(&painter, plot, layout, extras, drawn, lines);
    painter.end();
    return true;
}

void ExportImageDialog::accept()
{
    auto *pathEdit = findChild<QLineEdit *>(QStringLiteral("exportPath"));
    auto *widthSpin = findChild<NumberSpinBox *>(QStringLiteral("exportWidth"));
    auto *heightSpin = findChild<NumberSpinBox *>(QStringLiteral("exportHeight"));
    auto *dpiSpin = findChild<NumberSpinBox *>(QStringLiteral("exportDpi"));
    auto *cursorsBox = findChild<QCheckBox *>(QStringLiteral("exportCursors"));
    auto *titleEdit = findChild<QLineEdit *>(QStringLiteral("titleEdit"));
    auto *legendCheck = findChild<QCheckBox *>(QStringLiteral("legendCheck"));
    auto *cursorReadoutCheck = findChild<QCheckBox *>(QStringLiteral("cursorReadoutCheck"));
    auto *analysisReadoutCheck = findChild<QCheckBox *>(QStringLiteral("analysisReadoutCheck"));
    auto *positionCombo = findChild<QComboBox *>(QStringLiteral("positionCombo"));

    // Typed text that was not committed yet.
    widthSpin->interpretText();
    heightSpin->interpretText();
    dpiSpin->interpretText();

    const QString path = pathEdit->text().trimmed();

    ExportOptions options;
    options.sizePx = QSize(qRound(widthSpin->value()), qRound(heightSpin->value()));
    options.dpi = qRound(dpiSpin->value());
    options.includeCursors = cursorsBox->isChecked();
    options.includeLegend = legendCheck->isChecked();
    options.title = titleEdit->text();
    if (cursorReadoutCheck->isChecked())
        options.cursorReadout = m_readout;
    if (analysisReadoutCheck->isChecked())
        options.analysisReadout = m_analysisReadout;
    options.position = static_cast<ExportOptions::ReadoutPosition>(positionCombo->currentData().toInt());

    QString err;
    if (!render(m_plot, m_cursors, path, options, &err)) {
        QMessageBox::warning(this, tr("Export graph"),
                             err.isEmpty() ? tr("The graph could not be exported.") : err);
        return;
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kDirSetting), QFileInfo(path).absolutePath());
    settings.setValue(QStringLiteral("exportImage/dpi"), options.dpi);
    settings.setValue(QStringLiteral("exportImage/title"), titleEdit->text());
    settings.setValue(QStringLiteral("exportImage/legend"), legendCheck->isChecked());
    settings.setValue(QStringLiteral("exportImage/position"), positionCombo->currentData().toInt());
    // A disabled check box keeps the stored choice for the next time it is available.
    if (cursorsBox->isEnabled())
        settings.setValue(QStringLiteral("exportImage/cursors"), cursorsBox->isChecked());
    if (cursorReadoutCheck->isEnabled())
        settings.setValue(QStringLiteral("exportImage/cursorReadout"), cursorReadoutCheck->isChecked());
    if (analysisReadoutCheck->isEnabled())
        settings.setValue(QStringLiteral("exportImage/analysisReadout"),
                          analysisReadoutCheck->isChecked());
    QDialog::accept();
}

} // namespace mb
