#include "AnalysisPanel.h"

#include "AnalysisCommon.h"
#include "ClipboardCopy.h"
#include "ClosedLoopPage.h"
#include "OpenLoopPage.h"
#include "PlotCursors.h"
#include "NumberSpinBox.h"
#include "NumberText.h"
#include "Recorder.h"
#include "TrendPlot.h"

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_map.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mb {

using namespace analysis;

namespace {

constexpr int kMaxPercents = 20;
constexpr int kGuideRtti = QwtPlotItem::Rtti_PlotUserItem + 1;

/// Setpoint marker: a white disc with a dark ring, so it stands out on a curve of any colour,
/// and a small dot of the tag's colour in the middle to show which tag it belongs to.
class SetpointSymbol : public QwtSymbol
{
public:
    static constexpr int kSize = 13;
    explicit SetpointSymbol(const QColor &tagColor)
        : QwtSymbol(QwtSymbol::Ellipse, QBrush(Qt::white), QPen(QColor(0x20, 0x20, 0x20), 2),
                    QSize(kSize, kSize)),
          m_tagColor(tagColor)
    {
    }
    QColor tagColor() const { return m_tagColor; }

    void renderSymbols(QPainter *painter, const QPointF *points, int numPoints) const override
    {
        QwtSymbol::renderSymbols(painter, points, numPoints); // white disc, dark ring
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_tagColor);
        for (int i = 0; i < numPoints; ++i)
            painter->drawEllipse(points[i], 2.5, 2.5);
        painter->restore();
    }

private:
    QColor m_tagColor;
};

struct GuidePoint {
    QPointF pos;
    QColor color;
};

/// Dashed guide lines from setpoint points down to the bottom and left to the left edge of
/// the canvas; computed at draw time so they follow zoom and pan.
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

AnalysisPanel::AnalysisPanel(QWidget *parent)
    : QWidget(parent), m_ctx(std::make_unique<Context>())
{
    buildUi();
    syncControlsFromSettings();
    recompute();
}

AnalysisPanel::~AnalysisPanel()
{
    clearOverlays();
}

QWidget *AnalysisPanel::setpointsPage() const
{
    return m_setpointsPage;
}

// ---------------------------------------------------------------------------------------------
// UI

void AnalysisPanel::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *body = new QWidget;
    scroll->setWidget(body);
    auto *col = new QVBoxLayout(body);

    // Buttons
    auto *buttons = new QHBoxLayout;
    m_copyResultsButton = new QPushButton(tr("Copy results"), body);
    m_copyResultsButton->setObjectName(QStringLiteral("copyResultsButton"));
    m_copyResultsButton->setEnabled(false);
    m_exportButton = new QPushButton(tr("Export results (CSV)..."), body);
    m_exportButton->setObjectName(QStringLiteral("exportButton"));
    m_exportButton->setEnabled(false);
    buttons->addWidget(m_copyResultsButton);
    buttons->addWidget(m_exportButton);
    col->addLayout(buttons);
    connect(m_copyResultsButton, &QPushButton::clicked, this, [this] { copyResults(); });
    connect(m_exportButton, &QPushButton::clicked, this, &AnalysisPanel::exportResultsRequested);

    // 1. Signals
    auto *signalsGroup = new QGroupBox(tr("1. Signals"), body);
    signalsGroup->setObjectName(QStringLiteral("signalsGroup"));
    auto *sg = new QGridLayout(signalsGroup);
    m_pvCombo = new QComboBox(signalsGroup);
    m_pvCombo->setObjectName(QStringLiteral("pvCombo"));
    m_pvCombo->setToolTip(tr("The process value (PV): the measured signal that responds to the step."));
    m_coCombo = new QComboBox(signalsGroup);
    m_coCombo->setObjectName(QStringLiteral("coCombo"));
    m_coCombo->setToolTip(
        tr("The controller output (CO) that was stepped. Its change between the cursors is ΔCO; "
           "choose \"Typed ΔCO\" to enter ΔCO by hand."));
    m_deltaCoSpin = makeSpin("deltaCoSpin", -1e9, 1e9, 4, 0.0, signalsGroup);
    m_deltaCoSpin->setToolTip(tr("Change of the controller output (0 = unknown, no gain K)."));
    m_spCombo = new QComboBox(signalsGroup);
    m_spCombo->setObjectName(QStringLiteral("spCombo"));
    m_spCombo->setToolTip(tr("The setpoint (SP) of the control loop, used by the closed-loop "
                             "analysis. Choose \"Typed\" to enter it by hand there."));
    m_pvMinEdit = new QLineEdit(signalsGroup);
    m_pvMinEdit->setObjectName(QStringLiteral("pvMinEdit"));
    m_pvMinEdit->setPlaceholderText(tr("PV min"));
    m_pvMaxEdit = new QLineEdit(signalsGroup);
    m_pvMaxEdit->setObjectName(QStringLiteral("pvMaxEdit"));
    m_pvMaxEdit->setPlaceholderText(tr("PV max"));
    for (QLineEdit *e : {m_pvMinEdit, m_pvMaxEdit}) {
        e->setValidator(new NumberValidator(e)); // 72,3 or 72.3
        e->setToolTip(tr("Optional PV range (empty = not set). With both PV and CO ranges the "
                         "gains are also shown in %/%."));
    }
    m_coMinSpin = makeSpin("coMinSpin", -1e9, 1e9, 3, 0.0, signalsGroup);
    m_coMaxSpin = makeSpin("coMaxSpin", -1e9, 1e9, 3, 100.0, signalsGroup);
    m_coMinSpin->setToolTip(tr("CO range minimum (for gains in %/%)."));
    m_coMaxSpin->setToolTip(tr("CO range maximum (for gains in %/%)."));
    sg->addWidget(new QLabel(tr("Process value (PV)"), signalsGroup), 0, 0);
    sg->addWidget(m_pvCombo, 0, 1, 1, 2);
    sg->addWidget(new QLabel(tr("Controller output (CO)"), signalsGroup), 1, 0);
    sg->addWidget(m_coCombo, 1, 1, 1, 2);
    sg->addWidget(new QLabel(kDelta + tr("CO (typed)"), signalsGroup), 2, 0);
    sg->addWidget(m_deltaCoSpin, 2, 1, 1, 2);
    sg->addWidget(new QLabel(tr("Setpoint (SP)"), signalsGroup), 3, 0);
    sg->addWidget(m_spCombo, 3, 1, 1, 2);
    sg->addWidget(new QLabel(tr("Ranges for %/% gains"), signalsGroup), 4, 0, 1, 3);
    sg->addWidget(new QLabel(tr("PV"), signalsGroup), 5, 0);
    sg->addWidget(m_pvMinEdit, 5, 1);
    sg->addWidget(m_pvMaxEdit, 5, 2);
    sg->addWidget(new QLabel(tr("CO"), signalsGroup), 6, 0);
    sg->addWidget(m_coMinSpin, 6, 1);
    sg->addWidget(m_coMaxSpin, 6, 2);
    col->addWidget(signalsGroup);
    connect(m_pvCombo, &QComboBox::currentIndexChanged, this, [this] { onPvChanged(); });
    connect(m_coCombo, &QComboBox::currentIndexChanged, this, [this] { onCoChanged(); });
    connect(m_deltaCoSpin, &QDoubleSpinBox::valueChanged, this, [this] { onCoChanged(); });
    connect(m_spCombo, &QComboBox::currentIndexChanged, this, [this] { onSpChanged(); });
    connect(m_pvMinEdit, &QLineEdit::textChanged, this, [this] { onRangesChanged(); });
    connect(m_pvMaxEdit, &QLineEdit::textChanged, this, [this] { onRangesChanged(); });
    connect(m_coMinSpin, &QDoubleSpinBox::valueChanged, this, [this] { onRangesChanged(); });
    connect(m_coMaxSpin, &QDoubleSpinBox::valueChanged, this, [this] { onRangesChanged(); });

    // 2. Cursors
    auto *cursorsGroup = new QGroupBox(tr("2. Cursors"), body);
    cursorsGroup->setObjectName(QStringLiteral("cursorsGroup"));
    auto *cg = new QVBoxLayout(cursorsGroup);
    m_cursorLabel = new QLabel(cursorsGroup);
    m_cursorLabel->setObjectName(QStringLiteral("cursorLabel"));
    m_findStepButton = new QPushButton(tr("Find step"), cursorsGroup);
    m_findStepButton->setObjectName(QStringLiteral("findStepButton"));
    m_hintLabel = new QLabel(cursorsGroup);
    m_hintLabel->setObjectName(QStringLiteral("hintLabel"));
    m_hintLabel->setWordWrap(true);
    cg->addWidget(m_cursorLabel);
    cg->addWidget(m_findStepButton);
    cg->addWidget(m_hintLabel);
    col->addWidget(cursorsGroup);
    connect(m_findStepButton, &QPushButton::clicked, this, [this] { onFindStep(); });

    // Mode switch and the pages
    auto *modeSwitch = new QWidget(body);
    modeSwitch->setObjectName(QStringLiteral("modeSwitch"));
    auto *ml = new QHBoxLayout(modeSwitch);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    m_openLoopButton = new QToolButton(modeSwitch);
    m_openLoopButton->setObjectName(QStringLiteral("openLoopButton"));
    m_openLoopButton->setText(tr("Open loop"));
    m_openLoopButton->setToolTip(
        tr("Open loop: identify a model from a step of the controller output and tune the "
           "controller."));
    m_closedLoopButton = new QToolButton(modeSwitch);
    m_closedLoopButton->setObjectName(QStringLiteral("closedLoopButton"));
    m_closedLoopButton->setText(tr("Closed loop"));
    m_closedLoopButton->setToolTip(
        tr("Closed loop: performance of a recorded response of the control loop."));
    auto *modeGroup = new QButtonGroup(modeSwitch);
    modeGroup->setExclusive(true);
    for (QToolButton *b : {m_openLoopButton, m_closedLoopButton}) {
        b->setCheckable(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ml->addWidget(b);
    }
    modeGroup->addButton(m_openLoopButton, static_cast<int>(AnalysisMode::OpenLoop));
    modeGroup->addButton(m_closedLoopButton, static_cast<int>(AnalysisMode::ClosedLoop));
    m_openLoopButton->setChecked(true);
    col->addWidget(modeSwitch);
    connect(modeGroup, &QButtonGroup::idClicked, this,
            [this](int id) { onModeClicked(static_cast<AnalysisMode>(id)); });

    m_modeStack = new QStackedWidget(body);
    m_modeStack->setObjectName(QStringLiteral("modeStack"));
    const auto edited = [this] { userChangedSettings(); };
    m_openLoopPage = new OpenLoopPage(m_ctx.get(), edited, m_modeStack);
    m_closedLoopPage = new ClosedLoopPage(m_ctx.get(), edited, m_modeStack);
    m_modeStack->addWidget(m_openLoopPage);
    m_modeStack->addWidget(m_closedLoopPage);
    col->addWidget(m_modeStack);

    // Setpoints page: its own dock in the main window (hidden child until reparented).
    m_setpointsPage = new QWidget(this);
    m_setpointsPage->setObjectName(QStringLiteral("setpointsPage"));
    m_setpointsPage->hide();
    auto *spOuter = new QVBoxLayout(m_setpointsPage);
    spOuter->setContentsMargins(0, 0, 0, 0);
    auto *spScroll = new QScrollArea(m_setpointsPage);
    spScroll->setWidgetResizable(true);
    spScroll->setFrameShape(QFrame::NoFrame);
    spOuter->addWidget(spScroll);
    auto *spContent = new QWidget;
    spScroll->setWidget(spContent);
    auto *sp = new QGridLayout(spContent);

    m_setpointTagList = new QListWidget(spContent);
    m_setpointTagList->setObjectName(QStringLiteral("setpointTagList"));
    m_setpointTagList->setMaximumHeight(110);
    m_referenceCombo = new QComboBox(spContent);
    m_referenceCombo->setObjectName(QStringLiteral("referenceCombo"));
    m_referenceCombo->addItem(tr("Value at cursor"), static_cast<int>(ReferenceMode::CursorValue));
    m_referenceCombo->addItem(tr("Mean before cursor"), static_cast<int>(ReferenceMode::WindowMean));
    m_referenceCombo->setToolTip(
        tr("Value at cursor: the recorded value at each cursor.\n"
           "Mean before cursor: the mean of the W seconds ending at each cursor."));
    m_windowSpin = makeSpin("windowSpin", 0.1, 3600.0, 1, 1.0, spContent);
    m_windowSpin->setSuffix(tr(" s"));
    m_windowSpin->setToolTip(tr("Window W of the mean before each cursor."));
    m_smoothingSpin = makeSpin("smoothingSpin", 0.0, 3600.0, 2, 0.0, spContent);
    m_smoothingSpin->setSuffix(tr(" s"));
    m_smoothingSpin->setSpecialValueText(tr("Off"));
    m_smoothingSpin->setToolTip(
        tr("Centred moving average of width S applied before finding the setpoint times.\n"
           "Smoothing makes the 0 % point early by up to S/2 (and L biased low); when "
           "smoothing is used, choose a model \"From %\" above 0 (e.g. 10)."));
    m_percentList = new QListWidget(spContent);
    m_percentList->setObjectName(QStringLiteral("percentList"));
    m_percentList->setMaximumHeight(110);
    m_percentSpin = makeSpin("percentSpin", 0.0, 100.0, 2, 50.0, spContent); // not in the default list
    m_percentSpin->setSuffix(QStringLiteral(" %"));
    m_addPercentButton = new QPushButton(tr("Add"), spContent);
    m_addPercentButton->setObjectName(QStringLiteral("addPercentButton"));
    m_removePercentButton = new QPushButton(tr("Remove"), spContent);
    m_removePercentButton->setObjectName(QStringLiteral("removePercentButton"));
    m_detailedLabelsCheck = new QCheckBox(tr("Show time and value on plot labels"), spContent);
    m_detailedLabelsCheck->setObjectName(QStringLiteral("detailedLabelsCheck"));
    m_guideLinesCheck = new QCheckBox(tr("Guide lines"), spContent);
    m_guideLinesCheck->setObjectName(QStringLiteral("guideLinesCheck"));
    m_setpointTable = new QTableWidget(0, 6, spContent);
    m_setpointTable->setObjectName(QStringLiteral("setpointTable"));
    m_setpointTable->setHorizontalHeaderLabels(QStringList{
        tr("Tag"), QStringLiteral("%"), tr("Target"), tr("Measured"), tr("Time (s)"),
        QStringLiteral("t ") + kMinus + QStringLiteral(" C1 (s)")});
    m_setpointTable->verticalHeader()->setVisible(false);
    m_setpointTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_setpointTable->setMinimumHeight(140);
    setupTable(m_setpointTable);

    sp->addWidget(new QLabel(tr("Show setpoints on"), spContent), 0, 0, 1, 3);
    sp->addWidget(m_setpointTagList, 1, 0, 1, 3);
    sp->addWidget(new QLabel(tr("Reference"), spContent), 2, 0);
    sp->addWidget(m_referenceCombo, 2, 1, 1, 2);
    sp->addWidget(new QLabel(tr("Window"), spContent), 3, 0);
    sp->addWidget(m_windowSpin, 3, 1, 1, 2);
    sp->addWidget(new QLabel(tr("Smoothing"), spContent), 4, 0);
    sp->addWidget(m_smoothingSpin, 4, 1, 1, 2);
    sp->addWidget(new QLabel(tr("Setpoints (%)"), spContent), 5, 0, 1, 3);
    sp->addWidget(m_percentList, 6, 0, 3, 2);
    sp->addWidget(m_percentSpin, 6, 2);
    sp->addWidget(m_addPercentButton, 7, 2);
    sp->addWidget(m_removePercentButton, 8, 2);
    sp->addWidget(m_detailedLabelsCheck, 9, 0, 1, 3);
    sp->addWidget(m_guideLinesCheck, 10, 0, 1, 3);
    sp->addWidget(m_setpointTable, 11, 0, 1, 3);
    sp->setRowStretch(11, 1);
    col->addStretch(1);

    connect(m_setpointTagList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) { onSetpointItemChanged(item); });
    connect(m_referenceCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        m_ctx->settings.reference = m_referenceCombo->currentData().toInt() == 1
                                   ? ReferenceMode::WindowMean : ReferenceMode::CursorValue;
        updateEnabled();
        userChangedSettings();
    });
    connect(m_windowSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctx->settings.referenceWindowS = v;
        userChangedSettings();
    });
    connect(m_smoothingSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctx->settings.smoothingS = v;
        userChangedSettings();
    });
    connect(m_addPercentButton, &QPushButton::clicked, this, [this] { onAddPercent(); });
    connect(m_removePercentButton, &QPushButton::clicked, this, [this] { onRemovePercent(); });
    connect(m_detailedLabelsCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_ctx->settings.detailedLabels = on;
        userChangedSettings();
    });
    connect(m_guideLinesCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_ctx->settings.guideLines = on;
        userChangedSettings();
    });
}

