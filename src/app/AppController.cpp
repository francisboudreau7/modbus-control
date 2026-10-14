#include "AppController.h"

#include "AddTagDialog.h"
#include "AnalysisCsv.h"
#include "AnalysisView.h"
#include "ConnectionBar.h"
#include "CursorPanel.h"
#include "EncodingMonitor.h"
#include "ExportCsvDialog.h"
#include "ExportImageDialog.h"
#include "MainWindow.h"
#include "ModbusConnection.h"
#include "PlotCursors.h"
#include "PollWorker.h"
#include "QuickWritePanel.h"
#include "SessionIO.h"
#include "TagTableModel.h"
#include "TagTableWidget.h"
#include "TrendPlot.h"
#include "WriteController.h"

#include <QAction>
#include <QDateTime>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QDialog>
#include <QDir>
#include <QHash>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <utility>

namespace mb {

namespace {

ConnectionSettings defaultConnection()
{
    ConnectionSettings s;
    s.host = QStringLiteral("127.0.0.1");
    s.port = 1502;
    s.unitId = 1;
    s.timeoutMs = 1000;
    s.pollPeriodMs = 200;
    return s;
}

ConnectionSettings lastConnection()
{
    const ConnectionSettings d = defaultConnection();
    QSettings q;
    ConnectionSettings s;
    s.host = q.value(QStringLiteral("connection/lastHost"), d.host).toString();
    s.port = static_cast<quint16>(q.value(QStringLiteral("connection/lastPort"), d.port).toInt());
    s.unitId = q.value(QStringLiteral("connection/lastUnit"), d.unitId).toInt();
    s.timeoutMs = q.value(QStringLiteral("connection/lastTimeout"), d.timeoutMs).toInt();
    s.pollPeriodMs = q.value(QStringLiteral("connection/lastPeriod"), d.pollPeriodMs).toInt();
    return s;
}

void storeLastConnection(const ConnectionSettings &s)
{
    QSettings q;
    q.setValue(QStringLiteral("connection/lastHost"), s.host);
    q.setValue(QStringLiteral("connection/lastPort"), s.port);
    q.setValue(QStringLiteral("connection/lastUnit"), s.unitId);
    q.setValue(QStringLiteral("connection/lastTimeout"), s.timeoutMs);
    q.setValue(QStringLiteral("connection/lastPeriod"), s.pollPeriodMs);
    q.remove(QStringLiteral("connection/lastSingleRegisterWrites")); // obsolete FC06-only option
}

Tag makeTag(TagId id, const QString &name, Area area, quint16 address, ValueType type,
            const QString &color, bool plotted, bool writable, ByteOrder order = ByteOrder::ABCD)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.area = area;
    t.address = address;
    t.type = type;
    t.byteOrder = order;
    t.color = color;
    t.plotted = plotted;
    t.writable = writable;
    return t;
}

QList<Tag> demoTags()
{
    const Area ir = Area::InputRegister;
    QList<Tag> tags{
        makeTag(1, QStringLiteral("Sine"), ir, 0, ValueType::Int16, QStringLiteral("#1f77b4"), true, false),
        makeTag(2, QStringLiteral("Triangle"), ir, 1, ValueType::Int16, QStringLiteral("#ff7f0e"), true, false),
        makeTag(3, QStringLiteral("Counter"), ir, 2, ValueType::UInt16, QStringLiteral("#7f7f7f"), false, false),
        makeTag(4, QStringLiteral("Square"), ir, 3, ValueType::Int16, QStringLiteral("#2ca02c"), true, false),
        makeTag(5, QStringLiteral("Noise"), ir, 4, ValueType::Int16, QStringLiteral("#d62728"), true, false),
        makeTag(6, QStringLiteral("Sawtooth"), ir, 5, ValueType::Int16, QStringLiteral("#9467bd"), true, false),
        makeTag(7, QStringLiteral("Lag"), ir, 10, ValueType::Int16, QStringLiteral("#8c564b"), true, false),
        makeTag(8, QStringLiteral("Setpoint"), Area::HoldingRegister, 100, ValueType::Int16,
                QStringLiteral("#e377c2"), true, true),
        makeTag(9, QStringLiteral("Float sine"), ir, 20, ValueType::Float32, QStringLiteral("#bcbd22"),
                true, false, ByteOrder::ABCD),
        makeTag(10, QStringLiteral("Float sine (CDAB)"), ir, 22, ValueType::Float32,
                QStringLiteral("#17becf"), true, false, ByteOrder::CDAB),
        makeTag(11, QStringLiteral("Int32 saw"), ir, 24, ValueType::Int32, QStringLiteral("#393b79"),
                true, false, ByteOrder::ABCD),
    };
    for (Tag &t : tags) {
        t.analyzed = (t.id == 7); // Lag (IR 10): the step-response demo
        if (t.id == 8) {          // Setpoint (HR 100): quick-write presets and the right Y axis
            t.quickA = 0.0;
            t.quickB = 1000.0;
            t.writeMin = 0.0;
            t.writeMax = 2000.0;
            t.axis = YAxis::Right;
        }
    }
    return tags;
}

} // namespace

