#include "CsvExporter.h"

#include <QIODevice>
#include <QLocale>
#include <QString>

#include <cmath>

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

QString formatTimestamp(const QDateTime &start, double t)
{
    if (!start.isValid())
        return {};
    // Local time carrying its UTC offset, e.g. 2026-10-09T14:03:12.250-04:00 (a plain local
    // QDateTime would be written without an offset).
    const QDateTime local = start.addMSecs(qRound64(t * 1000.0)).toLocalTime();
    return local.toOffsetFromUtc(local.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

QString formatValue(double value)
{
    if (std::isnan(value))
        return {};
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

} // namespace

bool exportCsv(QIODevice &out, const Recorder &recorder, const QList<CsvColumn> &columns,
               const CsvOptions &options, QString *error)
{
    if (!out.isOpen() || !out.isWritable()) {
        if (error)
            *error = QStringLiteral("Device is not open for writing");
        return false;
    }

    const QString delimiter(options.delimiter);

    QString header = QStringLiteral("timestamp") + delimiter + QStringLiteral("elapsed_s");
    for (const CsvColumn &column : columns)
        header += delimiter + csvField(column.header, options.delimiter);
    header += QStringLiteral("\r\n");

    if (!writeText(out, header, error))
        return false;

    qsizetype first = 0;
    qsizetype last = recorder.size();
    if (options.useRange) {
        const auto range = recorder.rowRange(options.t1, options.t2);
        first = range.first;
        last = range.second;
    }

    const QDateTime start = recorder.startTime();
    for (qsizetype row = first; row < last; ++row) {
        const double t = recorder.timeAt(row);

        QString line = formatTimestamp(start, t) + delimiter + QString::number(t, 'f', 3);
        for (const CsvColumn &column : columns)
            line += delimiter + formatValue(recorder.valueAt(row, column.tagId));
        line += QStringLiteral("\r\n");

        if (!writeText(out, line, error))
            return false;
    }

    return true;
}

} // namespace mb
