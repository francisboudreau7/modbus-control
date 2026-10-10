#pragma once

#include "PollWorker.h"
#include "ProjectIO.h"
#include "Recorder.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QThread;
class QTimer;

namespace mb {

class MainWindow;
class WriteController;

/// Application logic. Owns the PollWorker (on its own QThread), the Recorder and the
/// WriteController, and wires them to the MainWindow:
/// - ConnectionBar connect/disconnect -> PollWorker; state -> bar, status label, writes.
/// - samplesReady -> Recorder::append + TagTableModel::updateSamples; a 10 Hz timer calls
///   TrendPlot::refresh and CursorPanel::refresh (only when new rows arrived).
/// - timeBaseReset -> Recorder clear + setStartTime, TrendPlot::resetData.
/// - tag edits -> PollWorker::setTags, TrendPlot/CursorPanel::setTags, modified flag.
/// - cursorsChanged -> CursorPanel::setCursors.
/// - File actions: New/Open/Save/Save As with "unsaved changes" prompts; last directory
///   and the last connection settings (used for New) are kept in QSettings.
/// - Analysis tab: AnalysisView gets the Recorder, the tags, the 10 Hz refresh and the data
///   reset like the Trend; its settings are saved/loaded with the project (and mark it
///   modified); "Copy cursors from Trend" copies the Trend's cursors; the results export
///   (button and Export menu action, enabled while a valid result exists) writes
///   exportAnalysisCsv(); the image export uses the Analysis plot while its tab is active.
/// - Quick writes: QuickWritePanel -> WriteController::requestQuickWrite; the Options action
///   "Confirm quick writes" mirrors WriteController::quickConfirm().
/// - Sessions (.mbsession): Save/Open session. Opening disconnects, replaces the recording and
///   the project (no project path; the title shows the session file) and restores both tabs'
///   cursors. Connecting while a loaded recording exists asks "Continue this recording" (the
///   worker's time base is restarted, the recording is kept, a NaN gap row is appended and
///   incoming batch times are shifted so time continues from the loaded start time) or
///   "Start a new recording". New rows after loading make the unsaved-changes prompt ask
///   to save the session.
/// - Export actions open ExportImageDialog / ExportCsvDialog.
class AppController : public QObject
{
    Q_OBJECT
public:
    explicit AppController(MainWindow *window, QObject *parent = nullptr);
    /// Disconnects and stops the worker thread cleanly.
    ~AppController() override;

    /// Loads a project (used for a file given on the command line). Shows errors in a box.
    bool openProject(const QString &path);

private:
    void wireConnection();
    void wireData();
    void wireTags();
    void wireWrites();
    void wireActions();
    void pushTags();
    void setModified(bool modified);
    void updateTitle();
    bool maybeSave();
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
    void newProject();
    void openDialog();
    bool loadFrom(const QString &path);
    void applyProject(const Project &project);
    void setAddressBase(int base); ///< Updates the Options action and the model without marking modified.
    void disconnectForProjectChange();
    Project currentProject() const;
    void exportImage();
    void exportCsv();
    void exportAnalysisResults();

    enum class ContinueChoice { Continue, StartNew, Cancel };

    void wireQuickWrites();
    bool saveSessionDialog();
    void openSessionDialog();
    bool loadSessionFile(const QString &path);
    /// Asks what to do on Connect with a loaded recording; true = go on connecting.
    bool prepareConnect();
    void onTimeBaseReset(const QDateTime &wall);
    void clearSessionState();
    /// If a session's recording is loaded: clears it (default capacity), resets the plots
    /// and the worker's time base. Always clears the session state.
    void discardLoadedRecording();
    static constexpr qsizetype kDefaultRecorderRows = 72000; ///< 2 h at 100 ms
    bool recordingDirty() const;
    std::optional<double> lastRowTime() const;

    MainWindow *m_window = nullptr;
    QThread *m_thread = nullptr;
    PollWorker *m_worker = nullptr;
    WriteController *m_writes = nullptr;
    Recorder m_recorder;
    QTimer *m_refreshTimer = nullptr;
    QString m_projectPath;
    bool m_modified = false;
    bool m_newData = false;
    ConnectionState m_state = ConnectionState::Disconnected;

    QString m_sessionPath;           ///< file of the opened/saved session (title when no project path)
    bool m_sessionLoaded = false;    ///< a session was opened (or its recording is still the loaded one)
    bool m_sessionDirty = false;     ///< rows were appended after loading / saving
    bool m_dropSamples = false;      ///< a session load is waiting for the worker to disconnect
    bool m_continuePending = false;  ///< the next timeBaseReset continues the loaded recording
    double m_timeOffset = 0.0;       ///< added to incoming batch times while continuing
    std::function<ContinueChoice()> m_continuePrompt;
};

} // namespace mb
