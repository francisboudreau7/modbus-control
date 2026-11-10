#include "ClosedLoopPage.h"

#include "ClipboardCopy.h"
#include "PidForm.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "TrendPlot.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mb {

using namespace analysis;

namespace {

constexpr int kMaxPredictionSteps = 20000;

/// "unit·s" and similar; empty when there is no unit.
QString productUnit(const QString &unit, const QString &suffix)
{
    return unit.isEmpty() ? QString() : unit + suffix;
}

QString secondsText(const std::optional<double> &v)
{
    return v ? g4(*v) + QStringLiteral(" s") : kDash;
}

} // namespace

ClosedLoopPage::ClosedLoopPage(Context *context, std::function<void()> userEdited, QWidget *parent)
    : QWidget(parent), m_ctx(context), m_userEdited(std::move(userEdited))
{
    setObjectName(QStringLiteral("closedLoopPage"));
    buildUi();
    syncFromSettings();
}

ClosedLoopPage::~ClosedLoopPage()
{
    clearOverlays();
}

// ---------------------------------------------------------------------------------------------
// UI

void ClosedLoopPage::buildUi()
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);

    // 3. Test
    auto *testGroup = new QGroupBox(tr("3. Test"), this);
    testGroup->setObjectName(QStringLiteral("closedTestGroup"));
    auto *tg = new QGridLayout(testGroup);
    m_testCombo = new QComboBox(testGroup);
    m_testCombo->setObjectName(QStringLiteral("testCombo"));
    m_testCombo->addItem(tr("Setpoint step"), static_cast<int>(ClosedLoopTest::SetpointStep));
    m_testCombo->addItem(tr("Disturbance"), static_cast<int>(ClosedLoopTest::Disturbance));
    m_testCombo->setToolTip(
        tr("Setpoint step: the setpoint changed and the PV follows it. Disturbance: the setpoint "
           "stays constant and the PV comes back after a load change."));
    m_spBeforeSpin = makeSpin("spBeforeSpin", -1e9, 1e9, 4, 0.0, testGroup);
    m_spAfterSpin = makeSpin("spAfterSpin", -1e9, 1e9, 4, 0.0, testGroup);
    m_spBeforeSpin->setToolTip(tr("Setpoint before the step (the step is at C1)."));
    m_spBeforeLabel = new QLabel(tr("SP before"), testGroup);
    m_spAfterLabel = new QLabel(tr("SP after"), testGroup);
    m_bandCombo = new QComboBox(testGroup);
    m_bandCombo->setObjectName(QStringLiteral("bandCombo"));
    m_bandCombo->addItem(QStringLiteral("2 %"), 2.0);
    m_bandCombo->addItem(QStringLiteral("5 %"), 5.0);
    m_bandCombo->setToolTip(
        tr("The PV is settled once it stays within this band around the final setpoint (a "
           "percentage of the setpoint step, or of the maximum deviation for a disturbance)."));
    m_hintLabel = new QLabel(testGroup);
    m_hintLabel->setObjectName(QStringLiteral("closedHintLabel"));
    m_hintLabel->setWordWrap(true);
    tg->addWidget(new QLabel(tr("Test"), testGroup), 0, 0);
    tg->addWidget(m_testCombo, 0, 1);
    tg->addWidget(m_spBeforeLabel, 1, 0);
    tg->addWidget(m_spBeforeSpin, 1, 1);
    tg->addWidget(m_spAfterLabel, 2, 0);
    tg->addWidget(m_spAfterSpin, 2, 1);
    tg->addWidget(new QLabel(tr("Settling band"), testGroup), 3, 0);
    tg->addWidget(m_bandCombo, 3, 1);
    tg->addWidget(m_hintLabel, 4, 0, 1, 2);
    col->addWidget(testGroup);
    connect(m_testCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        m_ctx->settings.closedLoop.test =
            static_cast<ClosedLoopTest>(m_testCombo->currentData().toInt());
        updateTestControls();
        m_userEdited();
    });
    connect(m_spBeforeSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctx->settings.closedLoop.typedSpBefore = v;
        m_userEdited();
    });
    connect(m_spAfterSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctx->settings.closedLoop.typedSpAfter = v;
        m_userEdited();
    });
    connect(m_bandCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        m_ctx->settings.closedLoop.settlingBandPct = m_bandCombo->currentData().toDouble();
        m_userEdited();
    });

    // 4. Performance
    auto *perfGroup = new QGroupBox(tr("4. Performance"), this);
    perfGroup->setObjectName(QStringLiteral("performanceGroup"));
    auto *pg = new QVBoxLayout(perfGroup);
    m_table = new QTableWidget(0, 2, perfGroup);
    m_table->setObjectName(QStringLiteral("performanceTable"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupTable(m_table);
    m_checksList = new QListWidget(perfGroup);
    m_checksList->setObjectName(QStringLiteral("closedChecksList"));
    m_checksList->setSelectionMode(QAbstractItemView::NoSelection);
    m_checksList->setFocusPolicy(Qt::NoFocus);
    m_checksList->setWordWrap(true);
    m_checksList->setTextElideMode(Qt::ElideNone);
    m_checksList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_checksList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_checksList->setVisible(false);
    pg->addWidget(m_table);
    pg->addWidget(m_checksList);
    col->addWidget(perfGroup);

    // 5. Prediction
    auto *predGroup = new QGroupBox(tr("5. Prediction"), this);
    predGroup->setObjectName(QStringLiteral("predictionGroup"));
    auto *dg = new QGridLayout(predGroup);
    m_showPredictionCheck = new QCheckBox(tr("Predict from the open-loop model"), predGroup);
    m_showPredictionCheck->setObjectName(QStringLiteral("showPredictionCheck"));
    m_showPredictionCheck->setToolTip(
        tr("Simulates the open-loop model (K, L, T) with the controller settings below and shows "
           "the predicted response on the plot and beside the measured numbers."));
    m_modelLabel = new QLabel(predGroup);
    m_modelLabel->setObjectName(QStringLiteral("predictionModelLabel"));
    m_modelLabel->setWordWrap(true);
    m_formLabel = new QLabel(predGroup);
    m_formLabel->setObjectName(QStringLiteral("predictionFormLabel"));
    m_formLabel->setWordWrap(true);
    m_gainLabel = new QLabel(predGroup);
    m_integralLabel = new QLabel(predGroup);
    m_derivativeLabel = new QLabel(predGroup);
    m_gainSpin = makeSpin("predGainSpin", -1e9, 1e9, 4, 1.0, predGroup);
    m_integralSpin = makeSpin("predIntegralSpin", 0.0, 1e9, 4, 0.0, predGroup);
    m_integralSpin->setSpecialValueText(tr("Off"));
    m_derivativeSpin = makeSpin("predDerivativeSpin", 0.0, 1e9, 4, 0.0, predGroup);
    m_derivativeSpin->setSpecialValueText(tr("Off"));
    m_takeCombo = new QComboBox(predGroup);
    m_takeCombo->setObjectName(QStringLiteral("takeTuningCombo"));
    m_takeCombo->setToolTip(tr("Choose a row of the tuning table (the rule chosen in the Open loop area)."));
    m_takeButton = new QPushButton(tr("Use"), predGroup);
    m_takeButton->setObjectName(QStringLiteral("takeTuningButton"));
    m_takeButton->setToolTip(tr("Copy this tuning into the controller settings above."));
    m_loadStepLabel = new QLabel(tr("Load step (CO units)"), predGroup);
    m_loadStepSpin = makeSpin("loadStepSpin", -1e9, 1e9, 4, 5.0, predGroup);
    m_loadStepSpin->setToolTip(
        tr("Size of the load change at the process input, in controller-output units."));
    m_modelKLabel = new QLabel(tr("K"), predGroup);
    m_modelLLabel = new QLabel(tr("L (s)"), predGroup);
    m_modelTLabel = new QLabel(tr("T (s)"), predGroup);
    m_modelKSpin = makeSpin("modelKSpin", -1e9, 1e9, 5, 1.0, predGroup);
    m_modelLSpin = makeSpin("modelLSpin", 0.0, 1e9, 4, 0.0, predGroup);
    m_modelTSpin = makeSpin("modelTSpin", 0.001, 1e9, 4, 1.0, predGroup);
    m_modelKSpin->setToolTip(tr("Process gain K: change of the PV per unit of controller output. "
                                "Type a model from another tool, or use the open-loop model."));
    m_modelLSpin->setToolTip(tr("Dead time L in seconds (0 or more)."));
    m_modelTSpin->setToolTip(tr("Time constant T in seconds (more than 0)."));
    m_useModelButton = new QPushButton(tr("Use the open-loop model"), predGroup);
    m_useModelButton->setObjectName(QStringLiteral("useOpenLoopModelButton"));
    m_useModelButton->setToolTip(
        tr("Copy K, L and T from the open-loop result of the current cursors into the model "
           "used for the prediction."));
    dg->addWidget(m_showPredictionCheck, 0, 0, 1, 3);
    dg->addWidget(m_modelLabel, 1, 0, 1, 3);
    dg->addWidget(m_modelKLabel, 2, 0);
    dg->addWidget(m_modelKSpin, 2, 1, 1, 2);
    dg->addWidget(m_modelLLabel, 3, 0);
    dg->addWidget(m_modelLSpin, 3, 1, 1, 2);
    dg->addWidget(m_modelTLabel, 4, 0);
    dg->addWidget(m_modelTSpin, 4, 1, 1, 2);
    dg->addWidget(m_useModelButton, 5, 0, 1, 3);
    dg->addWidget(m_formLabel, 6, 0, 1, 3);
    dg->addWidget(m_gainLabel, 7, 0);
    dg->addWidget(m_gainSpin, 7, 1, 1, 2);
    dg->addWidget(m_integralLabel, 8, 0);
    dg->addWidget(m_integralSpin, 8, 1, 1, 2);
    dg->addWidget(m_derivativeLabel, 9, 0);
    dg->addWidget(m_derivativeSpin, 9, 1, 1, 2);
    dg->addWidget(new QLabel(tr("Take tuning"), predGroup), 10, 0);
    dg->addWidget(m_takeCombo, 10, 1);
    dg->addWidget(m_takeButton, 10, 2);
    dg->addWidget(m_loadStepLabel, 11, 0);
    dg->addWidget(m_loadStepSpin, 11, 1, 1, 2);
    col->addWidget(predGroup);
    connect(m_showPredictionCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_ctx->settings.closedLoop.showPrediction = on;
        m_userEdited();
    });
    for (QDoubleSpinBox *spin : {m_gainSpin, m_integralSpin, m_derivativeSpin}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { onPredictionSpinEdited(); });
    }
    connect(m_takeButton, &QPushButton::clicked, this, [this] { onTakeTuning(); });
    connect(m_loadStepSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctx->settings.closedLoop.loadStep = v;
        m_userEdited();
    });
    for (QDoubleSpinBox *spin : {m_modelKSpin, m_modelLSpin, m_modelTSpin})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { onModelSpinEdited(); });
    connect(m_useModelButton, &QPushButton::clicked, this, [this] { onUseOpenLoopModel(); });
}

void ClosedLoopPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildChecks();
}

// ---------------------------------------------------------------------------------------------
// Controls <-> settings

void ClosedLoopPage::syncFromSettings()
{
    const ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    const bool was = m_updating;
    m_updating = true;
    {
        const QSignalBlocker b1(m_testCombo);
        const QSignalBlocker b2(m_spBeforeSpin);
        const QSignalBlocker b3(m_spAfterSpin);
        const QSignalBlocker b4(m_bandCombo);
        const QSignalBlocker b5(m_showPredictionCheck);
        const QSignalBlocker b6(m_modelKSpin);
        const QSignalBlocker b7(m_modelLSpin);
        const QSignalBlocker b8(m_modelTSpin);
        const QSignalBlocker b9(m_loadStepSpin);
        m_modelKSpin->setValue(cl.modelK);
        m_modelLSpin->setValue(cl.modelLS);
        m_modelTSpin->setValue(cl.modelTS);
        m_loadStepSpin->setValue(cl.loadStep);
        m_testCombo->setCurrentIndex(
            std::max(0, m_testCombo->findData(static_cast<int>(cl.test))));
        m_spBeforeSpin->setValue(cl.typedSpBefore);
        m_spAfterSpin->setValue(cl.typedSpAfter);
        int band = m_bandCombo->findData(cl.settlingBandPct);
        if (band < 0) { // a value from a project file: keep it visible
            m_bandCombo->addItem(QStringLiteral("%1 %").arg(g4(cl.settlingBandPct)),
                                 cl.settlingBandPct);
            band = m_bandCombo->count() - 1;
        }
        m_bandCombo->setCurrentIndex(band);
        m_showPredictionCheck->setChecked(cl.showPrediction);
    }
    updateTestControls();
    updatePredictionLabels();
    syncPredictionSpins();
    updateModelUi();
    m_updating = was;
}

