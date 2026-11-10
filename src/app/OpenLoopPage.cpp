#include "OpenLoopPage.h"

#include "PlotCursors.h"
#include "RichText.h"
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
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "ClipboardCopy.h"

#include <algorithm>
#include <cmath>

namespace mb {

using namespace analysis;

namespace {

constexpr int kModelPoints = 400;

/// Format string of a tuning cell: %1 = K, %2 = L, %3 = T, %4 = lambda, %5 = L/T. column 0 = Kp,
/// 1 = Ti, 2 = Td. Empty when the rule has no such cell.
QString tuningFormula(TuningRule rule, ControllerKind kind, int column)
{
    using K = ControllerKind;
    switch (rule) {
    case TuningRule::ZieglerNichols:
        if (column == 0) {
            return kind == K::P    ? QStringLiteral("%3 / (%1 × %2)")
                   : kind == K::PI ? QStringLiteral("0.9 × %3 / (%1 × %2)")
                                   : QStringLiteral("1.2 × %3 / (%1 × %2)");
        }
        if (column == 1 && kind == K::PI)
            return QStringLiteral("%2 / 0.3");
        if (column == 1 && kind == K::PID)
            return QStringLiteral("2 × %2");
        if (column == 2 && kind == K::PID)
            return QStringLiteral("0.5 × %2");
        return {};
    case TuningRule::CohenCoon:
        if (column == 0) {
            return kind == K::P    ? QStringLiteral("(%3 / (%1 × %2)) × (1 + %5 / 3)")
                   : kind == K::PI ? QStringLiteral("(%3 / (%1 × %2)) × (0.9 + %5 / 12)")
                                   : QStringLiteral("(%3 / (%1 × %2)) × (4/3 + %5 / 4)");
        }
        if (column == 1 && kind == K::PI)
            return QStringLiteral("%2 × (30 + 3 × %5) / (9 + 20 × %5)");
        if (column == 1 && kind == K::PID)
            return QStringLiteral("%2 × (32 + 6 × %5) / (13 + 8 × %5)");
        if (column == 2 && kind == K::PID)
            return QStringLiteral("4 × %2 / (11 + 2 × %5)");
        return {};
    case TuningRule::ImcLambda:
        if (kind == K::PI) {
            return column == 0   ? QStringLiteral("%3 / (%1 × (%4 + %2))")
                   : column == 1 ? QStringLiteral("%3")
                                 : QString();
        }
        if (kind == K::PID) {
            return column == 0   ? QStringLiteral("(%3 + %2 / 2) / (%1 × (%4 + %2 / 2))")
                   : column == 1 ? QStringLiteral("%3 + %2 / 2")
                                 : QStringLiteral("%3 × %2 / (2 × %3 + %2)");
        }
        return {};
    case TuningRule::Simc:
        if (kind == K::P || kind == K::PI) {
            if (column == 0)
                return QStringLiteral("%3 / (%1 × (%4 + %2))");
            if (column == 1 && kind == K::PI)
                return QStringLiteral("min(%3, 4 × (%4 + %2))");
        }
        return {};
    }
    return {};
}

} // namespace

OpenLoopPage::OpenLoopPage(Context *context, std::function<void()> userEdited, QWidget *parent)
    : QWidget(parent), m_ctx(context), m_userEdited(std::move(userEdited))
{
    setObjectName(QStringLiteral("openLoopPage"));
    buildUi();
}

OpenLoopPage::~OpenLoopPage()
{
    clearOverlays();
}

// ---------------------------------------------------------------------------------------------
// UI

void OpenLoopPage::buildUi()
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);

    // 3. Model
    auto *modelGroup = new QGroupBox(tr("3. Model"), this);
    modelGroup->setObjectName(QStringLiteral("modelGroup"));
    auto *mg = new QGridLayout(modelGroup);
    m_methodCombo = new QComboBox(modelGroup);
    m_methodCombo->setObjectName(QStringLiteral("methodCombo"));
    for (ModelMethod m : {ModelMethod::ZeroTo632, ModelMethod::Smith, ModelMethod::Sundaresan,
                          ModelMethod::Tangent, ModelMethod::Custom})
        m_methodCombo->addItem(methodName(m), static_cast<int>(m));
    m_methodCombo->setToolTip(
        tr("How L (dead time) and T (time constant) are found from two points of the response."));
    m_fromSpin = makeSpin("fromSpin", 0.0, 99.99, 2, 0.0, modelGroup);
    m_fromSpin->setSuffix(QStringLiteral(" %"));
    m_toSpin = makeSpin("toSpin", 0.0, 99.99, 2, 63.2, modelGroup);
    m_toSpin->setSuffix(QStringLiteral(" %"));
    m_tangentT632Check = new QCheckBox(tr("T from the 63.2 % point"), modelGroup);
    m_tangentT632Check->setObjectName(QStringLiteral("tangentT632Check"));
    m_tangentT632Check->setToolTip(
        tr("T = t63.2 - step time - L, instead of the time the tangent needs to go from y0 to y100."));
    m_resultsTable = new QTableWidget(0, 2, modelGroup);
    m_resultsTable->setObjectName(QStringLiteral("resultsTable"));
    m_resultsTable->setHorizontalHeaderLabels({tr("Quantity"), tr("Value")});
    m_resultsTable->verticalHeader()->setVisible(false);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_resultsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupTable(m_resultsTable);
    m_showModelCheck = new QCheckBox(tr("Show model curve"), modelGroup);
    m_showModelCheck->setObjectName(QStringLiteral("showModelCheck"));
    m_checksList = new QListWidget(modelGroup);
    m_checksList->setObjectName(QStringLiteral("checksList"));
    m_checksList->setSelectionMode(QAbstractItemView::NoSelection);
    m_checksList->setFocusPolicy(Qt::NoFocus);
    m_checksList->setWordWrap(true);
    m_checksList->setTextElideMode(Qt::ElideNone);
    m_checksList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_checksList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mg->addWidget(new QLabel(tr("Method"), modelGroup), 0, 0);
    mg->addWidget(m_methodCombo, 0, 1, 1, 3);
    mg->addWidget(new QLabel(tr("From %"), modelGroup), 1, 0);
    mg->addWidget(m_fromSpin, 1, 1);
    mg->addWidget(new QLabel(tr("To %"), modelGroup), 1, 2);
    mg->addWidget(m_toSpin, 1, 3);
    mg->addWidget(m_tangentT632Check, 2, 0, 1, 4);
    mg->addWidget(m_resultsTable, 3, 0, 1, 4);

    // Compare with: other methods' L, T and fit (and curves) next to the selected one.
    m_compareRow = new QWidget(modelGroup);
    m_compareRow->setObjectName(QStringLiteral("compareRow"));
    auto *cr = new QHBoxLayout(m_compareRow);
    cr->setContentsMargins(0, 0, 0, 0);
    cr->addWidget(new QLabel(tr("Compare with:"), m_compareRow));
    struct CompareBox {
        ModelMethod method;
        const char *name;
        QString label;
    };
    const QList<CompareBox> boxes = {
        {ModelMethod::ZeroTo632, "compare0632Check", tr("0 % / 63.2 %")},
        {ModelMethod::Smith, "compareSmithCheck", tr("Smith")},
        {ModelMethod::Sundaresan, "compareSundaresanCheck", tr("Sundaresan")},
        {ModelMethod::Tangent, "compareTangentCheck", tr("Tangent")},
    };
    for (const CompareBox &b : boxes) {
        auto *box = new QCheckBox(b.label, m_compareRow);
        box->setObjectName(QLatin1String(b.name));
        box->setToolTip(tr("Also show the L, T and model curve of %1.").arg(methodName(b.method)));
        cr->addWidget(box);
        m_compareChecks.insert(b.method, box);
        connect(box, &QCheckBox::toggled, this, [this] { onCompareToggled(); });
    }
    cr->addStretch(1);
    mg->addWidget(m_compareRow, 4, 0, 1, 4);
    m_compareTable = new QTableWidget(0, 4, modelGroup);
    m_compareTable->setObjectName(QStringLiteral("compareTable"));
    m_compareTable->setHorizontalHeaderLabels(
        {tr("Method"), QStringLiteral("L (s)"), QStringLiteral("T (s)"), tr("Fit error")});
    m_compareTable->verticalHeader()->setVisible(false);
    m_compareTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_compareTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_compareTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupTable(m_compareTable);
    m_compareTable->hide();
    mg->addWidget(m_compareTable, 5, 0, 1, 4);
    m_modelColorButton = new QPushButton(modelGroup);
    m_modelColorButton->setObjectName(QStringLiteral("modelColorButton"));
    m_modelColorButton->setToolTip(tr("Colour of the model curve"));
    m_modelColorButton->setFixedSize(28, 20);
    m_pickColor = [](const QColor &initial, QWidget *parent) {
        return QColorDialog::getColor(initial, parent, tr("Colour of the model curve"));
    };
    mg->addWidget(m_showModelCheck, 6, 0, 1, 3);
    mg->addWidget(m_modelColorButton, 6, 3, Qt::AlignRight);
    mg->addWidget(m_checksList, 7, 0, 1, 4);
    col->addWidget(modelGroup);
    connect(m_methodCombo, &QComboBox::currentIndexChanged, this, [this] { onMethodChanged(); });
    connect(m_fromSpin, &QDoubleSpinBox::valueChanged, this, [this] { onModelPercentChanged(); });
    connect(m_toSpin, &QDoubleSpinBox::valueChanged, this, [this] { onModelPercentChanged(); });
    connect(m_modelColorButton, &QPushButton::clicked, this, [this] { onPickModelColor(); });
    connect(m_tangentT632Check, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_ctx->settings.tangentT632 = on;
        m_userEdited();
    });
    connect(m_showModelCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_ctx->settings.showModel = on;
        m_userEdited();
    });

    // 4. Tuning
    auto *tuningGroup = new QGroupBox(tr("4. Tuning"), this);
    tuningGroup->setObjectName(QStringLiteral("tuningGroup"));
    auto *tg = new QGridLayout(tuningGroup);
    m_ruleCombo = new QComboBox(tuningGroup);
    m_ruleCombo->setObjectName(QStringLiteral("ruleCombo"));
    for (TuningRule r : {TuningRule::ZieglerNichols, TuningRule::CohenCoon, TuningRule::ImcLambda,
                         TuningRule::Simc})
        m_ruleCombo->addItem(ruleName(r), static_cast<int>(r));
    m_lambdaSpin = makeSpin("lambdaSpin", 0.0, 1e6, 2, 0.0, tuningGroup);
    m_lambdaSpin->setSuffix(tr(" s"));
    m_lambdaSpin->setSpecialValueText(tr("Auto (= L)"));
    m_lambdaSpin->setToolTip(
        tr("λ, the closed-loop time constant of IMC / lambda and SIMC (Auto = L)."));
    m_minutesCheck = new QCheckBox(tr("Ti, Td in minutes"), tuningGroup);
    m_minutesCheck->setObjectName(QStringLiteral("minutesCheck"));
    m_tuningTable = new QTableWidget(0, 4, tuningGroup);
    m_tuningTable->setObjectName(QStringLiteral("tuningTable"));
    m_tuningTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tuningTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setupTable(m_tuningTable);
    m_tuningNote = new QLabel(tuningGroup);
    m_tuningNote->setObjectName(QStringLiteral("tuningNote"));
    m_tuningNote->setWordWrap(true);
    tg->addWidget(new QLabel(tr("Rule"), tuningGroup), 0, 0);
    tg->addWidget(m_ruleCombo, 0, 1, 1, 2);
    tg->addWidget(new QLabel(QStringLiteral("λ"), tuningGroup), 1, 0);
    tg->addWidget(m_lambdaSpin, 1, 1, 1, 2);
    tg->addWidget(m_minutesCheck, 2, 0, 1, 3);
    tg->addWidget(m_tuningTable, 3, 0, 1, 3);
    tg->addWidget(m_tuningNote, 4, 0, 1, 3);
    col->addWidget(tuningGroup);
    connect(m_ruleCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        m_ctx->settings.tuningRule = static_cast<TuningRule>(m_ruleCombo->currentData().toInt());
        updateEnabled();
        m_userEdited();
    });
    connect(m_lambdaSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctx->settings.lambdaS = v;
        m_userEdited();
    });
    connect(m_minutesCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_ctx->settings.timeInMinutes = on;
        m_userEdited();
    });
}

void OpenLoopPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildChecks();
}

// ---------------------------------------------------------------------------------------------
// Controls <-> settings

void OpenLoopPage::syncModelSpins()
{
    const AnalysisSettings &s = m_ctx->settings;
    const QSignalBlocker b1(m_fromSpin);
    const QSignalBlocker b2(m_toSpin);
    if (s.method == ModelMethod::Custom) {
        m_fromSpin->setValue(s.modelPercentA);
        m_toSpin->setValue(s.modelPercentB);
    } else {
        const auto ab = modelPercents(s);
        m_fromSpin->setValue(ab.first);
        m_toSpin->setValue(ab.second);
    }
    m_prevFrom = m_fromSpin->value();
    m_prevTo = m_toSpin->value();
}

void OpenLoopPage::updateEnabled()
{
    const AnalysisSettings &s = m_ctx->settings;
    const bool custom = s.method == ModelMethod::Custom;
    m_fromSpin->setEnabled(custom);
    m_toSpin->setEnabled(custom);
    m_tangentT632Check->setVisible(s.method == ModelMethod::Tangent);
    // The selected method is always shown first in compareTable: no box for it.
    for (auto it = m_compareChecks.cbegin(); it != m_compareChecks.cend(); ++it)
        it.value()->setVisible(it.key() != s.method);
    const bool lam = s.tuningRule == TuningRule::ImcLambda || s.tuningRule == TuningRule::Simc;
    m_lambdaSpin->setEnabled(lam);
}

