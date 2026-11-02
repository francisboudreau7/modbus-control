#include "WriteLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace mb {

namespace {

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

QString formatTime(const QDateTime &time)
{
    if (!time.isValid())
        return {};
    // Local time carrying its UTC offset, e.g. 2026-10-31T14:03:12.250-04:00 (same as CsvExporter).
    const QDateTime local = time.toLocalTime();
    return local.toOffsetFromUtc(local.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

} // namespace

QStringList writeLogHeader()
{
    return {
        QStringLiteral("time"),
        QStringLiteral("tag"),
        QStringLiteral("tag_id"),
        QStringLiteral("registers"),
        QStringLiteral("function_code"),
        QStringLiteral("old_value"),
        QStringLiteral("new_value"),
        QStringLiteral("raw"),
        QStringLiteral("source"),
        QStringLiteral("result"),
        QStringLiteral("server"),
    };
}

QString writeLogCsvLine(const WriteLogEntry &entry, QChar delimiter)
{
    const QStringList fields = {
        formatTime(entry.time),
        csvField(entry.tagName, delimiter),
        QString::number(entry.tagId),
        csvField(entry.registers, delimiter),
        QString::number(entry.functionCode),
        csvField(entry.oldValue, delimiter),
        csvField(entry.newValue, delimiter),
        csvField(entry.raw, delimiter),
        csvField(entry.source, delimiter),
        csvField(entry.result, delimiter),
        csvField(entry.server, delimiter),
    };
    return fields.join(QString(delimiter));
}

QString writeLogFileName(const QDate &date)
{
    return QStringLiteral("writes-%1.csv").arg(date.toString(Qt::ISODate));
}

WriteLogFile::WriteLogFile(const QString &folder)
    : m_folder(folder)
{
}

QString WriteLogFile::folder() const
{
    return m_folder;
}

void WriteLogFile::setFolder(const QString &folder)
{
    m_folder = folder;
}

QString WriteLogFile::pathFor(const QDate &date) const
{
    return QDir(m_folder).filePath(writeLogFileName(date));
}

bool WriteLogFile::append(const WriteLogEntry &entry, QString *error)
{
    const QString path = pathFor(entry.time.date());

    if (!QDir().mkpath(m_folder)) {
        if (error)
            *error = QCoreApplication::translate("mb::WriteLog", "Cannot create folder %1").arg(m_folder);
        return false;
    }

    // A new or empty file gets the BOM (so Excel reads UTF-8) and the header line first.
    const bool needsHeader = !QFileInfo(path).exists() || QFileInfo(path).size() == 0;

    QByteArray data;
    if (needsHeader) {
        data += QByteArrayLiteral("\xEF\xBB\xBF");
        data += writeLogHeader().join(QChar(u',')).toUtf8();
        data += QByteArrayLiteral("\r\n");
    }
    data += writeLogCsvLine(entry).toUtf8();
    data += QByteArrayLiteral("\r\n");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error)
            *error = QCoreApplication::translate("mb::WriteLog", "Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size()) {
        if (error)
            *error = QCoreApplication::translate("mb::WriteLog", "Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (!file.flush()) {
        if (error)
            *error = QCoreApplication::translate("mb::WriteLog", "Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.close();
    return true;
}

QString WriteLogFile::defaultFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Modbus Control/Write logs"));
}

} // namespace mb
