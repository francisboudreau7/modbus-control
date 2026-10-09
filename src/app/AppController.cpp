#include "AppController.h"

#include "ConnectionBar.h"
#include "CursorPanel.h"
#include "ExportCsvDialog.h"
#include "ExportImageDialog.h"
#include "MainWindow.h"
#include "ModbusConnection.h"
#include "PlotCursors.h"
#include "PollWorker.h"
#include "TagTableModel.h"
#include "TagTableWidget.h"
#include "TrendPlot.h"
#include "WriteController.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QSettings>
#include <QThread>
#include <QTimer>

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
}

Tag makeTag(TagId id, const QString &name, Area area, quint16 address, ValueType type,
            const QString &color, bool plotted, bool writable)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.area = area;
    t.address = address;
    t.type = type;
    t.color = color;
    t.plotted = plotted;
    t.writable = writable;
    return t;
}

QList<Tag> demoTags()
{
    const Area ir = Area::InputRegister;
    return {
        makeTag(1, QStringLiteral("Sine"), ir, 0, ValueType::Int16, QStringLiteral("#1f77b4"), true, false),
        makeTag(2, QStringLiteral("Triangle"), ir, 1, ValueType::Int16, QStringLiteral("#ff7f0e"), true, false),
        makeTag(3, QStringLiteral("Counter"), ir, 2, ValueType::UInt16, QStringLiteral("#7f7f7f"), false, false),
        makeTag(4, QStringLiteral("Square"), ir, 3, ValueType::Int16, QStringLiteral("#2ca02c"), true, false),
        makeTag(5, QStringLiteral("Noise"), ir, 4, ValueType::Int16, QStringLiteral("#d62728"), true, false),
        makeTag(6, QStringLiteral("Sawtooth"), ir, 5, ValueType::Int16, QStringLiteral("#9467bd"), true, false),
        makeTag(7, QStringLiteral("Lag"), ir, 10, ValueType::Int16, QStringLiteral("#8c564b"), true, false),
        makeTag(8, QStringLiteral("Setpoint"), Area::HoldingRegister, 100, ValueType::Int16,
                QStringLiteral("#e377c2"), true, true),
    };
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

    m_writes = new WriteController(m_window->tagModel(), m_window);

    wireConnection();
    wireData();
    wireTags();
    wireWrites();
    wireActions();

    m_window->setCloseHandler([this] { return maybeSave(); });

    // First run: demo project, not modified.
    m_window->connectionBar()->setSettings(lastConnection());
    m_window->tagModel()->setTags(demoTags(), 9);
    pushTags();
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
}

void AppController::wireConnection()
{
    ConnectionBar *bar = m_window->connectionBar();
    connect(bar, &ConnectionBar::connectRequested, this, [this](const ConnectionSettings &s) {
        storeLastConnection(s);
        QMetaObject::invokeMethod(m_worker, [w = m_worker, s] { w->connectTo(s); }, Qt::QueuedConnection);
    });
    connect(bar, &ConnectionBar::disconnectRequested, this, [this] {
        QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::QueuedConnection);
    });
    connect(bar, &ConnectionBar::settingsEdited, this, [this] { setModified(true); });

    connect(m_worker, &PollWorker::stateChanged, this, [this](ConnectionState state, const QString &detail) {
        m_state = state;
        m_window->connectionBar()->setState(state, detail);
        m_window->setConnectionStatus(state, detail);
        m_writes->setConnected(state == ConnectionState::Connected);
        if (state == ConnectionState::Disconnected)
            m_window->tagModel()->clearLiveValues();
    });
    connect(m_worker, &PollWorker::message, this,
            [this](const QString &text) { m_window->showStatusMessage(text); });
}

void AppController::wireData()
{
    connect(m_worker, &PollWorker::samplesReady, this, [this](const SampleBatch &batch) {
        m_recorder.append(batch);
        m_window->tagModel()->updateSamples(batch);
        m_newData = true;
    });
    connect(m_worker, &PollWorker::timeBaseReset, this, [this](const QDateTime &wall) {
        m_recorder.clear();
        m_recorder.setStartTime(wall);
        m_window->trendPlot()->resetData();
        m_window->cursorPanel()->refresh();
        m_newData = false;
    });
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        if (!m_newData)
            return;
        m_newData = false;
        m_window->trendPlot()->refresh();
        m_window->cursorPanel()->refresh();
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
}

