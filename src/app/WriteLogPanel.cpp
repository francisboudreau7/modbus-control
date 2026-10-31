#include "WriteLogPanel.h"

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mb {

// T230 stub: T233 implements the contract in WriteLogPanel.h.

WriteLogPanel::WriteLogPanel(QWidget *parent)
    : QWidget(parent)
    , m_table(new QTableWidget(this))
    , m_folderLabel(new QLabel(this))
    , m_openFolder(new QPushButton(tr("Open folder"), this))
    , m_clear(new QPushButton(tr("Clear list"), this))
{
    m_table->setObjectName(QStringLiteral("writeLogTable"));
    m_folderLabel->setObjectName(QStringLiteral("folderLabel"));
    m_openFolder->setObjectName(QStringLiteral("openFolderButton"));
    m_clear->setObjectName(QStringLiteral("clearButton"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(m_folderLabel);
    layout->addWidget(m_openFolder);
    layout->addWidget(m_clear);
}

void WriteLogPanel::addEntry(const WriteLogEntry &entry)
{
    m_entries.append(entry);
}

QList<WriteLogEntry> WriteLogPanel::entries() const
{
    return m_entries;
}

void WriteLogPanel::clear()
{
    m_entries.clear();
}

void WriteLogPanel::setFolder(const QString &folder)
{
    m_folder = folder;
}

QString WriteLogPanel::folder() const
{
    return m_folder;
}

void WriteLogPanel::setFileError(const QString &text)
{
    Q_UNUSED(text);
}

void WriteLogPanel::setOpenFolder(std::function<void(const QString &folder)> open)
{
    m_open = std::move(open);
}

} // namespace mb