void OpenLoopPage::syncFromSettings()
{
    const AnalysisSettings &s = m_ctx->settings;
    const bool was = m_updating;
    m_updating = true;
    {
        const QSignalBlocker b4(m_methodCombo);
        const QSignalBlocker b8(m_showModelCheck);
        const QSignalBlocker b13(m_ruleCombo);
        const QSignalBlocker b14(m_lambdaSpin);
        const QSignalBlocker b15(m_minutesCheck);
        const QSignalBlocker b16(m_tangentT632Check);
        m_tangentT632Check->setChecked(s.tangentT632);
        for (auto it = m_compareChecks.cbegin(); it != m_compareChecks.cend(); ++it) {
            const QSignalBlocker blocker(it.value());
            it.value()->setChecked(s.compareMethods.contains(it.key()));
        }
        m_showModelCheck->setChecked(s.showModel);
        updateModelColorButton();
        m_methodCombo->setCurrentIndex(std::max(0, m_methodCombo->findData(static_cast<int>(s.method))));
        m_ruleCombo->setCurrentIndex(std::max(0, m_ruleCombo->findData(static_cast<int>(s.tuningRule))));
        m_lambdaSpin->setValue(s.lambdaS);
        m_minutesCheck->setChecked(s.timeInMinutes);
    }
    syncModelSpins();
    updateEnabled();
    m_updating = was;
}

void OpenLoopPage::onMethodChanged()
{
    if (m_updating)
        return;
    m_ctx->settings.method = static_cast<ModelMethod>(m_methodCombo->currentData().toInt());
    syncModelSpins();
    updateEnabled();
    m_userEdited();
}

void OpenLoopPage::onModelPercentChanged()
{
    if (m_updating || m_ctx->settings.method != ModelMethod::Custom)
        return;
    const double a = m_fromSpin->value();
    const double b = m_toSpin->value();
    if (!(a < b)) {
        const QSignalBlocker b1(m_fromSpin);
        const QSignalBlocker b2(m_toSpin);
        m_fromSpin->setValue(m_prevFrom);
        m_toSpin->setValue(m_prevTo);
        return;
    }
    m_prevFrom = a;
    m_prevTo = b;
    m_ctx->settings.modelPercentA = a;
    m_ctx->settings.modelPercentB = b;
    m_userEdited();
}

void OpenLoopPage::updateModelColorButton()
{
    QColor c(m_ctx->settings.modelColor);
    if (!c.isValid())
        c = QColor(0x20, 0x20, 0x20);
    m_modelColorButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; border: 1px solid #808080; }")
            .arg(c.name()));
}

void OpenLoopPage::onPickModelColor()
{
    QColor initial(m_ctx->settings.modelColor);
    if (!initial.isValid())
        initial = QColor(0x20, 0x20, 0x20);
    const QColor chosen = m_pickColor ? m_pickColor(initial, this) : QColor();
    if (!chosen.isValid())
        return;
    m_ctx->settings.modelColor = chosen.name();
    updateModelColorButton();
    m_userEdited();
}

// ---------------------------------------------------------------------------------------------
// Recompute

QList<ModelMethod> OpenLoopPage::comparedMethods() const
{
    QList<ModelMethod> out;
    for (ModelMethod m : {ModelMethod::ZeroTo632, ModelMethod::Smith, ModelMethod::Sundaresan,
                          ModelMethod::Tangent}) {
        if (m != m_ctx->settings.method && m_ctx->settings.compareMethods.contains(m))
            out.append(m);
    }
    return out;
}

void OpenLoopPage::onCompareToggled()
{
    if (m_updating)
        return;
    QList<ModelMethod> list; // fixed order, no Custom, no duplicates
    for (ModelMethod m : {ModelMethod::ZeroTo632, ModelMethod::Smith, ModelMethod::Sundaresan,
                          ModelMethod::Tangent}) {
        if (m_compareChecks.value(m)->isChecked())
            list.append(m);
    }
    m_ctx->settings.compareMethods = list;
    m_userEdited();
}

void OpenLoopPage::rebuildCompare()
{
    m_compareResults.clear();
    m_compareTable->setRowCount(0);
    const QList<ModelMethod> methods = comparedMethods();
    if (methods.isEmpty()) {
        m_compareTable->hide();
        return;
    }
    const AnalysisSettings &settings = m_ctx->settings;
    const PlotCursors *cur = m_ctx->cursors();
    const bool can = m_ctx->recorder && cur && cur->hasC1() && cur->hasC2() && settings.pvTagId != 0;
    for (ModelMethod m : methods) {
        StepResult r;
        if (can) {
            AnalysisSettings other = settings;
            other.method = m;
            r = analyzeStep(*m_ctx->recorder, settings.pvTagId, cur->c1(), cur->c2(), other);
        }
        m_compareResults.append({m, r});
    }

    const StepResult *selected = (!m_ctx->results.isEmpty() && m_ctx->results.first().valid)
                                     ? &m_ctx->results.first()
                                     : nullptr;
    const QString pu = m_ctx->pvUnit();
    auto addRow = [&](ModelMethod m, const StepResult *r, bool used) {
        const int row = m_compareTable->rowCount();
        m_compareTable->insertRow(row);
        const QString name = used ? tr("%1 (used for tuning)").arg(methodName(m)) : methodName(m);
        m_compareTable->setItem(row, 0, makeItem(name, false, rich::escaped(methodName(m))));
        const QString lText = r && r->deadTime ? g4(*r->deadTime) : kDash;
        const QString tText = r && r->timeConstant ? g4(*r->timeConstant) : kDash;
        QString fitText = kDash;
        if (r && r->fitRmsPercent) {
            fitText = QString::number(*r->fitRmsPercent, 'g', 3) + QStringLiteral(" % ")
                      + kDash.at(0) + u' ' + fitWord(*r->fitRmsPercent);
        }
        m_compareTable->setItem(row, 1, makeItem(lText, true, explainModelTime(true, r, m)));
        m_compareTable->setItem(row, 2, makeItem(tText, true, explainModelTime(false, r, m)));
        m_compareTable->setItem(row, 3, makeItem(fitText, true, explainResult(Quantity::Fit, r)));
    };
    addRow(settings.method, selected, true);
    for (const auto &entry : std::as_const(m_compareResults))
        addRow(entry.first, entry.second.valid ? &entry.second : nullptr, false);
    fitTableHeight(m_compareTable);
    m_compareTable->show();
}

void OpenLoopPage::recompute()
{
    rebuildCompare();
    rebuildResultsTable();
    rebuildChecks();
    rebuildTuningTable();
}

