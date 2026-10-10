#include "AnalysisView.h"

#include "ClipboardCopy.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "TrendPlot.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_map.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QClipboard>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

const QString kDash = QStringLiteral(u"—");   // em dash: missing value
const QString kDelta = QStringLiteral(u"Δ");  // capital delta
const QString kMinus = QStringLiteral(u"−");  // minus sign

constexpr int kMaxPercents = 20;

QString fmtNum(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 6) : kDash;
}

QString fmtNum(std::optional<double> v)
{
    return v ? fmtNum(*v) : kDash;
}

QString fmtSeconds(double t)
{
    return QString::number(t, 'f', 3) + QStringLiteral(" s");
}

QString fmtPlain(double p)
{
    return QLocale::c().toString(p, 'g', 6);
}

QString fmtPercent(double p)
{
    return fmtPlain(p) + QStringLiteral(" %");
}

QString tagLabel(const Tag &tag)
{
    return tag.unit.isEmpty() ? tag.name
                              : tag.name + QStringLiteral(" [") + tag.unit + QStringLiteral("]");
}

QTableWidgetItem *makeItem(const QString &text, bool alignRight)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (alignRight)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QDoubleSpinBox *makeSpin(const char *name, double lo, double hi, int decimals, double value,
                         QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setObjectName(QLatin1String(name));
    spin->setDecimals(decimals);
    spin->setRange(lo, hi);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

constexpr int kGuideRtti = QwtPlotItem::Rtti_PlotUserItem + 1;
constexpr int kModelPoints = 400;

/// Dashed guide lines from setpoint points down to the bottom and left to the left edge of
/// the canvas; computed at draw time so they follow zoom and pan.
struct GuidePoint {
    QPointF pos;
    QColor color;
};

class GuideLineItem : public QwtPlotItem
{
public:
    explicit GuideLineItem(const QList<GuidePoint> &points)
        : QwtPlotItem(QwtText(QStringLiteral("Guide lines (%1 points)").arg(points.size())))
        , m_points(points)
    {
        setItemAttribute(QwtPlotItem::Legend, false);
        setZ(15.0);
    }

    int rtti() const override { return kGuideRtti; }

    void draw(QPainter *painter, const QwtScaleMap &xMap, const QwtScaleMap &yMap,
              const QRectF &canvasRect) const override
    {
        painter->save();
        for (const GuidePoint &gp : m_points) {
            QColor c = gp.color;
            c.setAlpha(160);
            QPen pen(c, 1);
            pen.setStyle(Qt::DashLine);
            painter->setPen(pen);
            const double px = xMap.transform(gp.pos.x());
            const double py = yMap.transform(gp.pos.y());
            painter->drawLine(QPointF(px, py), QPointF(px, canvasRect.bottom()));
            painter->drawLine(QPointF(px, py), QPointF(canvasRect.left(), py));
        }
        painter->restore();
    }

private:
    QList<GuidePoint> m_points;
};

} // namespace

AnalysisView::AnalysisView(QWidget *parent)
    : QWidget(parent)
    , m_plot(new TrendPlot(this))
{
    buildUi();
    syncControlsFromSettings();
    recompute();

    connect(m_plot->cursors(), &PlotCursors::cursorsChanged, this, [this] {
        if (!m_settingCursors)
            recompute();
    });
}

AnalysisView::~AnalysisView()
{
    clearMarkers();
}