void ClosedLoopPage::updateTestControls()
{
    const ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    const bool typed = m_ctx->settings.spTagId == 0;
    const bool step = cl.test == ClosedLoopTest::SetpointStep;
    m_spBeforeLabel->setVisible(typed && step);
    m_spBeforeSpin->setVisible(typed && step);
    m_spAfterLabel->setText(step ? tr("SP after") : tr("Setpoint"));
    m_spAfterLabel->setVisible(typed);
    m_spAfterSpin->setVisible(typed);
    m_spAfterSpin->setToolTip(step ? tr("Setpoint after the step.")
                                   : tr("The constant setpoint during the disturbance test."));
    const QString suffix = m_ctx->pvUnit().isEmpty() ? QString() : u' ' + m_ctx->pvUnit();
    m_spBeforeSpin->setSuffix(suffix);
    m_spAfterSpin->setSuffix(suffix);
    m_loadStepLabel->setVisible(!step);
    m_loadStepSpin->setVisible(!step);
    const QString cu = m_ctx->coUnit();
    m_loadStepSpin->setSuffix(cu.isEmpty() ? QString() : u' ' + cu);
}

void ClosedLoopPage::updatePredictionLabels()
{
    const ControllerSettings &c = m_ctx->settings.controller;
    const bool minutes = m_ctx->settings.timeInMinutes;
    const PidColumnLabels labels = pidColumnLabels(c, minutes);
    m_gainLabel->setText(labels.gain);
    m_integralLabel->setText(labels.integral);
    m_derivativeLabel->setText(labels.derivative);
    m_modelKSpin->setSuffix(m_ctx->kUnit().isEmpty() ? QString() : u' ' + m_ctx->kUnit());
}

void ClosedLoopPage::syncPredictionSpins()
{
    const ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    IdealPid pid;
    pid.kp = cl.predictionKp;
    if (cl.predictionTiS > 0.0)
        pid.ti = cl.predictionTiS;
    if (cl.predictionTdS > 0.0)
        pid.td = cl.predictionTdS;
    const ControllerSettings &controller = m_ctx->settings.controller;
    const PidSpans spans = m_ctx->pidSpans();
    const bool minutes = m_ctx->settings.timeInMinutes;
    const FormValues fv = toForm(pid, controller, spans, minutes);
    m_formLabel->setText(fv.available ? QString() : fv.reason);
    m_formLabel->setVisible(!fv.available);
    for (QDoubleSpinBox *s : {m_gainSpin, m_integralSpin, m_derivativeSpin})
        s->setEnabled(fv.available);
    if (!fv.available)
        return;
    const PidColumnLabels labels = pidColumnLabels(controller, minutes);
    m_gainSpin->setToolTip(appendLines(
        tip(labels.gain, tr("controller gain of the predicted response."), {}),
        pidConversionLines(0, pid, controller, spans, minutes)));
    m_integralSpin->setToolTip(appendLines(
        tip(labels.integral, tr("integral action of the predicted response (Off = none)."), {}),
        pidConversionLines(1, pid, controller, spans, minutes)));
    m_derivativeSpin->setToolTip(appendLines(
        tip(labels.derivative, tr("derivative action of the predicted response (Off = none)."), {}),
        pidConversionLines(2, pid, controller, spans, minutes)));
    const bool was = m_updating;
    m_updating = true;
    {
        const QSignalBlocker b1(m_gainSpin);
        const QSignalBlocker b2(m_integralSpin);
        const QSignalBlocker b3(m_derivativeSpin);
        m_gainSpin->setValue(fv.gain.value_or(0.0));
        m_integralSpin->setValue(fv.integral.value_or(0.0));
        m_derivativeSpin->setValue(fv.derivative.value_or(0.0));
    }
    m_updating = was;
}

void ClosedLoopPage::onPredictionSpinEdited()
{
    if (m_updating)
        return;
    FormValues v;
    v.gain = m_gainSpin->value();
    if (m_integralSpin->value() > 0.0)
        v.integral = m_integralSpin->value();
    if (m_derivativeSpin->value() > 0.0)
        v.derivative = m_derivativeSpin->value();
    const std::optional<IdealPid> ideal =
        fromForm(v, m_ctx->settings.controller, m_ctx->pidSpans(), m_ctx->settings.timeInMinutes);
    if (!ideal) { // e.g. a gain of 0 or a band without ranges: show the stored values again
        syncPredictionSpins();
        return;
    }
    ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    cl.predictionKp = ideal->kp;
    cl.predictionTiS = ideal->ti.value_or(0.0);
    cl.predictionTdS = ideal->td.value_or(0.0);
    m_userEdited();
}

void ClosedLoopPage::onTakeTuning()
{
    const QList<TuningRow> rows = tuningRows();
    const int index = m_takeCombo->currentData().toInt();
    if (m_takeCombo->currentIndex() < 0 || index < 0 || index >= rows.size())
        return;
    const TuningRow &row = rows.at(index);
    if (!row.available || !row.kp)
        return;
    ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    cl.predictionKp = *row.kp;
    cl.predictionTiS = row.ti.value_or(0.0);
    cl.predictionTdS = row.td.value_or(0.0);
    syncPredictionSpins();
    m_userEdited();
}

void ClosedLoopPage::onModelSpinEdited()
{
    if (m_updating)
        return;
    ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    cl.modelK = m_modelKSpin->value();
    cl.modelLS = m_modelLSpin->value();
    cl.modelTS = m_modelTSpin->value();
    cl.modelSet = true;
    cl.modelSource = tr("typed");
    m_userEdited();
}

void ClosedLoopPage::onUseOpenLoopModel()
{
    QString source;
    const std::optional<ProcessModel> model = currentOpenLoopModel(&source);
    if (!model)
        return;
    ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    cl.modelK = model->k;
    cl.modelLS = model->l;
    cl.modelTS = model->t;
    cl.modelSet = true;
    cl.modelSource = source;
    m_userEdited();
}

std::optional<ProcessModel> ClosedLoopPage::currentOpenLoopModel(QString *source) const
{
    if (m_ctx->results.isEmpty())
        return std::nullopt;
    const StepResult &r = m_ctx->results.first();
    if (!r.valid || !r.gain || !r.deadTime || !r.timeConstant)
        return std::nullopt;
    const double k = *r.gain;
    if (!std::isfinite(k) || k == 0.0 || !std::isfinite(*r.deadTime) || *r.deadTime < 0.0
        || !std::isfinite(*r.timeConstant) || !(*r.timeConstant > 0.0))
        return std::nullopt;
    if (source) {
        *source = tr("%1, C1 = %2 s").arg(methodName(m_ctx->settings.method), g4(r.t1));
    }
    return ProcessModel{k, *r.deadTime, *r.timeConstant};
}

void ClosedLoopPage::refreshModel()
{
    // The model belongs to the project: only taken while none is set (no user-edit hook: the
    // project is not marked as modified, but the model is saved with the next save).
    ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    if (!cl.modelSet) {
        QString source;
        if (const std::optional<ProcessModel> model = currentOpenLoopModel(&source)) {
            cl.modelK = model->k;
            cl.modelLS = model->l;
            cl.modelTS = model->t;
            cl.modelSet = true;
            cl.modelSource = source;
        }
    }
    const bool was = m_updating;
    m_updating = true;
    {
        const QSignalBlocker b1(m_modelKSpin);
        const QSignalBlocker b2(m_modelLSpin);
        const QSignalBlocker b3(m_modelTSpin);
        m_modelKSpin->setValue(cl.modelK);
        m_modelLSpin->setValue(cl.modelLS);
        m_modelTSpin->setValue(cl.modelTS);
    }
    m_updating = was;
}