AppController::AppController(MainWindow *window, QObject *parent) : QObject(parent), m_window(window)
{
    m_thread = new QThread(this);
    m_worker = new PollWorker; // no parent: moved to the worker thread
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(100);

    m_window->trendPlot()->setRecorder(&m_recorder);
    m_window->cursorPanel()->setRecorder(&m_recorder);
    m_window->analysisView()->setRecorder(&m_recorder);

    m_writes = new WriteController(m_window->tagModel(), m_window, this);
    m_encoding = new EncodingMonitor(m_window->tagModel(), m_window, this);

    // Dialog seams (tests replace them): the real dialogs.
    m_prompts.question = [this](const QString &title, const QString &text,
                                QMessageBox::StandardButtons buttons,
                                QMessageBox::StandardButton defaultButton) {
        return QMessageBox::question(m_window, title, text, buttons, defaultButton);
    };
    m_prompts.warning = [this](const QString &title, const QString &text) {
        QMessageBox::warning(m_window, title, text);
    };
    m_prompts.saveFileName = [this](const QString &caption, const QString &path, const QString &filter) {
        return QFileDialog::getSaveFileName(m_window, caption, path, filter);
    };
    m_prompts.openFileName = [this](const QString &caption, const QString &dir, const QString &filter) {
        return QFileDialog::getOpenFileName(m_window, caption, dir, filter);
    };
    m_prompts.exec = [](QDialog &dialog) { dialog.exec(); };

    wireConnection();
    wireData();
    wireTags();
    wireWrites();
    wireActions();
    wireQuickWrites();

    m_window->setCloseHandler([this] { return maybeSave(); });

    // Every start: last connection settings and the demo tags, as an unmodified Untitled project.
    m_window->connectionBar()->setSettings(lastConnection());
    m_window->tagModel()->setTags(demoTags(), 12);
    m_encoding->onTagsReplaced();
    pushTags();
    // Analysis of the Lag step: the controller output step (deltaCo) is the Setpoint (HR 100, id 8).
    AnalysisSettings analysis;
    analysis.deltaCoTagId = 8;
    analysis.showModel = true;
    m_window->analysisView()->setSettings(analysis);
    // Compact view, but keep the Quick A/B presets of the demo Setpoint visible.
    QStringList hidden = compactHiddenColumns();
    hidden.removeAll(QStringLiteral("quickA"));
    hidden.removeAll(QStringLiteral("quickB"));
    m_window->tagTable()->setHiddenColumns(hidden);
    m_refreshTimer->start();
    updateTitle();
}

AppController::~AppController()
{
    m_window->setCloseHandler({});
    m_refreshTimer->stop();
    if (m_thread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
    }
    // The widgets may outlive the recorder; do not leave them a dangling pointer.
    m_window->trendPlot()->setRecorder(nullptr);
    m_window->cursorPanel()->setRecorder(nullptr);
    m_window->analysisView()->setRecorder(nullptr);
}

void AppController::wireConnection()
{
    ConnectionBar *bar = m_window->connectionBar();
    connect(bar, &ConnectionBar::connectRequested, this, [this](const ConnectionSettings &s) {
        if (!prepareConnect())
            return;
        storeLastConnection(s);
        QMetaObject::invokeMethod(m_worker, [w = m_worker, s] { w->connectTo(s); }, Qt::QueuedConnection);
    });
    connect(bar, &ConnectionBar::disconnectRequested, this, [this] {
        QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::QueuedConnection);
    });
    connect(bar, &ConnectionBar::settingsEdited, this, [this] { setModified(true); });

    connect(m_worker, &PollWorker::stateChanged, this, [this](ConnectionState state, const QString &detail) {
        const bool wasConnected = m_state == ConnectionState::Connected;
        m_state = state;
        m_window->connectionBar()->setState(state, detail);
        m_window->setConnectionStatus(state, detail);
        m_writes->setConnected(state == ConnectionState::Connected);
        if (QuickWritePanel *panel = m_window->quickWritePanel())
            panel->setConnected(state == ConnectionState::Connected);
        if (state == ConnectionState::Disconnected) {
            m_dropSamples = false;
            m_window->tagModel()->clearLiveValues();
        }
        if (state == ConnectionState::Connected)
            m_encoding->onConnected();
        else if (wasConnected)
            m_encoding->onDisconnected();
    });
    connect(m_worker, &PollWorker::message, this,
            [this](const QString &text) { m_window->showStatusMessage(text); });
}