// ---------------------------------------------------------------------------------------------
// Public API

void AnalysisPanel::setRecorder(const Recorder *recorder)
{
    m_ctx->recorder = recorder;
    recompute();
}

void AnalysisPanel::setPlot(TrendPlot *plot)
{
    if (plot == m_ctx->plot)
        return;
    clearOverlays();
    disconnect(m_cursorsConnection);
    disconnect(m_plotDestroyedConnection);
    m_ctx->plot = plot;
    if (m_ctx->plot) {
        m_cursorsConnection = connect(m_ctx->plot->cursors(), &PlotCursors::cursorsChanged, this, [this] {
            if (!m_settingCursors) {
                m_ctx->findNotSettled = false; // the user moved a cursor: Find step's verdict is stale
                recompute();
            }
        });
        // The plot owns (and deletes) the overlay items when it goes away first.
        m_plotDestroyedConnection = connect(m_ctx->plot, &QObject::destroyed, this, [this] {
            m_markers.clear();
            m_guideItems.clear();
            m_openLoopPage->forgetOverlays();
            m_closedLoopPage->forgetOverlays();
            m_ctx->plot = nullptr;
            recompute();
        });
    }
    recompute();
}

void AnalysisPanel::setTags(const QList<Tag> &tags)
{
    m_ctx->tags = tags;
    // Keep the PV / CO while their tags exist.
    if (!m_ctx->tagById(m_ctx->settings.pvTagId))
        m_ctx->settings.pvTagId = 0;
    if (!m_ctx->tagById(m_ctx->settings.deltaCoTagId) || m_ctx->settings.deltaCoTagId == m_ctx->settings.pvTagId)
        m_ctx->settings.deltaCoTagId = 0;
    if (!m_ctx->tagById(m_ctx->settings.spTagId) || m_ctx->settings.spTagId == m_ctx->settings.pvTagId)
        m_ctx->settings.spTagId = 0;
    rebuildPvCombo();
    rebuildCoCombo();
    rebuildSpCombo();
    rebuildSetpointList();
    updateEnabled();
    recompute();
}

