#pragma once

#include "PollWorker.h"

#include <QMainWindow>

#include <functional>

class QAction;
class QDockWidget;
class QLabel;

namespace mb {

class AnalysisPanel;
class ConnectionBar;
class QuickWritePanel;
class CursorPanel;
class TagTableModel;
class TagTableWidget;
class TrendPlot;
class WriteLogPanel;

/// Main window layout (no application logic; AppController wires the actions):
/// - Toolbar "Connection": ConnectionBar.
/// - Central widget: the TrendPlot (no tabs since v1.2.6; the old QSettings key "mainWindow/tab"
///   is ignored).
/// - Bottom dock "Tags": TagTableWidget (over tagModel()).
/// - Right dock "Cursors": CursorPanel.
/// - Right dock "Quick write": QuickWritePanel over tagModel() (tabified with Cursors).
/// - Right dock "Analysis" (objectName "analysisDock"): AnalysisPanel (it scrolls by itself), tabified
///   with Cursors and Quick write; its toggle is in the View menu. setPlot(trendPlot()) is called
///   by the window.
/// - Right dock "Setpoints" (objectName "setpointsDock"): AnalysisPanel::setpointsPage(), tabified
///   after Analysis; View menu toggle. The destructor deletes the Analysis dock first.
/// - Bottom dock "Write log" (objectName "writeLogDock", v1.3.3): WriteLogPanel, tabified with
///   Tags (Tags stays the raised tab); View menu toggle.
/// - Menus: File (New, Open..., Save, Save As..., Save session..., Open session..., Quit), Export (Graph image...,
///   Data (CSV)..., Analysis results (CSV)...), View (dock toggles), Options (Use 1-based register numbers, Confirm quick writes, separator, Lock tag configuration), Help (Quick Start, User Guide, About).
/// - Status bar: transient messages on the left, permanent labels on the right: "lockLabel"
///   (bold "Tags locked", tooltip "The tag configuration is locked (Options > Lock tag
///   configuration)"; hidden unless setTagsLocked(true)) and the connection label.
/// Window geometry and dock state are saved/restored with QSettings.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    struct Actions {
        QAction *newProject = nullptr;
        QAction *openProject = nullptr;
        QAction *saveProject = nullptr;
        QAction *saveProjectAs = nullptr;
        /// "Save &session...": project + recording + cursors (.mbsession).
        QAction *saveSession = nullptr;
        /// "Open s&ession...".
        QAction *openSession = nullptr;
        QAction *quit = nullptr;
        QAction *exportImage = nullptr;
        QAction *exportCsv = nullptr;
        /// "Analysis &results (CSV)..."; enabled by AppController when results exist.
        QAction *exportAnalysisCsv = nullptr;
        /// Options menu, checkable: "Use 1-based register numbers".
        QAction *oneBasedAddresses = nullptr;
        /// Options menu, checkable, checked at start: "Confirm &quick writes".
        QAction *confirmQuickWrites = nullptr;
        /// v1.3.3 Options menu, checkable, unchecked: "&Lock tag configuration", tooltip "Prevent
        /// changes to tags, registers and write limits (saved in the project)". AppController
        /// handles it (unlock prompt) and keeps its check state in sync.
        QAction *lockTags = nullptr;
        /// Help menu (before About): "&Quick Start" and "&User Guide"; AppController opens the
        /// documentation (see DocsLinks.h).
        QAction *quickStart = nullptr;
        QAction *userGuide = nullptr;
        QAction *about = nullptr;
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    ConnectionBar *connectionBar() const;
    TagTableModel *tagModel() const;
    TagTableWidget *tagTable() const;
    TrendPlot *trendPlot() const;
    CursorPanel *cursorPanel() const;
    /// The Analysis dock's panel (v1.2.6).
    AnalysisPanel *analysisPanel() const;
    QuickWritePanel *quickWritePanel() const;
    /// The Write log dock's panel (v1.3.3).
    WriteLogPanel *writeLogPanel() const;
    const Actions &actions() const;

    /// Permanent status bar label, e.g. "Connected to 127.0.0.1:1502 (unit 1)".
    void setConnectionStatus(mb::ConnectionState state, const QString &detail);
    void showStatusMessage(const QString &text, int timeoutMs = 5000);
    /// Shows or hides the permanent "Tags locked" label (v1.3.3).
    void setTagsLocked(bool locked);
    /// Window title: "<name>[*] — Modbus Control" (uses QWidget::setWindowModified).
    void setDocumentTitle(const QString &name, bool modified);
    /// Called from closeEvent; returning false cancels the close (unsaved changes).
    void setCloseHandler(std::function<bool()> handler);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Implementation-defined; the task may change the private section freely.
    ConnectionBar *m_connectionBar = nullptr;
    TagTableModel *m_tagModel = nullptr;
    TagTableWidget *m_tagTable = nullptr;
    TrendPlot *m_trendPlot = nullptr;
    CursorPanel *m_cursorPanel = nullptr;
    AnalysisPanel *m_analysisPanel = nullptr;
    QDockWidget *m_analysisDock = nullptr;
    QuickWritePanel *m_quickWritePanel = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_lockLabel = nullptr;
    WriteLogPanel *m_writeLogPanel = nullptr;
    Actions m_actions;
    std::function<bool()> m_closeHandler;
};

} // namespace mb
