#include "WriteLogPanel.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace mb {

namespace {

const QColor kFailureColor(0xc6, 0x28, 0x28);

QString functionCodeText(int functionCode)
{
    return QStringLiteral("FC%1").arg(functionCode, 2, 10, QLatin1Char('0'));
}

/// The log entries (and the CSV file) keep English tokens; the panel translates them for display.
QString displayText(const QString &text)
{
    if (text == QLatin1String("unknown"))
        return QCoreApplication::translate("mb::WriteLogPanel", "unknown");
    if (text == QLatin1String("quick"))
        return QCoreApplication::translate("mb::WriteLogPanel", "quick", "write source");
    if (text == QLatin1String("table"))
        return QCoreApplication::translate("mb::WriteLogPanel", "table", "write source");
    if (text == QLatin1String("selection"))
        return QCoreApplication::translate("mb::WriteLogPanel", "selection", "write source");
    if (text == QLatin1String("No reply (disconnected)"))
        return QCoreApplication::translate("mb::WriteLogPanel", "No reply (disconnected)");
    return text;
}

QString rowToolTip(const WriteLogEntry &e)
{
    //: %1 = date and time, %2 = where the write came from (table, quick, selection), %3 = server address
    return QCoreApplication::translate("mb::WriteLogPanel", "%1\nSource: %2\nServer: %3")
        .arg(e.time.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")), displayText(e.source), e.server);
}

} // namespace

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

    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("Tag"), tr("Register"), tr("FC"),
                                        tr("Old → New"), tr("Raw"), tr("Result")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSortingEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    // The label may shrink below its text; the text is elided in the middle to fit.
    m_folderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_folderLabel->installEventFilter(this);

    m_open = [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        QDir().mkpath(m_folder); // before the first write the folder does not exist yet
        m_open(m_folder);
    });
    connect(m_clear, &QPushButton::clicked, this, &WriteLogPanel::clear);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_folderLabel, 1);
    buttons->addWidget(m_openFolder);
    buttons->addWidget(m_clear);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
    updateFolderLabel();
}

void WriteLogPanel::addEntry(const WriteLogEntry &entry)
{
    // Follow the newest row only when the user was already at the bottom (or nothing to scroll).
    QScrollBar *bar = m_table->verticalScrollBar();
    const bool atBottom = m_table->rowCount() == 0 || bar->value() >= bar->maximum();

    m_entries.append(entry);
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const QString tip = rowToolTip(entry);
    const QStringList texts = {entry.time.toString(QStringLiteral("hh:mm:ss.zzz")),
                               entry.tagName,
                               entry.registers,
                               functionCodeText(entry.functionCode),
                               QStringLiteral("%1 → %2").arg(displayText(entry.oldValue), displayText(entry.newValue)),
                               entry.raw,
                               displayText(entry.result)};
    for (int col = 0; col < texts.size(); ++col) {
        auto *item = new QTableWidgetItem(texts.at(col));
        item->setToolTip(tip);
        if (col == texts.size() - 1 && !entry.ok)
            item->setForeground(kFailureColor);
        m_table->setItem(row, col, item);
    }
    if (atBottom)
        m_table->scrollToBottom();
}

QList<WriteLogEntry> WriteLogPanel::entries() const
{
    return m_entries;
}

void WriteLogPanel::clear()
{
    m_entries.clear();
    m_table->setRowCount(0);
}

void WriteLogPanel::setFolder(const QString &folder)
{
    m_folder = folder;
    updateFolderLabel();
}

QString WriteLogPanel::folder() const
{
    return m_folder;
}

void WriteLogPanel::setFileError(const QString &text)
{
    m_fileError = text;
    updateFolderLabel();
}

void WriteLogPanel::setOpenFolder(std::function<void(const QString &folder)> open)
{
    if (open)
        m_open = std::move(open);
    else
        m_open = [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };
}

bool WriteLogPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_folderLabel && event->type() == QEvent::Resize)
        updateFolderLabel();
    return QWidget::eventFilter(watched, event);
}

void WriteLogPanel::updateFolderLabel()
{
    const bool error = !m_fileError.isEmpty();
    const QString full = error ? m_fileError : tr("Saved to %1").arg(m_folder);
    // Before the first layout the width is meaningless: show everything.
    QString shown = full;
    if (m_folderLabel->isVisible() && m_folderLabel->width() > 0)
        shown = QFontMetrics(m_folderLabel->font())
                    .elidedText(full, Qt::ElideMiddle, m_folderLabel->width());
    if (m_folderLabel->text() != shown)
        m_folderLabel->setText(shown);
    m_folderLabel->setToolTip(full);
    m_folderLabel->setStyleSheet(error ? QStringLiteral("color: #c62828;") : QString());
}

} // namespace mb