void OpenLoopPage::rebuildResultsTable()
{
    m_resultsTable->setRowCount(0);
    const QList<StepResult> &results = m_ctx->results;
    const StepResult *r = results.isEmpty() ? nullptr : &results.first();
    const bool ok = r && r->valid;

    struct Row {
        Quantity q;
        QString label;
    };
    QList<Row> rows = {
        {Quantity::Y0, QStringLiteral("y0")},
        {Quantity::Y100, QStringLiteral("y100")},
        {Quantity::DeltaPv, kDelta + QStringLiteral("PV")},
        {Quantity::DeltaCo, kDelta + QStringLiteral("CO")},
        {Quantity::K, QStringLiteral("K")},
    };
    if (m_ctx->percentGainsShown())
        rows.append({Quantity::KPct, QStringLiteral("K (%/%)")});
    rows.append({{Quantity::L, QStringLiteral("L")},
                 {Quantity::T, QStringLiteral("T")},
                 {Quantity::LOverT, QStringLiteral("L/T")},
                 {Quantity::Fit, tr("Fit error")}});

    const QString pu = m_ctx->pvUnit();
    for (const Row &row : std::as_const(rows)) {
        QString value = kDash;
        if (ok) {
            switch (row.q) {
            case Quantity::Y0:
                value = withUnit(g4(r->y0), pu);
                break;
            case Quantity::Y100:
                value = withUnit(g4(r->y100), pu);
                break;
            case Quantity::DeltaPv:
                value = withUnit(g4(r->deltaPv), pu);
                break;
            case Quantity::DeltaCo:
                value = r->deltaCo ? withUnit(g4(*r->deltaCo), m_ctx->coUnit()) : kDash;
                break;
            case Quantity::K:
                value = r->gain ? withUnit(g4(*r->gain), m_ctx->kUnit()) : kDash;
                break;
            case Quantity::KPct:
                if (r->gain)
                    value = g4(*r->gain * *m_ctx->coSpan() / *m_ctx->pvSpan()) + QStringLiteral(" %/%");
                break;
            case Quantity::L:
                value = r->deadTime ? g4(*r->deadTime) + QStringLiteral(" s") : kDash;
                break;
            case Quantity::T:
                value = r->timeConstant ? g4(*r->timeConstant) + QStringLiteral(" s") : kDash;
                break;
            case Quantity::LOverT:
                if (r->deadTime && r->timeConstant)
                    value = g4(*r->deadTime / *r->timeConstant);
                break;
            case Quantity::Fit:
                if (r->fitRms) {
                    value = withUnit(g4(*r->fitRms), pu);
                    if (r->fitRmsPercent) {
                        value += QStringLiteral(" (") + QString::number(*r->fitRmsPercent, 'g', 3)
                                 + QStringLiteral(" %) ") + kDash.at(0) + u' '
                                 + fitWord(*r->fitRmsPercent);
                    }
                }
                break;
            }
        }
        const QString toolTip = explainResult(row.q, ok ? r : nullptr);
        const int n = m_resultsTable->rowCount();
        m_resultsTable->insertRow(n);
        m_resultsTable->setItem(n, 0, makeItem(row.label, false, toolTip));
        m_resultsTable->setItem(n, 1, makeItem(value, true, toolTip));
    }
    fitTableHeight(m_resultsTable);
}

void OpenLoopPage::rebuildChecks()
{
    m_checksList->clear();
    const QList<StepResult> &results = m_ctx->results;
    const AnalysisSettings &settings = m_ctx->settings;
    if (m_ctx->recorder && !results.isEmpty() && results.first().valid) {
        const auto ab = modelPercents(settings);
        const QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
        QList<StepCheck> checks = checkStep(*m_ctx->recorder, results.first(), settings);
        // Find step reported that the PV never settled before the end of the range: say so even
        // when the slope test at C2 does not fire (noisy or flat end).
        if (m_ctx->findNotSettled && !checks.contains(StepCheck::NotSettled)) {
            qsizetype pos = 0;
            while (pos < checks.size() && checks.at(pos) < StepCheck::NotSettled)
                ++pos;
            checks.insert(pos, StepCheck::NotSettled); // keep enum order
        }
        const int textWidth = std::max(120, m_checksList->viewport()->width() - 40);
        int total = 4;
        const auto addItem = [&](const QString &text) {
            auto *item = new QListWidgetItem(icon, text, m_checksList);
            item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(text);
            const int h = m_checksList->fontMetrics()
                              .boundingRect(0, 0, textWidth, 2000, Qt::TextWordWrap, text)
                              .height()
                          + 8;
            item->setSizeHint(QSize(0, std::max(h, 24)));
            total += std::max(h, 24);
        };
        for (StepCheck c : checks) {
            QString text;
            switch (c) {
            case StepCheck::NoDeltaCo:
                if (settings.deltaCoTagId != 0) {
                    text = tr("The controller output (CO) does not change between the cursors: K "
                              "can't be computed. Move the cursors around the step.");
                } else {
                    text = tr("K can't be computed: choose the controller output (CO) or type ΔCO "
                              "in step 1.");
                }
                break;
            case StepCheck::NoModel:
                text = tr("L and T can't be computed: the PV does not reach both model points "
                          "(%1 % and %2 %) between the cursors.")
                           .arg(fmtPlain(ab.first), fmtPlain(ab.second));
                break;
            case StepCheck::NotSettled:
                text = tr("The PV is still moving at C2: move C2 later.");
                break;
            case StepCheck::SmallStep:
                text = tr("The step is small compared with the noise: the results are uncertain.");
                break;
            case StepCheck::NegativeDeadTime:
                text = tr("L is negative: C1 is after the start of the response. Move C1 before "
                          "the step.");
                break;
            case StepCheck::DeadTimeDominant:
                text = tr("L/T > 1: dead time dominates. Ziegler-Nichols and Cohen-Coon will be "
                          "aggressive; prefer IMC / lambda or SIMC.");
                break;
            case StepCheck::DeadTimeFromFirstMove:
                text = tr("L is taken at the first movement of the PV, so lags such as transmitter "
                          "damping are counted in T and L is short. Ziegler-Nichols and Cohen-Coon "
                          "then give a Kp that is too high: choose the Smith method, or IMC / "
                          "lambda or SIMC.");
                break;
            case StepCheck::TangentNoisy:
                text = tr("The tangent is taken on noisy data, so its slope is uncertain. Set a "
                          "smoothing S, or choose the Smith method.");
                break;
            case StepCheck::FitPoor:
                text = tr("The model does not fit the response well (fit error > 8 %).");
                break;
            }
            addItem(text);
        }
        if (coMovedAfterStep(results.first())) {
            addItem(tr("The controller output changes after the step: the loop looks closed. Put "
                       "the controller in manual for an open-loop test, or use the Closed loop "
                       "area."));
        }
        m_checksList->setFixedHeight(total);
    }
    m_checksList->setVisible(m_checksList->count() > 0);
}

bool OpenLoopPage::coMovedAfterStep(const StepResult &r) const
{
    const AnalysisSettings &s = m_ctx->settings;
    const Recorder *rec = m_ctx->recorder;
    if (!rec || !m_ctx->tagById(s.deltaCoTagId) || !r.valid || !r.deltaCo || *r.deltaCo == 0.0)
        return false;
    const double threshold = 0.1 * std::abs(*r.deltaCo);
    const double start = r.stepTime + (r.deadTime ? std::max(*r.deadTime, 0.0) : 1.0);
    if (!(start < r.t2))
        return false;
    const auto range = rec->rowRange(start, r.t2);
    double first = kNaN;
    double prev = kNaN;
    bool have = false;
    for (qsizetype i = range.first; i < range.second; ++i) {
        const double v = rec->valueAt(i, s.deltaCoTagId);
        if (std::isnan(v))
            continue;
        if (!have)
            first = v;
        else if (std::abs(v - prev) > threshold)
            return true; // a jump between consecutive samples
        prev = v;
        have = true;
    }
    return have && std::abs(prev - first) > threshold; // or a drift over the window
}

