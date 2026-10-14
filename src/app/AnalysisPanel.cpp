#include "AnalysisPanel.h"

#include "ClipboardCopy.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "RichText.h"
#include "TrendPlot.h"

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_map.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
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
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

const QString kDash = QStringLiteral(u"—");  // em dash: missing value
const QString kDelta = QStringLiteral(u"Δ");
const QString kMinus = QStringLiteral(u"−");
const QString kTimes = QStringLiteral(u"×");

constexpr int kMaxPercents = 20;
constexpr int kGuideRtti = QwtPlotItem::Rtti_PlotUserItem + 1;
constexpr int kModelPoints = 400;

/// 4 significant digits (tables and tooltips); "—" for NaN / infinite.
QString g4(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 4) : kDash;
}

QString g4(std::optional<double> v)
{
    return v ? g4(*v) : kDash;
}

/// Negative numbers in parentheses, for use inside formulas ("a - (-3)").
QString g4p(double v)
{
    return v < 0.0 ? QStringLiteral("(") + g4(v) + QStringLiteral(")") : g4(v);
}

/// 6 significant digits (setpoint table).
QString g6(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 6) : kDash;
}

QString withUnit(const QString &number, const QString &unit)
{
    return unit.isEmpty() ? number : number + u' ' + unit;
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

QString fitWord(double fitRmsPercent)
{
    switch (fitQuality(fitRmsPercent)) {
    case FitQuality::Good:
        return QStringLiteral("Good");
    case FitQuality::Acceptable:
        return QStringLiteral("Acceptable");
    case FitQuality::Poor:
        return QStringLiteral("Poor");
    }
    return {};
}

/// "name: meaning<br>line<br>line" - everything escaped, name in bold.
QString tip(const QString &name, const QString &meaning, const QStringList &lines)
{
    QString s = rich::bold(name) + QStringLiteral(": ") + rich::escaped(meaning);
    for (const QString &line : lines) {
        if (!line.isEmpty())
            s += QStringLiteral("<br>") + rich::escaped(line);
    }
    return s;
}

QTableWidgetItem *makeItem(const QString &text, bool alignRight, const QString &toolTip = {})
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (alignRight)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!toolTip.isEmpty())
        item->setToolTip(toolTip);
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

void fitTableHeight(QTableWidget *t)
{
    int h = t->horizontalHeader()->sizeHint().height() + 2 * t->frameWidth() + 2;
    for (int r = 0; r < t->rowCount(); ++r)
        h += t->rowHeight(r);
    t->setFixedHeight(h);
}

void setupTable(QTableWidget *t)
{
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    installCopyShortcut(t);
}

std::optional<double> parseNumber(const QString &text)
{
    QString s = text.trimmed();
    s.replace(u',', u'.');
    if (s.isEmpty())
        return std::nullopt;
    bool ok = false;
    const double v = QLocale::c().toDouble(s, &ok);
    return (ok && std::isfinite(v)) ? std::optional<double>(v) : std::nullopt;
}

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

/// Replaces %1..%5 in `format` (QString::arg would warn about unused numbers).
QString substitute(QString format, const QStringList &values)
{
    for (int i = values.size(); i >= 1; --i)
        format.replace(QStringLiteral("%") + QString::number(i), values.at(i - 1));
    return format;
}

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

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    syncControlsFromSettings();
    recompute();
}

