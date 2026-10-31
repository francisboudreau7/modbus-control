#pragma once

#include "PollWorker.h"
#include "ProjectIO.h"
#include "Recorder.h"
#include "TagTableModel.h"
#include "WriteLog.h"

#include <QHash>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QUrl>

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
/// - Start (v1.3.3, T245): an empty Untitled project (no tags, unmodified, default analysis
///   settings, compact columns) with the last connection settings; the demo tags are no longer
///   loaded. File > Open example... (after the unsaved-changes prompt) opens m_prompts.openFileName
///   ("Open example", examplesDir(), "Modbus Control projects (*.mbproj)") and loads the chosen
///   file like Open (the project path is that file). examplesDir(): the first existing of
///   <appDir>/../examples (installed) and <appDir>/../../../examples (build tree
///   build/<preset>/bin); else the last project directory.
/// - Cursors (v1.3.3, T242): PlotCursors::cursorsChanged -> CursorPanel::setCursors(all cursors)
///   and the Analysis panel (C1/C2 only, as before); CursorPanel::removeCursorRequested ->
///   PlotCursors::removeCursor. Sessions save and restore C3..C6 (SessionCursors::trendExtra).
///   The image export passes CursorPanel::readoutText() and AnalysisPanel::readoutText()
///   separately (ExportImageDialog's two-readout constructor).
/// - Numbering (v1.3.3, T244): EncodingMonitor::addressBaseChangeRequested(base) ->
///   setAddressBase(base) and the project is marked modified.
/// - v1.2.6 (T195): the Analysis tab is replaced by the Analysis dock (AnalysisPanel): it gets the
///   Recorder, the main TrendPlot (setPlot), the tags, the 10 Hz refresh and the data reset; its
///   settings are saved/loaded with the project (and mark it modified); "Copy cursors from Trend"
///   is gone (one set of cursors); the results export (button and Export menu action, enabled
///   while results().first() is valid) writes exportAnalysisCsv() of results(); the image export
///   uses the Trend plot with CursorPanel::readoutText() followed by AnalysisPanel::readoutText()
///   (when not empty). Sessions: the Trend cursors are saved (analysisC1/C2 written equal to them);
///   on load the Trend cursors are used, or the Analysis cursors when the session has no Trend
///   cursors. The demo project chooses Lag as the PV and Setpoint as the CO.
/// - Quick writes: QuickWritePanel -> WriteController::requestQuickWrite; the Options action
///   "Confirm quick writes" mirrors WriteController::quickConfirm().
/// - Sessions (.mbsession): Save/Open session. Opening disconnects, replaces the recording and
///   the project (no project path; the title shows the session file) and restores the
///   cursors (see above). Connecting while a loaded recording exists asks "Continue this recording" (the
///   worker's time base is restarted, the recording is kept, a NaN gap row is appended and
///   incoming batch times are shifted so time continues from the loaded start time) or
///   "Start a new recording". New rows after loading make the unsaved-changes prompt ask
///   to save the session.
/// - Add tag: the tag table's Add opens AddTagDialog (through m_prompts.exec) with the project's
///   tags, the address base and EncodingMonitor::deviceOrder(); its read function issues
///   PollWorker::readRegisters with request ids from kAddTagRequestBase up (the EncodingMonitor's
///   ids stay below it) and routes PollWorker::readFinished answers with those ids back to the
///   dialog's callbacks; setConnected follows the connection state while it is open. Accepted:
///   the tag is checked again against the model's current tags (they can change while the dialog
///   is open, e.g. from an encoding prompt); if it now overlaps one, a warning ("Tag not added",
///   "'<name or HR 110>' would overlap '<other>' (HR 110-111), changed while the dialog was open.")
///   and nothing is added. Otherwise TagTableModel::addTag(dialog.tag()), then EncodingMonitor::confirmDetected(id) when
///   dialog.detectionConfirmed(), then TagTableWidget::showRow(row).
/// - Help > Quick Start / User Guide: opens docs::siteUrl(page, docs::languageFor(QLocale::
///   system())) through m_prompts.openUrl; when that returns false, opens the installed copy
///   (QUrl::fromLocalFile(docs::localPath(QCoreApplication::applicationDirPath(), ...))) if the
///   file exists; otherwise warns "Open documentation": "Could not open <url>." (T202)
/// - Trend Y-axis ranges (v1.3.1, T221): Project::yLeft/yRight are applied with
///   TrendPlot::setAxisRange on load (Auto for a new project) and saved from axisRange(); the
///   plot's axisRangesChanged() marks the project modified.
/// - Tag-table columns: the project's hiddenColumns are applied on load (compact view for a new
///   project); changing them from the header menu marks the project modified.
/// - Export actions open ExportImageDialog / ExportCsvDialog.
/// - Classroom safety (v1.3.3, PLAN.md "v1.3.3 Classroom safety", T234):
///   - TagTableModel::setConnected(state != Disconnected) on every connection state change;
///     the Options action "Use 1-based register numbers" is disabled while connected or locked.
///   - Teacher lock: Project::locked is applied on load (TagTableModel::setLocked, the lockTags
///     action's check state, MainWindow::setTagsLocked) and saved; a new project is unlocked.
///     Checking lockTags locks at once. Unchecking asks (m_prompts.question, title "Unlock tags")
///     "Unlock the tag configuration? Students can then change registers and make tags
///     writable." with Yes ("Unlock") / Cancel (default); Cancel re-checks the action. Both mark
///     the project modified. While locked the tag table's Add opens nothing (status message "The
///     tag configuration is locked (Options > Lock tag configuration)"), and accepting an
///     EncodingDialog that would change a tag shows that message instead.
///   - TagTableModel::writableCleared -> m_prompts.information (title "Writable turned off"),
///     rich text: headline "'<name>' is no longer writable.", rows "Was:" / "Now:" (the two
///     register texts) and "Tick Writable again to write to the new register."
///   - TagTableModel::setConfirmWritable -> m_prompts.question (title "Make writable"), rich text
///     headline "Make '<name>' writable?", row "Register:" registerText, when wasInput the line
///     "This tag was an input register (<IR text>). The holding register with the same number
///     can be a different value in the device.", and "Values typed in its Value cell are then
///     written to the device."; Yes ("Make writable") / Cancel (default). The Add tag dialog asks
///     the same question before adding a writable tag (wasInput when an input tag overlaps its
///     registers); Cancel adds nothing. The "Writable turned off" box is shown from a zero-delay
///     timer, after the model change has finished.
///   - Write log: WriteController::setServer("<host>:<port> unit <id>") on connect;
///     WriteController::writeLogged -> MainWindow::writeLogPanel()->addEntry and m_writeLog
///     (WriteLogFile, folder WriteLogFile::defaultFolder(); tests set another one) append. The
///     first append failure of the session shows a status message "Write log not saved: <error>"
///     and WriteLogPanel::setFileError(error); later failures only update the panel; a later
///     success clears the error.
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
        /// Opens a URL in the default browser / application; false when that failed.
        /// Default: QDesktopServices::openUrl.
        std::function<bool(const QUrl &url)> openUrl;
        /// Rich-text information box with an OK button (Writable turned off).
        std::function<void(const QString &title, const QString &text)> information;
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
    /// Yes/Cancel question (Cancel is the default) whose Yes button reads `yesLabel`.
    bool askYesCancel(const QString &title, const QString &text, const QString &yesLabel);
    /// Applies the teacher lock to the model, the action and the window without marking modified.
    void applyLocked(bool locked);
    /// Connection state -> model and the 1-based action (disabled while connected or locked).
    void updateRegisterLocks();
    void onLockActionToggled(bool on);
    void onWriteLogged(const WriteLogEntry &entry);
    void wireClassroomSafety();
    static QString lockedMessage();

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
    /// Help > Quick Start / User Guide (see the class comment). Stub until T202.
    void openDocumentation(bool userGuide);
    /// Cursor readout, then the Analysis readout (when not empty), for the image export.
    QString imageReadoutText() const;

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
    /// The "Make writable" question (TagTableModel::setConfirmWritable and the Add tag dialog).
    bool confirmWritable(const TagTableModel::WritableAsk &request);
    void onAddTagReadFinished(quint64 requestId, const ModbusResult &result, const QList<quint16> &values);
    quint64 m_nextAddTagRequest = kAddTagRequestBase;
    QHash<quint64, std::function<void(const ModbusResult &, const QList<quint16> &)>> m_addTagReads;
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

    WriteLogFile m_writeLog;            ///< one CSV per day; tests point it at a temporary folder
    bool m_writeLogErrorShown = false;  ///< the status message was shown this session
    QString m_yesLabel;                 ///< label of the Yes button for the next default question
};

} // namespace mb