void OpenLoopPage::rebuildTuningTable()
{
    const AnalysisSettings &settings = m_ctx->settings;
    const QList<StepResult> &results = m_ctx->results;
    const ControllerSettings &controller = settings.controller;
    m_tuningTable->clearSpans();
    m_tuningTable->setRowCount(0);
    const bool minutes = settings.timeInMinutes;
    // The Kp (%/%) column only goes with the ideal form shown as a gain (PB is already the percent form).
    const bool pct = m_ctx->percentGainsShown() && controller.form == PidForm::Ideal
                     && controller.gain == GainUnit::Gain;
    const PidColumnLabels labels = pidColumnLabels(controller, minutes);
    QStringList headers{labels.gain};
    if (pct)
        headers << QStringLiteral("Kp (%/%)");
    headers << labels.integral << labels.derivative;
    m_tuningTable->setColumnCount(int(headers.size()));
    m_tuningTable->setHorizontalHeaderLabels(headers);

    const StepResult *r = (!results.isEmpty() && results.first().valid) ? &results.first() : nullptr;
    QList<TuningRow> rows;
    if (r && r->gain && r->deadTime && r->timeConstant) {
        rows = tune(*r->gain, *r->deadTime, *r->timeConstant, settings.tuningRule,
                    settings.lambdaS);
    }
    QStringList rowLabels;
    const PidSpans spans = m_ctx->pidSpans();
    for (int i = 0; i < rows.size(); ++i) {
        const TuningRow &tr_ = rows.at(i);
        rowLabels << toString(tr_.kind);
        m_tuningTable->insertRow(i);
        if (!tr_.available) {
            const QString text = tr("not defined");
            const QString toolTip =
                tip(toString(tr_.kind),
                    tr("%1 gives no %2 setting for this model.")
                        .arg(ruleName(settings.tuningRule), toString(tr_.kind)),
                    {});
            m_tuningTable->setItem(i, 0, makeItem(text, false, toolTip));
            m_tuningTable->setSpan(i, 0, 1, m_tuningTable->columnCount());
            continue;
        }
        const IdealPid ideal{tr_.kp.value_or(0.0), tr_.ti, tr_.td};
        const FormValues fv = toForm(ideal, controller, spans, minutes);
        if (!fv.available) {
            // This form has no equivalent of the row (or needs ranges): say why across the row.
            const QString toolTip = tip(toString(tr_.kind), fv.reason, {});
            m_tuningTable->setItem(i, 0, makeItem(fv.reason, false, toolTip));
            m_tuningTable->setSpan(i, 0, 1, m_tuningTable->columnCount());
            continue;
        }
        int c = 0;
        // part: 0 = gain, 1 = integral, 2 = derivative (conversion lines); column: explainTuning().
        auto put = [&](const QString &text, int column, int part) {
            QString toolTip = explainTuning(column, tr_.kind, tr_, *r);
            if (part >= 0)
                toolTip = appendLines(toolTip, pidConversionLines(part, ideal, controller, spans, minutes));
            m_tuningTable->setItem(i, c++, makeItem(text, true, toolTip));
        };
        put(tr_.kp ? g4(fv.gain) : kDash, 0, 0);
        if (pct)
            put(tr_.kp ? g4(*tr_.kp * *m_ctx->pvSpan() / *m_ctx->coSpan()) : kDash, 3, -1);
        put(g4(fv.integral), 1, 1);
        put(g4(fv.derivative), 2, 2);
    }
    m_tuningTable->setVerticalHeaderLabels(rowLabels);
    fitTableHeight(m_tuningTable);

    QString note = tr("Ideal (ISA) form: u = Kp (e + 1/Ti ∫e dt + Td de/dt). Ti and Td in %1.")
                       .arg(minutes ? tr("min") : tr("s"));
    if (controller.form != PidForm::Ideal)
        note += u' ' + tr("Converted from the ideal form.");
    if (r && r->gain && *r->gain < 0.0)
        note += u' ' + tr("K is negative: the controller must be direct acting (Kp is shown negative).");
    m_tuningNote->setText(note);
}

// ---------------------------------------------------------------------------------------------
// Plot overlays

void OpenLoopPage::setOverlaysShown(bool shown)
{
    if (shown == m_overlaysShown)
        return;
    m_overlaysShown = shown;
    rebuildOverlays();
}

void OpenLoopPage::clearOverlays()
{
    for (QwtPlotItem *item : std::as_const(m_overlayItems)) {
        item->detach();
        delete item;
    }
    m_overlayItems.clear();
}

void OpenLoopPage::forgetOverlays()
{
    m_overlayItems.clear();
}

namespace {

/// The model curve of `r` (from t1 to t2, ~400 points, with a point at the end of the dead time).
QwtPlotCurve *makeModelCurve(const StepResult &r, const QString &title)
{
    QVector<QPointF> pts;
    pts.reserve(kModelPoints + 2);
    for (int k = 0; k < kModelPoints; ++k) {
        const double t = r.t1 + (r.t2 - r.t1) * k / (kModelPoints - 1);
        pts.append(QPointF(t, fopdtModelValue(r, t)));
    }
    const double tb = r.stepTime + *r.deadTime;
    if (tb > r.t1 && tb < r.t2) {
        const auto it = std::lower_bound(pts.begin(), pts.end(), tb,
                                         [](const QPointF &p, double t) { return p.x() < t; });
        pts.insert(it, QPointF(tb, fopdtModelValue(r, tb)));
    }
    auto *curve = new QwtPlotCurve(title);
    curve->setSamples(pts);
    return curve;
}

/// Pen of the compared method's curve: its own dash pattern, so the curves tell apart.
void setCompareDash(QPen &pen, ModelMethod method)
{
    switch (method) {
    case ModelMethod::ZeroTo632:
        pen.setStyle(Qt::DotLine);
        break;
    case ModelMethod::Smith:
        pen.setStyle(Qt::DashDotLine);
        break;
    case ModelMethod::Sundaresan:
        pen.setStyle(Qt::DashDotDotLine);
        break;
    case ModelMethod::Tangent:
    case ModelMethod::Custom:
        pen.setDashPattern({6.0, 2.0, 1.0, 2.0, 1.0, 2.0, 1.0, 2.0});
        break;
    }
}

} // namespace