AnalysisPanel::~AnalysisPanel()
{
    clearOverlays(true);
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
    m_pvMinEdit = new QLineEdit(signalsGroup);
    m_pvMinEdit->setObjectName(QStringLiteral("pvMinEdit"));
    m_pvMinEdit->setPlaceholderText(tr("PV min"));
    m_pvMaxEdit = new QLineEdit(signalsGroup);
    m_pvMaxEdit->setObjectName(QStringLiteral("pvMaxEdit"));
    m_pvMaxEdit->setPlaceholderText(tr("PV max"));
    for (QLineEdit *e : {m_pvMinEdit, m_pvMaxEdit}) {
        auto *v = new QDoubleValidator(-1e12, 1e12, 6, e);
        v->setLocale(QLocale::c());
        e->setValidator(v);
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
    sg->addWidget(new QLabel(tr("Ranges for %/% gains"), signalsGroup), 3, 0, 1, 3);
    sg->addWidget(new QLabel(tr("PV"), signalsGroup), 4, 0);
    sg->addWidget(m_pvMinEdit, 4, 1);
    sg->addWidget(m_pvMaxEdit, 4, 2);
    sg->addWidget(new QLabel(tr("CO"), signalsGroup), 5, 0);
    sg->addWidget(m_coMinSpin, 5, 1);
    sg->addWidget(m_coMaxSpin, 5, 2);
    col->addWidget(signalsGroup);
    connect(m_pvCombo, &QComboBox::currentIndexChanged, this, [this] { onPvChanged(); });
    connect(m_coCombo, &QComboBox::currentIndexChanged, this, [this] { onCoChanged(); });
    connect(m_deltaCoSpin, &QDoubleSpinBox::valueChanged, this, [this] { onCoChanged(); });
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

    // 3. Model
    auto *modelGroup = new QGroupBox(tr("3. Model"), body);
    modelGroup->setObjectName(QStringLiteral("modelGroup"));
    auto *mg = new QGridLayout(modelGroup);
    m_methodCombo = new QComboBox(modelGroup);
    m_methodCombo->setObjectName(QStringLiteral("methodCombo"));
    for (ModelMethod m : {ModelMethod::ZeroTo632, ModelMethod::Smith, ModelMethod::Sundaresan,
                          ModelMethod::Custom})
        m_methodCombo->addItem(toString(m), static_cast<int>(m));
    m_methodCombo->setToolTip(
        tr("How L (dead time) and T (time constant) are found from two points of the response."));
    m_fromSpin = makeSpin("fromSpin", 0.0, 99.99, 2, 0.0, modelGroup);
    m_fromSpin->setSuffix(QStringLiteral(" %"));
    m_toSpin = makeSpin("toSpin", 0.0, 99.99, 2, 63.2, modelGroup);
    m_toSpin->setSuffix(QStringLiteral(" %"));
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
    mg->addWidget(m_resultsTable, 2, 0, 1, 4);
    mg->addWidget(m_showModelCheck, 3, 0, 1, 4);
    mg->addWidget(m_checksList, 4, 0, 1, 4);
    col->addWidget(modelGroup);
    connect(m_methodCombo, &QComboBox::currentIndexChanged, this, [this] { onMethodChanged(); });
    connect(m_fromSpin, &QDoubleSpinBox::valueChanged, this, [this] { onModelPercentChanged(); });
    connect(m_toSpin, &QDoubleSpinBox::valueChanged, this, [this] { onModelPercentChanged(); });
    connect(m_showModelCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.showModel = on;
        userChangedSettings();
    });

    // 4. Tuning
    auto *tuningGroup = new QGroupBox(tr("4. Tuning"), body);
    tuningGroup->setObjectName(QStringLiteral("tuningGroup"));
    auto *tg = new QGridLayout(tuningGroup);
    m_ruleCombo = new QComboBox(tuningGroup);
    m_ruleCombo->setObjectName(QStringLiteral("ruleCombo"));
    for (TuningRule r : {TuningRule::ZieglerNichols, TuningRule::CohenCoon, TuningRule::ImcLambda,
                         TuningRule::Simc})
        m_ruleCombo->addItem(toString(r), static_cast<int>(r));
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
        m_settings.tuningRule = static_cast<TuningRule>(m_ruleCombo->currentData().toInt());
        updateEnabled();
        userChangedSettings();
    });
    connect(m_lambdaSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_settings.lambdaS = v;
        userChangedSettings();
    });
    connect(m_minutesCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.timeInMinutes = on;
        userChangedSettings();
    });

    // Setpoints and options (collapsible)
    m_setpointsGroup = new QGroupBox(tr("Setpoints and options"), body);
    m_setpointsGroup->setObjectName(QStringLiteral("setpointsGroup"));
    m_setpointsGroup->setCheckable(true);
    m_setpointsGroup->setChecked(true);
    auto *spOuter = new QVBoxLayout(m_setpointsGroup);
    auto *spContent = new QWidget(m_setpointsGroup);
    spOuter->addWidget(spContent);
    auto *sp = new QGridLayout(spContent);
    sp->setContentsMargins(0, 0, 0, 0);

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
    m_percentSpin = makeSpin("percentSpin", 0.0, 100.0, 2, 28.3, spContent);
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
    col->addWidget(m_setpointsGroup);
    col->addStretch(1);

    // Collapse toggle: the group box would disable its contents when unchecked; keep them enabled
    // (they are just hidden). The check state is not saved.
    connect(m_setpointsGroup, &QGroupBox::toggled, this, [spContent](bool on) {
        spContent->setVisible(on);
        spContent->setEnabled(true);
    });
    m_setpointsGroup->setChecked(false);
    spContent->setVisible(false);
    spContent->setEnabled(true);

    connect(m_setpointTagList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) { onSetpointItemChanged(item); });
    connect(m_referenceCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating)
            return;
        m_settings.reference = m_referenceCombo->currentData().toInt() == 1
                                   ? ReferenceMode::WindowMean : ReferenceMode::CursorValue;
        updateEnabled();
        userChangedSettings();
    });
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
    connect(m_addPercentButton, &QPushButton::clicked, this, [this] { onAddPercent(); });
    connect(m_removePercentButton, &QPushButton::clicked, this, [this] { onRemovePercent(); });
    connect(m_detailedLabelsCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.detailedLabels = on;
        userChangedSettings();
    });
    connect(m_guideLinesCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_settings.guideLines = on;
        userChangedSettings();
    });
}

void AnalysisPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildChecks();
}

// ---------------------------------------------------------------------------------------------
// Public API

void AnalysisPanel::setRecorder(const Recorder *recorder)
{
    m_recorder = recorder;
    recompute();
}

void AnalysisPanel::setPlot(TrendPlot *plot)
{
    if (plot == m_plot)
        return;
    clearOverlays(true);
    disconnect(m_cursorsConnection);
    disconnect(m_plotDestroyedConnection);
    m_plot = plot;
    if (m_plot) {
        m_cursorsConnection = connect(m_plot->cursors(), &PlotCursors::cursorsChanged, this, [this] {
            if (!m_settingCursors)
                recompute();
        });
        // The plot owns (and deletes) the overlay items when it goes away first.
        m_plotDestroyedConnection = connect(m_plot, &QObject::destroyed, this, [this] {
            m_markers.clear();
            m_guideItems.clear();
            m_modelCurves.clear();
            m_plot = nullptr;
            recompute();
        });
    }
    recompute();
}

void AnalysisPanel::setTags(const QList<Tag> &tags)
{
    m_tags = tags;
    // Keep the PV / CO while their tags exist.
    if (!tagById(m_settings.pvTagId))
        m_settings.pvTagId = 0;
    if (!tagById(m_settings.deltaCoTagId) || m_settings.deltaCoTagId == m_settings.pvTagId)
        m_settings.deltaCoTagId = 0;
    rebuildPvCombo();
    rebuildCoCombo();
    rebuildSetpointList();
    updateEnabled();
    recompute();
}