void AnalysisPanel::normalizeSettings()
{
    // Keep the list invariant (sorted, no duplicates, 0..100, at most 20) even for a hand-edited
    // project file.
    QList<double> percents;
    for (double p : std::as_const(m_ctx->settings.percents)) {
        if (p >= 0.0 && p <= 100.0)
            percents.append(p);
    }
    std::sort(percents.begin(), percents.end());
    percents.erase(std::unique(percents.begin(), percents.end(),
                               [](double a, double b) { return std::abs(a - b) < 1e-9; }),
                   percents.end());
    m_ctx->settings.percents = percents.mid(0, kMaxPercents);

    // Tags that are not in the project (deleted before the file was saved) mean "none".
    if (!m_ctx->tags.isEmpty()) {
        if (!m_ctx->tagById(m_ctx->settings.pvTagId))
            m_ctx->settings.pvTagId = 0;
        if (!m_ctx->tagById(m_ctx->settings.deltaCoTagId))
            m_ctx->settings.deltaCoTagId = 0;
        if (!m_ctx->tagById(m_ctx->settings.spTagId))
            m_ctx->settings.spTagId = 0;
    }
    if (m_ctx->settings.pvTagId != 0 && m_ctx->settings.deltaCoTagId == m_ctx->settings.pvTagId)
        m_ctx->settings.deltaCoTagId = 0;
    if (m_ctx->settings.pvTagId != 0 && m_ctx->settings.spTagId == m_ctx->settings.pvTagId)
        m_ctx->settings.spTagId = 0;
}