void AnalysisView::buildUi()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_plot);

    auto *panel = new QWidget;
    auto *col = new QVBoxLayout(panel);

    // Buttons
    auto *buttons = new QHBoxLayout;
    m_copyCursorsButton = new QPushButton(tr("Copy cursors from Trend"), panel);
    m_copyCursorsButton->setObjectName(QStringLiteral("copyCursorsButton"));
    m_exportButton = new QPushButton(tr("Export results (CSV)..."), panel);
    m_exportButton->setObjectName(QStringLiteral("exportResultsButton"));
    m_exportButton->setEnabled(false);
    buttons->addWidget(m_copyCursorsButton);
    m_copyResultsButton = new QPushButton(tr("Copy results"), panel);
    m_copyResultsButton->setObjectName(QStringLiteral("copyResultsButton"));
    m_copyResultsButton->setEnabled(false);
    buttons->addWidget(m_exportButton);
    buttons->addWidget(m_copyResultsButton);
    col->addLayout(buttons);
    connect(m_copyResultsButton, &QPushButton::clicked, this, [this] { copyResults(); });
    connect(m_copyCursorsButton, &QPushButton::clicked, this, &AnalysisView::copyCursorsRequested);
    connect(m_exportButton, &QPushButton::clicked, this, &AnalysisView::exportResultsRequested);

    // References
    auto *refBox = new QGroupBox(tr("References"), panel);
    auto *refGrid = new QGridLayout(refBox);
    m_referenceCombo = new QComboBox(refBox);
    m_referenceCombo->setObjectName(QStringLiteral("referenceCombo"));
    m_referenceCombo->addItem(tr("Value at cursor"), static_cast<int>(ReferenceMode::CursorValue));
    m_referenceCombo->addItem(tr("Mean before cursor"), static_cast<int>(ReferenceMode::WindowMean));
    m_referenceCombo->setToolTip(
        tr("Value at cursor: the recorded value at each cursor.\n"
           "Mean before cursor: the mean of the W seconds ending at each cursor."));
    m_windowSpin = makeSpin("windowSpin", 0.1, 3600.0, 1, 1.0, refBox);
    m_windowSpin->setSuffix(tr(" s"));
    m_windowSpin->setToolTip(tr("Window W of the mean before each cursor."));
    m_smoothingSpin = makeSpin("smoothingSpin", 0.0, 3600.0, 2, 0.0, refBox);
    m_smoothingSpin->setSuffix(tr(" s"));
    m_smoothingSpin->setSpecialValueText(tr("Off"));
    m_smoothingSpin->setToolTip(
        tr("Centred moving average of width S applied before finding the setpoint times.\n"
           "Smoothing makes the 0 % point early by up to S/2 (and L biased low); when "
           "smoothing is used, choose a model \"From %\" above 0 (e.g. 10)."));
    refGrid->addWidget(new QLabel(tr("Reference"), refBox), 0, 0);
    refGrid->addWidget(m_referenceCombo, 0, 1);
    refGrid->addWidget(new QLabel(tr("Window"), refBox), 1, 0);
    refGrid->addWidget(m_windowSpin, 1, 1);
    refGrid->addWidget(new QLabel(tr("Smoothing"), refBox), 2, 0);
    refGrid->addWidget(m_smoothingSpin, 2, 1);
    col->addWidget(refBox);
    // Each control updates only its own field, so a loaded value the spin box displays
    // rounded is not rewritten when another control changes.
    connect(m_referenceCombo, &QComboBox::currentIndexChanged, this, [this] { onReferenceChanged(); });
    connect(m_windowSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_settings.referenceWindowS = v;
        userChangedSettings();
    });
    connect(m_smoothingSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_settings.smoothingS = v;
        userChangedSettings();
    });

    // Setpoints
    auto *spBox = new QGroupBox(tr("Setpoints"), panel);
    auto *spGrid = new QGridLayout(spBox);
    m_percentList = new QListWidget(spBox);
    m_percentList->setObjectName(QStringLiteral("percentList"));
    m_percentList->setMaximumHeight(110);
    m_percentSpin = makeSpin("percentSpin", 0.0, 100.0, 2, 28.3, spBox);
    m_percentSpin->setSuffix(QStringLiteral(" %"));
    m_addPercentButton = new QPushButton(tr("Add"), spBox);
    m_addPercentButton->setObjectName(QStringLiteral("addPercentButton"));
    m_removePercentButton = new QPushButton(tr("Remove"), spBox);
    m_removePercentButton->setObjectName(QStringLiteral("removePercentButton"));
    spGrid->addWidget(m_percentList, 0, 0, 3, 1);
    spGrid->addWidget(m_percentSpin, 0, 1);
    spGrid->addWidget(m_addPercentButton, 1, 1);
    spGrid->addWidget(m_removePercentButton, 2, 1);
    m_detailedLabelsCheck = new QCheckBox(tr("Show time and value on plot labels"), spBox);
    m_detailedLabelsCheck->setObjectName(QStringLiteral("detailedLabelsCheck"));
    spGrid->addWidget(m_detailedLabelsCheck, 3, 0, 1, 2);
    connect(m_detailedLabelsCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.detailedLabels = on;
        userChangedSettings();
    });
    m_guideLinesCheck = new QCheckBox(tr("Guide lines"), spBox);
    m_guideLinesCheck->setObjectName(QStringLiteral("guideLinesCheck"));
    spGrid->addWidget(m_guideLinesCheck, 4, 0, 1, 2);
    connect(m_guideLinesCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.guideLines = on;
        userChangedSettings();
    });
    col->addWidget(spBox);
    connect(m_addPercentButton, &QPushButton::clicked, this, [this] { onAddPercent(); });
    connect(m_removePercentButton, &QPushButton::clicked, this, [this] { onRemovePercent(); });

    // Model
    auto *modelBox = new QGroupBox(tr("Model"), panel);
    auto *mGrid = new QGridLayout(modelBox);
    m_modelFromSpin = makeSpin("modelFromSpin", 0.0, 99.99, 2, 0.0, modelBox);
    m_modelFromSpin->setSuffix(QStringLiteral(" %"));
    m_modelToSpin = makeSpin("modelToSpin", 0.0, 99.99, 2, 63.2, modelBox);
    m_modelToSpin->setSuffix(QStringLiteral(" %"));
    m_deltaCoCombo = new QComboBox(modelBox);
    m_deltaCoCombo->setObjectName(QStringLiteral("deltaCoCombo"));
    m_deltaCoCombo->addItem(tr("Typed value"), 0u);
    m_deltaCoSpin = makeSpin("deltaCoSpin", -1e9, 1e9, 4, 0.0, modelBox);
    m_deltaCoSpin->setToolTip(tr("Change of the controller output (0 = unknown, no gain K)."));
    mGrid->addWidget(new QLabel(tr("From %"), modelBox), 0, 0);
    mGrid->addWidget(m_modelFromSpin, 0, 1);
    mGrid->addWidget(new QLabel(tr("To %"), modelBox), 1, 0);
    mGrid->addWidget(m_modelToSpin, 1, 1);
    mGrid->addWidget(new QLabel(kDelta + QStringLiteral("CO"), modelBox), 2, 0);
    mGrid->addWidget(m_deltaCoCombo, 2, 1);
    mGrid->addWidget(m_deltaCoSpin, 3, 1);
    m_showModelCheck = new QCheckBox(tr("Show model curve"), modelBox);
    m_showModelCheck->setObjectName(QStringLiteral("showModelCheck"));
    mGrid->addWidget(m_showModelCheck, 4, 0, 1, 2);
    connect(m_showModelCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.showModel = on;
        userChangedSettings();
    });
    col->addWidget(modelBox);
    connect(m_modelFromSpin, &QDoubleSpinBox::valueChanged, this, [this] { onModelPercentChanged(); });
    connect(m_modelToSpin, &QDoubleSpinBox::valueChanged, this, [this] { onModelPercentChanged(); });
    connect(m_deltaCoCombo, &QComboBox::currentIndexChanged, this, [this] { onDeltaCoChanged(); });
    connect(m_deltaCoSpin, &QDoubleSpinBox::valueChanged, this, [this] { onDeltaCoChanged(); });

    // Tables
    m_setpointTable = new QTableWidget(0, 6, panel);
    m_setpointTable->setObjectName(QStringLiteral("setpointTable"));
    m_setpointTable->setHorizontalHeaderLabels(QStringList{
        tr("Tag"), QStringLiteral("%"), tr("Target"), tr("Measured"), tr("Time (s)"),
        QStringLiteral("t ") + kMinus + QStringLiteral(" C1 (s)")});
    m_setpointTable->verticalHeader()->setVisible(false);
    m_setpointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_setpointTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_setpointTable->setMinimumHeight(140);
    m_setpointTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    installCopyShortcut(m_setpointTable);

    m_modelTable = new QTableWidget(0, 9, panel);
    m_modelTable->setObjectName(QStringLiteral("modelTable"));
    m_modelTable->setHorizontalHeaderLabels(QStringList{
        tr("Tag"), QStringLiteral("y0"), QStringLiteral("y100"), kDelta + QStringLiteral("PV"),
        kDelta + QStringLiteral("CO"), QStringLiteral("K"), QStringLiteral("L (s)"),
        QStringLiteral("T (s)"), tr("Fit RMS")});
    m_modelTable->verticalHeader()->setVisible(false);
    m_modelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_modelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_modelTable->setMinimumHeight(100);
    m_modelTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    installCopyShortcut(m_modelTable);

    col->addWidget(new QLabel(tr("Setpoints"), panel));
    col->addWidget(m_setpointTable);
    col->addWidget(new QLabel(tr("Model"), panel));
    col->addWidget(m_modelTable);
    col->addStretch(1);

    auto *scroll = new QScrollArea(splitter);
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel);
    scroll->setMinimumWidth(380);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
}