void AppController::wireTags()
{
    connect(m_window->tagModel(), &TagTableModel::tagsEdited, this, [this] {
        pushTags();
        setModified(true);
    });
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
}

void AppController::wireActions()
{
    const MainWindow::Actions &a = m_window->actions();
    connect(a.newProject, &QAction::triggered, this, [this] { newProject(); });
    connect(a.openProject, &QAction::triggered, this, [this] { openDialog(); });
    connect(a.saveProject, &QAction::triggered, this, [this] { save(); });
    connect(a.saveProjectAs, &QAction::triggered, this, [this] { saveAs(); });
    connect(a.exportImage, &QAction::triggered, this, [this] { exportImage(); });
    connect(a.exportCsv, &QAction::triggered, this, [this] { exportCsv(); });
}

void AppController::pushTags()
{
    const QList<Tag> tags = m_window->tagModel()->tags();
    QMetaObject::invokeMethod(m_worker, [w = m_worker, tags] { w->setTags(tags); }, Qt::QueuedConnection);
    m_window->trendPlot()->setTags(tags);
    m_window->cursorPanel()->setTags(tags);
}

void AppController::setModified(bool modified)
{
    m_modified = modified;
    updateTitle();
}

void AppController::updateTitle()
{
    const QString name = m_projectPath.isEmpty() ? QStringLiteral("Untitled")
                                                 : QFileInfo(m_projectPath).completeBaseName();
    m_window->setDocumentTitle(name, m_modified);
}

bool AppController::maybeSave()
{
    if (!m_modified)
        return true;
    const auto answer = QMessageBox::question(
        m_window, QStringLiteral("Unsaved changes"),
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
    pushTags();
}

bool AppController::saveTo(const QString &path)
{
    QString error;
    if (!saveProject(path, currentProject(), &error)) {
        QMessageBox::warning(m_window, QStringLiteral("Save project"),
                             QStringLiteral("Could not save \"%1\":\n%2").arg(path, error));
        return false;
    }
    m_projectPath = path;
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
    QString path = QFileDialog::getSaveFileName(m_window, QStringLiteral("Save project as"), dir,
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
    pushTags();
    m_projectPath.clear();
    setModified(false);
}

void AppController::openDialog()
{
    if (!maybeSave())
        return;
    const QString dir = QSettings().value(QStringLiteral("project/dir")).toString();
    const QString path = QFileDialog::getOpenFileName(m_window, QStringLiteral("Open project"), dir,
                                                      QStringLiteral("Modbus Control project (*.mbproj)"));
    if (!path.isEmpty())
        loadFrom(path);
}

bool AppController::loadFrom(const QString &path)
{
    QString error;
    const std::optional<Project> project = loadProject(path, &error);
    if (!project) {
        QMessageBox::warning(m_window, QStringLiteral("Open project"),
                             QStringLiteral("Could not open \"%1\":\n%2").arg(path, error));
        return false;
    }
    applyProject(*project);
    m_projectPath = path;
    QSettings().setValue(QStringLiteral("project/dir"), QFileInfo(path).absolutePath());
    setModified(false);
    return true;
}

bool AppController::openProject(const QString &path)
{
    return loadFrom(path);
}

void AppController::exportImage()
{
    TrendPlot *trend = m_window->trendPlot();
    ExportImageDialog dialog(trend->plot(), trend->cursors(), m_window->cursorPanel()->readoutText(), m_window);
    dialog.exec();
}

void AppController::exportCsv()
{
    PlotCursors *cursors = m_window->trendPlot()->cursors();
    std::optional<std::pair<double, double>> range;
    if (cursors->hasC1() && cursors->hasC2())
        range = std::make_pair(qMin(cursors->c1(), cursors->c2()), qMax(cursors->c1(), cursors->c2()));
    ExportCsvDialog dialog(m_recorder, m_window->tagModel()->tags(), range, m_window);
    dialog.exec();
}

} // namespace mb
