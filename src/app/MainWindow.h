#pragma once

#include "PollWorker.h"

#include <QMainWindow>

#include <functional>

class QAction;
class QLabel;

namespace mb {

class AnalysisView;
class ConnectionBar;
class QuickWritePanel;
class CursorPanel;
class TagTableModel;
class TagTableWidget;
class TrendPlot;

/// Main window layout (no application logic; AppController wires the actions):
/// - Toolbar "Connection": ConnectionBar.
/// - Central widget: QTabWidget with the tabs "Trend" (TrendPlot) and "Analysis" (AnalysisView).
///   The active tab is saved/restored with QSettings ("mainWindow/tab").
/// - Bottom dock "Tags": TagTableWidget (over tagModel()).
/// - Right dock "Cursors": CursorPanel.
/// - Right dock "Quick write": QuickWritePanel over tagModel() (tabified with Cursors).
/// - Menus: File (New, Open..., Save, Save As..., Save session..., Open session..., Quit), Export (Graph image...,
///   Data (CSV)..., Analysis results (CSV)...), View (dock toggles), Options (Use 1-based register numbers, Confirm quick writes), Help (About).
/// - Status bar: transient messages on the left, permanent connection label on the right.
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
        QAction *about = nullptr;
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    ConnectionBar *connectionBar() const;
    TagTableModel *tagModel() const;
    TagTableWidget *tagTable() const;
    TrendPlot *trendPlot() const;
    CursorPanel *cursorPanel() const;
    AnalysisView *analysisView() const;
    QuickWritePanel *quickWritePanel() const;
    /// True while the Analysis tab is the current tab (image export uses its plot).
    bool isAnalysisTabActive() const;
    /// Makes the Trend (false) or Analysis (true) tab current.
    void setAnalysisTabActive(bool active);
    const Actions &actions() const;

    /// Permanent status bar label, e.g. "Connected to 127.0.0.1:1502 (unit 1)".
    void setConnectionStatus(mb::ConnectionState state, const QString &detail);
    void showStatusMessage(const QString &text, int timeoutMs = 5000);
    /// Window title: "<name>[*] — Modbus Control" (uses QWidget::setWindowModified).
    void setDocumentTitle(const QString &name, bool modified);
    /// Called from closeEvent; returning false cancels the close (unsaved changes).
    void setCloseHandler(std::function<bool()> handler);

signals:
    /// The current tab changed (Trend <-> Analysis).
    void analysisTabActiveChanged(bool active);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Implementation-defined; the task may change the private section freely.
    ConnectionBar *m_connectionBar = nullptr;
    TagTableModel *m_tagModel = nullptr;
    TagTableWidget *m_tagTable = nullptr;
    TrendPlot *m_trendPlot = nullptr;
    CursorPanel *m_cursorPanel = nullptr;
    AnalysisView *m_analysisView = nullptr;
    QuickWritePanel *m_quickWritePanel = nullptr;
    QLabel *m_connectionLabel = nullptr;
    Actions m_actions;
    std::function<bool()> m_closeHandler;
};

} // namespace mb
