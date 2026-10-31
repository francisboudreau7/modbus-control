#pragma once

#include "WriteLog.h"

#include <QList>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;
class TstWriteLogPanel;

namespace mb {

/// Content of the "Write log" dock (v1.3.3, PLAN.md "v1.3.3 Classroom safety"): the writes sent
/// in this session.
/// - "writeLogTable" (QTableWidget, read-only, row selection, no sorting): one row per entry,
///   newest at the bottom; columns "Time" (hh:mm:ss.zzz), "Tag", "Register", "FC" ("FC06" /
///   "FC16"), "Old → New" ("50 % → -25 %"), "Raw", "Result". The Result cell of a failed entry
///   (ok false) is red (ForegroundRole #c62828); the row's ToolTipRole holds the date, the source
///   and the server. After addEntry the table scrolls to the new row only when it was scrolled
///   to the bottom (or empty) before, so reading older rows is not disturbed.
/// - "folderLabel": "Saved to <folder>" (elided in the middle, full path as tooltip), or the last
///   file error in red when setFileError() was called with a non-empty text.
/// - "openFolderButton" "Open folder": creates folder() when missing, then calls the open-folder
///   function with it (default:
///   QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); tests replace it).
/// - "clearButton" "Clear list": removes every row (the files are not touched).
class WriteLogPanel : public QWidget
{
    Q_OBJECT
public:
    explicit WriteLogPanel(QWidget *parent = nullptr);

    void addEntry(const WriteLogEntry &entry);
    QList<WriteLogEntry> entries() const;
    void clear();

    void setFolder(const QString &folder);
    QString folder() const;
    /// Empty = no error (the folder text is shown again).
    void setFileError(const QString &text);
    void setOpenFolder(std::function<void(const QString &folder)> open);

private:
    friend class ::TstWriteLogPanel;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void updateFolderLabel();
    // Implementation-defined; T233 may change the private section freely.
    QTableWidget *m_table = nullptr;
    QLabel *m_folderLabel = nullptr;
    QPushButton *m_openFolder = nullptr;
    QPushButton *m_clear = nullptr;
    QList<WriteLogEntry> m_entries;
    QString m_folder;
    QString m_fileError;
    std::function<void(const QString &)> m_open;
};

} // namespace mb