std::optional<ProcessModel> ClosedLoopPage::openLoopModel() const
{
    const ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    if (!cl.modelSet || !std::isfinite(cl.modelK) || cl.modelK == 0.0 || !std::isfinite(cl.modelLS)
        || cl.modelLS < 0.0 || !std::isfinite(cl.modelTS) || !(cl.modelTS > 0.0))
        return std::nullopt;
    return ProcessModel{cl.modelK, cl.modelLS, cl.modelTS};
}

QList<TuningRow> ClosedLoopPage::tuningRows() const
{
    const std::optional<ProcessModel> model = openLoopModel();
    if (!model)
        return {};
    return tune(model->k, model->l, model->t, m_ctx->settings.tuningRule, m_ctx->settings.lambdaS);
}

void ClosedLoopPage::rebuildTakeTuningCombo()
{
    const QList<TuningRow> rows = tuningRows();
    const int keep = m_takeCombo->currentIndex();
    const QSignalBlocker b(m_takeCombo);
    m_takeCombo->clear();
    auto *model = qobject_cast<QStandardItemModel *>(m_takeCombo->model());
    for (int i = 0; i < rows.size(); ++i) {
        const TuningRow &row = rows.at(i);
        QString text = toString(row.kind);
        if (!row.available)
            text += QStringLiteral(" (") + tr("not defined") + u')';
        m_takeCombo->addItem(text, i);
        if (row.available && row.kp) {
            QStringList parts{QStringLiteral("Kp = ") + g4(*row.kp)};
            if (row.ti)
                parts << QStringLiteral("Ti = ") + g4(*row.ti) + QStringLiteral(" s");
            if (row.td)
                parts << QStringLiteral("Td = ") + g4(*row.td) + QStringLiteral(" s");
            m_takeCombo->setItemData(i, parts.join(QStringLiteral(", ")), Qt::ToolTipRole);
        }
        if (model && !row.available)
            model->item(i)->setEnabled(false);
    }
    if (!rows.isEmpty()) {
        int index = keep >= 0 ? keep : 1; // PI by default
        if (index >= rows.size() || !rows.at(index).available) {
            index = -1;
            for (int i = 0; i < rows.size() && index < 0; ++i) {
                if (rows.at(i).available)
                    index = i;
            }
        }
        m_takeCombo->setCurrentIndex(index);
    }
    const bool any = m_takeCombo->currentIndex() >= 0;
    m_takeCombo->setEnabled(any);
    m_takeButton->setEnabled(any);
}

void ClosedLoopPage::updateModelUi()
{
    const std::optional<ProcessModel> model = openLoopModel();
    if (model) {
        m_modelLabel->setText(
            tr("Model: K = %1, L = %2 s, T = %3 s (%4)")
                .arg(withUnit(g4(model->k), m_ctx->kUnit()), g4(model->l), g4(model->t),
                     m_ctx->settings.closedLoop.modelSource));
    } else {
        m_modelLabel->setText(tr("No model yet: do the open-loop step test first (Open loop "
                                 "area), or type one."));
    }
    m_useModelButton->setEnabled(currentOpenLoopModel().has_value());
    rebuildTakeTuningCombo();
}

// ---------------------------------------------------------------------------------------------
// Recompute

void ClosedLoopPage::recompute()
{
    const AnalysisSettings &s = m_ctx->settings;
    m_result = {};
    m_analysed = false;
    refreshModel();
    if (m_ctx->recorder && s.pvTagId != 0 && m_ctx->hasBothCursors()) {
        const PlotCursors *cur = m_ctx->cursors();
        const TagId sp = m_ctx->tagById(s.spTagId) ? s.spTagId : 0;
        const TagId co = m_ctx->tagById(s.deltaCoTagId) ? s.deltaCoTagId : 0;
        m_result = analyzeClosedLoop(*m_ctx->recorder, s.pvTagId, sp, co, cur->c1(), cur->c2(),
                                     s.closedLoop, s.coRangeMin, s.coRangeMax);
        m_analysed = true;
    }
    updateTestControls();
    updatePredictionLabels();
    {
        const bool was = m_updating;
        m_updating = true;
        syncPredictionSpins(); // the ranges may have changed (PB, gains in %)
        m_updating = was;
    }
    updateModelUi();
    updatePrediction();
    rebuildTable();
    rebuildChecks();
    updateHint();
}

void ClosedLoopPage::updatePrediction()
{
    const AnalysisSettings &s = m_ctx->settings;
    const ClosedLoopSettings &cl = s.closedLoop;
    const std::optional<ProcessModel> model = openLoopModel();
    const double duration = m_result.t2 - m_result.startTime;
    if (!cl.showPrediction || !model || !m_analysed || !m_result.valid || !(duration > 0.0)
        || !(s.coRangeMin < s.coRangeMax)) {
        m_pred = {};
        return;
    }
    const bool step = cl.test == ClosedLoopTest::SetpointStep;
    const Tag *coTag = m_ctx->tagById(s.deltaCoTagId);
    double co0 = kNaN;
    if (coTag && m_ctx->recorder)
        co0 = m_ctx->recorder->sampleAt(coTag->id, m_result.startTime);
    const bool hasCo = std::isfinite(co0);
    if (!hasCo)
        co0 = (s.coRangeMin + s.coRangeMax) / 2.0;

    SimulationSetup setup;
    setup.model = *model;
    setup.pid.kp = cl.predictionKp;
    if (cl.predictionTiS > 0.0)
        setup.pid.ti = cl.predictionTiS;
    if (cl.predictionTdS > 0.0)
        setup.pid.td = cl.predictionTdS;
    setup.test = cl.test;
    setup.pv0 = m_result.pvAtStart;
    setup.co0 = co0;
    setup.spStep = step ? m_result.spAfter - m_result.spBefore : 0.0;
    setup.loadStep = step ? 0.0 : cl.loadStep;
    setup.coMin = s.coRangeMin;
    setup.coMax = s.coRangeMax;
    setup.dtS = std::max(0.1, duration / kMaxPredictionSteps);
    // The simulated setpoint step happens at its first step: shift so that it lands on startTime.
    const double shift = step ? m_result.startTime - setup.dtS : m_result.startTime;
    setup.durationS = duration + (step ? setup.dtS : 0.0);

    const QList<double> key{double(cl.test), setup.pv0, setup.co0, setup.spStep, setup.loadStep,
                            setup.coMin, setup.coMax, setup.durationS, setup.dtS, shift,
                            setup.model.k, setup.model.l, setup.model.t, setup.pid.kp,
                            setup.pid.ti.value_or(0.0), setup.pid.td.value_or(0.0),
                            cl.settlingBandPct, hasCo ? 1.0 : 0.0, m_result.t2};
    if (m_pred.valid && m_pred.key == key)
        return;

    m_pred = {};
    m_pred.key = key;
    ClosedLoopSeries series = simulateClosedLoop(setup);
    if (series.t.size() < 2)
        return;
    for (double &t : series.t)
        t += shift;
    m_pred.series = series;
    m_pred.hasOutput = hasCo;
    ClosedLoopSeries analysed = series;
    if (!hasCo)
        analysed.co.clear();
    m_pred.result = analyzeClosedLoopSeries(analysed, series.t.first(), series.t.last(), cl,
                                            s.coRangeMin, s.coRangeMax);
    m_pred.valid = true;
}