void OpenLoopPage::rebuildOverlays()
{
    clearOverlays();
    if (!m_ctx->plot || !m_overlaysShown)
        return;
    const AnalysisSettings &settings = m_ctx->settings;
    const QList<StepResult> &results = m_ctx->results;
    if (results.isEmpty())
        return;
    QwtPlot *plot = m_ctx->plot->plot();
    const StepResult &r = results.first();
    const Tag *tag = m_ctx->tagById(r.tagId);
    if (!r.valid || !tag || !tag->plotted)
        return;
    const int axis = tag->axis == YAxis::Right ? QwtPlot::yRight : QwtPlot::yLeft;

    // Width: the PV curve's pen width + 1 (the PV curve has the tag's legend title).
    double penWidth = 1.5;
    const QString pvTitle = tag->unit.isEmpty() ? tag->name
                                                : QStringLiteral("%1 [%2]").arg(tag->name, tag->unit);
    const QwtPlotItemList curves = plot->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : curves) {
        if (item->title().text() == pvTitle) {
            penWidth = static_cast<QwtPlotCurve *>(item)->pen().widthF();
            break;
        }
    }
    QColor modelColor(settings.modelColor);
    if (!modelColor.isValid())
        modelColor = QColor(0x20, 0x20, 0x20);

    const auto attach = [&](QwtPlotItem *item, double z) {
        item->setItemAttribute(QwtPlotItem::Legend, false);
        item->setYAxis(axis);
        item->setZ(z);
        item->attach(plot);
        m_overlayItems.append(item);
    };

    if (settings.showModel && r.deadTime && r.timeConstant && r.t2 > r.t1) {
        QwtPlotCurve *curve = makeModelCurve(r, tr("%1 model").arg(tag->name));
        curve->setPen(QPen(modelColor, penWidth + 1.0, Qt::DashLine));
        attach(curve, 12.0);
    }

    // The compared methods' model curves: always while compared, each with its own dash pattern.
    for (const auto &entry : std::as_const(m_compareResults)) {
        const StepResult &c = entry.second;
        if (!c.valid || !c.deadTime || !c.timeConstant || !(c.t2 > c.t1))
            continue;
        QwtPlotCurve *curve =
            makeModelCurve(c, tr("%1 model (%2)").arg(tag->name, methodName(entry.first)));
        QPen pen(modelColor, penWidth + 1.0);
        setCompareDash(pen, entry.first);
        curve->setPen(pen);
        attach(curve, 11.5);
    }

    // The tangent construction of the Tangent method: the line from y0 to y100 and its point.
    if (settings.method == ModelMethod::Tangent && r.tangent && r.tangent->slope != 0.0
        && std::isfinite(r.tangent->slope)) {
        const StepResult::TangentLine &t = *r.tangent;
        const double x0 = t.time + (r.y0 - t.value) / t.slope;
        const double x100 = t.time + (r.y100 - t.value) / t.slope;
        auto *line = new QwtPlotCurve(tr("%1 tangent").arg(tag->name));
        line->setSamples(QVector<QPointF>{QPointF(x0, r.y0), QPointF(x100, r.y100)});
        line->setPen(QPen(modelColor, penWidth, Qt::DashLine));
        attach(line, 12.0);
        auto *point = new QwtPlotMarker;
        point->setValue(t.time, t.value);
        point->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, QBrush(modelColor), QPen(Qt::white, 1),
                                       QSize(7, 7)));
        attach(point, 12.5);
    }
}

// ---------------------------------------------------------------------------------------------
// Copy / readout

QString OpenLoopPage::copyText() const
{
    // The tuning table's controller names are row headers, which tableToTsv() does not include.
    QString tuning = tableToTsv(m_tuningTable->model(), {});
    QStringList lines = tuning.split(QStringLiteral("\r\n"));
    QString tuningWithKinds;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).isEmpty() && i == lines.size() - 1)
            break;
        const QString kind =
            i == 0 ? tr("Controller")
                   : (m_tuningTable->verticalHeaderItem(i - 1) ? m_tuningTable->verticalHeaderItem(i - 1)->text()
                                                               : QString());
        tuningWithKinds += kind + u'\t' + lines.at(i) + QStringLiteral("\r\n");
    }
    QString text = tableToTsv(m_resultsTable->model(), {}) + QStringLiteral("\r\n");
    if (!m_compareTable->isHidden())
        text += tableToTsv(m_compareTable->model(), {}) + QStringLiteral("\r\n");
    return text + tuningWithKinds;
}

QString OpenLoopPage::readoutText() const
{
    const QList<StepResult> &results = m_ctx->results;
    if (results.isEmpty() || !results.first().valid)
        return {};
    const StepResult &pv = results.first();
    const Tag *pvTag = m_ctx->tagById(pv.tagId);
    const auto ab = modelPercents(m_ctx->settings);
    QStringList model;
    if (pv.gain)
        model << QStringLiteral("K = ") + withUnit(g4(*pv.gain), m_ctx->kUnit());
    if (pv.deadTime)
        model << QStringLiteral("L = ") + g4(*pv.deadTime) + QStringLiteral(" s");
    if (pv.timeConstant)
        model << QStringLiteral("T = ") + g4(*pv.timeConstant) + QStringLiteral(" s");
    if (pv.fitRmsPercent) {
        model << tr("fit %1 % (%2)")
                     .arg(QString::number(*pv.fitRmsPercent, 'g', 3), fitWord(*pv.fitRmsPercent));
    }
    QString line = (pvTag ? tagLabel(*pvTag) : QString::number(pv.tagId)) + QStringLiteral(" (")
                   + fmtPercent(ab.first) + QStringLiteral(" / ") + fmtPercent(ab.second)
                   + QStringLiteral(")");
    if (!model.isEmpty())
        line += QStringLiteral(": ") + model.join(QStringLiteral(", "));
    return line;
}

// ---------------------------------------------------------------------------------------------
// Tooltips

