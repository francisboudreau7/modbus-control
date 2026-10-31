#include "WriteLog.h"

#include <QDir>
#include <QStandardPaths>

namespace mb {

// T230 stubs: T232 implements the contract in WriteLog.h.

QStringList writeLogHeader()
{
    return {};
}

QString writeLogCsvLine(const WriteLogEntry &entry, QChar delimiter)
{
    Q_UNUSED(entry);
    Q_UNUSED(delimiter);
    return {};
}

QString writeLogFileName(const QDate &date)
{
    Q_UNUSED(date);
    return {};
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
    Q_UNUSED(entry);
    if (error)
        *error = QStringLiteral("not implemented");
    return false;
}

QString WriteLogFile::defaultFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Modbus Control/Write logs"));
}

} // namespace mb
