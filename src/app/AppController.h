#pragma once

#include "PollWorker.h"
#include "ProjectIO.h"
#include "Recorder.h"

#include <QMessageBox>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QDialog;
class QThread;
class QTimer;

class TstAppController; // test access to the dialog seams

namespace mb {

class EncodingMonitor;
class MainWindow;
class WriteController;

/// Application logic. Owns the PollWorker (on its own QThread), the Recorder, the
/// WriteController and the EncodingMonitor (32-bit encoding detection), and wires them to the MainWindow:
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
/// - Add tag: the tag table's Add opens AddTagDialog (through m_prompts.exec) with the project's
///   tags, the address base and EncodingMonitor::deviceOrder(); its read function issues
///   PollWorker::readRegisters with request ids from kAddTagRequestBase up (the EncodingMonitor's
///   ids stay below it) and routes PollWorker::readFinished answers with those ids back to the
///   dialog's callbacks; setConnected follows the connection state while it is open. Accepted:
///   TagTableModel::addTag(dialog.tag()), then EncodingMonitor::confirmDetected(id) when
///   dialog.detectionConfirmed(), then TagTableWidget::showRow(row).
/// - Tag-table columns: the project's hiddenColumns are applied on load (compact view for a new
///   project); changing them from the header menu marks the project modified.
/// - Export actions open ExportImageDialog / ExportCsvDialog.
/// Every dialog goes through private seams (m_prompts, m_continuePrompt) whose defaults show
/// the real dialogs; tests (TstAppController) replace them. QSettings keys are written under
/// the application's organization/application names (tests use their own).
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
    friend class ::TstAppController;

    /// Dialog seams; the constructor installs the real dialogs.
    struct Prompts {
        std::function<QMessageBox::StandardButton(const QString &title, const QString &text,
                                                  QMessageBox::StandardButtons buttons,
                                                  QMessageBox::StandardButton defaultButton)>
            question;
        std::function<void(const QString &title, const QString &text)> warning;
        /// Returns the chosen path or an empty string (cancelled).
        std::function<QString(const QString &caption, const QString &path, const QString &filter)>
            saveFileName;
        std::function<QString(const QString &caption, const QString &dir, const QString &filter)>
            openFileName;
        /// Runs a modal dialog (the export dialogs).
        std::function<void(QDialog &dialog)> exec;
    };
    Prompts m_prompts;
    QMessageBox::StandardButton ask(const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons = QMessageBox::Yes
                                                                           | QMessageBox::No,
                                    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
    void warn(const QString &title, const QString &text);
    QString saveFileName(const QString &caption, const QString &path, const QString &filter);
    QString openFileName(const QString &caption, const QString &dir, const QString &filter);
    void runDialog(QDialog &dialog);

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
    /// First request id used for the Add tag dialog's reads.
    static constexpr quint64 kAddTagRequestBase = quint64(1) << 62;
    /// Opens the Add tag dialog (see the class comment).
    void addTagDialog();
    bool recordingDirty() const;
    std::optional<double> lastRowTime() const;

    MainWindow *m_window = nullptr;
    QThread *m_thread = nullptr;
    PollWorker *m_worker = nullptr;
    WriteController *m_writes = nullptr;
    EncodingMonitor *m_encoding = nullptr;
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
    quint64 m_tagsGeneration = 0;    ///< bumped by every pushTags()
    quint64 m_tagsApplied = 0;       ///< last generation the worker has applied
    std::function<ContinueChoice()> m_continuePrompt;
};

} // namespace mb