QString OpenLoopPage::explainResult(Quantity quantity, const StepResult *r) const
{
    const AnalysisSettings &settings = m_ctx->settings;
    const QString pu = m_ctx->pvUnit();
    const QString cu = m_ctx->coUnit();
    const bool mean = settings.reference == ReferenceMode::WindowMean;
    const QString w = g4(settings.referenceWindowS);
    QString name;
    QString meaning;
    QStringList lines;

    switch (quantity) {
    case Quantity::Y0:
    case Quantity::Y100: {
        const bool first = quantity == Quantity::Y0;
        const QString c = first ? QStringLiteral("C1") : QStringLiteral("C2");
        const QString y = first ? QStringLiteral("y0") : QStringLiteral("y100");
        name = first ? tr("y0, level before the step") : tr("y100, level after the step");
        if (mean) {
            meaning = tr("mean of the PV over the %1 s before %2 (%3 % of the step).")
                          .arg(w, c, first ? QStringLiteral("0") : QStringLiteral("100"));
            lines << tr("%1 = mean of PV over [%2 - W, %2]").arg(y, c);
        } else {
            meaning = tr("value of the PV at cursor %1 (%2 % of the step).")
                          .arg(c, first ? QStringLiteral("0") : QStringLiteral("100"));
            lines << y + QStringLiteral(" = PV(%1)").arg(c);
        }
        if (r) {
            const double t = first ? r->t1 : r->t2;
            const double v = first ? r->y0 : r->y100;
            if (mean) {
                lines << tr("%1 = mean of PV over [%2 - %3, %2] = %4")
                                 .arg(y, fmtSeconds(t), w, withUnit(g4(v), pu));
            } else {
                lines << y + QStringLiteral(" = PV(%1) = %2").arg(fmtSeconds(t), withUnit(g4(v), pu));
            }
        }
        break;
    }
    case Quantity::DeltaPv:
        name = kDelta + tr("PV, change of the PV");
        meaning = tr("how much the PV moved between the cursors.");
        lines << kDelta + QStringLiteral("PV = y100 - y0");
        if (r) {
            lines << kDelta + QStringLiteral("PV = %1 - %2 = %3")
                                  .arg(g4p(r->y100), g4p(r->y0), withUnit(g4(r->deltaPv), pu));
        }
        break;
    case Quantity::DeltaCo: {
        name = kDelta + tr("CO, change of the controller output");
        const Tag *co = m_ctx->tagById(settings.deltaCoTagId);
        if (co) {
            meaning = tr("how much the controller output %1 changed between the cursors.").arg(co->name);
            lines << kDelta + QStringLiteral("CO = CO(C2) - CO(C1)");
            if (r && r->deltaCo) {
                std::optional<double> a;
                std::optional<double> b;
                if (m_ctx->recorder) {
                    a = referenceLevel(*m_ctx->recorder, co->id, r->t1, settings.reference,
                                       settings.referenceWindowS);
                    b = referenceLevel(*m_ctx->recorder, co->id, r->t2, settings.reference,
                                       settings.referenceWindowS);
                }
                if (a && b) {
                    lines << kDelta + QStringLiteral("CO = %1 - %2 = %3")
                                          .arg(g4p(*b), g4p(*a), withUnit(g4(*r->deltaCo), cu));
                } else {
                    lines << kDelta + QStringLiteral("CO = %1").arg(withUnit(g4(*r->deltaCo), cu));
                }
            }
        } else {
            meaning = tr("change of the controller output, typed in step 1.");
            lines << kDelta + tr("CO = typed in step 1");
            if (r && r->deltaCo)
                lines << kDelta + QStringLiteral("CO = %1").arg(g4(*r->deltaCo));
        }
        break;
    }
    case Quantity::K:
        name = tr("K, process gain");
        meaning = tr("change of the PV per unit of controller output, once settled.");
        lines << QStringLiteral("K = ") + kDelta + QStringLiteral("PV / ") + kDelta + QStringLiteral("CO");
        if (r && r->gain && r->deltaCo) {
            lines << QStringLiteral("K = %1 / %2 = %3")
                         .arg(g4p(r->deltaPv), g4p(*r->deltaCo), withUnit(g4(*r->gain), m_ctx->kUnit()));
        }
        break;
    case Quantity::KPct: {
        const std::optional<double> pvSpan = m_ctx->pvSpan();
        const std::optional<double> coSpan = m_ctx->coSpan();
        name = tr("K (%/%), process gain in percent of range");
        meaning = tr("K for signals expressed in percent of their ranges.");
        lines << tr("K (%/%) = K %1 CO span / PV span").arg(kTimes);
        if (r && r->gain && pvSpan && coSpan) {
            lines << QStringLiteral("K (%/%) = %1 %2 %3 / %4 = %5 %/%")
                         .arg(g4p(*r->gain), kTimes, g4p(*coSpan), g4p(*pvSpan),
                              g4(*r->gain * *coSpan / *pvSpan));
        }
        break;
    }
    case Quantity::L:
        return explainModelTime(true, r, settings.method);
    case Quantity::T:
        return explainModelTime(false, r, settings.method);
    case Quantity::LOverT:
        name = tr("L/T, controllability ratio");
        meaning = tr("controllability ratio: below 0.3 easy, above 1 dead-time dominated.");
        lines << QStringLiteral("L/T = L / T");
        if (r && r->deadTime && r->timeConstant) {
            lines << QStringLiteral("L/T = %1 / %2 = %3")
                         .arg(g4p(*r->deadTime), g4p(*r->timeConstant),
                              g4(*r->deadTime / *r->timeConstant));
        }
        break;
    case Quantity::Fit:
        name = tr("Fit error");
        meaning = tr("how far the model curve is from the recorded PV between the cursors: RMS of "
                     "(PV - model), and as % of |%1PV|.").arg(kDelta);
        lines << tr("fit % = 100 %1 RMS(PV - model) / |%2PV|").arg(kTimes, kDelta);
        if (r && r->fitRms && r->fitRmsPercent) {
            lines << QStringLiteral("fit % = 100 %1 %2 / |%3| = %4 %")
                         .arg(kTimes, g4(*r->fitRms), g4(r->deltaPv), g4(*r->fitRmsPercent));
            lines << tr("Under 3 % is Good, under 8 % Acceptable, otherwise Poor (here: %1).")
                         .arg(fitWord(*r->fitRmsPercent));
        } else {
            lines << tr("Under 3 % is Good, under 8 % Acceptable, otherwise Poor.");
        }
        break;
    }
    return tip(name, meaning, lines);
}