void AnalysisPanel::setSettings(const AnalysisSettings &settings)
{
    m_ctx->settings = settings;
    m_ctx->findNotSettled = false;
    normalizeSettings();
    syncControlsFromSettings();
    recompute();
}

AnalysisSettings AnalysisPanel::settings() const
{
    return m_ctx->settings;
}

QList<StepResult> AnalysisPanel::results() const
{
    return m_ctx->results;
}

// ---------------------------------------------------------------------------------------------
// Controls <-> settings

void AnalysisPanel::rebuildPvCombo()
{
    const QSignalBlocker blocker(m_pvCombo);
    m_pvCombo->clear();
    m_pvCombo->addItem(tr("Choose..."), 0u);
    for (const Tag &t : std::as_const(m_ctx->tags)) {
        if (!t.isBit())
            m_pvCombo->addItem(tagLabel(t), t.id);
    }
    const int idx = m_ctx->settings.pvTagId != 0 ? m_pvCombo->findData(m_ctx->settings.pvTagId) : -1;
    m_pvCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AnalysisPanel::rebuildCoCombo()
{
    const QSignalBlocker blocker(m_coCombo);
    m_coCombo->clear();
    m_coCombo->addItem(tr("Typed ΔCO"), 0u);
    for (const Tag &t : std::as_const(m_ctx->tags)) {
        if (!t.isBit() && (t.id != m_ctx->settings.pvTagId || m_ctx->settings.pvTagId == 0))
            m_coCombo->addItem(tagLabel(t), t.id);
    }
    const int idx = m_ctx->settings.deltaCoTagId != 0 ? m_coCombo->findData(m_ctx->settings.deltaCoTagId) : -1;
    m_coCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AnalysisPanel::rebuildSpCombo()
{
    // Same rules as the CO combo: every non-bit tag except the PV.
    const QSignalBlocker blocker(m_spCombo);
    m_spCombo->clear();
    m_spCombo->addItem(tr("Typed"), 0u);
    for (const Tag &t : std::as_const(m_ctx->tags)) {
        if (!t.isBit() && (t.id != m_ctx->settings.pvTagId || m_ctx->settings.pvTagId == 0))
            m_spCombo->addItem(tagLabel(t), t.id);
    }
    const int idx = m_ctx->settings.spTagId != 0 ? m_spCombo->findData(m_ctx->settings.spTagId) : -1;
    m_spCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AnalysisPanel::rebuildSetpointList()
{
    const QSignalBlocker blocker(m_setpointTagList);
    m_setpointTagList->clear();
    for (const Tag &t : std::as_const(m_ctx->tags)) {
        if (t.isBit() || (!t.plotted && t.id != m_ctx->settings.pvTagId))
            continue;
        auto *item = new QListWidgetItem(tagLabel(t), m_setpointTagList);
        item->setData(Qt::UserRole, t.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(m_ctx->settings.setpointTagIds.contains(t.id) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::DecorationRole, QColor(t.color));
    }
}

void AnalysisPanel::rebuildPercentList()
{
    const QSignalBlocker blocker(m_percentList);
    m_percentList->clear();
    for (double p : std::as_const(m_ctx->settings.percents)) {
        auto *item = new QListWidgetItem(fmtPercent(p), m_percentList);
        item->setData(Qt::UserRole, p);
    }
}

void AnalysisPanel::updateEnabled()
{
    m_deltaCoSpin->setEnabled(m_coCombo->currentIndex() == 0);
    m_windowSpin->setEnabled(m_ctx->settings.reference == ReferenceMode::WindowMean);
}

void AnalysisPanel::updateModeSwitch()
{
    // Only the shown page and the overlays on the plot change; nothing is recomputed. The hidden
    // page's height is ignored so the stack is as tall as the shown page.
    const bool open = m_ctx->settings.mode != AnalysisMode::ClosedLoop;
    {
        const QSignalBlocker b1(m_openLoopButton);
        const QSignalBlocker b2(m_closedLoopButton);
        m_openLoopButton->setChecked(open);
        m_closedLoopButton->setChecked(!open);
    }
    QWidget *shown = open ? static_cast<QWidget *>(m_openLoopPage) : m_closedLoopPage;
    QWidget *hidden = open ? static_cast<QWidget *>(m_closedLoopPage) : m_openLoopPage;
    hidden->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    shown->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_modeStack->setCurrentWidget(shown);
    m_modeStack->updateGeometry();
    const bool changed = m_openLoopPage->overlaysShown() != open
                         || m_closedLoopPage->overlaysShown() == open;
    m_openLoopPage->setOverlaysShown(open);
    m_closedLoopPage->setOverlaysShown(!open);
    if (changed && m_ctx->plot)
        m_ctx->plot->plot()->replot();
}

void AnalysisPanel::onModeClicked(AnalysisMode mode)
{
    if (m_updating || mode == m_ctx->settings.mode)
        return;
    m_ctx->settings.mode = mode;
    updateModeSwitch();
    emit settingsChanged();
}

void AnalysisPanel::syncControlsFromSettings()
{
    const bool was = m_updating;
    m_updating = true;
    rebuildPvCombo();
    rebuildCoCombo();
    rebuildSpCombo();
    rebuildSetpointList();
    rebuildPercentList();
    {
        const QSignalBlocker b1(m_referenceCombo);
        const QSignalBlocker b2(m_windowSpin);
        const QSignalBlocker b3(m_smoothingSpin);
        const QSignalBlocker b5(m_deltaCoSpin);
        const QSignalBlocker b6(m_detailedLabelsCheck);
        const QSignalBlocker b7(m_guideLinesCheck);
        const QSignalBlocker b9(m_pvMinEdit);
        const QSignalBlocker b10(m_pvMaxEdit);
        const QSignalBlocker b11(m_coMinSpin);
        const QSignalBlocker b12(m_coMaxSpin);
        m_guideLinesCheck->setChecked(m_ctx->settings.guideLines);
        m_detailedLabelsCheck->setChecked(m_ctx->settings.detailedLabels);
        m_referenceCombo->setCurrentIndex(m_ctx->settings.reference == ReferenceMode::WindowMean ? 1 : 0);
        m_windowSpin->setValue(m_ctx->settings.referenceWindowS);
        m_smoothingSpin->setValue(m_ctx->settings.smoothingS);
        m_deltaCoSpin->setValue(m_ctx->settings.manualDeltaCo);
        m_pvMinEdit->setText(m_ctx->settings.pvRangeMin ? QLocale::c().toString(*m_ctx->settings.pvRangeMin, 'g', 10)
                                                   : QString());
        m_pvMaxEdit->setText(m_ctx->settings.pvRangeMax ? QLocale::c().toString(*m_ctx->settings.pvRangeMax, 'g', 10)
                                                   : QString());
        m_coMinSpin->setValue(m_ctx->settings.coRangeMin);
        m_coMaxSpin->setValue(m_ctx->settings.coRangeMax);
    }
    updateEnabled();
    m_openLoopPage->syncFromSettings();
    m_closedLoopPage->syncFromSettings();
    updateModeSwitch();
    m_updating = was;
}

void AnalysisPanel::userChangedSettings()
{
    recompute();
    emit settingsChanged();
}

void AnalysisPanel::onPvChanged()
{
    if (m_updating)
        return;
    const TagId id = m_pvCombo->currentData().toUInt();
    m_ctx->findNotSettled = false;
    m_ctx->settings.pvTagId = id;
    if (id != 0 && !m_ctx->settings.setpointTagIds.contains(id))
        m_ctx->settings.setpointTagIds.append(id);
    if (id != 0 && m_ctx->settings.deltaCoTagId == id)
        m_ctx->settings.deltaCoTagId = 0;
    if (id != 0 && m_ctx->settings.spTagId == id)
        m_ctx->settings.spTagId = 0;
    rebuildCoCombo();
    rebuildSpCombo();
    rebuildSetpointList();
    updateEnabled();
    userChangedSettings();
}

void AnalysisPanel::onCoChanged()
{
    if (m_updating)
        return;
    m_ctx->findNotSettled = false;
    m_ctx->settings.deltaCoTagId = m_coCombo->currentData().toUInt();
    m_ctx->settings.manualDeltaCo = m_deltaCoSpin->value();
    updateEnabled();
    userChangedSettings();
}

void AnalysisPanel::onSpChanged()
{
    if (m_updating)
        return;
    m_ctx->settings.spTagId = m_spCombo->currentData().toUInt();
    userChangedSettings();
}

void AnalysisPanel::onRangesChanged()
{
    if (m_updating)
        return;
    const auto pvMin = parseNumber(m_pvMinEdit->text());
    const auto pvMax = parseNumber(m_pvMaxEdit->text());
    const bool changed = pvMin != m_ctx->settings.pvRangeMin || pvMax != m_ctx->settings.pvRangeMax
                         || m_coMinSpin->value() != m_ctx->settings.coRangeMin
                         || m_coMaxSpin->value() != m_ctx->settings.coRangeMax;
    if (!changed)
        return;
    m_ctx->settings.pvRangeMin = pvMin;
    m_ctx->settings.pvRangeMax = pvMax;
    m_ctx->settings.coRangeMin = m_coMinSpin->value();
    m_ctx->settings.coRangeMax = m_coMaxSpin->value();
    userChangedSettings();
}

void AnalysisPanel::onAddPercent()
{
    if (m_updating || m_ctx->settings.percents.size() >= kMaxPercents)
        return;
    const double p = std::round(m_percentSpin->value() * 100.0) / 100.0;
    for (double q : std::as_const(m_ctx->settings.percents)) {
        if (std::abs(q - p) < 1e-9)
            return;
    }
    m_ctx->settings.percents.append(p);
    std::sort(m_ctx->settings.percents.begin(), m_ctx->settings.percents.end());
    rebuildPercentList();
    userChangedSettings();
}

void AnalysisPanel::onRemovePercent()
{
    if (m_updating)
        return;
    const int row = m_percentList->currentRow();
    if (row < 0 || row >= m_ctx->settings.percents.size())
        return;
    m_ctx->settings.percents.removeAt(row);
    rebuildPercentList();
    userChangedSettings();
}

void AnalysisPanel::onSetpointItemChanged(QListWidgetItem *item)
{
    if (m_updating || !item)
        return;
    const TagId id = item->data(Qt::UserRole).toUInt();
    const bool checked = item->checkState() == Qt::Checked;
    if (checked && !m_ctx->settings.setpointTagIds.contains(id))
        m_ctx->settings.setpointTagIds.append(id);
    else if (!checked)
        m_ctx->settings.setpointTagIds.removeAll(id);
    else
        return;
    userChangedSettings();
}

// ---------------------------------------------------------------------------------------------
// Find step

void AnalysisPanel::onFindStep()
{
    const TagId co = m_ctx->settings.deltaCoTagId;
    const TagId pv = m_ctx->settings.pvTagId;
    if (!m_ctx->recorder || !m_ctx->plot || co == 0 || pv == 0)
        return;
    m_hintOverride.clear();
    m_ctx->findNotSettled = false;
    if (m_ctx->recorder->size() < 2) {
        m_hintOverride = tr("No step found in the controller output in the visible range.");
        updateHint();
        return;
    }
    const double first = m_ctx->recorder->timeAt(0);
    const double last = m_ctx->recorder->timeAt(m_ctx->recorder->size() - 1);
    double from = first;
    double to = last;
    if (QwtPlot *p = m_ctx->plot->plot()) {
        const QwtInterval iv = p->axisInterval(QwtPlot::xBottom);
        if (iv.isValid()) {
            from = std::max(first, iv.minValue());
            to = std::min(last, iv.maxValue());
        }
    }
    auto hasCoSamples = [&](double a, double b) {
        if (!(b > a))
            return false;
        const auto range = m_ctx->recorder->rowRange(a, b);
        for (qsizetype i = range.first; i < range.second; ++i) {
            if (!std::isnan(m_ctx->recorder->valueAt(i, co)))
                return true;
        }
        return false;
    };
    if (!hasCoSamples(from, to)) {
        from = first;
        to = last;
    }
    const std::optional<StepWindow> w = findStep(*m_ctx->recorder, co, pv, from, to);
    if (!w) {
        m_hintOverride = tr("No step found in the controller output in the visible range.");
        updateHint();
        return;
    }
    PlotCursors *cur = m_ctx->plot->cursors();
    m_settingCursors = true;
    cur->setC1(w->c1);
    cur->setC2(w->c2);
    m_settingCursors = false;
    m_ctx->findNotSettled = !w->settled;
    recompute();
}

// ---------------------------------------------------------------------------------------------
// Recompute

void AnalysisPanel::rememberDataState()
{
    if (m_ctx->recorder && m_ctx->recorder->size() > 0) {
        m_lastSize = m_ctx->recorder->size();
        m_lastTime = m_ctx->recorder->timeAt(m_lastSize - 1);
        m_lastEvicted = m_ctx->recorder->evictedCount();
    } else {
        m_lastSize = 0;
        m_lastTime = 0.0;
    }
}

void AnalysisPanel::recompute()
{
    m_hintOverride.clear();
    m_ctx->results.clear();
    const PlotCursors *cur = m_ctx->plot ? m_ctx->plot->cursors() : nullptr;
    if (m_ctx->recorder && cur && cur->hasC1() && cur->hasC2() && m_ctx->settings.pvTagId != 0) {
        m_ctx->results.append(
            analyzeStep(*m_ctx->recorder, m_ctx->settings.pvTagId, cur->c1(), cur->c2(), m_ctx->settings));
        for (const Tag &t : std::as_const(m_ctx->tags)) {
            // Unplotted setpoint tags show nothing, so they are not analysed either (the CSV
            // export then matches what the panel shows). The PV is analysed even unplotted.
            if (t.id != m_ctx->settings.pvTagId && t.plotted && !t.isBit()
                && m_ctx->settings.setpointTagIds.contains(t.id))
                m_ctx->results.append(analyzeStep(*m_ctx->recorder, t.id, cur->c1(), cur->c2(), m_ctx->settings));
        }
    }
    rememberDataState();

    const bool valid = !m_ctx->results.isEmpty() && m_ctx->results.first().valid;
    m_copyResultsButton->setEnabled(valid);
    m_exportButton->setEnabled(valid);

    updateCursorLabel();
    updateFindButton();
    m_openLoopPage->recompute();
    m_closedLoopPage->recompute();
    rebuildSetpointTable();
    rebuildOverlays();
    updateHint();
    emit resultsChanged();
}

void AnalysisPanel::updateCursorLabel()
{
    const PlotCursors *cur = m_ctx->plot ? m_ctx->plot->cursors() : nullptr;
    const QString c1 = (cur && cur->hasC1()) ? fmtSeconds(cur->c1()) : kDash;
    const QString c2 = (cur && cur->hasC2()) ? fmtSeconds(cur->c2()) : kDash;
    m_cursorLabel->setText(QStringLiteral("C1 = %1, C2 = %2").arg(c1, c2));
}

void AnalysisPanel::updateHint()
{
    QString text;
    const PlotCursors *cur = m_ctx->plot ? m_ctx->plot->cursors() : nullptr;
    if (m_ctx->settings.pvTagId == 0) {
        text = tr("Choose the process value (PV) in step 1.");
    } else if (!(cur && cur->hasC1() && cur->hasC2())) {
        text = tr("Place C1 just before the step and C2 after the PV has settled, or click Find step.");
    } else if (!m_ctx->results.isEmpty() && !m_ctx->results.first().valid) {
        text = m_ctx->results.first().error;
    }
    if (!m_hintOverride.isEmpty())
        text = m_hintOverride;
    m_hintLabel->setText(text);
}

void AnalysisPanel::updateFindButton()
{
    QString why;
    if (m_ctx->settings.pvTagId == 0)
        why = tr("Choose the process value (PV) in step 1.");
    else if (m_ctx->settings.deltaCoTagId == 0)
        why = tr("Choose the controller output (CO) in step 1: Find step looks for its step.");
    else if (!m_ctx->recorder)
        why = tr("There is no recording to search.");
    m_findStepButton->setEnabled(why.isEmpty());
    m_findStepButton->setToolTip(
        why.isEmpty()
            ? tr("Looks for the largest step of the controller output in the visible time range "
                 "(the whole recording if there is none) and places C1 and C2 around it.")
            : why);
}

void AnalysisPanel::rebuildSetpointTable()
{
    m_setpointTable->setRowCount(0);
    int row = 0;
    for (const StepResult &r : std::as_const(m_ctx->results)) {
        const Tag *tag = m_ctx->tagById(r.tagId);
        if (!r.valid || !m_ctx->shownTag(tag))
            continue;
        const QString label = tagLabel(*tag);
        const QColor color(tag->color);
        for (const SetpointResult &sp : r.setpoints) {
            m_setpointTable->insertRow(row);
            auto *ti = makeItem(label, false);
            if (color.isValid())
                ti->setData(Qt::DecorationRole, color);
            m_setpointTable->setItem(row, 0, ti);
            const QString texts[5] = {
                fmtPlain(sp.percent),
                g6(sp.target),
                sp.found ? g6(sp.measured) : kDash,
                sp.found ? g6(sp.time) : kDash,
                sp.found ? g6(sp.time - r.t1) : kDash, // from the earlier cursor, like the CSV
            };
            for (int c = 0; c < 5; ++c)
                m_setpointTable->setItem(row, c + 1,
                                         makeItem(texts[c], true, explainSetpoint(c + 1, r, sp, tag->name)));
            ++row;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Plot overlays

void AnalysisPanel::clearOverlays()
{
    m_openLoopPage->clearOverlays();
    m_closedLoopPage->clearOverlays();
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
}

void AnalysisPanel::rebuildOverlays()
{
    clearOverlays();
    if (!m_ctx->plot)
        return;
    QwtPlot *plot = m_ctx->plot->plot();
    // The pages attach their overlays only while their mode is shown.
    m_openLoopPage->rebuildOverlays();
    m_closedLoopPage->rebuildOverlays();
    // Setpoint markers and guide lines belong to the setpoints page: shown in both modes.
    QMap<int, QList<GuidePoint>> guidePoints; // by Y axis (QwtPlot::yLeft / yRight)
    for (const StepResult &r : std::as_const(m_ctx->results)) {
        const Tag *tag = m_ctx->tagById(r.tagId);
        if (!r.valid || !tag)
            continue;
        const QColor color(tag->color);
        const int axis = tag->axis == YAxis::Right ? QwtPlot::yRight : QwtPlot::yLeft;

        if (!m_ctx->shownTag(tag))
            continue;
        for (const SetpointResult &sp : r.setpoints) {
            if (!sp.found)
                continue;
            auto *marker = new QwtPlotMarker;
            marker->setValue(sp.time, sp.target);
            marker->setSymbol(new SetpointSymbol(color));
            marker->setZ(25.0); // above the curves and the model curve
            QString text = fmtPercent(sp.percent);
            if (m_ctx->settings.detailedLabels) {
                text += QStringLiteral("\nt = ") + QString::number(sp.time, 'f', 3)
                        + QStringLiteral(" s\ny = ") + g6(sp.target);
                if (!tag->unit.isEmpty())
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
            marker->attach(plot);
            m_markers.append(marker);
        }
    }
    if (m_ctx->settings.guideLines) {
        for (auto it = guidePoints.cbegin(); it != guidePoints.cend(); ++it) {
            auto *item = new GuideLineItem(it.value());
            item->setYAxis(it.key());
            item->attach(plot);
            m_guideItems.append(item);
        }
    }
    plot->replot();
}

// ---------------------------------------------------------------------------------------------
// Copy / readout

void AnalysisPanel::copyResults()
{
    // Open-loop tables (as before), the closed-loop part in Closed loop mode, the setpoint table.
    QString text = m_openLoopPage->copyText() + QStringLiteral("\r\n");
    if (m_ctx->settings.mode == AnalysisMode::ClosedLoop) {
        const QString closed = m_closedLoopPage->copyText();
        if (!closed.isEmpty())
            text += closed + QStringLiteral("\r\n");
    }
    text += tableToTsv(m_setpointTable->model(), {});
    QGuiApplication::clipboard()->setText(text);
}

QString AnalysisPanel::readoutText() const
{
    const PlotCursors *cur = m_ctx->plot ? m_ctx->plot->cursors() : nullptr;
    if (!cur || !cur->hasC1() || !cur->hasC2() || m_ctx->results.isEmpty() || !m_ctx->results.first().valid)
        return {};

    // No "C1 = ..., C2 = ..." line: the cursor readout composed before this text has it.
    QStringList lines;
    lines << m_openLoopPage->readoutText();
    if (m_ctx->settings.mode == AnalysisMode::ClosedLoop) {
        const QString closed = m_closedLoopPage->readoutText();
        if (!closed.isEmpty())
            lines << closed;
    }

    for (int i = 1; i < m_ctx->results.size(); ++i) {
        const StepResult &r = m_ctx->results.at(i);
        const Tag *tag = m_ctx->tagById(r.tagId);
        if (!m_ctx->shownTag(tag))
            continue;
        const QString name = tagLabel(*tag);
        if (!r.valid) {
            lines << name + QStringLiteral(": ") + r.error;
            continue;
        }
        QStringList sps;
        for (const SetpointResult &sp : r.setpoints) {
            sps << fmtPercent(sp.percent)
                       + (sp.found ? QStringLiteral(" ") + tr("at %1").arg(fmtSeconds(sp.time))
                                   : QStringLiteral(" ") + tr("not found"));
        }
        lines << name + QStringLiteral(": ") + sps.join(QStringLiteral(", "));
    }
    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------------------------
// Slots

void AnalysisPanel::refresh()
{
    if (!m_ctx->recorder)
        return;

    const qsizetype size = m_ctx->recorder->size();
    const double last = size > 0 ? m_ctx->recorder->timeAt(size - 1) : 0.0;
    const bool reset = size < m_lastSize || (size > 0 && m_lastSize > 0 && last < m_lastTime);
    const bool grew = size > m_lastSize || (size > 0 && last > m_lastTime);
    if (reset) {
        recompute();
        return;
    }
    if (!grew)
        return;

    const PlotCursors *cur = m_ctx->plot ? m_ctx->plot->cursors() : nullptr;
    if (!cur || !cur->hasC1() || !cur->hasC2()) {
        rememberDataState();
        return;
    }
    const double limit = std::max(cur->c1(), cur->c2()) + m_ctx->settings.smoothingS / 2.0;
    // Once the recording is full, old rows are evicted; results that used them must update.
    const bool lostData = m_ctx->recorder->evictedCount() != m_lastEvicted
                          && std::min(cur->c1(), cur->c2()) - m_ctx->settings.referenceWindowS
                                 - m_ctx->settings.smoothingS / 2.0 < m_ctx->recorder->timeAt(0);
    if (m_lastSize == 0 || m_lastTime < limit || lostData)
        recompute();
    else
        rememberDataState();
}

void AnalysisPanel::resetData()
{
    recompute();
}

// ---------------------------------------------------------------------------------------------
// Tooltips

QString AnalysisPanel::explainSetpoint(int column, const StepResult &r, const SetpointResult &sp,
                                       const QString &tagName) const
{
    const QString pu = m_ctx->pvUnit();
    const Tag *tag = m_ctx->tagById(r.tagId);
    const QString unit = tag ? tag->unit : pu;
    const QString p = g4(sp.percent / 100.0);
    QString name;
    QString meaning;
    QStringList lines;
    switch (column) {
    case 1:
        name = fmtPercent(sp.percent) + QStringLiteral(" ") + tagName;
        meaning = tr("the requested share of the step: 0 % is y0, 100 % is y100.");
        lines << tr("target = y0 + p (y100 - y0)");
        break;
    case 2:
        name = tr("Target");
        meaning = tr("the PV level of this setpoint.");
        lines << tr("target = y0 + p (y100 - y0)");
        lines << tr("target = %1 + %2 %3 (%4 - %5) = %6")
                     .arg(g4p(r.y0), p, kTimes, g4p(r.y100), g4p(r.y0), withUnit(g4(sp.target), unit));
        break;
    case 3:
        name = tr("Measured");
        meaning = tr("the recorded value at the found time, interpolated between the two "
                     "neighbouring samples.");
        lines << tr("measured = PV(t)");
        if (sp.found)
            lines << tr("measured = PV(%1) = %2")
                         .arg(fmtSeconds(sp.time), withUnit(g4(sp.measured), unit));
        break;
    case 4:
        name = tr("Time");
        if (sp.percent == 0.0) {
            meaning = tr("0 % is placed at C1, where the PV is at y0 = %1.")
                          .arg(withUnit(g4(sp.target), unit));
            lines << tr("t = time of C1");
        } else {
            meaning = tr("time at which the PV reaches y0 + p (y100 - y0) = %1.")
                          .arg(withUnit(g4(sp.target), unit));
            lines << tr("t = first time the PV crosses the target");
        }
        if (sp.found)
            lines << QStringLiteral("t = %1").arg(fmtSeconds(sp.time));
        else
            lines << tr("Not found between the cursors.");
        break;
    default:
        name = QStringLiteral("t ") + kMinus + QStringLiteral(" C1");
        meaning = tr("time from the earlier cursor to the setpoint.");
        lines << tr("t - C1 = time - %1").arg(fmtSeconds(r.t1));
        if (sp.found)
            lines << QStringLiteral("t - C1 = %1 - %2 = %3 s")
                         .arg(g4p(sp.time), g4p(r.t1), g4(sp.time - r.t1));
        break;
    }
    return tip(name, meaning, lines);
}

} // namespace mb