void AnalysisPanel::normalizeSettings()
{
    // Keep the list invariant (sorted, no duplicates, 0..100, at most 20) even for a hand-edited
    // project file.
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

    // Tags that are not in the project (deleted before the file was saved) mean "none".
    if (!m_tags.isEmpty()) {
        if (!tagById(m_settings.pvTagId))
            m_settings.pvTagId = 0;
        if (!tagById(m_settings.deltaCoTagId))
            m_settings.deltaCoTagId = 0;
    }
    if (m_settings.pvTagId != 0 && m_settings.deltaCoTagId == m_settings.pvTagId)
        m_settings.deltaCoTagId = 0;
}

void AnalysisPanel::setSettings(const AnalysisSettings &settings)
{
    m_settings = settings;
    normalizeSettings();
    syncControlsFromSettings();
    recompute();
}

AnalysisSettings AnalysisPanel::settings() const
{
    return m_settings;
}

QList<StepResult> AnalysisPanel::results() const
{
    return m_results;
}

const Tag *AnalysisPanel::tagById(TagId id) const
{
    if (id == 0)
        return nullptr;
    for (const Tag &t : m_tags) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

bool AnalysisPanel::shownTag(const Tag *tag) const
{
    return tag && m_settings.setpointTagIds.contains(tag->id)
           && (tag->plotted || tag->id == m_settings.pvTagId);
}

QString AnalysisPanel::pvUnit() const
{
    const Tag *t = tagById(m_settings.pvTagId);
    return t ? t->unit : QString();
}

QString AnalysisPanel::coUnit() const
{
    const Tag *t = tagById(m_settings.deltaCoTagId);
    return t ? t->unit : QString();
}

QString AnalysisPanel::kUnit() const
{
    const QString p = pvUnit();
    const QString c = coUnit();
    return (!p.isEmpty() && !c.isEmpty()) ? p + u'/' + c : QString();
}

std::optional<double> AnalysisPanel::pvSpan() const
{
    if (m_settings.pvRangeMin && m_settings.pvRangeMax) {
        const double s = *m_settings.pvRangeMax - *m_settings.pvRangeMin;
        if (s != 0.0 && std::isfinite(s))
            return s;
    }
    return std::nullopt;
}

std::optional<double> AnalysisPanel::coSpan() const
{
    const double s = m_settings.coRangeMax - m_settings.coRangeMin;
    if (s != 0.0 && std::isfinite(s))
        return s;
    return std::nullopt;
}

bool AnalysisPanel::percentGainsShown() const
{
    return pvSpan() && coSpan();
}

// ---------------------------------------------------------------------------------------------
// Controls <-> settings

void AnalysisPanel::rebuildPvCombo()
{
    const QSignalBlocker blocker(m_pvCombo);
    m_pvCombo->clear();
    m_pvCombo->addItem(tr("Choose..."), 0u);
    for (const Tag &t : std::as_const(m_tags))
        m_pvCombo->addItem(tagLabel(t), t.id);
    const int idx = m_settings.pvTagId != 0 ? m_pvCombo->findData(m_settings.pvTagId) : -1;
    m_pvCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AnalysisPanel::rebuildCoCombo()
{
    const QSignalBlocker blocker(m_coCombo);
    m_coCombo->clear();
    m_coCombo->addItem(tr("Typed ΔCO"), 0u);
    for (const Tag &t : std::as_const(m_tags)) {
        if (t.id != m_settings.pvTagId || m_settings.pvTagId == 0)
            m_coCombo->addItem(tagLabel(t), t.id);
    }
    const int idx = m_settings.deltaCoTagId != 0 ? m_coCombo->findData(m_settings.deltaCoTagId) : -1;
    m_coCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AnalysisPanel::rebuildSetpointList()
{
    const QSignalBlocker blocker(m_setpointTagList);
    m_setpointTagList->clear();
    for (const Tag &t : std::as_const(m_tags)) {
        if (!t.plotted && t.id != m_settings.pvTagId)
            continue;
        auto *item = new QListWidgetItem(tagLabel(t), m_setpointTagList);
        item->setData(Qt::UserRole, t.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(m_settings.setpointTagIds.contains(t.id) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::DecorationRole, QColor(t.color));
    }
}

void AnalysisPanel::rebuildPercentList()
{
    const QSignalBlocker blocker(m_percentList);
    m_percentList->clear();
    for (double p : std::as_const(m_settings.percents)) {
        auto *item = new QListWidgetItem(fmtPercent(p), m_percentList);
        item->setData(Qt::UserRole, p);
    }
}

void AnalysisPanel::syncModelSpins()
{
    const QSignalBlocker b1(m_fromSpin);
    const QSignalBlocker b2(m_toSpin);
    if (m_settings.method == ModelMethod::Custom) {
        m_fromSpin->setValue(m_settings.modelPercentA);
        m_toSpin->setValue(m_settings.modelPercentB);
    } else {
        const auto ab = modelPercents(m_settings);
        m_fromSpin->setValue(ab.first);
        m_toSpin->setValue(ab.second);
    }
    m_prevFrom = m_fromSpin->value();
    m_prevTo = m_toSpin->value();
}

void AnalysisPanel::updateEnabled()
{
    m_deltaCoSpin->setEnabled(m_coCombo->currentIndex() == 0);
    const bool custom = m_settings.method == ModelMethod::Custom;
    m_fromSpin->setEnabled(custom);
    m_toSpin->setEnabled(custom);
    m_windowSpin->setEnabled(m_settings.reference == ReferenceMode::WindowMean);
    const bool lam = m_settings.tuningRule == TuningRule::ImcLambda
                     || m_settings.tuningRule == TuningRule::Simc;
    m_lambdaSpin->setEnabled(lam);
}

void AnalysisPanel::syncControlsFromSettings()
{
    const bool was = m_updating;
    m_updating = true;
    rebuildPvCombo();
    rebuildCoCombo();
    rebuildSetpointList();
    rebuildPercentList();
    {
        const QSignalBlocker b1(m_referenceCombo);
        const QSignalBlocker b2(m_windowSpin);
        const QSignalBlocker b3(m_smoothingSpin);
        const QSignalBlocker b4(m_methodCombo);
        const QSignalBlocker b5(m_deltaCoSpin);
        const QSignalBlocker b6(m_detailedLabelsCheck);
        const QSignalBlocker b7(m_guideLinesCheck);
        const QSignalBlocker b8(m_showModelCheck);
        const QSignalBlocker b9(m_pvMinEdit);
        const QSignalBlocker b10(m_pvMaxEdit);
        const QSignalBlocker b11(m_coMinSpin);
        const QSignalBlocker b12(m_coMaxSpin);
        const QSignalBlocker b13(m_ruleCombo);
        const QSignalBlocker b14(m_lambdaSpin);
        const QSignalBlocker b15(m_minutesCheck);
        m_guideLinesCheck->setChecked(m_settings.guideLines);
        m_showModelCheck->setChecked(m_settings.showModel);
        m_detailedLabelsCheck->setChecked(m_settings.detailedLabels);
        m_referenceCombo->setCurrentIndex(m_settings.reference == ReferenceMode::WindowMean ? 1 : 0);
        m_windowSpin->setValue(m_settings.referenceWindowS);
        m_smoothingSpin->setValue(m_settings.smoothingS);
        m_methodCombo->setCurrentIndex(std::max(0, m_methodCombo->findData(static_cast<int>(m_settings.method))));
        m_deltaCoSpin->setValue(m_settings.manualDeltaCo);
        m_pvMinEdit->setText(m_settings.pvRangeMin ? QLocale::c().toString(*m_settings.pvRangeMin, 'g', 10)
                                                   : QString());
        m_pvMaxEdit->setText(m_settings.pvRangeMax ? QLocale::c().toString(*m_settings.pvRangeMax, 'g', 10)
                                                   : QString());
        m_coMinSpin->setValue(m_settings.coRangeMin);
        m_coMaxSpin->setValue(m_settings.coRangeMax);
        m_ruleCombo->setCurrentIndex(std::max(0, m_ruleCombo->findData(static_cast<int>(m_settings.tuningRule))));
        m_lambdaSpin->setValue(m_settings.lambdaS);
        m_minutesCheck->setChecked(m_settings.timeInMinutes);
    }
    syncModelSpins();
    updateEnabled();
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
    m_settings.pvTagId = id;
    if (id != 0 && !m_settings.setpointTagIds.contains(id))
        m_settings.setpointTagIds.append(id);
    if (id != 0 && m_settings.deltaCoTagId == id)
        m_settings.deltaCoTagId = 0;
    rebuildCoCombo();
    rebuildSetpointList();
    updateEnabled();
    userChangedSettings();
}

void AnalysisPanel::onCoChanged()
{
    if (m_updating)
        return;
    m_settings.deltaCoTagId = m_coCombo->currentData().toUInt();
    m_settings.manualDeltaCo = m_deltaCoSpin->value();
    updateEnabled();
    userChangedSettings();
}

void AnalysisPanel::onMethodChanged()
{
    if (m_updating)
        return;
    m_settings.method = static_cast<ModelMethod>(m_methodCombo->currentData().toInt());
    syncModelSpins();
    updateEnabled();
    userChangedSettings();
}

void AnalysisPanel::onModelPercentChanged()
{
    if (m_updating || m_settings.method != ModelMethod::Custom)
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
    m_settings.modelPercentA = a;
    m_settings.modelPercentB = b;
    userChangedSettings();
}

void AnalysisPanel::onRangesChanged()
{
    if (m_updating)
        return;
    const auto pvMin = parseNumber(m_pvMinEdit->text());
    const auto pvMax = parseNumber(m_pvMaxEdit->text());
    const bool changed = pvMin != m_settings.pvRangeMin || pvMax != m_settings.pvRangeMax
                         || m_coMinSpin->value() != m_settings.coRangeMin
                         || m_coMaxSpin->value() != m_settings.coRangeMax;
    if (!changed)
        return;
    m_settings.pvRangeMin = pvMin;
    m_settings.pvRangeMax = pvMax;
    m_settings.coRangeMin = m_coMinSpin->value();
    m_settings.coRangeMax = m_coMaxSpin->value();
    userChangedSettings();
}

void AnalysisPanel::onAddPercent()
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

void AnalysisPanel::onRemovePercent()
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

void AnalysisPanel::onSetpointItemChanged(QListWidgetItem *item)
{
    if (m_updating || !item)
        return;
    const TagId id = item->data(Qt::UserRole).toUInt();
    const bool checked = item->checkState() == Qt::Checked;
    if (checked && !m_settings.setpointTagIds.contains(id))
        m_settings.setpointTagIds.append(id);
    else if (!checked)
        m_settings.setpointTagIds.removeAll(id);
    else
        return;
    userChangedSettings();
}

// ---------------------------------------------------------------------------------------------
// Find step

void AnalysisPanel::onFindStep()
{
    const TagId co = m_settings.deltaCoTagId;
    const TagId pv = m_settings.pvTagId;
    if (!m_recorder || !m_plot || co == 0 || pv == 0)
        return;
    m_hintOverride.clear();
    if (m_recorder->size() < 2) {
        m_hintOverride = tr("No step found in the controller output in the visible range.");
        updateHint();
        return;
    }
    const double first = m_recorder->timeAt(0);
    const double last = m_recorder->timeAt(m_recorder->size() - 1);
    double from = first;
    double to = last;
    if (QwtPlot *p = m_plot->plot()) {
        const QwtInterval iv = p->axisInterval(QwtPlot::xBottom);
        if (iv.isValid()) {
            from = std::max(first, iv.minValue());
            to = std::min(last, iv.maxValue());
        }
    }
    auto hasCoSamples = [&](double a, double b) {
        if (!(b > a))
            return false;
        const auto range = m_recorder->rowRange(a, b);
        for (qsizetype i = range.first; i < range.second; ++i) {
            if (!std::isnan(m_recorder->valueAt(i, co)))
                return true;
        }
        return false;
    };
    if (!hasCoSamples(from, to)) {
        from = first;
        to = last;
    }
    const std::optional<StepWindow> w = findStep(*m_recorder, co, pv, from, to);
    if (!w) {
        m_hintOverride = tr("No step found in the controller output in the visible range.");
        updateHint();
        return;
    }
    PlotCursors *cur = m_plot->cursors();
    m_settingCursors = true;
    cur->setC1(w->c1);
    cur->setC2(w->c2);
    m_settingCursors = false;
    recompute();
}

// ---------------------------------------------------------------------------------------------
// Recompute

void AnalysisPanel::rememberDataState()
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

void AnalysisPanel::recompute()
{
    m_hintOverride.clear();
    m_results.clear();
    const PlotCursors *cur = m_plot ? m_plot->cursors() : nullptr;
    if (m_recorder && cur && cur->hasC1() && cur->hasC2() && m_settings.pvTagId != 0) {
        m_results.append(
            analyzeStep(*m_recorder, m_settings.pvTagId, cur->c1(), cur->c2(), m_settings));
        for (const Tag &t : std::as_const(m_tags)) {
            if (t.id != m_settings.pvTagId && m_settings.setpointTagIds.contains(t.id))
                m_results.append(analyzeStep(*m_recorder, t.id, cur->c1(), cur->c2(), m_settings));
        }
    }
    rememberDataState();

    const bool valid = !m_results.isEmpty() && m_results.first().valid;
    m_copyResultsButton->setEnabled(valid);
    m_exportButton->setEnabled(valid);

    updateCursorLabel();
    updateFindButton();
    rebuildResultsTable();
    rebuildChecks();
    rebuildTuningTable();
    rebuildSetpointTable();
    rebuildOverlays();
    updateHint();
    emit resultsChanged();
}

void AnalysisPanel::updateCursorLabel()
{
    const PlotCursors *cur = m_plot ? m_plot->cursors() : nullptr;
    const QString c1 = (cur && cur->hasC1()) ? fmtSeconds(cur->c1()) : kDash;
    const QString c2 = (cur && cur->hasC2()) ? fmtSeconds(cur->c2()) : kDash;
    m_cursorLabel->setText(QStringLiteral("C1 = %1, C2 = %2").arg(c1, c2));
}

void AnalysisPanel::updateHint()
{
    QString text;
    const PlotCursors *cur = m_plot ? m_plot->cursors() : nullptr;
    if (m_settings.pvTagId == 0) {
        text = tr("Choose the process value (PV) in step 1.");
    } else if (!(cur && cur->hasC1() && cur->hasC2())) {
        text = tr("Place C1 just before the step and C2 after the PV has settled, or click Find step.");
    } else if (!m_results.isEmpty() && !m_results.first().valid) {
        text = m_results.first().error;
    }
    if (!m_hintOverride.isEmpty())
        text = m_hintOverride;
    m_hintLabel->setText(text);
}

void AnalysisPanel::updateFindButton()
{
    QString why;
    if (m_settings.pvTagId == 0)
        why = tr("Choose the process value (PV) in step 1.");
    else if (m_settings.deltaCoTagId == 0)
        why = tr("Choose the controller output (CO) in step 1: Find step looks for its step.");
    else if (!m_recorder)
        why = tr("There is no recording to search.");
    m_findStepButton->setEnabled(why.isEmpty());
    m_findStepButton->setToolTip(
        why.isEmpty()
            ? tr("Looks for the largest step of the controller output in the visible time range "
                 "(the whole recording if there is none) and places C1 and C2 around it.")
            : why);
}

void AnalysisPanel::rebuildResultsTable()
{
    m_resultsTable->setRowCount(0);
    const StepResult *r = m_results.isEmpty() ? nullptr : &m_results.first();
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
    if (percentGainsShown())
        rows.append({Quantity::KPct, QStringLiteral("K (%/%)")});
    rows.append({{Quantity::L, QStringLiteral("L")},
                 {Quantity::T, QStringLiteral("T")},
                 {Quantity::LOverT, QStringLiteral("L/T")},
                 {Quantity::Fit, tr("Fit error")}});

    const QString pu = pvUnit();
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
                value = r->deltaCo ? withUnit(g4(*r->deltaCo), coUnit()) : kDash;
                break;
            case Quantity::K:
                value = r->gain ? withUnit(g4(*r->gain), kUnit()) : kDash;
                break;
            case Quantity::KPct:
                if (r->gain)
                    value = g4(*r->gain * *coSpan() / *pvSpan()) + QStringLiteral(" %/%");
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

void AnalysisPanel::rebuildChecks()
{
    m_checksList->clear();
    if (m_recorder && !m_results.isEmpty() && m_results.first().valid) {
        const auto ab = modelPercents(m_settings);
        const QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
        const QList<StepCheck> checks = checkStep(*m_recorder, m_results.first());
        const int textWidth = std::max(120, m_checksList->viewport()->width() - 40);
        int total = 4;
        for (StepCheck c : checks) {
            QString text;
            switch (c) {
            case StepCheck::NoDeltaCo:
                text = tr("K can't be computed: choose the controller output (CO) or type ΔCO in "
                          "step 1.");
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
            case StepCheck::FitPoor:
                text = tr("The model does not fit the response well (fit error > 8 %).");
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

void AnalysisPanel::rebuildTuningTable()
{
    m_tuningTable->clearSpans();
    m_tuningTable->setRowCount(0);
    const bool pct = percentGainsShown();
    const bool minutes = m_settings.timeInMinutes;
    const QString tUnit = minutes ? QStringLiteral("min") : QStringLiteral("s");
    QStringList headers{QStringLiteral("Kp")};
    if (pct)
        headers << QStringLiteral("Kp (%/%)");
    headers << QStringLiteral("Ti (%1)").arg(tUnit) << QStringLiteral("Td (%1)").arg(tUnit);
    m_tuningTable->setColumnCount(int(headers.size()));
    m_tuningTable->setHorizontalHeaderLabels(headers);

    const StepResult *r = (!m_results.isEmpty() && m_results.first().valid) ? &m_results.first() : nullptr;
    QList<TuningRow> rows;
    if (r && r->gain && r->deadTime && r->timeConstant) {
        rows = tune(*r->gain, *r->deadTime, *r->timeConstant, m_settings.tuningRule,
                    m_settings.lambdaS);
    }
    QStringList rowLabels;
    const double div = minutes ? 60.0 : 1.0;
    for (int i = 0; i < rows.size(); ++i) {
        const TuningRow &tr_ = rows.at(i);
        rowLabels << toString(tr_.kind);
        m_tuningTable->insertRow(i);
        if (!tr_.available) {
            const QString text = tr("not defined");
            const QString toolTip =
                tip(toString(tr_.kind),
                    tr("%1 gives no %2 setting for this model.")
                        .arg(toString(m_settings.tuningRule), toString(tr_.kind)),
                    {});
            m_tuningTable->setItem(i, 0, makeItem(text, false, toolTip));
            m_tuningTable->setSpan(i, 0, 1, m_tuningTable->columnCount());
            continue;
        }
        int c = 0;
        auto put = [&](const QString &text, int column) {
            const QString toolTip = explainTuning(column, tr_.kind, tr_, *r);
            m_tuningTable->setItem(i, c++, makeItem(text, true, toolTip));
        };
        put(g4(tr_.kp), 0);
        if (pct)
            put(tr_.kp ? g4(*tr_.kp * *pvSpan() / *coSpan()) : kDash, 3);
        put(tr_.ti ? g4(*tr_.ti / div) : kDash, 1);
        put(tr_.td ? g4(*tr_.td / div) : kDash, 2);
    }
    m_tuningTable->setVerticalHeaderLabels(rowLabels);
    fitTableHeight(m_tuningTable);

    QString note = tr("Ideal (ISA) form: u = Kp (e + 1/Ti ∫e dt + Td de/dt). Ti and Td in %1.")
                       .arg(minutes ? tr("min") : tr("s"));
    if (r && r->gain && *r->gain < 0.0)
        note += u' ' + tr("K is negative: the controller must be direct acting (Kp is shown negative).");
    m_tuningNote->setText(note);
}

void AnalysisPanel::rebuildSetpointTable()
{
    m_setpointTable->setRowCount(0);
    int row = 0;
    for (const StepResult &r : std::as_const(m_results)) {
        const Tag *tag = tagById(r.tagId);
        if (!r.valid || !shownTag(tag))
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

void AnalysisPanel::clearOverlays(bool detach)
{
    Q_UNUSED(detach)
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

void AnalysisPanel::rebuildOverlays()
{
    clearOverlays(true);
    if (!m_plot)
        return;
    QwtPlot *plot = m_plot->plot();
    QMap<int, QList<GuidePoint>> guidePoints; // by Y axis (QwtPlot::yLeft / yRight)
    for (int i = 0; i < m_results.size(); ++i) {
        const StepResult &r = m_results.at(i);
        const Tag *tag = tagById(r.tagId);
        if (!r.valid || !tag)
            continue;
        const QColor color(tag->color);
        const int axis = tag->axis == YAxis::Right ? QwtPlot::yRight : QwtPlot::yLeft;

        if (i == 0 && m_settings.showModel && r.deadTime && r.timeConstant && r.t2 > r.t1) {
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
            auto *curve = new QwtPlotCurve(tag->name + QStringLiteral(" model"));
            curve->setSamples(pts);
            curve->setPen(QPen(color, 1.5, Qt::DashLine));
            curve->setItemAttribute(QwtPlotItem::Legend, false);
            curve->setYAxis(axis);
            curve->setZ(12.0);
            curve->attach(plot);
            m_modelCurves.append(curve);
        }
        if (!shownTag(tag))
            continue;
        for (const SetpointResult &sp : r.setpoints) {
            if (!sp.found)
                continue;
            auto *marker = new QwtPlotMarker;
            marker->setValue(sp.time, sp.target);
            marker->setSymbol(new SetpointSymbol(color));
            marker->setZ(25.0); // above the curves and the model curve
            QString text = fmtPercent(sp.percent);
            if (m_settings.detailedLabels) {
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
    if (m_settings.guideLines) {
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
    const QString text = tableToTsv(m_resultsTable->model(), {}) + QStringLiteral("\r\n")
                         + tuningWithKinds + QStringLiteral("\r\n")
                         + tableToTsv(m_setpointTable->model(), {});
    QGuiApplication::clipboard()->setText(text);
}

QString AnalysisPanel::readoutText() const
{
    const PlotCursors *cur = m_plot ? m_plot->cursors() : nullptr;
    if (!cur || !cur->hasC1() || !cur->hasC2() || m_results.isEmpty() || !m_results.first().valid)
        return {};

    QStringList lines{QStringLiteral("C1 = ") + fmtSeconds(cur->c1()) + QStringLiteral(", C2 = ")
                      + fmtSeconds(cur->c2())};
    const StepResult &pv = m_results.first();
    const Tag *pvTag = tagById(pv.tagId);
    const auto ab = modelPercents(m_settings);
    QStringList model;
    if (pv.gain)
        model << QStringLiteral("K = ") + withUnit(g4(*pv.gain), kUnit());
    if (pv.deadTime)
        model << QStringLiteral("L = ") + g4(*pv.deadTime) + QStringLiteral(" s");
    if (pv.timeConstant)
        model << QStringLiteral("T = ") + g4(*pv.timeConstant) + QStringLiteral(" s");
    if (pv.fitRmsPercent) {
        model << QStringLiteral("fit ") + QString::number(*pv.fitRmsPercent, 'g', 3)
                     + QStringLiteral(" % (") + fitWord(*pv.fitRmsPercent) + u')';
    }
    QString line = (pvTag ? tagLabel(*pvTag) : QString::number(pv.tagId)) + QStringLiteral(" (")
                   + fmtPercent(ab.first) + QStringLiteral(" / ") + fmtPercent(ab.second)
                   + QStringLiteral(")");
    if (!model.isEmpty())
        line += QStringLiteral(": ") + model.join(QStringLiteral(", "));
    lines << line;

    for (int i = 1; i < m_results.size(); ++i) {
        const StepResult &r = m_results.at(i);
        const Tag *tag = tagById(r.tagId);
        if (!shownTag(tag))
            continue;
        const QString name = tagLabel(*tag);
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
        lines << name + QStringLiteral(": ") + sps.join(QStringLiteral(", "));
    }
    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------------------------
// Slots

void AnalysisPanel::refresh()
{
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

    const PlotCursors *cur = m_plot ? m_plot->cursors() : nullptr;
    if (!cur || !cur->hasC1() || !cur->hasC2()) {
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

void AnalysisPanel::resetData()
{
    recompute();
}

// ---------------------------------------------------------------------------------------------
// Tooltips

QString AnalysisPanel::explainResult(Quantity quantity, const StepResult *r) const
{
    const QString pu = pvUnit();
    const QString cu = coUnit();
    const bool mean = m_settings.reference == ReferenceMode::WindowMean;
    const QString w = g4(m_settings.referenceWindowS);
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
            lines << y + QStringLiteral(" = mean of PV over [%1 - W, %1]").arg(c);
        } else {
            meaning = tr("value of the PV at cursor %1 (%2 % of the step).")
                          .arg(c, first ? QStringLiteral("0") : QStringLiteral("100"));
            lines << y + QStringLiteral(" = PV(%1)").arg(c);
        }
        if (r) {
            const double t = first ? r->t1 : r->t2;
            const double v = first ? r->y0 : r->y100;
            if (mean) {
                lines << y + QStringLiteral(" = mean of PV over [%1 - %2, %1] = %3")
                                 .arg(fmtSeconds(t), w, withUnit(g4(v), pu));
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
        const Tag *co = tagById(m_settings.deltaCoTagId);
        if (co) {
            meaning = tr("how much the controller output %1 changed between the cursors.").arg(co->name);
            lines << kDelta + QStringLiteral("CO = CO(C2) - CO(C1)");
            if (r && r->deltaCo) {
                std::optional<double> a;
                std::optional<double> b;
                if (m_recorder) {
                    a = referenceLevel(*m_recorder, co->id, r->t1, m_settings.reference,
                                       m_settings.referenceWindowS);
                    b = referenceLevel(*m_recorder, co->id, r->t2, m_settings.reference,
                                       m_settings.referenceWindowS);
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
            lines << kDelta + QStringLiteral("CO = typed in step 1");
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
                         .arg(g4p(r->deltaPv), g4p(*r->deltaCo), withUnit(g4(*r->gain), kUnit()));
        }
        break;
    case Quantity::KPct:
        name = tr("K (%/%), process gain in percent of range");
        meaning = tr("K for signals expressed in percent of their ranges.");
        lines << QStringLiteral("K (%/%) = K ") + kTimes + QStringLiteral(" CO span / PV span");
        if (r && r->gain && pvSpan() && coSpan()) {
            lines << QStringLiteral("K (%/%) = %1 %2 %3 / %4 = %5 %/%")
                         .arg(g4p(*r->gain), kTimes, g4p(*coSpan()), g4p(*pvSpan()),
                              g4(*r->gain * *coSpan() / *pvSpan()));
        }
        break;
    case Quantity::L:
        return explainModelTime(true, r);
    case Quantity::T:
        return explainModelTime(false, r);
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
        lines << QStringLiteral("fit % = 100 ") + kTimes + QStringLiteral(" RMS(PV - model) / |")
                     + kDelta + QStringLiteral("PV|");
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

QString AnalysisPanel::explainModelTime(bool forL, const StepResult *r) const
{
    const auto ab = modelPercents(m_settings);
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
    const ModelMethod m = m_settings.method;
    const QString approx = m == ModelMethod::Sundaresan ? QStringLiteral("≈") : QStringLiteral("=");

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
                lines << QStringLiteral("L = %1 - %2 = %3 s").arg(B, g4p(T), g4(L));
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
                lines << QStringLiteral("T = 1.5 (%1 - %2) = %3 s").arg(B, g4p(ta), g4(T));
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
        }
    }
    if (ok) {
        lines << tr("Times are measured from the step time (t = %1 s).").arg(g4(r->stepTime));
    }
    return tip(name, meaning, lines);
}

QString AnalysisPanel::explainSetpoint(int column, const StepResult &r, const SetpointResult &sp,
                                       const QString &tagName) const
{
    const QString pu = pvUnit();
    const Tag *tag = tagById(r.tagId);
    const QString unit = tag ? tag->unit : pu;
    const QString p = g4(sp.percent / 100.0);
    QString name;
    QString meaning;
    QStringList lines;
    switch (column) {
    case 1:
        name = fmtPercent(sp.percent) + QStringLiteral(" ") + tagName;
        meaning = tr("the requested share of the step: 0 % is y0, 100 % is y100.");
        lines << QStringLiteral("target = y0 + p (y100 - y0)");
        break;
    case 2:
        name = tr("Target");
        meaning = tr("the PV level of this setpoint.");
        lines << QStringLiteral("target = y0 + p (y100 - y0)");
        lines << QStringLiteral("target = %1 + %2 %3 (%4 - %5) = %6")
                     .arg(g4p(r.y0), p, kTimes, g4p(r.y100), g4p(r.y0), withUnit(g4(sp.target), unit));
        break;
    case 3:
        name = tr("Measured");
        meaning = tr("the recorded value at the found time, interpolated between the two "
                     "neighbouring samples.");
        lines << QStringLiteral("measured = PV(t)");
        if (sp.found)
            lines << QStringLiteral("measured = PV(%1) = %2")
                         .arg(fmtSeconds(sp.time), withUnit(g4(sp.measured), unit));
        break;
    case 4:
        name = tr("Time");
        meaning = tr("time at which the PV reaches y0 + p (y100 - y0) = %1.")
                      .arg(withUnit(g4(sp.target), unit));
        lines << QStringLiteral("t = first time the PV crosses the target");
        if (sp.found)
            lines << QStringLiteral("t = %1").arg(fmtSeconds(sp.time));
        else
            lines << tr("Not found between the cursors.");
        break;
    default:
        name = QStringLiteral("t ") + kMinus + QStringLiteral(" C1");
        meaning = tr("time from the earlier cursor to the setpoint.");
        lines << QStringLiteral("t - C1 = time - %1").arg(fmtSeconds(r.t1));
        if (sp.found)
            lines << QStringLiteral("t - C1 = %1 - %2 = %3 s")
                         .arg(g4p(sp.time), g4p(r.t1), g4(sp.time - r.t1));
        break;
    }
    return tip(name, meaning, lines);
}

QString AnalysisPanel::explainTuning(int column, ControllerKind kind, const TuningRow &row,
                                     const StepResult &r) const
{
    // column: 0 = Kp, 1 = Ti, 2 = Td, 3 = Kp (%/%)
    const TuningRule rule = m_settings.tuningRule;
    const bool minutes = m_settings.timeInMinutes;
    const double K = r.gain.value_or(kNaN);
    const double L = r.deadTime.value_or(kNaN);
    const double T = r.timeConstant.value_or(kNaN);
    const double lam = effectiveLambda(L, m_settings.lambdaS);
    const QString ruleName = toString(rule);
    const QString kindName = toString(kind);
    QString name;
    QString meaning;
    QStringList lines;
    const QString pu = pvUnit();
    const QString cu = coUnit();
    const QString kpUnit = (!pu.isEmpty() && !cu.isEmpty()) ? cu + u'/' + pu : QString();

    if (column == 3) {
        name = QStringLiteral("Kp (%/%), ") + kindName;
        meaning = tr("controller gain for signals expressed in percent of their ranges.");
        lines << QStringLiteral("Kp (%/%) = Kp ") + kTimes + QStringLiteral(" PV span / CO span");
        if (row.kp && pvSpan() && coSpan()) {
            lines << QStringLiteral("Kp (%/%) = %1 %2 %3 / %4 = %5 %/%")
                         .arg(g4p(*row.kp), kTimes, g4p(*pvSpan()), g4p(*coSpan()),
                              g4(*row.kp * *pvSpan() / *coSpan()));
        }
        return tip(name, meaning, lines);
    }

    const QString format = tuningFormula(rule, kind, column);
    const QString symbol = column == 0 ? QStringLiteral("Kp") : column == 1 ? QStringLiteral("Ti")
                                                                              : QStringLiteral("Td");
    name = symbol + QStringLiteral(", ") + kindName + QStringLiteral(" (") + ruleName + u')';
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
            lines << (m_settings.lambdaS > 0.0
                          ? QStringLiteral("λ = %1 s").arg(g4(lam))
                          : tr("λ = L = %1 s (Auto)").arg(g4(lam)));
        }
    }
    return tip(name, meaning, lines);
}

} // namespace mb
