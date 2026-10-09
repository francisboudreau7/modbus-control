#include "ExportImageDialog.h"

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
#include <QSpinBox>
#include <QStandardPaths>
#include <QSvgGenerator>

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

/// Draws the readout as a framed text box at the top-left corner of the canvas. The canvas
/// position comes from the on-screen plot and is scaled to the target size, which
/// approximates the layout QwtPlotRenderer produces for a different size.
void paintReadout(QPainter *p, const QwtPlot *plot, const QSizeF &target, const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(9.0);

    p->save();
    p->setFont(font);
    // Metrics against the painter's device so the box fits at any DPI.
    const QFontMetricsF fm(font, p->device());
    qreal textWidth = 0.0;
    for (const QString &line : lines)
        textWidth = qMax(textWidth, fm.horizontalAdvance(line));
    const qreal pad = 6.0;
    const qreal margin = 10.0;
    const qreal lineHeight = fm.lineSpacing();

    const qreal sx = plot->width() > 0 ? target.width() / plot->width() : 1.0;
    const qreal sy = plot->height() > 0 ? target.height() / plot->height() : 1.0;
    const QPoint canvasTopLeft = plot->canvas()->geometry().topLeft();

    const QRectF box(canvasTopLeft.x() * sx + margin, canvasTopLeft.y() * sy + margin,
                     textWidth + 2 * pad, lineHeight * lines.size() + 2 * pad);

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);
    p->setPen(QPen(QColor(80, 80, 80), 1));
    p->setBrush(QColor(255, 255, 255, 217)); // about 85 % opaque white
    p->drawRect(box);

    p->setPen(Qt::black);
    qreal baseline = box.top() + pad + fm.ascent();
    for (const QString &line : lines) {
        p->drawText(QPointF(box.left() + pad, baseline), line);
        baseline += lineHeight;
    }
    p->restore();
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
    : QDialog(parent), m_plot(plot), m_cursors(cursors), m_readout(readoutText)
{
    setWindowTitle(tr("Export graph"));

    auto *pathEdit = new QLineEdit(this);
    pathEdit->setObjectName(QStringLiteral("exportPath"));
    auto *browse = new QPushButton(tr("Browse..."), this);
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(pathEdit, 1);
    fileRow->addWidget(browse);

    const QSize defaultSize = plot ? plot->size() : QSize(800, 400);

    auto *widthSpin = new QSpinBox(this);
    widthSpin->setObjectName(QStringLiteral("exportWidth"));
    widthSpin->setRange(kMinPx, kMaxPx);
    widthSpin->setSuffix(tr(" px"));
    widthSpin->setValue(qBound(kMinPx, defaultSize.width(), kMaxPx));

    auto *heightSpin = new QSpinBox(this);
    heightSpin->setObjectName(QStringLiteral("exportHeight"));
    heightSpin->setRange(kMinPx, kMaxPx);
    heightSpin->setSuffix(tr(" px"));
    heightSpin->setValue(qBound(kMinPx, defaultSize.height(), kMaxPx));

    auto *dpiSpin = new QSpinBox(this);
    dpiSpin->setObjectName(QStringLiteral("exportDpi"));
    dpiSpin->setRange(kMinDpi, kMaxDpi);
    dpiSpin->setSuffix(tr(" dpi"));
    dpiSpin->setValue(96);

    const bool cursorsAvailable = cursors && (cursors->hasC1() || cursors->hasC2());
    auto *cursorsBox = new QCheckBox(tr("Include cursors"), this);
    cursorsBox->setObjectName(QStringLiteral("exportCursors"));
    cursorsBox->setEnabled(cursorsAvailable);
    cursorsBox->setChecked(cursorsAvailable);

    const bool readoutAvailable = !readoutText.isEmpty();
    auto *readoutBox = new QCheckBox(tr("Include cursor readout"), this);
    readoutBox->setObjectName(QStringLiteral("exportReadout"));
    readoutBox->setEnabled(readoutAvailable);
    readoutBox->setChecked(readoutAvailable);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("exportButtons"));
    QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(false); // until a path is entered

    auto *form = new QFormLayout(this);
    form->addRow(tr("File:"), fileRow);
    form->addRow(tr("Width:"), widthSpin);
    form->addRow(tr("Height:"), heightSpin);
    form->addRow(tr("Resolution:"), dpiSpin);
    form->addRow(cursorsBox);
    form->addRow(readoutBox);
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
    MarkerHideGuard markerGuard(includeCursors ? nullptr : cursors);

    QwtPlotRenderer renderer;
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardBackground, false);
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardCanvasFrame, true);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);

    const QRectF target(0, 0, sizePx.width(), sizePx.height());

    if (suffix == QLatin1String("png")) {
        QImage image(sizePx, QImage::Format_ARGB32);
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
            renderer.render(plot, &painter, target);
            if (!readout.isEmpty())
                paintReadout(&painter, plot, target.size(), readout);
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
        generator.setSize(sizePx);
        generator.setViewBox(QRect(QPoint(0, 0), sizePx));
        generator.setResolution(dpi);
        generator.setTitle(QStringLiteral("Modbus Control graph"));
        QPainter painter;
        if (!painter.begin(&generator)) {
            setError(error, QStringLiteral("Cannot write SVG file '%1'").arg(path));
            return false;
        }
        painter.fillRect(target, Qt::white);
        renderer.render(plot, &painter, target);
        if (!readout.isEmpty())
            paintReadout(&painter, plot, target.size(), readout);
        painter.end();
        return true;
    }

    // PDF: one page of sizePx at dpi. QPdfWriter is used directly because
    // QwtPlotRenderer::renderDocument() cannot draw the readout.
    const qreal widthMm = sizePx.width() * 25.4 / dpi;
    const qreal heightMm = sizePx.height() * 25.4 / dpi;
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
    const QRectF page(0, 0, writer.width(), writer.height());
    painter.fillRect(page, Qt::white);
    renderer.render(plot, &painter, page);
    if (!readout.isEmpty())
        paintReadout(&painter, plot, page.size(), readout);
    painter.end();
    return true;
}

void ExportImageDialog::accept()
{
    auto *pathEdit = findChild<QLineEdit *>(QStringLiteral("exportPath"));
    auto *widthSpin = findChild<QSpinBox *>(QStringLiteral("exportWidth"));
    auto *heightSpin = findChild<QSpinBox *>(QStringLiteral("exportHeight"));
    auto *dpiSpin = findChild<QSpinBox *>(QStringLiteral("exportDpi"));
    auto *cursorsBox = findChild<QCheckBox *>(QStringLiteral("exportCursors"));
    auto *readoutBox = findChild<QCheckBox *>(QStringLiteral("exportReadout"));

    const QString path = pathEdit->text().trimmed();
    const QString readout = readoutBox->isChecked() ? m_readout : QString();

    QString err;
    if (!render(m_plot, m_cursors, path, QSize(widthSpin->value(), heightSpin->value()),
                dpiSpin->value(), cursorsBox->isChecked(), readout, &err)) {
        QMessageBox::warning(this, tr("Export graph"),
                             err.isEmpty() ? tr("The graph could not be exported.") : err);
        return;
    }

    QSettings().setValue(QString::fromLatin1(kDirSetting), QFileInfo(path).absolutePath());
    QDialog::accept();
}

} // namespace mb