const Tag *AnalysisView::tagById(TagId id) const
{
    for (const Tag &t : m_tags) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

void AnalysisView::setRecorder(const Recorder *recorder)
{
    m_recorder = recorder;
    m_plot->setRecorder(recorder);
    recompute();
}

void AnalysisView::setTags(const QList<Tag> &tags)
{
    m_tags = tags;

    QList<Tag> plotTags = tags;
    for (Tag &t : plotTags)
        t.plotted = t.analyzed;
    m_plot->setTags(plotTags);

    // dCO combo: "Typed value" + every tag; keep the selection when it still exists.
    const TagId wanted = m_settings.deltaCoTagId;
    bool found = false;
    {
        const QSignalBlocker blocker(m_deltaCoCombo);
        while (m_deltaCoCombo->count() > 1)
            m_deltaCoCombo->removeItem(1);
        for (const Tag &t : tags)
            m_deltaCoCombo->addItem(tagLabel(t), t.id);
        const int idx = wanted != 0 ? m_deltaCoCombo->findData(wanted) : -1;
        found = idx >= 0;
        m_deltaCoCombo->setCurrentIndex(found ? idx : 0);
    }
    if (!found)
        m_settings.deltaCoTagId = 0;
    m_deltaCoSpin->setEnabled(m_deltaCoCombo->currentIndex() == 0);
    recompute();
}

void AnalysisView::syncControlsFromSettings()
{
    const bool was = m_updating;
    m_updating = true;
    {
        const QSignalBlocker b1(m_referenceCombo);
        const QSignalBlocker b2(m_windowSpin);
        const QSignalBlocker b3(m_smoothingSpin);
        const QSignalBlocker b4(m_modelFromSpin);
        const QSignalBlocker b5(m_modelToSpin);
        const QSignalBlocker b6(m_deltaCoCombo);
        const QSignalBlocker b7(m_deltaCoSpin);
        const QSignalBlocker b8(m_detailedLabelsCheck);
        const QSignalBlocker b9(m_guideLinesCheck);
        const QSignalBlocker b10(m_showModelCheck);
        m_guideLinesCheck->setChecked(m_settings.guideLines);
        m_showModelCheck->setChecked(m_settings.showModel);
        m_detailedLabelsCheck->setChecked(m_settings.detailedLabels);
        m_referenceCombo->setCurrentIndex(m_settings.reference == ReferenceMode::WindowMean ? 1 : 0);
        m_windowSpin->setValue(m_settings.referenceWindowS);
        m_smoothingSpin->setValue(m_settings.smoothingS);
        m_modelFromSpin->setValue(m_settings.modelPercentA);
        m_modelToSpin->setValue(m_settings.modelPercentB);
        const int idx = m_settings.deltaCoTagId != 0
                            ? m_deltaCoCombo->findData(m_settings.deltaCoTagId) : -1;
        m_deltaCoCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        // A ΔCO tag that is not in the project (e.g. deleted before the file was saved)
        // means "Typed value", as the combo shows; otherwise the typed value is ignored.
        if (idx < 0 && !m_tags.isEmpty())
            m_settings.deltaCoTagId = 0;
        m_deltaCoSpin->setValue(m_settings.manualDeltaCo);
    }
    m_windowSpin->setEnabled(m_settings.reference == ReferenceMode::WindowMean);
    m_deltaCoSpin->setEnabled(m_deltaCoCombo->currentIndex() == 0);
    m_prevFrom = m_modelFromSpin->value();
    m_prevTo = m_modelToSpin->value();
    rebuildPercentList();
    m_updating = was;
}

void AnalysisView::rebuildPercentList()
{
    const QSignalBlocker blocker(m_percentList);
    m_percentList->clear();
    for (double p : std::as_const(m_settings.percents)) {
        auto *item = new QListWidgetItem(fmtPercent(p), m_percentList);
        item->setData(Qt::UserRole, p);
    }
}

void AnalysisView::setSettings(const AnalysisSettings &settings)
{
    m_settings = settings;
    // Keep the list invariant (sorted, no duplicates, 0..100, at most 20) even for a
    // hand-edited project file.
    QList<double> percents;
    for (double p : std::as_const(m_settings.percents)) {
        if (p >= 0.0 && p <= 100.0)
            percents.append(p);
    }
    std::sort(percents.begin(), percents.end());
    percents.erase(std::unique(percents.begin(), percents.end(),
                               [](double a, double b) { return std::abs(a - b) < 1e-9; }),
                   percents.end());
    m_settings.percents = percents.mid(0, kMaxPercents);
    syncControlsFromSettings();
    recompute();
}

AnalysisSettings AnalysisView::settings() const
{
    return m_settings;
}

TrendPlot *AnalysisView::trendPlot() const
{
    return m_plot;
}

QList<StepResult> AnalysisView::results() const
{
    return m_results;
}

void AnalysisView::userChangedSettings()
{
    recompute();
    emit settingsChanged();
}

void AnalysisView::onReferenceChanged()
{
    if (m_updating)
        return;
    m_settings.reference = m_referenceCombo->currentData().toInt() == 1
                               ? ReferenceMode::WindowMean : ReferenceMode::CursorValue;
    m_windowSpin->setEnabled(m_settings.reference == ReferenceMode::WindowMean);
    userChangedSettings();
}

void AnalysisView::onModelPercentChanged()
{
    if (m_updating)
        return;
    const double a = m_modelFromSpin->value();
    const double b = m_modelToSpin->value();
    if (!(a < b)) {
        const QSignalBlocker b1(m_modelFromSpin);
        const QSignalBlocker b2(m_modelToSpin);
        m_modelFromSpin->setValue(m_prevFrom);
        m_modelToSpin->setValue(m_prevTo);
        return;
    }
    m_prevFrom = a;
    m_prevTo = b;
    m_settings.modelPercentA = a;
    m_settings.modelPercentB = b;
    userChangedSettings();
}

void AnalysisView::onDeltaCoChanged()
{
    if (m_updating)
        return;
    m_settings.deltaCoTagId = m_deltaCoCombo->currentData().toUInt();
    m_settings.manualDeltaCo = m_deltaCoSpin->value();
    m_deltaCoSpin->setEnabled(m_deltaCoCombo->currentIndex() == 0);
    userChangedSettings();
}

void AnalysisView::onAddPercent()
{
    if (m_updating || m_settings.percents.size() >= kMaxPercents)
        return;
    const double p = std::round(m_percentSpin->value() * 100.0) / 100.0;
    for (double q : std::as_const(m_settings.percents)) {
        if (std::abs(q - p) < 1e-9)
            return;
    }
    m_settings.percents.append(p);
    std::sort(m_settings.percents.begin(), m_settings.percents.end());
    rebuildPercentList();
    userChangedSettings();
}

void AnalysisView::onRemovePercent()
{
    if (m_updating)
        return;
    const int row = m_percentList->currentRow();
    if (row < 0 || row >= m_settings.percents.size())
        return;
    m_settings.percents.removeAt(row);
    rebuildPercentList();
    userChangedSettings();
}

void AnalysisView::clearMarkers()
{
    for (QwtPlotMarker *m : std::as_const(m_markers)) {
        m->detach();
        delete m;
    }
    m_markers.clear();
    for (QwtPlotItem *g : std::as_const(m_guideItems)) {
        g->detach();
        delete g;
    }
    m_guideItems.clear();
    for (QwtPlotCurve *c : std::as_const(m_modelCurves)) {
        c->detach();
        delete c;
    }
    m_modelCurves.clear();
}

void AnalysisView::copyResults()
{
    const QString text = tableToTsv(m_setpointTable->model(), {})
                         + QStringLiteral("\r\n") + tableToTsv(m_modelTable->model(), {});
    QGuiApplication::clipboard()->setText(text);
}

void AnalysisView::rebuildMarkers()
{
    clearMarkers();
    QMap<int, QList<GuidePoint>> guidePoints; // by Y axis (QwtPlot::yLeft / yRight)
    for (const StepResult &r : std::as_const(m_results)) {
        if (!r.valid)
            continue;
        const Tag *tag = tagById(r.tagId);
        const QColor color = tag ? QColor(tag->color) : QColor(Qt::black);
        const int axis = (tag && tag->axis == YAxis::Right) ? QwtPlot::yRight : QwtPlot::yLeft;

        if (m_settings.showModel && r.deadTime && r.timeConstant && r.t2 > r.t1) {
            QVector<QPointF> pts;
            pts.reserve(kModelPoints + 2);
            for (int i = 0; i < kModelPoints; ++i) {
                const double t = r.t1 + (r.t2 - r.t1) * i / (kModelPoints - 1);
                pts.append(QPointF(t, fopdtModelValue(r, t)));
            }
            const double tb = r.stepTime + *r.deadTime;
            if (tb > r.t1 && tb < r.t2) {
                const auto it = std::lower_bound(pts.begin(), pts.end(), tb,
                                                 [](const QPointF &p, double t) { return p.x() < t; });
                pts.insert(it, QPointF(tb, fopdtModelValue(r, tb)));
            }
            auto *curve = new QwtPlotCurve(tag ? tag->name + QStringLiteral(" model")
                                               : QStringLiteral("model"));
            curve->setSamples(pts);
            curve->setPen(QPen(color, 1.5, Qt::DashLine));
            curve->setItemAttribute(QwtPlotItem::Legend, false);
            curve->setYAxis(axis);
            curve->setZ(12.0);
            curve->attach(m_plot->plot());
            m_modelCurves.append(curve);
        }
        for (const SetpointResult &sp : r.setpoints) {
            if (!sp.found)
                continue;
            auto *marker = new QwtPlotMarker;
            marker->setValue(sp.time, sp.target);
            marker->setSymbol(new QwtSymbol(QwtSymbol::XCross, QBrush(), QPen(color, 2),
                                            QSize(11, 11)));
            QString text = fmtPercent(sp.percent);
            if (m_settings.detailedLabels) {
                text += QStringLiteral("\nt = ") + QString::number(sp.time, 'f', 3)
                        + QStringLiteral(" s\ny = ") + fmtNum(sp.target);
                if (tag && !tag->unit.isEmpty())
                    text += u' ' + tag->unit;
            }
            // Dark text in an opaque box with a tag-coloured border: readable over any curve.
            QwtText label(text);
            label.setColor(QColor(0x20, 0x20, 0x20));
            label.setBackgroundBrush(QColor(255, 255, 255, 235));
            label.setBorderPen(QPen(color, 1));
            label.setBorderRadius(3);
            label.setRenderFlags(Qt::AlignLeft | Qt::AlignVCenter);
            marker->setLabel(label);
            marker->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
            marker->setYAxis(axis);
            guidePoints[axis].append({QPointF(sp.time, sp.target), color});
            marker->attach(m_plot->plot());
            m_markers.append(marker);
        }
    }
    if (m_settings.guideLines) {
        for (auto it = guidePoints.cbegin(); it != guidePoints.cend(); ++it) {
            auto *item = new GuideLineItem(it.value());
            item->setYAxis(it.key());
            item->attach(m_plot->plot());
            m_guideItems.append(item);
        }
    }
}

void AnalysisView::rebuildTables()
{
    m_setpointTable->setRowCount(0);
    m_modelTable->clearSpans();
    m_modelTable->setRowCount(0);

    int spRow = 0;
    int mRow = 0;
    for (const StepResult &r : std::as_const(m_results)) {
        const Tag *tag = tagById(r.tagId);
        const QString label = tag ? tagLabel(*tag) : QString::number(r.tagId);
        const QColor color = tag ? QColor(tag->color) : QColor();

        // Model table row
        m_modelTable->insertRow(mRow);
        auto *tagItem = makeItem(label, false);
        if (color.isValid())
            tagItem->setData(Qt::DecorationRole, color);
        m_modelTable->setItem(mRow, 0, tagItem);
        if (!r.valid) {
            m_modelTable->setItem(mRow, 1, makeItem(r.error, false));
            m_modelTable->setSpan(mRow, 1, 1, 8);
        } else {
            QString fit = kDash;
            if (r.fitRms) {
                fit = fmtNum(*r.fitRms);
                if (r.fitRmsPercent)
                    fit += QStringLiteral(" (") + QString::number(*r.fitRmsPercent, 'g', 3)
                           + QStringLiteral(" %)");
            }
            const QString texts[8] = {fmtNum(r.y0), fmtNum(r.y100), fmtNum(r.deltaPv),
                                      fmtNum(r.deltaCo), fmtNum(r.gain), fmtNum(r.deadTime),
                                      fmtNum(r.timeConstant), fit};
            for (int c = 0; c < 8; ++c)
                m_modelTable->setItem(mRow, c + 1, makeItem(texts[c], true));
        }
        ++mRow;

        // Setpoint rows
        if (!r.valid)
            continue;
        for (const SetpointResult &sp : r.setpoints) {
            m_setpointTable->insertRow(spRow);
            auto *ti = makeItem(label, false);
            if (color.isValid())
                ti->setData(Qt::DecorationRole, color);
            m_setpointTable->setItem(spRow, 0, ti);
            const QString texts[5] = {
                fmtPlain(sp.percent),
                fmtNum(sp.target),
                sp.found ? fmtNum(sp.measured) : kDash,
                sp.found ? fmtNum(sp.time) : kDash,
                sp.found ? fmtNum(sp.time - r.t1) : kDash, // from the earlier cursor, like the CSV
            };
            for (int c = 0; c < 5; ++c)
                m_setpointTable->setItem(spRow, c + 1, makeItem(texts[c], true));
            ++spRow;
        }
    }
}

void AnalysisView::rememberDataState()
{
    if (m_recorder && m_recorder->size() > 0) {
        m_lastSize = m_recorder->size();
        m_lastTime = m_recorder->timeAt(m_lastSize - 1);
        m_lastEvicted = m_recorder->evictedCount();
    } else {
        m_lastSize = 0;
        m_lastTime = 0.0;
    }
}

void AnalysisView::recompute()
{
    m_results.clear();
    const PlotCursors *cur = m_plot->cursors();
    if (m_recorder && cur->hasC1() && cur->hasC2()) {
        for (const Tag &t : std::as_const(m_tags)) {
            if (t.analyzed)
                m_results.append(analyzeStep(*m_recorder, t.id, cur->c1(), cur->c2(), m_settings));
        }
    }
    rememberDataState();
    rebuildTables();
    rebuildMarkers();
    m_plot->plot()->replot();

    const bool anyValid = std::any_of(m_results.cbegin(), m_results.cend(),
                                      [](const StepResult &r) { return r.valid; });
    m_exportButton->setEnabled(anyValid);
    m_copyResultsButton->setEnabled(anyValid);
    emit resultsChanged();
}

QString AnalysisView::readoutText() const
{
    const PlotCursors *cur = m_plot->cursors();
    if (!cur->hasC1() || !cur->hasC2())
        return {};

    QStringList lines{QStringLiteral("C1 = ") + fmtSeconds(cur->c1())
                      + QStringLiteral(", C2 = ") + fmtSeconds(cur->c2())};
    for (const StepResult &r : m_results) {
        const Tag *tag = tagById(r.tagId);
        const QString name = tag ? tagLabel(*tag) : QString::number(r.tagId);
        if (!r.valid) {
            lines << name + QStringLiteral(": ") + r.error;
            continue;
        }
        QStringList sps;
        for (const SetpointResult &sp : r.setpoints) {
            sps << fmtPercent(sp.percent)
                       + (sp.found ? QStringLiteral(" at ") + fmtSeconds(sp.time)
                                   : QStringLiteral(" not found"));
        }
        QString line = name + QStringLiteral(": ") + sps.join(QStringLiteral(", "));
        QStringList model;
        if (r.gain)
            model << QStringLiteral("K = ") + fmtNum(*r.gain);
        if (r.deadTime)
            model << QStringLiteral("L = ") + fmtNum(*r.deadTime) + QStringLiteral(" s");
        if (r.timeConstant)
            model << QStringLiteral("T = ") + fmtNum(*r.timeConstant) + QStringLiteral(" s");
        if (!model.isEmpty())
            line += QStringLiteral("; ") + model.join(QStringLiteral(", "));
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

void AnalysisView::refresh()
{
    m_plot->refresh();
    if (!m_recorder)
        return;

    const qsizetype size = m_recorder->size();
    const double last = size > 0 ? m_recorder->timeAt(size - 1) : 0.0;
    const bool reset = size < m_lastSize || (size > 0 && m_lastSize > 0 && last < m_lastTime);
    const bool grew = size > m_lastSize || (size > 0 && last > m_lastTime);
    if (reset) {
        recompute();
        return;
    }
    if (!grew)
        return;

    const PlotCursors *cur = m_plot->cursors();
    if (!cur->hasC1() || !cur->hasC2()) {
        rememberDataState();
        return;
    }
    const double limit = std::max(cur->c1(), cur->c2()) + m_settings.smoothingS / 2.0;
    // Once the recording is full, old rows are evicted; results that used them must update.
    const bool lostData = m_recorder->evictedCount() != m_lastEvicted
                          && std::min(cur->c1(), cur->c2()) - m_settings.referenceWindowS
                                 - m_settings.smoothingS / 2.0 < m_recorder->timeAt(0);
    if (m_lastSize == 0 || m_lastTime < limit || lostData)
        recompute();
    else
        rememberDataState();
}

void AnalysisView::resetData()
{
    m_plot->resetData();
    recompute();
}

void AnalysisView::setCursors(std::optional<double> t1, std::optional<double> t2)
{
    PlotCursors *cur = m_plot->cursors();
    m_settingCursors = true;
    if (!t1 || !t2)
        cur->clear();
    if (t1)
        cur->setC1(*t1);
    if (t2)
        cur->setC2(*t2);
    m_settingCursors = false;
    recompute();
}

} // namespace mb
