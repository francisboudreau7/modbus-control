#include "AnalysisCommon.h"

#include "ClipboardCopy.h"
#include "NumberSpinBox.h"
#include "PlotCursors.h"
#include "RichText.h"
#include "TrendPlot.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLocale>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <cmath>

namespace mb::analysis {

// ---------------------------------------------------------------------------------------------
// Context

const Tag *Context::tagById(TagId id) const
{
    if (id == 0)
        return nullptr;
    for (const Tag &t : tags) {
        if (t.id == id)
            return t.isBit() ? nullptr : &t; // bit tags are never a PV, CO or setpoint tag
    }
    return nullptr;
}

bool Context::shownTag(const Tag *tag) const
{
    return tag && settings.setpointTagIds.contains(tag->id) && tag->plotted && !tag->isBit();
}

QString Context::pvUnit() const
{
    const Tag *t = tagById(settings.pvTagId);
    return t ? t->unit : QString();
}

QString Context::coUnit() const
{
    const Tag *t = tagById(settings.deltaCoTagId);
    return t ? t->unit : QString();
}

QString Context::kUnit() const
{
    const QString p = pvUnit();
    const QString c = coUnit();
    return (!p.isEmpty() && !c.isEmpty()) ? p + u'/' + c : QString();
}

std::optional<double> Context::pvSpan() const
{
    if (settings.pvRangeMin && settings.pvRangeMax) {
        const double s = *settings.pvRangeMax - *settings.pvRangeMin;
        if (s != 0.0 && std::isfinite(s))
            return s;
    }
    return std::nullopt;
}

std::optional<double> Context::coSpan() const
{
    const double s = settings.coRangeMax - settings.coRangeMin;
    if (s != 0.0 && std::isfinite(s))
        return s;
    return std::nullopt;
}

bool Context::percentGainsShown() const
{
    return pvSpan() && coSpan();
}

const PlotCursors *Context::cursors() const
{
    return plot ? plot->cursors() : nullptr;
}

bool Context::hasBothCursors() const
{
    const PlotCursors *cur = cursors();
    return cur && cur->hasC1() && cur->hasC2();
}

// ---------------------------------------------------------------------------------------------
// Formatting

QString g4(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 4) : kDash;
}

QString g4(std::optional<double> v)
{
    return v ? g4(*v) : kDash;
}

QString g4p(double v)
{
    return v < 0.0 ? QStringLiteral("(") + g4(v) + QStringLiteral(")") : g4(v);
}

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
        return QCoreApplication::translate("mb::AnalysisPanel", "Good");
    case FitQuality::Acceptable:
        return QCoreApplication::translate("mb::AnalysisPanel", "Acceptable");
    case FitQuality::Poor:
        return QCoreApplication::translate("mb::AnalysisPanel", "Poor");
    }
    return {};
}

QString methodName(ModelMethod method)
{
    switch (method) {
    case ModelMethod::ZeroTo632:
        return QCoreApplication::translate("mb::AnalysisPanel", "0 % / 63.2 % (two points)");
    case ModelMethod::Smith:
        return QCoreApplication::translate("mb::AnalysisPanel", "Smith (28.3 % / 63.2 %)");
    case ModelMethod::Sundaresan:
        return QCoreApplication::translate("mb::AnalysisPanel",
                                           "Sundaresan-Krishnaswamy (35.3 % / 85.3 %)");
    case ModelMethod::Custom:
        return QCoreApplication::translate("mb::AnalysisPanel", "Custom");
    case ModelMethod::Tangent:
        return QCoreApplication::translate("mb::AnalysisPanel", "Tangent (steepest slope)");
    }
    return toString(method);
}

QString ruleName(TuningRule rule)
{
    switch (rule) {
    case TuningRule::ZieglerNichols:
        return QCoreApplication::translate("mb::AnalysisPanel", "Ziegler-Nichols (open loop)");
    case TuningRule::CohenCoon:
    case TuningRule::ImcLambda:
    case TuningRule::Simc:
        break;
    }
    return toString(rule);
}

QString tip(const QString &name, const QString &meaning, const QStringList &lines)
{
    QString s = rich::bold(name) + QStringLiteral(": ") + rich::escaped(meaning);
    for (const QString &line : lines) {
        if (!line.isEmpty())
            s += QStringLiteral("<br>") + rich::escaped(line);
    }
    return s;
}

QString substitute(QString format, const QStringList &values)
{
    for (int i = int(values.size()); i >= 1; --i)
        format.replace(QStringLiteral("%") + QString::number(i), values.at(i - 1));
    return format;
}

// ---------------------------------------------------------------------------------------------
// Widgets

QTableWidgetItem *makeItem(const QString &text, bool alignRight, const QString &toolTip)
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
    auto *spin = new NumberSpinBox(parent);
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

} // namespace mb::analysis
