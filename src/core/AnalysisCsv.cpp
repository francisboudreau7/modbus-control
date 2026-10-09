#include "AnalysisCsv.h"

#include <QIODevice>
#include <QLocale>
#include <QString>
#include <QStringList>

#include <cmath>
#include <optional>

namespace mb {

namespace {

bool writeText(QIODevice &out, const QString &text, QString *error)
{
    const QByteArray bytes = text.toUtf8();
    const qint64 written = out.write(bytes);
    if (written != bytes.size()) {
        if (error)
            *error = QStringLiteral("Write failed: %1").arg(out.errorString());
        return false;
    }
    return true;
}

QString csvField(const QString &text, QChar delimiter)
{
    const bool needsQuotes = text.contains(delimiter) || text.contains(u'"')
                             || text.contains(u'\r') || text.contains(u'\n');
    if (!needsQuotes)
        return text;

    QString quoted = text;
    quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + quoted + QStringLiteral("\"");
}

QString formatValue(double value)
{
    if (std::isnan(value))
        return {};
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString formatOptional(const std::optional<double> &value)
{
    return value ? formatValue(*value) : QString();
}

QString joinRow(const QStringList &fields, QChar delimiter)
{
    QStringList cells;
    cells.reserve(fields.size());
    for (const QString &field : fields)
        cells << csvField(field, delimiter);
    return cells.join(QString(delimiter)) + QStringLiteral("\r\n");
}

QString tagName(const QHash<TagId, QString> &tagHeaders, TagId id)
{
    if (tagHeaders.contains(id))
        return tagHeaders.value(id);
    return QStringLiteral("Tag %1").arg(id);
}

} // namespace

bool exportAnalysisCsv(QIODevice &out, const QList<StepResult> &results,
                       const QHash<TagId, QString> &tagHeaders, QChar delimiter,
                       QString *error)
{
    if (!out.isOpen() || !out.isWritable()) {
        if (error)
            *error = QStringLiteral("Device is not open for writing");
        return false;
    }

    QString text = joinRow({QStringLiteral("tag"), QStringLiteral("percent"),
                            QStringLiteral("target"), QStringLiteral("measured"),
                            QStringLiteral("time_s"), QStringLiteral("time_from_c1_s")},
                           delimiter);
    for (const StepResult &result : results) {
        if (!result.valid)
            continue;
        const QString tag = tagName(tagHeaders, result.tagId);
        for (const SetpointResult &setpoint : result.setpoints) {
            const QString target = std::isfinite(setpoint.target)
                                       ? formatValue(setpoint.target)
                                       : QString();
            const QString timeFromC1 = std::isnan(setpoint.time)
                                           ? QString()
                                           : formatValue(setpoint.time - result.t1);
            text += joinRow({tag, formatValue(setpoint.percent), target,
                             formatValue(setpoint.measured), formatValue(setpoint.time),
                             timeFromC1},
                            delimiter);
        }
    }

    text += QStringLiteral("\r\n");
    text += joinRow({QStringLiteral("tag"), QStringLiteral("c1_s"), QStringLiteral("c2_s"),
                     QStringLiteral("y0"), QStringLiteral("y100"), QStringLiteral("delta_pv"),
                     QStringLiteral("delta_co"), QStringLiteral("step_time_s"),
                     QStringLiteral("gain_k"), QStringLiteral("dead_time_l_s"),
                     QStringLiteral("time_constant_t_s"), QStringLiteral("fit_rms"),
                     QStringLiteral("fit_rms_percent"), QStringLiteral("note")},
                    delimiter);
    for (const StepResult &result : results) {
        const QString tag = tagName(tagHeaders, result.tagId);
        const QString t1 = formatValue(result.t1);
        const QString t2 = formatValue(result.t2);
        if (!result.valid) {
            text += joinRow({tag, t1, t2, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, result.error},
                            delimiter);
            continue;
        }
        text += joinRow({tag, t1, t2, formatValue(result.y0), formatValue(result.y100),
                         formatValue(result.deltaPv), formatOptional(result.deltaCo),
                         formatValue(result.stepTime), formatOptional(result.gain),
                         formatOptional(result.deadTime), formatOptional(result.timeConstant),
                         formatOptional(result.fitRms), formatOptional(result.fitRmsPercent),
                         QString()},
                        delimiter);
    }

    return writeText(out, text, error);
}

} // namespace mb