void AppController::wireData()
{
    connect(m_worker, &PollWorker::samplesReady, this, [this](const SampleBatch &batch) {
        if (m_dropSamples)
            return; // in flight while a session is being loaded
        SampleBatch shifted = batch;
        shifted.t += m_timeOffset; // continuing a loaded recording
        m_recorder.append(shifted);
        // A batch decoded with the previous tag configuration must not refresh the table's
        // live values (they vouch for 32-bit writes): wait until the worker has the new tags.
        const bool accepted = m_tagsApplied == m_tagsGeneration;
        if (accepted)
            m_window->tagModel()->updateSamples(shifted);
        if (m_sessionLoaded)
            m_sessionDirty = true;
        m_newData = true;
        // Last: it may open a modal dialog (the event loop then keeps delivering samples).
        if (accepted)
            m_encoding->onSamples(batch);
    });
    connect(m_worker, &PollWorker::timeBaseReset, this,
            [this](const QDateTime &wall) { onTimeBaseReset(wall); });
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        if (!m_newData)
            return;
        m_newData = false;
        m_window->trendPlot()->refresh();
        m_window->analysisView()->refresh();
        m_window->cursorPanel()->refresh();
    });

    connect(m_window->trendPlot(), &TrendPlot::clearDataRequested, this, [this] {
        if (m_recorder.size() > 0) {
            const auto answer = ask(QStringLiteral("Clear data"),
                QStringLiteral("Discard all recorded data? This cannot be undone."));
            if (answer != QMessageBox::Yes)
                return;
        }
        // The timeBaseReset handler clears the recorder and the plot.
        QMetaObject::invokeMethod(m_worker, &PollWorker::resetTimeBase, Qt::QueuedConnection);
        m_window->trendPlot()->cursors()->clear();
    });

    PlotCursors *cursors = m_window->trendPlot()->cursors();
    connect(cursors, &PlotCursors::cursorsChanged, this, [this, cursors] {
        std::optional<double> c1;
        std::optional<double> c2;
        if (cursors->hasC1())
            c1 = cursors->c1();
        if (cursors->hasC2())
            c2 = cursors->c2();
        m_window->cursorPanel()->setCursors(c1, c2);
    });

    AnalysisView *analysis = m_window->analysisView();
    connect(analysis, &AnalysisView::settingsChanged, this, [this] { setModified(true); });
    connect(analysis, &AnalysisView::copyCursorsRequested, this, [this] {
        const PlotCursors *tc = m_window->trendPlot()->cursors();
        if (!tc->hasC1() && !tc->hasC2()) {
            m_window->showStatusMessage(QStringLiteral("Place cursors on the Trend first"));
            return;
        }
        std::optional<double> c1;
        std::optional<double> c2;
        if (tc->hasC1())
            c1 = tc->c1();
        if (tc->hasC2())
            c2 = tc->c2();
        m_window->analysisView()->setCursors(c1, c2);
    });
    connect(analysis, &AnalysisView::exportResultsRequested, this, [this] { exportAnalysisResults(); });
    connect(analysis, &AnalysisView::resultsChanged, this, [this] {
        bool any = false;
        for (const StepResult &r : m_window->analysisView()->results())
            any = any || r.valid;
        m_window->actions().exportAnalysisCsv->setEnabled(any);
    });
}

void AppController::wireTags()
{
    connect(m_window->tagModel(), &TagTableModel::tagsEdited, this, [this] {
        pushTags();
        setModified(true);
        m_encoding->onTagsChanged();
    });
    // The visible columns are saved in the project.
    connect(m_window->tagTable(), &TagTableWidget::hiddenColumnsChanged, this,
            [this] { setModified(true); });
}