double ClosedLoopPage::settlingBand(const ClosedLoopResult &r) const
{
    const double pct = m_ctx->settings.closedLoop.settlingBandPct;
    if (r.test == ClosedLoopTest::SetpointStep)
        return pct / 100.0 * std::abs(r.spAfter - r.spBefore);
    return pct / 100.0 * std::abs(r.maxDeviation.value_or(kNaN));
}

// ---------------------------------------------------------------------------------------------
// Table

void ClosedLoopPage::rebuildTable()
{
    const ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    const bool step = cl.test == ClosedLoopTest::SetpointStep;
    const bool showPred = cl.showPrediction;
    const bool measuredOk = m_analysed && m_result.valid;
    // The rows follow the test, not the (possibly invalid) result.
    struct Row {
        Quantity q;
        QString label;
    };
    QList<Row> rows;
    if (step) {
        rows = {{Quantity::SpBefore, tr("SP before")},
                {Quantity::SpAfter, tr("SP after")},
                {Quantity::Overshoot, tr("Overshoot")},
                {Quantity::PeakTime, tr("Peak time")},
                {Quantity::RiseTime, tr("Rise time (10-90 %)")},
                {Quantity::SettlingTime, tr("Settling time (%1 % band)").arg(g4(cl.settlingBandPct))}};
    } else {
        rows = {{Quantity::Setpoint, tr("Setpoint")},
                {Quantity::MaxDeviation, tr("Max deviation")},
                {Quantity::MaxDeviationTime, tr("Time of max deviation")},
                {Quantity::SettlingTime, tr("Settling time (%1 % band)").arg(g4(cl.settlingBandPct))}};
    }
    rows.append({{Quantity::DecayRatio, tr("Decay ratio")},
                 {Quantity::Period, tr("Period")},
                 {Quantity::SteadyStateError, tr("Steady-state error")},
                 {Quantity::Iae, QStringLiteral("IAE")},
                 {Quantity::Ise, QStringLiteral("ISE")},
                 {Quantity::Itae, QStringLiteral("ITAE")},
                 {Quantity::OutMin, tr("Output min")},
                 {Quantity::OutMax, tr("Output max")},
                 {Quantity::OutTravel, tr("Output travel")}});

    m_table->clearContents();
    m_table->setRowCount(0);
    m_table->setColumnCount(showPred ? 3 : 2);
    QStringList headers{tr("Quantity"), tr("Measured")};
    if (showPred)
        headers << tr("Predicted");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int c = 1; c < m_table->columnCount(); ++c)
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);

    for (const Row &row : std::as_const(rows)) {
        const int n = m_table->rowCount();
        m_table->insertRow(n);
        const QString measuredTip = explain(row.q, measuredOk ? &m_result : nullptr, false);
        m_table->setItem(n, 0, makeItem(row.label, false, measuredTip));
        m_table->setItem(n, 1,
                         makeItem(measuredOk ? valueText(row.q, m_result) : kDash, true, measuredTip));
        if (showPred) {
            const bool ok = m_pred.valid;
            m_table->setItem(n, 2,
                             makeItem(ok ? valueText(row.q, m_pred.result) : kDash, true,
                                      explain(row.q, ok ? &m_pred.result : nullptr, true)));
        }
    }
    fitTableHeight(m_table);
}

QString ClosedLoopPage::valueText(Quantity q, const ClosedLoopResult &r) const
{
    if (!r.valid)
        return kDash;
    const QString pu = m_ctx->pvUnit();
    const QString cu = m_ctx->coUnit();
    auto pv = [&](double v) { return std::isfinite(v) ? withUnit(g4(v), pu) : kDash; };
    auto co = [&](const std::optional<double> &v) { return v ? withUnit(g4(*v), cu) : kDash; };
    switch (q) {
    case Quantity::SpBefore:
        return pv(r.spBefore);
    case Quantity::SpAfter:
    case Quantity::Setpoint:
        return pv(r.spAfter);
    case Quantity::Overshoot:
        if (!r.overshoot)
            return kDash;
        return tr("%1 (%2 % of the step)")
            .arg(withUnit(g4(*r.overshoot), pu), g4(r.overshootPct.value_or(kNaN)));
    case Quantity::PeakTime:
        return secondsText(r.peakTime);
    case Quantity::RiseTime:
        return secondsText(r.riseTime);
    case Quantity::MaxDeviation:
        return r.maxDeviation ? withUnit(g4(*r.maxDeviation), pu) : kDash;
    case Quantity::MaxDeviationTime:
        return secondsText(r.maxDeviationTime);
    case Quantity::SettlingTime:
        return secondsText(r.settlingTime);
    case Quantity::DecayRatio:
        return g4(r.decayRatio);
    case Quantity::Period:
        return secondsText(r.period);
    case Quantity::SteadyStateError:
        return pv(r.steadyStateError);
    case Quantity::Iae:
        return std::isfinite(r.iae) ? withUnit(g4(r.iae), productUnit(pu, QStringLiteral(u"·s")))
                                    : kDash;
    case Quantity::Ise:
        return std::isfinite(r.ise) ? withUnit(g4(r.ise), productUnit(pu, QStringLiteral(u"²·s")))
                                    : kDash;
    case Quantity::Itae:
        return std::isfinite(r.itae) ? withUnit(g4(r.itae), productUnit(pu, QStringLiteral(u"·s²")))
                                     : kDash;
    case Quantity::OutMin:
        return co(r.outMin);
    case Quantity::OutMax:
        return co(r.outMax);
    case Quantity::OutTravel:
        return co(r.outTravel);
    }
    return kDash;
}