QString OpenLoopPage::explainModelTime(bool forL, const StepResult *r, ModelMethod method) const
{
    AnalysisSettings settings = m_ctx->settings;
    settings.method = method;
    const auto ab = modelPercents(settings);
    const double fa = ab.first / 100.0;
    const double fb = ab.second / 100.0;
    const QString sa = QStringLiteral("t") + QString::number(ab.first, 'g', 4);
    const QString sb = QStringLiteral("t") + QString::number(ab.second, 'g', 4);
    const QString faS = g4(fa);
    const QString fbS = g4(fb);
    const bool ok = r && r->deadTime && r->timeConstant;
    double L = 0.0;
    double T = 0.0;
    double ta = 0.0;
    double tb = 0.0;
    if (ok) {
        L = *r->deadTime;
        T = *r->timeConstant;
        ta = L - T * std::log(1.0 - fa);                       // from the step time
        tb = ta + T * (std::log(1.0 - fa) - std::log(1.0 - fb));
    }
    const QString A = ok ? g4(ta) : QString();
    const QString B = ok ? g4(tb) : QString();
    QStringList lines;
    const ModelMethod m = method;
    // Smith and Sundaresan use rounded textbook constants (1.5, 0.67, ...): the computed value is
    // from the exact two-point formula, so a hand calculation can differ in the 4th digit.
    const QString approx = (m == ModelMethod::Sundaresan || m == ModelMethod::Smith)
                               ? QStringLiteral("≈")
                               : QStringLiteral("=");

    QString name;
    QString meaning;
    if (forL) {
        name = tr("L, dead time");
        meaning = tr("delay between the step and the start of the PV response.");
        switch (m) {
        case ModelMethod::ZeroTo632:
            lines << QStringLiteral("L = t0");
            if (ok)
                lines << QStringLiteral("L = %1 s").arg(A);
            break;
        case ModelMethod::Smith:
            lines << QStringLiteral("L = %1 - T").arg(sb);
            if (ok)
                lines << QStringLiteral("L = %1 - %2 %3 %4 s").arg(B, g4p(T), approx, g4(L));
            break;
        case ModelMethod::Sundaresan:
            lines << QStringLiteral("L = 1.3 %1 - 0.29 %2").arg(sa, sb);
            if (ok) {
                lines << QStringLiteral("L = 1.3 %1 %2 - 0.29 %1 %3 %4 %5 s")
                             .arg(kTimes, A, B, approx, g4(L));
            }
            break;
        case ModelMethod::Custom:
            lines << QStringLiteral("L = %1 + T ln(1 - a)").arg(sa);
            if (ok) {
                lines << QStringLiteral("L = %1 + %2 %3 ln(1 - %4) = %5 s")
                             .arg(A, g4p(T), kTimes, faS, g4(L));
            }
            break;
        case ModelMethod::Tangent:
            lines << tr("L = time the tangent reaches y0 - step time");
            if (ok && r->tangent && r->tangent->slope != 0.0) {
                const StepResult::TangentLine &tl = *r->tangent;
                const double x0 = tl.time + (r->y0 - tl.value) / tl.slope;
                lines << tr("The tangent touches the response at t = %1 s, where the PV is %2, with "
                            "slope %3 per s.")
                             .arg(g4(tl.time), g4(tl.value), g4(tl.slope));
                lines << QStringLiteral("L = %1 - %2 = %3 s").arg(g4p(x0), g4p(r->stepTime), g4(L));
            }
            break;
        }
    } else {
        name = tr("T, time constant");
        meaning = tr("time the PV needs to cover 63.2 % of its change after the dead time.");
        switch (m) {
        case ModelMethod::ZeroTo632:
            lines << QStringLiteral("T = %1 - %2").arg(sb, sa);
            if (ok)
                lines << QStringLiteral("T = %1 - %2 = %3 s").arg(B, g4p(ta), g4(T));
            break;
        case ModelMethod::Smith:
            lines << QStringLiteral("T = 1.5 (%1 - %2)").arg(sb, sa);
            if (ok)
                lines << QStringLiteral("T = 1.5 (%1 - %2) %3 %4 s").arg(B, g4p(ta), approx, g4(T));
            break;
        case ModelMethod::Sundaresan:
            lines << QStringLiteral("T = 0.67 (%1 - %2)").arg(sb, sa);
            if (ok)
                lines << QStringLiteral("T = 0.67 (%1 - %2) %3 %4 s").arg(B, g4p(ta), approx, g4(T));
            break;
        case ModelMethod::Custom:
            lines << QStringLiteral("T = (%1 - %2) / (ln(1 - a) - ln(1 - b))").arg(sb, sa);
            if (ok) {
                lines << QStringLiteral("T = (%1 - %2) / (ln(1 - %3) - ln(1 - %4)) = %5 s")
                             .arg(B, g4p(ta), faS, fbS, g4(T));
            }
            break;
        case ModelMethod::Tangent:
            if (m_ctx->settings.tangentT632) {
                lines << tr("T = t63.2 - step time - L");
                if (ok) {
                    lines << QStringLiteral("T = %1 - %2 - %3 = %4 s")
                                 .arg(g4p(r->stepTime + L + T), g4p(r->stepTime), g4p(L), g4(T));
                }
            } else {
                lines << QStringLiteral("T = ") + kDelta + QStringLiteral("PV / slope");
                if (ok && r->tangent) {
                    lines << QStringLiteral("T = %1 / %2 = %3 s")
                                 .arg(g4p(r->deltaPv), g4p(r->tangent->slope), g4(T));
                }
            }
            break;
        }
    }
    if (ok) {
        lines << tr("Times are measured from the step time (t = %1 s).").arg(g4(r->stepTime));
    }
    return tip(name, meaning, lines);
}

QString OpenLoopPage::explainTuning(int column, ControllerKind kind, const TuningRow &row,
                                    const StepResult &r) const
{
    // column: 0 = Kp, 1 = Ti, 2 = Td, 3 = Kp (%/%)
    const AnalysisSettings &settings = m_ctx->settings;
    const TuningRule rule = settings.tuningRule;
    const bool minutes = settings.timeInMinutes;
    const double K = r.gain.value_or(kNaN);
    const double L = r.deadTime.value_or(kNaN);
    const double T = r.timeConstant.value_or(kNaN);
    const double lam = effectiveLambda(L, settings.lambdaS);
    const QString ruleText = ruleName(rule);
    const QString kindName = toString(kind);
    QString name;
    QString meaning;
    QStringList lines;
    const QString pu = m_ctx->pvUnit();
    const QString cu = m_ctx->coUnit();
    const QString kpUnit = (!pu.isEmpty() && !cu.isEmpty()) ? cu + u'/' + pu : QString();

    if (column == 3) {
        const std::optional<double> pvSpan = m_ctx->pvSpan();
        const std::optional<double> coSpan = m_ctx->coSpan();
        name = QStringLiteral("Kp (%/%), ") + kindName;
        meaning = tr("controller gain for signals expressed in percent of their ranges.");
        lines << tr("Kp (%/%) = Kp %1 PV span / CO span").arg(kTimes);
        if (row.kp && pvSpan && coSpan) {
            lines << QStringLiteral("Kp (%/%) = %1 %2 %3 / %4 = %5 %/%")
                         .arg(g4p(*row.kp), kTimes, g4p(*pvSpan), g4p(*coSpan),
                              g4(*row.kp * *pvSpan / *coSpan));
        }
        return tip(name, meaning, lines);
    }

    const QString format = tuningFormula(rule, kind, column);
    const QString symbol = column == 0 ? QStringLiteral("Kp") : column == 1 ? QStringLiteral("Ti")
                                                                              : QStringLiteral("Td");
    name = symbol + QStringLiteral(", ") + kindName + QStringLiteral(" (") + ruleText + u')';
    meaning = column == 0   ? tr("proportional gain: controller output change per unit of PV error.")
              : column == 1 ? tr("integral time: how long the integral action takes to repeat the "
                                 "proportional action.")
                            : tr("derivative time: how far ahead the derivative action looks.");
    if (!format.isEmpty()) {
        lines << symbol + QStringLiteral(" = ")
                     + substitute(format, {QStringLiteral("K"), QStringLiteral("L"),
                                           QStringLiteral("T"), QStringLiteral(u"λ"),
                                           QStringLiteral("r")});
        const std::optional<double> value = column == 0 ? row.kp : column == 1 ? row.ti : row.td;
        if (value && std::isfinite(K)) {
            const QString numeric =
                substitute(format, {g4p(K), g4p(L), g4p(T), g4p(lam), g4p(L / T)});
            QString result = column == 0 ? withUnit(g4(*value), kpUnit)
                                         : g4(*value) + QStringLiteral(" s");
            if (column != 0 && minutes)
                result += QStringLiteral(" = ") + g4(*value / 60.0) + QStringLiteral(" min");
            lines << symbol + QStringLiteral(" = ") + numeric + QStringLiteral(" = ") + result;
        }
        if (format.contains(QStringLiteral("%5")))
            lines << QStringLiteral("r = L / T = %1 / %2 = %3").arg(g4p(L), g4p(T), g4(L / T));
        if (format.contains(QStringLiteral("%4"))) {
            lines << (settings.lambdaS > 0.0
                          ? QStringLiteral("λ = %1 s").arg(g4(lam))
                          : tr("λ = L = %1 s (Auto)").arg(g4(lam)));
        }
    }
    return tip(name, meaning, lines);
}

} // namespace mb