void AppController::wireWrites()
{
    connect(m_window->tagModel(), &TagTableModel::writeRequested, m_writes, &WriteController::requestWrite);
    connect(m_window->tagTable(), &TagTableWidget::writeSelectionRequested, m_writes,
            &WriteController::requestWriteSelection);
    connect(m_writes, &WriteController::writeRegisters, m_worker, &PollWorker::writeRegisters,
            Qt::QueuedConnection);
    connect(m_worker, &PollWorker::writeFinished, m_writes, &WriteController::onWriteFinished);
    connect(m_writes, &WriteController::message, this,
            [this](const QString &text) { m_window->showStatusMessage(text); });
    connect(m_window->tagTable(), &TagTableWidget::detectEncodingRequested, m_encoding,
            &EncodingMonitor::detectNow);
    connect(m_writes, &WriteController::detectEncodingRequested, m_encoding,
            &EncodingMonitor::detectNow);
    connect(m_encoding, &EncodingMonitor::message, this,
            [this](const QString &text) { m_window->showStatusMessage(text); });
    connect(m_encoding, &EncodingMonitor::readRequested, m_worker, &PollWorker::readRegisters,
            Qt::QueuedConnection);
    connect(m_worker, &PollWorker::readFinished, m_encoding, &EncodingMonitor::onReadFinished);
    connect(m_worker, &PollWorker::readFinished, this, &AppController::onAddTagReadFinished);
    m_window->tagTable()->setAddTagHandler([this] { addTagDialog(); });
}

void AppController::wireActions()
{
    const MainWindow::Actions &a = m_window->actions();
    connect(a.newProject, &QAction::triggered, this, [this] { newProject(); });
    connect(a.openProject, &QAction::triggered, this, [this] { openDialog(); });
    connect(a.saveProject, &QAction::triggered, this, [this] { save(); });
    connect(a.saveProjectAs, &QAction::triggered, this, [this] { saveAs(); });
    connect(a.saveSession, &QAction::triggered, this, [this] { saveSessionDialog(); });
    connect(a.openSession, &QAction::triggered, this, [this] { openSessionDialog(); });
    connect(a.exportImage, &QAction::triggered, this, [this] { exportImage(); });
    connect(a.exportCsv, &QAction::triggered, this, [this] { exportCsv(); });
    connect(a.exportAnalysisCsv, &QAction::triggered, this, [this] { exportAnalysisResults(); });
    connect(a.oneBasedAddresses, &QAction::toggled, this, [this](bool on) {
        m_window->tagModel()->setAddressBase(on ? 1 : 0);
        QMetaObject::invokeMethod(m_worker, [w = m_worker, on] { w->setAddressBase(on ? 1 : 0); },
                                  Qt::QueuedConnection);
        setModified(true);
    });
}

void AppController::wireQuickWrites()
{
    if (QuickWritePanel *panel = m_window->quickWritePanel()) {
        connect(panel, &QuickWritePanel::quickWriteRequested, m_writes, &WriteController::requestQuickWrite);
        panel->setConnected(m_state == ConnectionState::Connected);
    }
    QAction *action = m_window->actions().confirmQuickWrites;
    if (!action)
        return;
    {
        const QSignalBlocker blocker(action);
        action->setChecked(m_writes->quickConfirm());
    }
    connect(action, &QAction::toggled, m_writes, &WriteController::setQuickConfirm);
    connect(m_writes, &WriteController::quickConfirmChanged, this, [action](bool on) {
        const QSignalBlocker blocker(action);
        action->setChecked(on);
    });
}