void ClosedLoopPage::rebuildChecks()
{
    m_checksList->clear();
    if (m_analysed) {
        const QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
        const int textWidth = std::max(120, m_checksList->viewport()->width() - 40);
        int total = 4;
        for (ClosedLoopCheck c : std::as_const(m_result.checks)) {
            QString text;
            switch (c) {
            case ClosedLoopCheck::NoSetpointStep:
                text = tr("The setpoint does not change between the cursors: choose Disturbance, "
                          "or move the cursors around the setpoint change.");
                break;
            case ClosedLoopCheck::SetpointMoved:
                text = tr("The setpoint changes between the cursors: choose Setpoint step.");
                break;
            case ClosedLoopCheck::NotSettled:
                text = tr("The PV is still outside the settling band at C2: move C2 later.");
                break;
            case ClosedLoopCheck::OutputSaturated:
                text = tr("The output reached its limit: the response shows the limits of the "
                          "valve, not only the tuning.");
                break;
            case ClosedLoopCheck::NoOutput:
                text = tr("Choose the controller output (CO) in step 1 to see the output rows.");
                break;
            case ClosedLoopCheck::NoiseAboveBand:
                text = tr("The noise on the PV is larger than the settling band: the settling time, "
                          "the peaks and the decay ratio are unreliable. Choose the 5 % band, or "
                          "reduce the noise (transmitter damping).");
                break;
            }
            auto *item = new QListWidgetItem(icon, text, m_checksList);
            item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(text);
            const int h = m_checksList->fontMetrics()
                              .boundingRect(0, 0, textWidth, 2000, Qt::TextWordWrap, text)
                              .height()
                          + 8;
            item->setSizeHint(QSize(0, std::max(h, 24)));
            total += std::max(h, 24);
        }
        m_checksList->setFixedHeight(total);
    }
    m_checksList->setVisible(m_checksList->count() > 0);
}

void ClosedLoopPage::updateHint()
{
    QString text;
    if (m_ctx->settings.pvTagId == 0) {
        text = tr("Choose the process value (PV) in step 1.");
    } else if (!m_ctx->hasBothCursors()) {
        text = tr("Place C1 at the setpoint change (or just before the disturbance) and C2 after "
                  "the PV has settled.");
    } else if (m_analysed && !m_result.valid) {
        text = m_result.error;
    }
    m_hintLabel->setText(text);
}

// ---------------------------------------------------------------------------------------------
// Plot overlays

void ClosedLoopPage::setOverlaysShown(bool shown)
{
    if (shown == m_overlaysShown)
        return;
    m_overlaysShown = shown;
    rebuildOverlays();
}

void ClosedLoopPage::clearOverlays()
{
    for (QwtPlotItem *item : std::as_const(m_items)) {
        item->detach();
        delete item;
    }
    m_items.clear();
}

void ClosedLoopPage::forgetOverlays()
{
    m_items.clear();
}