void AppController::pushTags()
{
    const QList<Tag> tags = m_window->tagModel()->tags();
    const quint64 generation = ++m_tagsGeneration;
    QMetaObject::invokeMethod(
        m_worker,
        [w = m_worker, tags, generation, self = QPointer<AppController>(this)] {
            w->setTags(tags);
            // Queued after every batch the worker emitted with the old tags.
            QMetaObject::invokeMethod(self, [self, generation] {
                if (self && generation > self->m_tagsApplied)
                    self->m_tagsApplied = generation;
            }, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    m_window->trendPlot()->setTags(tags);
    m_window->cursorPanel()->setTags(tags);
    m_window->analysisView()->setTags(tags);
}

void AppController::setModified(bool modified)
{
    m_modified = modified;
    updateTitle();
}

void AppController::updateTitle()
{
    QString name = QStringLiteral("Untitled");
    if (!m_projectPath.isEmpty())
        name = QFileInfo(m_projectPath).completeBaseName();
    else if (!m_sessionPath.isEmpty())
        name = QFileInfo(m_sessionPath).fileName();
    m_window->setDocumentTitle(name, m_modified || recordingDirty());
}

bool AppController::recordingDirty() const
{
    return m_sessionLoaded && m_sessionDirty;
}

bool AppController::maybeSave()
{
    if (recordingDirty()) {
        const auto answer = ask(QStringLiteral("Unsaved changes"),
            QStringLiteral("The recording has changed. Save the session?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Save)
            return saveSessionDialog();
        return answer == QMessageBox::Discard;
    }
    if (!m_modified)
        return true;
    const auto answer = ask(QStringLiteral("Unsaved changes"),
        QStringLiteral("The project has unsaved changes. Save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

Project AppController::currentProject() const
{
    Project p;
    p.connection = m_window->connectionBar()->settings();
    p.tags = m_window->tagModel()->tags();
    p.nextTagId = m_window->tagModel()->nextTagId();
    p.addressBase = m_window->tagModel()->addressBase();
    p.analysis = m_window->analysisView()->settings();
    p.hiddenColumns = m_window->tagTable()->hiddenColumns();
    return p;
}

void AppController::disconnectForProjectChange()
{
    if (m_state == ConnectionState::Disconnected)
        return;
    // Same path as the Disconnect button.
    QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::QueuedConnection);
    m_window->showStatusMessage(QStringLiteral("Disconnected to load project"));
}

void AppController::applyProject(const Project &project)
{
    disconnectForProjectChange();
    m_window->connectionBar()->setSettings(project.connection);
    m_window->tagModel()->setTags(project.tags, project.nextTagId);
    m_encoding->onTagsReplaced();
    setAddressBase(project.addressBase);
    pushTags();
    m_window->analysisView()->setSettings(project.analysis);
    m_window->tagTable()->setHiddenColumns(project.hiddenColumns);
}

void AppController::setAddressBase(int base)
{
    // Loading a project must not mark it modified: update the action without its toggled slot.
    QAction *action = m_window->actions().oneBasedAddresses;
    const QSignalBlocker blocker(action);
    action->setChecked(base == 1);
    m_window->tagModel()->setAddressBase(base);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, base] { w->setAddressBase(base); },
                              Qt::QueuedConnection);
}

bool AppController::saveTo(const QString &path)
{
    QString error;
    if (!saveProject(path, currentProject(), &error)) {
        warn(QStringLiteral("Save project"),
                             QStringLiteral("Could not save \"%1\":\n%2").arg(path, error));
        return false;
    }
    m_projectPath = path;
    m_sessionPath.clear();
    QSettings().setValue(QStringLiteral("project/dir"), QFileInfo(path).absolutePath());
    setModified(false);
    m_window->showStatusMessage(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool AppController::save()
{
    if (m_projectPath.isEmpty())
        return saveAs();
    return saveTo(m_projectPath);
}

bool AppController::saveAs()
{
    const QString dir = QSettings().value(QStringLiteral("project/dir")).toString();
    QString path = saveFileName(QStringLiteral("Save project as"), dir,
                                                QStringLiteral("Modbus Control project (*.mbproj)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("mbproj"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".mbproj");
    return saveTo(path);
}

void AppController::newProject()
{
    if (!maybeSave())
        return;
    disconnectForProjectChange();
    m_window->connectionBar()->setSettings(lastConnection());
    m_window->tagModel()->setTags({}, 1);
    m_encoding->onTagsReplaced();
    setAddressBase(0);
    pushTags();
    m_window->analysisView()->setSettings(AnalysisSettings{});
    m_window->tagTable()->setHiddenColumns(compactHiddenColumns());
    m_projectPath.clear();
    discardLoadedRecording();
    setModified(false);
}

void AppController::openDialog()
{
    if (!maybeSave())
        return;
    const QString dir = QSettings().value(QStringLiteral("project/dir")).toString();
    const QString path = openFileName(QStringLiteral("Open project"), dir,
                                                      QStringLiteral("Modbus Control project (*.mbproj)"));
    if (!path.isEmpty())
        loadFrom(path);
}

bool AppController::loadFrom(const QString &path)
{
    QString error;
    const std::optional<Project> project = loadProject(path, &error);
    if (!project) {
        warn(QStringLiteral("Open project"),
                             QStringLiteral("Could not open \"%1\":\n%2").arg(path, error));
        return false;
    }
    applyProject(*project);
    m_projectPath = path;
    discardLoadedRecording();
    QSettings().setValue(QStringLiteral("project/dir"), QFileInfo(path).absolutePath());
    setModified(false);
    return true;
}

void AppController::clearSessionState()
{
    m_sessionPath.clear();
    m_sessionLoaded = false;
    m_sessionDirty = false;
    m_continuePending = false;
    m_timeOffset = 0.0;
}

void AppController::discardLoadedRecording()
{
    // A loaded session's times do not come from the worker clock; live samples would be
    // older than its last row and silently ignored. Start over with the default capacity.
    if (m_sessionLoaded) {
        m_recorder.setCapacity(kDefaultRecorderRows); // also clears
        m_window->trendPlot()->resetData();
        m_window->analysisView()->resetData();
        m_window->cursorPanel()->refresh();
        QMetaObject::invokeMethod(m_worker, &PollWorker::resetTimeBase, Qt::QueuedConnection);
    }
    clearSessionState();
}

std::optional<double> AppController::lastRowTime() const
{
    if (m_recorder.size() == 0)
        return std::nullopt;
    return m_recorder.timeAt(m_recorder.size() - 1);
}

bool AppController::saveSessionDialog()
{
    QSettings settings;
    const QString dir = settings.value(QStringLiteral("session/dir")).toString();
    QString name = QStringLiteral("session.mbsession");
    if (!m_projectPath.isEmpty())
        name = QFileInfo(m_projectPath).completeBaseName() + QStringLiteral(".mbsession");
    else if (!m_sessionPath.isEmpty())
        name = QFileInfo(m_sessionPath).completeBaseName() + QStringLiteral(".mbsession");
    QString path = saveFileName(QStringLiteral("Save session"), dir.isEmpty() ? name : QDir(dir).filePath(name),
        QStringLiteral("Modbus Control sessions (*.mbsession)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("mbsession"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".mbsession");

    SessionCursors cursors;
    const PlotCursors *tc = m_window->trendPlot()->cursors();
    if (tc->hasC1())
        cursors.trendC1 = tc->c1();
    if (tc->hasC2())
        cursors.trendC2 = tc->c2();
    const PlotCursors *ac = m_window->analysisView()->trendPlot()->cursors();
    if (ac->hasC1())
        cursors.analysisC1 = ac->c1();
    if (ac->hasC2())
        cursors.analysisC2 = ac->c2();

    QString error;
    if (!saveSession(path, currentProject(), m_recorder, cursors, &error)) {
        warn(QStringLiteral("Save session"),
                             QStringLiteral("Could not save \"%1\":\n%2").arg(path, error));
        return false;
    }
    settings.setValue(QStringLiteral("session/dir"), QFileInfo(path).absolutePath());
    m_sessionPath = path; // title (when no project file) and next default name
    if (m_sessionLoaded)
        m_sessionDirty = false;
    updateTitle();
    m_window->showStatusMessage(QStringLiteral("Session saved to %1").arg(QFileInfo(path).fileName()));
    return true;
}

void AppController::openSessionDialog()
{
    if (!maybeSave())
        return;
    if (!m_sessionLoaded && m_recorder.size() > 0) {
        const auto answer = ask(QStringLiteral("Open session"),
            QStringLiteral("Opening a session replaces the current recording. Save it as a session first?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveSessionDialog()))
            return;
    }
    const QString dir = QSettings().value(QStringLiteral("session/dir")).toString();
    const QString path = openFileName(QStringLiteral("Open session"), dir,
        QStringLiteral("Modbus Control sessions (*.mbsession);;All files (*)"));
    if (!path.isEmpty())
        loadSessionFile(path);
}

bool AppController::loadSessionFile(const QString &path)
{
    Project project;
    Recorder recording;
    SessionCursors cursors;
    QString error;
    if (!loadSession(path, project, recording, cursors, &error)) {
        warn(QStringLiteral("Open session"),
                             QStringLiteral("Could not open \"%1\":\n%2").arg(path, error));
        return false;
    }
    QSettings().setValue(QStringLiteral("session/dir"), QFileInfo(path).absolutePath());

    // Samples already queued by the worker must not land in the loaded recording.
    if (m_state != ConnectionState::Disconnected)
        m_dropSamples = true;
    applyProject(project); // also disconnects
    m_recorder = std::move(recording);
    m_continuePending = false;
    m_timeOffset = 0.0;
    m_sessionLoaded = true;
    m_sessionDirty = false;
    m_sessionPath = path;
    m_projectPath.clear();

    TrendPlot *trend = m_window->trendPlot();
    trend->resetData();
    trend->setWindowSeconds(0.0); // All
    trend->refresh();
    m_window->analysisView()->resetData();
    m_window->analysisView()->refresh();
    m_window->cursorPanel()->refresh();
    m_newData = false;

    trend->cursors()->clear();
    if (cursors.trendC1)
        trend->cursors()->setC1(*cursors.trendC1);
    if (cursors.trendC2)
        trend->cursors()->setC2(*cursors.trendC2);
    m_window->analysisView()->setCursors(cursors.analysisC1, cursors.analysisC2);

    setModified(false);
    m_window->showStatusMessage(
        QStringLiteral("Session loaded: %1 rows (offline)").arg(m_recorder.size()));
    return true;
}

bool AppController::prepareConnect()
{
    if (!m_sessionLoaded || m_recorder.size() == 0)
        return true;
    ContinueChoice choice = ContinueChoice::Cancel;
    if (m_continuePrompt) {
        choice = m_continuePrompt();
    } else {
        QMessageBox box(QMessageBox::Question, QStringLiteral("Connect"),
                        QStringLiteral("A recording from a session is loaded. Continue it or start a new one?"),
                        QMessageBox::NoButton, m_window);
        QPushButton *cont = box.addButton(QStringLiteral("Continue this recording"), QMessageBox::AcceptRole);
        QPushButton *fresh = box.addButton(QStringLiteral("Start a new recording"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cont);
        box.exec();
        if (box.clickedButton() == cont)
            choice = ContinueChoice::Continue;
        else if (box.clickedButton() == fresh)
            choice = ContinueChoice::StartNew;
    }
    if (choice == ContinueChoice::Cancel)
        return false;
    if (choice == ContinueChoice::StartNew && recordingDirty() && !maybeSave())
        return false;
    if (choice == ContinueChoice::Continue) {
        m_continuePending = true;
    } else {
        m_sessionLoaded = false;
        m_sessionDirty = false;
        m_sessionPath.clear();
        m_timeOffset = 0.0;
        updateTitle();
    }
    // Make sure a time base reset (with its wall-clock time) follows even if the worker
    // already has a running clock; it is queued ahead of the connect request.
    QMetaObject::invokeMethod(m_worker, &PollWorker::resetTimeBase, Qt::QueuedConnection);
    return true;
}

void AppController::onTimeBaseReset(const QDateTime &wall)
{
    if (m_continuePending && m_recorder.size() > 0) {
        m_continuePending = false;
        const double last = *lastRowTime();
        double offset = last + 1.0;
        const QDateTime start = m_recorder.startTime();
        if (start.isValid() && wall.isValid())
            offset = static_cast<double>(start.msecsTo(wall)) / 1000.0;
        constexpr double kGap = 1.0e-3;
        offset = std::max(offset, last + 2.0 * kGap);
        m_timeOffset = offset;
        // One all-NaN row breaks the trace between the old and the new samples.
        SampleBatch gap;
        gap.t = last + kGap;
        m_recorder.append(gap);
        m_sessionDirty = true;
        m_newData = true;
        updateTitle();
        return;
    }
    m_continuePending = false;
    m_timeOffset = 0.0;
    if (m_sessionLoaded) { // the loaded recording is being discarded (Clear data)
        m_sessionLoaded = false;
        m_sessionDirty = false;
        m_sessionPath.clear();
        updateTitle();
    }
    if (m_recorder.capacity() != kDefaultRecorderRows)
        m_recorder.setCapacity(kDefaultRecorderRows); // a loaded session may have raised it
    m_recorder.clear();
    m_recorder.setStartTime(wall);
    m_window->trendPlot()->resetData();
    m_window->analysisView()->resetData();
    m_window->cursorPanel()->refresh();
    m_newData = false;
}

QMessageBox::StandardButton AppController::ask(const QString &title, const QString &text,
                                               QMessageBox::StandardButtons buttons,
                                               QMessageBox::StandardButton defaultButton)
{
    return m_prompts.question(title, text, buttons, defaultButton);
}

void AppController::warn(const QString &title, const QString &text)
{
    m_prompts.warning(title, text);
}

QString AppController::saveFileName(const QString &caption, const QString &path, const QString &filter)
{
    return m_prompts.saveFileName(caption, path, filter);
}

QString AppController::openFileName(const QString &caption, const QString &dir, const QString &filter)
{
    return m_prompts.openFileName(caption, dir, filter);
}

void AppController::runDialog(QDialog &dialog)
{
    m_prompts.exec(dialog);
}

bool AppController::openProject(const QString &path)
{
    return loadFrom(path);
}

void AppController::exportImage()
{
    if (m_window->isAnalysisTabActive()) {
        AnalysisView *view = m_window->analysisView();
        ExportImageDialog dialog(view->trendPlot()->plot(), view->trendPlot()->cursors(),
                                 view->readoutText(), m_window);
        runDialog(dialog);
        return;
    }
    TrendPlot *trend = m_window->trendPlot();
    ExportImageDialog dialog(trend->plot(), trend->cursors(), m_window->cursorPanel()->readoutText(), m_window);
    runDialog(dialog);
}

void AppController::exportCsv()
{
    PlotCursors *cursors = m_window->trendPlot()->cursors();
    std::optional<std::pair<double, double>> range;
    if (cursors->hasC1() && cursors->hasC2())
        range = std::make_pair(qMin(cursors->c1(), cursors->c2()), qMax(cursors->c1(), cursors->c2()));
    ExportCsvDialog dialog(m_recorder, m_window->tagModel()->tags(), range, m_window);
    runDialog(dialog);
}

void AppController::exportAnalysisResults()
{
    const QList<StepResult> results = m_window->analysisView()->results();
    bool any = false;
    for (const StepResult &r : results)
        any = any || r.valid;
    if (!any)
        return;

    QSettings settings;
    QString dir = settings.value(QStringLiteral("export/csvDir")).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = saveFileName(QStringLiteral("Export analysis results"),
        QDir(dir).filePath(QStringLiteral("analysis.csv")),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    settings.setValue(QStringLiteral("export/csvDir"), QFileInfo(path).absolutePath());

    QHash<TagId, QString> headers;
    for (const Tag &t : m_window->tagModel()->tags())
        headers.insert(t.id, t.unit.isEmpty() ? t.name : QStringLiteral("%1 [%2]").arg(t.name, t.unit));

    QString error;
    bool ok = false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
    } else if (!exportAnalysisCsv(file, results, headers, u',', &error)) {
        file.cancelWriting();
    } else if (!file.commit()) {
        error = file.errorString();
    } else {
        ok = true;
    }
    if (ok) {
        m_window->showStatusMessage(QStringLiteral("Analysis results exported to %1").arg(path));
        return;
    }
    if (error.isEmpty())
        error = QStringLiteral("Unknown error");
    warn(QStringLiteral("Export analysis results"),
                         QStringLiteral("Could not write \"%1\":\n%2").arg(path, error));
}

void AppController::addTagDialog()
{
    TagTableModel *model = m_window->tagModel();
    AddTagDialog dialog(model->tags(), model->addressBase(), m_encoding->deviceOrder(), m_window);
    dialog.setReadFunction([this](Area area, quint16 address, int count, AddTagDialog::ReadDone done) {
        const quint64 id = m_nextAddTagRequest++;
        m_addTagReads.insert(id, std::move(done));
        QMetaObject::invokeMethod(
            m_worker, [w = m_worker, id, area, address, count] { w->readRegisters(id, area, address, count); },
            Qt::QueuedConnection);
    });
    dialog.setConnected(m_state == ConnectionState::Connected);
    // The worker answers every read exactly once (NotConnected when not connected, and queued
    // reads fail on disconnect), so a pending callback is never left behind.
    const QMetaObject::Connection link = connect(
        m_worker, &PollWorker::stateChanged, &dialog,
        [&dialog](ConnectionState state, const QString &) {
            dialog.setConnected(state == ConnectionState::Connected);
        });
    runDialog(dialog);
    disconnect(link);
    if (dialog.result() != QDialog::Accepted)
        return;
    const int row = model->addTag(dialog.tag());
    if (dialog.detectionConfirmed() && row >= 0 && row < model->tags().size())
        m_encoding->confirmDetected(model->tags().at(row).id);
    m_window->tagTable()->showRow(row);
}

void AppController::onAddTagReadFinished(quint64 requestId, const ModbusResult &result,
                                         const QList<quint16> &values)
{
    if (requestId < kAddTagRequestBase)
        return;
    const auto it = m_addTagReads.find(requestId);
    if (it == m_addTagReads.end())
        return;
    AddTagDialog::ReadDone done = std::move(it.value());
    m_addTagReads.erase(it);
    if (done)
        done(result, values);
}

} // namespace mb