void ClosedLoopPage::rebuildOverlays()
{
    clearOverlays();
    if (!m_ctx->plot || !m_overlaysShown || !m_analysed || !m_result.valid
        || !m_ctx->hasBothCursors())
        return;
    const AnalysisSettings &settings = m_ctx->settings;
    const ClosedLoopResult &r = m_result;
    QwtPlot *plot = m_ctx->plot->plot();
    const Tag *pvTag = m_ctx->tagById(settings.pvTagId);
    if (!pvTag || !pvTag->plotted)
        return;
    auto axisOf = [](const Tag &t) { return t.axis == YAxis::Right ? QwtPlot::yRight : QwtPlot::yLeft; };
    const int pvAxis = axisOf(*pvTag);
    const QColor pvColor(pvTag->color);

    // Settling band: SP after +- b from the start to C2.
    const double b = settlingBand(r);
    if (std::isfinite(b) && b > 0.0 && r.t2 > r.startTime) {
        for (double sign : {1.0, -1.0}) {
            auto *line = new QwtPlotCurve;
            const double y = r.spAfter + sign * b;
            line->setSamples(QVector<QPointF>{QPointF(r.startTime, y), QPointF(r.t2, y)});
            QColor c = pvColor.isValid() ? pvColor : QColor(0x60, 0x60, 0x60);
            c = c.lighter(150);
            line->setPen(QPen(c, 1.0, Qt::DotLine));
            line->setItemAttribute(QwtPlotItem::Legend, false);
            line->setYAxis(pvAxis);
            line->setZ(11.0);
            line->attach(plot);
            m_items.append(line);
        }
    }

    // Overshoot peak / maximum deviation.
    std::optional<QPointF> point;
    QString label;
    if (r.test == ClosedLoopTest::SetpointStep) {
        if (r.overshoot && r.peakTime && *r.overshoot > 0.0) {
            const double sign = r.spAfter >= r.spBefore ? 1.0 : -1.0;
            point = QPointF(r.startTime + *r.peakTime, r.spAfter + sign * *r.overshoot);
            label = tr("Overshoot %1").arg(withUnit(g4(*r.overshoot), m_ctx->pvUnit()));
        }
    } else if (r.maxDeviation && r.maxDeviationTime) {
        point = QPointF(r.startTime + *r.maxDeviationTime, r.spAfter - *r.maxDeviation);
        label = tr("Max deviation %1").arg(withUnit(g4(*r.maxDeviation), m_ctx->pvUnit()));
    }
    if (point) {
        auto *marker = new QwtPlotMarker;
        marker->setValue(*point);
        const QColor ring = pvColor.isValid() ? pvColor : QColor(0x20, 0x20, 0x20);
        marker->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, QBrush(QColor(255, 255, 255)),
                                        QPen(ring, 2.0), QSize(9, 9)));
        QwtText text(label);
        text.setColor(QColor(0x20, 0x20, 0x20));
        text.setBackgroundBrush(QColor(255, 255, 255, 235));
        text.setBorderPen(QPen(ring, 1));
        text.setBorderRadius(3);
        marker->setLabel(text);
        marker->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
        marker->setYAxis(pvAxis);
        marker->setZ(25.0);
        marker->attach(plot);
        m_items.append(marker);
    }

    // Predicted curves.
    if (settings.closedLoop.showPrediction && m_pred.valid) {
        auto makeCurve = [&](const QList<double> &y, const QColor &color, double width, int axis) {
            QVector<QPointF> pts;
            pts.reserve(m_pred.series.t.size());
            for (qsizetype i = 0; i < m_pred.series.t.size() && i < y.size(); ++i)
                pts.append(QPointF(m_pred.series.t.at(i), y.at(i)));
            auto *curve = new QwtPlotCurve;
            curve->setSamples(pts);
            curve->setPen(QPen(color, width, Qt::DashLine));
            curve->setItemAttribute(QwtPlotItem::Legend, false);
            curve->setYAxis(axis);
            curve->setZ(12.0);
            curve->attach(plot);
            m_items.append(curve);
        };
        double penWidth = 1.5;
        const QwtPlotItemList curves = plot->itemList(QwtPlotItem::Rtti_PlotCurve);
        for (QwtPlotItem *item : curves) {
            if (item->title().text() == tagLabel(*pvTag)) {
                penWidth = static_cast<QwtPlotCurve *>(item)->pen().widthF();
                break;
            }
        }
        QColor modelColor(settings.modelColor);
        if (!modelColor.isValid())
            modelColor = QColor(0x20, 0x20, 0x20);
        makeCurve(m_pred.series.pv, modelColor, penWidth + 1.0, pvAxis);
        const Tag *coTag = m_ctx->tagById(settings.deltaCoTagId);
        if (m_pred.hasOutput && coTag && coTag->plotted && !m_pred.series.co.isEmpty()) {
            QColor coColor(coTag->color);
            if (!coColor.isValid())
                coColor = modelColor;
            makeCurve(m_pred.series.co, coColor, penWidth + 1.0, axisOf(*coTag));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Copy / readout

QString ClosedLoopPage::copyText() const
{
    if (!m_analysed || !m_result.valid)
        return {};
    return tableToTsv(m_table->model(), {});
}

QString ClosedLoopPage::summary(const ClosedLoopResult &r) const
{
    const QString pu = m_ctx->pvUnit();
    const QString settling =
        r.settlingTime ? g4(*r.settlingTime) + QStringLiteral(" s") : tr("not settled");
    const QString band = g4(m_ctx->settings.closedLoop.settlingBandPct);
    if (r.test == ClosedLoopTest::SetpointStep) {
        const QString os = r.overshoot ? withUnit(g4(*r.overshoot), pu) : kDash;
        return tr("overshoot %1 (%2 % of the step), settling %3 (%4 % band), IAE %5")
            .arg(os, g4(r.overshootPct.value_or(kNaN)), settling, band, g4(r.iae));
    }
    const QString dev = r.maxDeviation ? withUnit(g4(*r.maxDeviation), pu) : kDash;
    return tr("max deviation %1, settling %2 (%3 % band), IAE %4")
        .arg(dev, settling, band, g4(r.iae));
}

QString ClosedLoopPage::readoutText() const
{
    if (!m_analysed || !m_result.valid)
        return {};
    const QString pu = m_ctx->pvUnit();
    QString line;
    if (m_result.test == ClosedLoopTest::SetpointStep) {
        line = tr("SP step %1 → %2: ")
                   .arg(g4(m_result.spBefore), withUnit(g4(m_result.spAfter), pu));
    } else {
        line = tr("Disturbance at SP %1: ").arg(withUnit(g4(m_result.spAfter), pu));
    }
    line += summary(m_result);
    if (m_ctx->settings.closedLoop.showPrediction && m_pred.valid)
        line += QStringLiteral("\n") + tr("Predicted: ") + summary(m_pred.result);
    return line;
}

// ---------------------------------------------------------------------------------------------
// Tooltips

QString ClosedLoopPage::explain(Quantity q, const ClosedLoopResult *r, bool predicted) const
{
    const QString pu = m_ctx->pvUnit();
    const QString cu = m_ctx->coUnit();
    const ClosedLoopSettings &cl = m_ctx->settings.closedLoop;
    const bool step = (r ? r->test : cl.test) == ClosedLoopTest::SetpointStep;
    const QString bandPct = g4(cl.settlingBandPct);
    auto pvv = [&](double v) { return withUnit(g4(v), pu); };
    QString name;
    QString meaning;
    QStringList lines;

    switch (q) {
    case Quantity::SpBefore:
        name = tr("SP before, setpoint before the step");
        meaning = tr("the setpoint just before the change (at C1).");
        lines << tr("SP before = SP at C1 (or the value typed in step 3)");
        if (r)
            lines << QStringLiteral("SP before = ") + pvv(r->spBefore);
        break;
    case Quantity::SpAfter:
        name = tr("SP after, setpoint after the step");
        meaning = tr("the setpoint the PV has to reach (at C2).");
        lines << tr("SP after = SP at C2 (or the value typed in step 3)");
        if (r)
            lines << QStringLiteral("SP after = ") + pvv(r->spAfter);
        break;
    case Quantity::Setpoint:
        name = tr("Setpoint");
        meaning = tr("the constant setpoint during the disturbance test.");
        lines << tr("Setpoint = SP at C1 (or the value typed in step 3)");
        if (r)
            lines << QStringLiteral("Setpoint = ") + pvv(r->spAfter);
        break;
    case Quantity::Overshoot: {
        name = tr("Overshoot");
        meaning = tr("how far the PV goes past the new setpoint, in PV units and as a percentage "
                     "of the step.");
        const bool up = !r || r->spAfter >= r->spBefore;
        lines << (up ? tr("overshoot = max(PV) - SP after") : tr("overshoot = SP after - min(PV)"));
        lines << tr("% of the step = overshoot / |SP after - SP before| %1 100").arg(kTimes);
        if (r && r->overshoot) {
            const double os = *r->overshoot;
            const double step_ = std::abs(r->spAfter - r->spBefore);
            const double peak = up ? r->spAfter + os : r->spAfter - os;
            lines << (up ? QStringLiteral("overshoot = max(PV) - SP after = %1 - %2 = %3")
                         : QStringLiteral("overshoot = SP after - min(PV) = %2 - %1 = %3"))
                         .arg(g4p(peak), g4p(r->spAfter), pvv(os));
            lines << QStringLiteral("%1 / %2 %3 100 = %4 %")
                         .arg(g4(os), g4(step_), kTimes, g4(r->overshootPct.value_or(kNaN)));
        }
        break;
    }
    case Quantity::PeakTime:
        name = tr("Peak time");
        meaning = tr("time from the setpoint change to the overshoot peak.");
        lines << QStringLiteral("peak time = t(peak) - t(SP change)");
        if (r && r->peakTime) {
            lines << QStringLiteral("peak time = %1 - %2 = %3 s")
                         .arg(g4(r->startTime + *r->peakTime), g4(r->startTime), g4(*r->peakTime));
        } else if (r) {
            lines << tr("There is no overshoot, so there is no peak.");
        }
        break;
    case Quantity::RiseTime:
        name = tr("Rise time (10-90 %)");
        meaning = tr("time the PV needs to go from 10 % to 90 % of the step: how fast the "
                     "response is.");
        lines << QStringLiteral("rise time = t(90 %) - t(10 %)");
        if (r && r->riseTime) {
            const double d = r->spAfter - r->spBefore;
            lines << QStringLiteral("10 % level = %1 + 0.1 %2 %3 = %4; 90 % level = %1 + 0.9 %2 %3 = %5")
                         .arg(g4p(r->spBefore), kTimes, g4p(d), g4(r->spBefore + 0.1 * d),
                              g4(r->spBefore + 0.9 * d));
            lines << QStringLiteral("rise time = %1 s").arg(g4(*r->riseTime));
        }
        break;
    case Quantity::MaxDeviation:
        name = tr("Max deviation");
        meaning = tr("the largest distance between the setpoint and the PV after the "
                     "disturbance (negative: the PV fell below the setpoint).");
        lines << QStringLiteral("max deviation = SP - PV at the largest |SP - PV|");
        if (r && r->maxDeviation) {
            lines << QStringLiteral("max deviation = %1 - %2 = %3")
                         .arg(g4p(r->spAfter), g4p(r->spAfter - *r->maxDeviation),
                              pvv(*r->maxDeviation));
        }
        break;
    case Quantity::MaxDeviationTime:
        name = tr("Time of max deviation");
        meaning = tr("time from the disturbance (C1) to the largest deviation.");
        lines << QStringLiteral("t = t(max deviation) - t(C1)");
        if (r && r->maxDeviationTime) {
            lines << QStringLiteral("t = %1 - %2 = %3 s")
                         .arg(g4(r->startTime + *r->maxDeviationTime), g4(r->startTime),
                              g4(*r->maxDeviationTime));
        }
        break;
    case Quantity::SettlingTime: {
        name = tr("Settling time (%1 % band)").arg(bandPct);
        meaning = tr("time after which the PV stays inside the band around the final setpoint.");
        lines << (step ? tr("band = %1 % of |SP after - SP before|; settled when |SP - PV| <= band")
                             .arg(bandPct)
                       : tr("band = %1 % of |max deviation|; settled when |SP - PV| <= band")
                             .arg(bandPct));
        if (r) {
            const double bnd = settlingBand(*r);
            const double base = step ? std::abs(r->spAfter - r->spBefore)
                                     : std::abs(r->maxDeviation.value_or(kNaN));
            lines << QStringLiteral("band = %1 %2 %3 / 100 = %4")
                         .arg(bandPct, kTimes, g4(base), pvv(bnd));
            if (r->settlingTime) {
                lines << tr("settling time = time the PV enters the band for good, measured from "
                            "the start = %1 s")
                             .arg(g4(*r->settlingTime));
            } else {
                lines << tr("The PV is still outside the band at C2: not settled.");
            }
        }
        break;
    }
    case Quantity::DecayRatio: {
        name = tr("Decay ratio");
        meaning = tr("size of the second peak of the error divided by the first: how fast the "
                     "oscillation dies out.");
        lines << QStringLiteral("decay ratio = second peak / first peak");
        lines << tr("A quarter-amplitude decay is 0.25; the smaller, the better damped.");
        if (r && r->decayRatio) {
            const std::optional<double> first = step ? r->overshoot
                                                     : (r->maxDeviation
                                                            ? std::optional<double>(std::abs(*r->maxDeviation))
                                                            : std::nullopt);
            if (first && *first > 0.0) {
                lines << QStringLiteral("decay ratio = %1 / %2 = %3")
                             .arg(g4(*r->decayRatio * *first), g4(*first), g4(*r->decayRatio));
            } else {
                lines << QStringLiteral("decay ratio = %1").arg(g4(*r->decayRatio));
            }
        } else if (r) {
            lines << tr("Fewer than two peaks above the band: the response does not oscillate.");
        }
        break;
    }
    case Quantity::Period:
        name = tr("Period");
        meaning = tr("time between the first and the second peak: the period of the oscillation.");
        lines << QStringLiteral("period = t(second peak) - t(first peak)");
        if (r && r->period)
            lines << QStringLiteral("period = %1 s").arg(g4(*r->period));
        else if (r)
            lines << tr("Fewer than two peaks above the band: the response does not oscillate.");
        break;
    case Quantity::SteadyStateError:
        name = tr("Steady-state error");
        meaning = tr("what is left between the setpoint and the PV at the end; an integral "
                     "action should bring it to zero.");
        lines << tr("error = SP after - mean(PV over the last 10 % of the test)");
        if (r && std::isfinite(r->steadyStateError)) {
            lines << QStringLiteral("error = %1 - %2 = %3")
                         .arg(g4p(r->spAfter), g4p(r->spAfter - r->steadyStateError),
                              pvv(r->steadyStateError));
        }
        break;
    case Quantity::Iae:
        name = tr("IAE, integral of the absolute error");
        meaning = tr("area between SP and PV; smaller is better; compare tunings on the same "
                     "test.");
        lines << QStringLiteral("IAE = ∫ |SP - PV| dt");
        if (r && std::isfinite(r->iae))
            lines << QStringLiteral("IAE = %1").arg(withUnit(g4(r->iae), productUnit(pu, QStringLiteral(u"·s"))));
        break;
    case Quantity::Ise:
        name = tr("ISE, integral of the squared error");
        meaning = tr("like IAE but large errors count much more, so it punishes big overshoots; "
                     "compare tunings on the same test.");
        lines << QStringLiteral("ISE = ∫ (SP - PV)² dt");
        if (r && std::isfinite(r->ise))
            lines << QStringLiteral("ISE = %1").arg(withUnit(g4(r->ise), productUnit(pu, QStringLiteral(u"²·s"))));
        break;
    case Quantity::Itae:
        name = tr("ITAE, time-weighted absolute error");
        meaning = tr("like IAE but an error counts more the later it happens, so it favours a "
                     "fast settling; compare tunings on the same test.");
        lines << QStringLiteral("ITAE = ∫ t |SP - PV| dt");
        if (r && std::isfinite(r->itae))
            lines << QStringLiteral("ITAE = %1").arg(withUnit(g4(r->itae), productUnit(pu, QStringLiteral(u"·s²"))));
        break;
    case Quantity::OutMin:
    case Quantity::OutMax: {
        const bool lo = q == Quantity::OutMin;
        name = lo ? tr("Output min") : tr("Output max");
        meaning = lo ? tr("the lowest controller output between the start and C2.")
                     : tr("the highest controller output between the start and C2.");
        lines << (lo ? QStringLiteral("min(CO)") : QStringLiteral("max(CO)"));
        const std::optional<double> v = r ? (lo ? r->outMin : r->outMax) : std::nullopt;
        if (v)
            lines << QStringLiteral("%1 = %2").arg(lo ? QStringLiteral("min(CO)") : QStringLiteral("max(CO)"),
                                                   withUnit(g4(*v), cu));
        lines << tr("Output range: %1 to %2.")
                     .arg(g4(m_ctx->settings.coRangeMin), g4(m_ctx->settings.coRangeMax));
        break;
    }
    case Quantity::OutTravel:
        name = tr("Output travel");
        meaning = tr("total distance the controller output moved: less travel means less wear "
                     "on the valve.");
        lines << QStringLiteral("travel = Σ |CO[i] - CO[i-1]|");
        if (r && r->outTravel)
            lines << QStringLiteral("travel = %1").arg(withUnit(g4(*r->outTravel), cu));
        break;
    }
    if (predicted) {
        lines << tr("Predicted: simulation of the open-loop model with the controller settings "
                    "of step 5.");
    }
    return tip(name, meaning, lines);
}

} // namespace mb
