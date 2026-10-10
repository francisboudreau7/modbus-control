#include "AnalysisView.h"
#include "AppController.h"
#include "ConnectionBar.h"
#include "CursorPanel.h"
#include "MainWindow.h"
#include "MetaTypes.h"
#include "ModbusConnection.h"
#include "PlotCursors.h"
#include "QuickWritePanel.h"
#include "Recorder.h"
#include "TagTableModel.h"
#include "TagTableWidget.h"
#include "TrendPlot.h"
#include "WriteController.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <functional>
#include <memory>

using namespace mb;

namespace {

/// Starts modbus_test_server on an OS-chosen port and waits for the LISTENING line
/// (same pattern as tst_integration.cpp).
class ServerProcess
{
public:
    ~ServerProcess() { stop(); }

    bool start()
    {
        stop();
        m_log.clear();
        m_buffer.clear();
        m_port = 0;
        const QStringList args{QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                               QStringLiteral("--port"), QStringLiteral("0")};
        m_proc.setProcessChannelMode(QProcess::MergedChannels);
        m_proc.start(QStringLiteral(MC_TEST_SERVER_PATH), args);
        if (!m_proc.waitForStarted(10000)) {
            m_log += QStringLiteral("failed to start: %1\n").arg(m_proc.errorString());
            return false;
        }
        static const QRegularExpression re(QStringLiteral("^LISTENING (\\d+)\\s*$"),
                                           QRegularExpression::MultilineOption);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            m_proc.waitForReadyRead(200);
            const QString chunk = QString::fromLocal8Bit(m_proc.readAll());
            m_log += chunk;
            m_buffer += chunk;
            const auto m = re.match(m_buffer);
            if (m.hasMatch()) {
                m_port = static_cast<quint16>(m.captured(1).toUInt());
                return true;
            }
            if (m_proc.state() == QProcess::NotRunning)
                break;
        }
        return false;
    }

    void stop()
    {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.kill();
            m_proc.waitForFinished(5000);
        }
        if (m_proc.isOpen())
            m_log += QString::fromLocal8Bit(m_proc.readAll());
        m_port = 0;
    }

    quint16 port() const { return m_port; }
    QString log() const { return m_log; }

private:
    QProcess m_proc;
    QString m_log;
    QString m_buffer;
    quint16 m_port = 0;
};

int pointsIn(QwtPlot *plot)
{
    int n = 0;
    const QwtPlotItemList items = plot->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : items)
        n += static_cast<int>(static_cast<QwtPlotCurve *>(item)->dataSize());
    return n;
}

TagSample sampleOf(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.raw = static_cast<quint32>(static_cast<qint32>(value)) & 0xFFFFu;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

} // namespace

class TstAppController : public QObject
{
    Q_OBJECT

    using CC = AppController::ContinueChoice;

    // ---- fixture -------------------------------------------------------------------------

    /// Everything the replaced dialog seams saw or still have to answer.
    struct Seams {
        QList<QMessageBox::StandardButton> answers; ///< AppController::question
        QStringList questions;
        QStringList warnings;                       ///< AppController::warning
        QStringList saveNames;                      ///< saveFileName answers ("" = cancel)
        QStringList openNames;
        QStringList saveCaptions;
        QStringList openCaptions;
        QList<CC> continueChoices;
        std::function<void(QDialog &)> onExec;
        int execCount = 0;

        QList<bool> confirms;                       ///< WriteController::m_confirm
        QStringList confirmTexts;
        QStringList writeWarnings;                  ///< WriteController::m_warn
        QList<WriteController::QuickAnswer> quickAnswers;
        QList<bool> offers;                         ///< m_offerSingleWrites
        QList<bool> implausible;                    ///< m_warnImplausible

        QStringList unexpected;                     ///< a seam was called without an answer ready
    };

    Seams d;
    QTemporaryDir m_tmp;
    ServerProcess m_server;
    std::unique_ptr<MainWindow> win;
    std::unique_ptr<AppController> ctl;

    static void clearSettings()
    {
        QSettings s;
        s.clear();
        s.sync();
    }

    QString P(const QString &name) const { return m_tmp.filePath(name); }

    void installSeams()
    {
        auto &p = ctl->m_prompts;
        p.question = [this](const QString &title, const QString &text, QMessageBox::StandardButtons,
                            QMessageBox::StandardButton) {
            d.questions << title + QStringLiteral(": ") + text;
            if (d.answers.isEmpty()) {
                d.unexpected << QStringLiteral("question: ") + title + QStringLiteral(": ") + text;
                return QMessageBox::NoButton;
            }
            return d.answers.takeFirst();
        };
        p.warning = [this](const QString &title, const QString &text) {
            d.warnings << title + QStringLiteral(": ") + text;
        };
        p.saveFileName = [this](const QString &caption, const QString &, const QString &) {
            d.saveCaptions << caption;
            if (d.saveNames.isEmpty()) {
                d.unexpected << QStringLiteral("saveFileName: ") + caption;
                return QString();
            }
            return d.saveNames.takeFirst();
        };
        p.openFileName = [this](const QString &caption, const QString &, const QString &) {
            d.openCaptions << caption;
            if (d.openNames.isEmpty()) {
                d.unexpected << QStringLiteral("openFileName: ") + caption;
                return QString();
            }
            return d.openNames.takeFirst();
        };
        p.exec = [this](QDialog &dialog) {
            ++d.execCount;
            if (!d.onExec) {
                d.unexpected << QStringLiteral("exec: ") + dialog.windowTitle();
                return;
            }
            d.onExec(dialog);
        };
        ctl->m_continuePrompt = [this]() {
            if (d.continueChoices.isEmpty()) {
                d.unexpected << QStringLiteral("continue prompt");
                return CC::Cancel;
            }
            return d.continueChoices.takeFirst();
        };

        WriteController *w = ctl->m_writes;
        w->m_confirm = [this](const QString &question) {
            d.confirmTexts << question;
            if (d.confirms.isEmpty()) {
                d.unexpected << QStringLiteral("confirm: ") + question;
                return false;
            }
            return d.confirms.takeFirst();
        };
        w->m_warn = [this](const QString &text) { d.writeWarnings << text; };
        w->m_quickConfirmPrompt = [this](const QString &question) {
            if (d.quickAnswers.isEmpty()) {
                d.unexpected << QStringLiteral("quick confirm: ") + question;
                return WriteController::QuickAnswer{};
            }
            return d.quickAnswers.takeFirst();
        };
        w->m_warnImplausible = [this](const QString &text) {
            if (d.implausible.isEmpty()) {
                d.unexpected << QStringLiteral("implausible: ") + text;
                return false;
            }
            return d.implausible.takeFirst();
        };
        w->m_offerSingleWrites = [this](const QString &text) {
            if (d.offers.isEmpty()) {
                d.unexpected << QStringLiteral("offer single writes: ") + text;
                return false;
            }
            return d.offers.takeFirst();
        };
        w->m_askValues = [this](const QList<Tag> &, const QList<double> &)
            -> std::optional<QList<double>> {
            d.unexpected << QStringLiteral("askValues");
            return std::nullopt;
        };
    }

    void startApp()
    {
        d = Seams{};
        win = std::make_unique<MainWindow>();
        ctl = std::make_unique<AppController>(win.get());
        installSeams();
    }

    void stopApp()
    {
        ctl.reset();
        win.reset();
    }

    /// Simulates an application restart (settings and files persist).
    void restart()
    {
        stopApp();
        startApp();
    }

    QPushButton *connectButton() const { return win->connectionBar()->findChild<QPushButton *>(); }

    void waitState(ConnectionState state, int timeoutMs = 8000)
    {
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_state == state, timeoutMs);
    }

    /// Starts the test server (once) and presses Connect.
    void connectLive()
    {
        if (m_server.port() == 0)
            QVERIFY2(m_server.start(), qPrintable(m_server.log()));
        ConnectionSettings s = win->connectionBar()->settings();
        s.host = QStringLiteral("127.0.0.1");
        s.port = m_server.port();
        s.timeoutMs = 1000;
        s.pollPeriodMs = 100;
        win->connectionBar()->setSettings(s);
        QVERIFY(connectButton()->isEnabled());
        connectButton()->click();
        waitState(ConnectionState::Connected);
    }

    void disconnectLive()
    {
        connectButton()->click();
        waitState(ConnectionState::Disconnected);
    }

    void noUnexpected()
    {
        QVERIFY2(d.unexpected.isEmpty(), qPrintable(d.unexpected.join(QLatin1Char('\n'))));
        QVERIFY2(d.warnings.isEmpty(), qPrintable(d.warnings.join(QLatin1Char('\n'))));
        QVERIFY2(d.writeWarnings.isEmpty(), qPrintable(d.writeWarnings.join(QLatin1Char('\n'))));
    }

    /// Fills the recorder with a synthetic step: Setpoint (id 8) steps 0 -> 1000 at t = 10 s,
    /// Lag (id 7) follows with a 1 s dead time and a 3 s time constant. 301 rows, 0 .. 30 s.
    void feedStep()
    {
        Recorder &r = ctl->m_recorder;
        r.clear();
        r.setStartTime(QDateTime::currentDateTime().addSecs(-60));
        for (int i = 0; i <= 300; ++i) {
            const double t = i * 0.1;
            const double sp = t >= 10.0 ? 1000.0 : 0.0;
            const double lag = t < 11.0 ? 0.0 : 1000.0 * (1.0 - std::exp(-(t - 11.0) / 3.0));
            SampleBatch b;
            b.t = t;
            b.samples << sampleOf(1, 100.0 * std::sin(t)) << sampleOf(7, lag) << sampleOf(8, sp);
            r.append(b);
        }
        win->trendPlot()->resetData();
        win->analysisView()->resetData();
        win->trendPlot()->refresh();
        win->analysisView()->refresh();
        win->cursorPanel()->refresh();
    }

    /// feedStep() + cursors + File > Save session to P(name).mbsession; returns the path.
    QString makeSessionFile(const QString &name)
    {
        feedStep();
        win->trendPlot()->cursors()->setC1(5.0);
        win->trendPlot()->cursors()->setC2(20.0);
        win->analysisView()->setCursors(9.0, 29.0);
        d.saveNames << P(name);
        win->actions().saveSession->trigger();
        return P(name) + QStringLiteral(".mbsession");
    }

    bool modified() const { return ctl->m_modified; }

    TagTableModel *model() const { return win->tagModel(); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ModbusControlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_appcontroller"));
        clearSettings();
        registerMetaTypes();
        QVERIFY(m_tmp.isValid());
    }

    void cleanupTestCase()
    {
        stopApp();
        m_server.stop();
        clearSettings();
    }

    void init()
    {
        clearSettings();
        startApp();
    }

    void cleanup()
    {
        const QStringList unexpected = d.unexpected;
        stopApp();
        m_server.stop();
        clearSettings();
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join(QLatin1Char('\n'))));
    }

    // ---- start-up and project files ------------------------------------------------------

    void startUp()
    {
        QCOMPARE(model()->rowCount(), 11);
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("Untitled")));
        QVERIFY(!win->isWindowModified());
        QVERIFY(!modified());
        QVERIFY(ctl->m_state == ConnectionState::Disconnected);
        // Quick A/B presets of the demo Setpoint stay visible in the compact view.
        const QStringList hidden = win->tagTable()->hiddenColumns();
        QVERIFY(!hidden.isEmpty());
        QVERIFY(!hidden.contains(QStringLiteral("quickA")));
        QVERIFY(!hidden.contains(QStringLiteral("quickB")));
        QVERIFY(win->actions().confirmQuickWrites->isChecked());
        QVERIFY(!win->actions().exportAnalysisCsv->isEnabled());
        noUnexpected();
    }

    void newProjectPrompts()
    {
        model()->setData(model()->index(0, TagTableModel::Name), QStringLiteral("Renamed"));
        QVERIFY(modified());
        QVERIFY(win->isWindowModified());

        // Cancel: nothing happens.
        d.answers << QMessageBox::Cancel;
        win->actions().newProject->trigger();
        QCOMPARE(d.questions.size(), 1);
        QCOMPARE(model()->rowCount(), 11);
        QVERIFY(modified());

        // Save with a cancelled file dialog aborts the New as well.
        d.answers << QMessageBox::Save;
        d.saveNames << QString();
        win->actions().newProject->trigger();
        QCOMPARE(model()->rowCount(), 11);
        QVERIFY(modified());

        // Save: written to disk (extension added), then the new empty project.
        d.answers << QMessageBox::Save;
        d.saveNames << P(QStringLiteral("a"));
        win->actions().newProject->trigger();
        QVERIFY(QFileInfo::exists(P(QStringLiteral("a.mbproj"))));
        QCOMPARE(model()->rowCount(), 0);
        QVERIFY(!modified());
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("Untitled")));
        {
            QStringList got = win->tagTable()->hiddenColumns();
            QStringList want = compactHiddenColumns();
            got.sort();
            want.sort();
            QCOMPARE(got, want);
        }

        // Open it again through the dialog (nothing to save first).
        d.openNames << P(QStringLiteral("a.mbproj"));
        win->actions().openProject->trigger();
        QCOMPARE(model()->rowCount(), 11);
        QCOMPARE(model()->tagAt(0).name, QStringLiteral("Renamed"));
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("a")));
        QVERIFY(!modified());

        // Discard: changes are dropped and the new project replaces them.
        model()->setData(model()->index(1, TagTableModel::Name), QStringLiteral("Other"));
        d.answers << QMessageBox::Discard;
        win->actions().newProject->trigger();
        QCOMPARE(model()->rowCount(), 0);
        QVERIFY(!modified());

        // Open with unsaved changes asks too (Cancel keeps the project, dialog never opens).
        model()->addTag();
        QVERIFY(modified());
        d.answers << QMessageBox::Cancel;
        const int openCalls = d.openCaptions.size();
        win->actions().openProject->trigger();
        QCOMPARE(d.openCaptions.size(), openCalls);
        QCOMPARE(model()->rowCount(), 1);

        // A cancelled file dialog does nothing.
        d.answers << QMessageBox::Discard;
        d.openNames << QString();
        win->actions().openProject->trigger();
        QCOMPARE(model()->rowCount(), 1);
        noUnexpected();
    }

    void corruptAndUnwritableFiles()
    {
        QFile bad(P(QStringLiteral("corrupt.mbproj")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ this is not json");
        bad.close();

        d.openNames << bad.fileName();
        win->actions().openProject->trigger();
        QCOMPARE(d.warnings.size(), 1);
        QVERIFY(d.warnings.first().startsWith(QStringLiteral("Open project")));
        QCOMPARE(model()->rowCount(), 11); // unchanged
        d.warnings.clear();

        QVERIFY(!ctl->openProject(P(QStringLiteral("does-not-exist.mbproj"))));
        QCOMPARE(d.warnings.size(), 1);
        d.warnings.clear();

        // Saving into a folder that does not exist fails with a warning and keeps the flag.
        model()->addTag();
        QVERIFY(modified());
        d.saveNames << P(QStringLiteral("nodir/x.mbproj"));
        win->actions().saveProjectAs->trigger();
        QCOMPARE(d.warnings.size(), 1);
        QVERIFY(d.warnings.first().startsWith(QStringLiteral("Save project")));
        QVERIFY(modified());
        d.warnings.clear();

        // Same for a session file.
        d.saveNames << P(QStringLiteral("nodir/x.mbsession"));
        win->actions().saveSession->trigger();
        QCOMPARE(d.warnings.size(), 1);
        QVERIFY(d.warnings.first().startsWith(QStringLiteral("Save session")));
        d.warnings.clear();

        d.openNames << bad.fileName();
        d.answers << QMessageBox::Discard; // unsaved tag; nothing recorded, so no second question
        win->actions().openSession->trigger();
        QCOMPARE(d.warnings.size(), 1);
        QVERIFY(d.warnings.first().startsWith(QStringLiteral("Open session")));
        d.warnings.clear();
        noUnexpected();
    }

    void exampleProjectsOpen()
    {
        QVERIFY(ctl->openProject(QStringLiteral(MC_EXAMPLES_DIR "/demo.mbproj")));
        QVERIFY(model()->rowCount() > 0);
        QVERIFY(!modified());
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("demo")));
        QVERIFY(ctl->openProject(QStringLiteral(MC_EXAMPLES_DIR "/cm50.mbproj")));
        QVERIFY(model()->rowCount() > 0);
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("cm50")));
        noUnexpected();
    }

    void modifiedFlagAndRoundTrip()
    {
        const QString file = P(QStringLiteral("flags.mbproj"));
        QVERIFY(!modified());

        // First change, then Save asks for a name.
        auto spins = win->connectionBar()->findChildren<QSpinBox *>();
        QVERIFY(spins.size() >= 4);
        spins.at(0)->setValue(1503); // port
        QVERIFY(modified());
        d.saveNames << file;
        win->actions().saveProject->trigger();
        QVERIFY(!modified());
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("flags")));

        const std::function<void()> edits[] = {
            [&] { model()->setData(model()->index(2, TagTableModel::Name), QStringLiteral("Edited")); },
            [&] { win->actions().oneBasedAddresses->setChecked(true); },
            [&] {
                auto *guide = win->analysisView()->findChild<QCheckBox *>(QStringLiteral("guideLinesCheck"));
                QVERIFY(guide);
                guide->click();
            },
            [&] {
                QMenu *menu = win->tagTable()->headerMenu();
                QAction *a = menu->findChild<QAction *>(
                    QStringLiteral("columnAction_%1").arg(int(TagTableModel::Offset)));
                QVERIFY(a);
                a->trigger();
                delete menu;
            },
            [&] { spins.at(1)->setValue(7); }, // unit id
        };
        for (const auto &edit : edits) {
            edit();
            QVERIFY(modified());
            QVERIFY(win->isWindowModified());
            win->actions().saveProject->trigger(); // known path: no dialog
            QVERIFY(!modified());
        }
        QCOMPARE(model()->addressBase(), 1);

        const bool guide = win->analysisView()->settings().guideLines;
        const QStringList hidden = win->tagTable()->hiddenColumns();
        stopApp();
        startApp();
        QVERIFY(ctl->openProject(file));
        QCOMPARE(model()->tagAt(2).name, QStringLiteral("Edited"));
        QCOMPARE(model()->addressBase(), 1);
        QVERIFY(win->actions().oneBasedAddresses->isChecked());
        QCOMPARE(win->analysisView()->settings().guideLines, guide);
        QCOMPARE(win->tagTable()->hiddenColumns(), hidden);
        QCOMPARE(win->connectionBar()->settings().port, quint16(1503));
        QCOMPARE(win->connectionBar()->settings().unitId, 7);
        QVERIFY(!modified());
        noUnexpected();
    }

    // ---- live connection -----------------------------------------------------------------

    void connectAndPoll()
    {
        connectLive();
        QVERIFY(ctl->m_writes->m_connected);
        QCOMPARE(connectButton()->text(), QStringLiteral("Disconnect"));
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 5, 8000);
        const int sine = model()->rowOf(1);
        QTRY_VERIFY_WITH_TIMEOUT(
            model()->data(model()->index(sine, TagTableModel::Value), Qt::EditRole).isValid(), 5000);
        QVERIFY(model()->goodValue(1).has_value());
        QVERIFY(!modified()); // connecting is not a project change
        // Live values land in the plot too (refresh timer).
        QTRY_VERIFY_WITH_TIMEOUT(pointsIn(win->trendPlot()->plot()) > 0, 3000);

        // A tag edit while connected re-pushes the tags to the worker.
        model()->setData(model()->index(0, TagTableModel::Name), QStringLiteral("Sine2"));
        QVERIFY(modified());
        const qsizetype before = ctl->m_recorder.size();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() > before + 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_tagsApplied == ctl->m_tagsGeneration, 3000);

        disconnectLive();
        QVERIFY(!ctl->m_writes->m_connected);
        QVERIFY(!model()->data(model()->index(sine, TagTableModel::Value), Qt::EditRole).isValid());
        QVERIFY(connectButton()->text() == QStringLiteral("Connect"));
        QVERIFY(ctl->m_recorder.size() > 0); // the recording is kept

        // Loading a project while connected disconnects first.
        connectLive();
        d.answers << QMessageBox::Discard; // modified (renamed tag)
        win->actions().newProject->trigger();
        waitState(ConnectionState::Disconnected);
        QCOMPARE(model()->rowCount(), 0);
        noUnexpected();
    }

    void writes()
    {
        const int sp = model()->rowOf(8);
        const QModelIndex spValue = model()->index(sp, TagTableModel::Value);
        QSignalSpy sent(ctl->m_writes, &WriteController::writeRegisters);

        // Not connected: only a status message, no dialog.
        QVERIFY(model()->setData(spValue, 100.0));
        QCOMPARE(sent.count(), 0);
        QVERIFY(d.confirmTexts.isEmpty());

        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(8).has_value(), 5000);

        // Not writable: refused by the model.
        QVERIFY(!model()->setData(model()->index(model()->rowOf(1), TagTableModel::Value), 1.0));

        // Confirmed write comes back through the poll.
        d.confirms << true;
        QVERIFY(model()->setData(spValue, 500.0));
        QCOMPARE(d.confirmTexts.size(), 1);
        QVERIFY(d.confirmTexts.first().contains(QStringLiteral("Setpoint")));
        QCOMPARE(sent.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(8) && qFuzzyCompare(*model()->goodValue(8), 500.0), 5000);

        // Declined: nothing is sent.
        d.confirms << false;
        QVERIFY(model()->setData(spValue, 700.0));
        QCOMPARE(sent.count(), 1);
        QTest::qWait(400);
        QCOMPARE(*model()->goodValue(8), 500.0);

        // Outside the write limits (0..2000): warning, no confirmation, no write.
        QVERIFY(model()->setData(spValue, 5000.0));
        QCOMPARE(d.writeWarnings.size(), 1);
        QVERIFY(d.writeWarnings.first().contains(QStringLiteral("accepts")));
        d.writeWarnings.clear();
        QCOMPARE(sent.count(), 1);
        QCOMPARE(d.confirmTexts.size(), 2);

        // Quick write: confirmed with "don't ask again", then the next one is silent.
        d.quickAnswers << WriteController::QuickAnswer{true, true};
        ctl->m_writes->requestQuickWrite(8, 300.0);
        QCOMPARE(sent.count(), 2);
        QVERIFY(!ctl->m_writes->quickConfirm());
        QVERIFY(!win->actions().confirmQuickWrites->isChecked());
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(8) && qFuzzyCompare(*model()->goodValue(8), 300.0), 5000);
        ctl->m_writes->requestQuickWrite(8, 400.0);
        QCOMPARE(sent.count(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(8) && qFuzzyCompare(*model()->goodValue(8), 400.0), 5000);
        win->actions().confirmQuickWrites->setChecked(true);
        QVERIFY(ctl->m_writes->quickConfirm());

        // Back to 0 so later tests (the server is restarted anyway) see a clean setpoint.
        d.confirms << true;
        model()->setData(spValue, 0.0);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(8) && qFuzzyIsNull(*model()->goodValue(8)), 5000);
        noUnexpected();
    }

    void fc06ButtonReconnects()
    {
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 3, 5000);
        WriteController *w = ctl->m_writes;
        QVERIFY(!w->singleRegisterWrites());
        QVERIFY(!modified());

        const ModbusResult illegal{ModbusError::Exception, 1, QStringLiteral("Illegal function")};

        // The user closes the box: nothing changes.
        w->m_pending.insert(900, {11});
        d.offers << false;
        w->onWriteFinished(900, illegal);
        QVERIFY(!w->singleRegisterWrites());
        QVERIFY(!win->connectionBar()->settings().singleRegisterWrites);
        QVERIFY(!modified());

        // "Use FC06 only and reconnect".
        QSignalSpy requested(w, &WriteController::singleRegisterWritesRequested);
        w->m_pending.insert(901, {11});
        d.offers << true;
        w->onWriteFinished(901, illegal);
        QCOMPARE(requested.count(), 1);
        QVERIFY(win->connectionBar()->settings().singleRegisterWrites);
        QVERIFY(w->singleRegisterWrites());
        QVERIFY(modified());
        QSettings q;
        QVERIFY(q.value(QStringLiteral("connection/lastSingleRegisterWrites")).toBool());

        // Reconnected with the new setting and still polling.
        waitState(ConnectionState::Connected);
        const qsizetype n = ctl->m_recorder.size();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() > n + 2, 8000);

        // Already in FC06 mode: no second offer. A 16-bit failure never offers it either.
        w->m_pending.insert(902, {11});
        w->onWriteFinished(902, illegal);
        w->m_pending.insert(903, {8});
        w->onWriteFinished(903, ModbusResult{ModbusError::Timeout, 0, QStringLiteral("Timed out")});
        w->m_pending.insert(904, {8});
        w->onWriteFinished(904, ModbusResult{});
        QCOMPARE(requested.count(), 1);

        // The signal alone (as the dialog would emit it) also works when disconnected.
        disconnectLive();
        ConnectionSettings s = win->connectionBar()->settings();
        s.singleRegisterWrites = false;
        win->connectionBar()->setSettings(s);
        emit w->singleRegisterWritesRequested();
        QVERIFY(win->connectionBar()->settings().singleRegisterWrites);
        noUnexpected();
    }

    void clearDataPrompt()
    {
        feedStep();
        QVERIFY(ctl->m_recorder.size() > 0);

        d.answers << QMessageBox::No;
        emit win->trendPlot()->clearDataRequested();
        QCOMPARE(d.questions.size(), 1);
        QTest::qWait(200);
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301));
        QVERIFY(pointsIn(win->trendPlot()->plot()) > 0);

        win->trendPlot()->cursors()->setC1(3.0);
        d.answers << QMessageBox::Yes;
        emit win->trendPlot()->clearDataRequested();
        QTRY_COMPARE_WITH_TIMEOUT(ctl->m_recorder.size(), qsizetype(0), 3000);
        QCOMPARE(pointsIn(win->trendPlot()->plot()), 0);
        QVERIFY(!win->trendPlot()->cursors()->hasC1());

        // Empty recording: no question.
        emit win->trendPlot()->clearDataRequested();
        QTest::qWait(100);
        QCOMPARE(d.questions.size(), 2);
        noUnexpected();
    }

    // ---- exports -------------------------------------------------------------------------

    void exportImageAndCsv()
    {
        feedStep();
        win->trendPlot()->cursors()->setC1(4.0);
        win->trendPlot()->cursors()->setC2(12.0);

        // Image export of the Trend: accepted.
        const QString png = P(QStringLiteral("trend.png"));
        d.onExec = [&](QDialog &dlg) {
            dlg.findChild<QLineEdit *>(QStringLiteral("exportPath"))->setText(png);
            dlg.findChild<QSpinBox *>(QStringLiteral("exportWidth"))->setValue(300);
            dlg.findChild<QSpinBox *>(QStringLiteral("exportHeight"))->setValue(200);
            dlg.accept();
        };
        win->actions().exportImage->trigger();
        QCOMPARE(d.execCount, 1);
        QVERIFY(QFileInfo(png).size() > 0);

        // Analysis tab active: its plot is exported; rejected this time.
        win->setAnalysisTabActive(true);
        QVERIFY(win->isAnalysisTabActive());
        d.onExec = [](QDialog &dlg) { dlg.reject(); };
        win->actions().exportImage->trigger();
        QCOMPARE(d.execCount, 2);
        win->setAnalysisTabActive(false);

        // CSV with the cursor range available.
        const QString csv = P(QStringLiteral("trend.csv"));
        d.onExec = [&](QDialog &dlg) {
            dlg.findChild<QLineEdit *>()->setText(csv);
            dlg.accept();
        };
        win->actions().exportCsv->trigger();
        QCOMPARE(d.execCount, 3);
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("Setpoint"));
        QVERIFY(text.count('\n') >= 100);
        f.close();

        // Without cursors, cancelled.
        win->trendPlot()->cursors()->clear();
        d.onExec = [](QDialog &dlg) { dlg.reject(); };
        win->actions().exportCsv->trigger();
        QCOMPARE(d.execCount, 4);
        noUnexpected();
    }

    void exportAnalysisResults()
    {
        // Nothing to export yet: no dialog even when called directly.
        QVERIFY(!win->actions().exportAnalysisCsv->isEnabled());
        win->actions().exportAnalysisCsv->trigger(); // disabled action: no-op
        QVERIFY(d.saveCaptions.isEmpty());

        feedStep();
        win->analysisView()->setCursors(9.0, 29.0);
        bool any = false;
        for (const StepResult &r : win->analysisView()->results())
            any = any || r.valid;
        QVERIFY2(any, "the synthetic step produced no valid analysis result");
        QVERIFY(win->actions().exportAnalysisCsv->isEnabled());

        // "Copy cursors from Trend": needs cursors there first.
        win->analysisView()->setCursors(std::nullopt, std::nullopt);
        emit win->analysisView()->copyCursorsRequested(); // none on the trend: status message only
        win->trendPlot()->cursors()->setC1(9.0);
        win->trendPlot()->cursors()->setC2(29.0);
        emit win->analysisView()->copyCursorsRequested();
        QVERIFY(win->analysisView()->trendPlot()->cursors()->hasC1());
        QVERIFY(win->analysisView()->trendPlot()->cursors()->hasC2());
        QVERIFY(win->actions().exportAnalysisCsv->isEnabled());

        // Cancelled dialog.
        d.saveNames << QString();
        win->actions().exportAnalysisCsv->trigger();
        QCOMPARE(d.saveCaptions.size(), 1);

        // Written.
        const QString out = P(QStringLiteral("analysis.csv"));
        d.saveNames << out;
        emit win->analysisView()->exportResultsRequested();
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("Lag"));
        QVERIFY(d.warnings.isEmpty());

        // Failure: the target is a directory / lives in a missing folder.
        d.saveNames << P(QStringLiteral("missing/analysis.csv"));
        win->actions().exportAnalysisCsv->trigger();
        QCOMPARE(d.warnings.size(), 1);
        QVERIFY(d.warnings.first().startsWith(QStringLiteral("Export analysis results")));
        d.warnings.clear();
        noUnexpected();
    }

    // ---- sessions ------------------------------------------------------------------------

    void sessionSaveAndOpen()
    {
        const QString file = makeSessionFile(QStringLiteral("s1"));
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("s1.mbsession")));
        QVERIFY(!win->isWindowModified());

        // Fresh application, as after a restart: open the session (regression for the 1.2
        // bug where the plots stayed empty).
        restart();
        QCOMPARE(ctl->m_recorder.size(), qsizetype(0));
        d.openNames << file;
        win->actions().openSession->trigger();
        QVERIFY(d.unexpected.isEmpty());
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301));
        QVERIFY(ctl->m_sessionLoaded);
        QVERIFY(!win->isWindowModified());
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("s1.mbsession")));
        QVERIFY(pointsIn(win->trendPlot()->plot()) > 0);
        QVERIFY(pointsIn(win->analysisView()->trendPlot()->plot()) > 0);
        QVERIFY(win->trendPlot()->cursors()->hasC1());
        QVERIFY(win->trendPlot()->cursors()->hasC2());
        QVERIFY(qAbs(win->trendPlot()->cursors()->c1() - 5.0) < 1e-6);
        QVERIFY(qAbs(win->trendPlot()->cursors()->c2() - 20.0) < 1e-6);
        QVERIFY(win->analysisView()->trendPlot()->cursors()->hasC1());
        QVERIFY(win->analysisView()->trendPlot()->cursors()->hasC2());
        QVERIFY(qAbs(win->analysisView()->trendPlot()->cursors()->c1() - 9.0) < 1e-6);
        QVERIFY(!win->analysisView()->results().isEmpty());
        QVERIFY(win->actions().exportAnalysisCsv->isEnabled());
        QCOMPARE(model()->rowCount(), 11); // the session's project replaced the tags

        // Save again under the same loaded session: not dirty, name from the session.
        d.saveNames << P(QStringLiteral("s2"));
        win->actions().saveSession->trigger();
        QVERIFY(QFileInfo::exists(P(QStringLiteral("s2.mbsession"))));
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("s2.mbsession")));

        // Cancelled save dialog.
        d.saveNames << QString();
        win->actions().saveSession->trigger();
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("s2.mbsession")));
        noUnexpected();
    }

    void openSessionWithUnsavedRecording()
    {
        const QString file = makeSessionFile(QStringLiteral("s3"));
        feedStep(); // an unsaved recording again (not a loaded session)
        ctl->m_sessionLoaded = false;

        // Cancel: nothing opened, no file dialog.
        d.answers << QMessageBox::Cancel;
        win->actions().openSession->trigger();
        QVERIFY(d.openCaptions.isEmpty());
        QVERIFY(!ctl->m_sessionLoaded);

        // Save, but the save dialog is cancelled: aborts.
        d.answers << QMessageBox::Save;
        d.saveNames << QString();
        win->actions().openSession->trigger();
        QVERIFY(d.openCaptions.isEmpty());

        // Save and open.
        d.answers << QMessageBox::Save;
        d.saveNames << P(QStringLiteral("s3b"));
        d.openNames << file;
        win->actions().openSession->trigger();
        QVERIFY(QFileInfo::exists(P(QStringLiteral("s3b.mbsession"))));
        QVERIFY(ctl->m_sessionLoaded);

        // Discard and open again (the loaded session needs no question this time).
        restart();
        feedStep();
        d.answers << QMessageBox::Discard;
        d.openNames << file;
        win->actions().openSession->trigger();
        QVERIFY(ctl->m_sessionLoaded);
        QVERIFY(pointsIn(win->trendPlot()->plot()) > 0);

        // A cancelled file dialog keeps the loaded session.
        d.openNames << QString();
        win->actions().openSession->trigger();
        QVERIFY(ctl->m_sessionLoaded);
        noUnexpected();
    }

    void continueRecording()
    {
        const QString file = makeSessionFile(QStringLiteral("c1"));
        restart();
        d.openNames << file;
        win->actions().openSession->trigger();
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301));
        const double oldLast = ctl->m_recorder.timeAt(300);

        // Cancel: no connection is attempted.
        d.continueChoices << CC::Cancel;
        QVERIFY(m_server.start());
        {
            ConnectionSettings s = win->connectionBar()->settings();
            s.host = QStringLiteral("127.0.0.1");
            s.port = m_server.port();
            s.pollPeriodMs = 100;
            win->connectionBar()->setSettings(s);
        }
        connectButton()->click();
        QTest::qWait(500);
        QVERIFY(ctl->m_state == ConnectionState::Disconnected);
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301));
        QVERIFY(ctl->m_sessionLoaded);

        // Continue: old rows stay, a gap row follows, then new rows with later times.
        d.continueChoices << CC::Continue;
        connectButton()->click();
        waitState(ConnectionState::Connected);
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 301 + 1 + 4, 8000);
        QCOMPARE(ctl->m_recorder.timeAt(300), oldLast);
        QVERIFY(ctl->m_recorder.timeAt(301) > oldLast);
        QVERIFY(std::isnan(ctl->m_recorder.valueAt(301, 7)));
        QVERIFY(std::isnan(ctl->m_recorder.valueAt(301, 8)));
        for (qsizetype i = 302; i < ctl->m_recorder.size(); ++i) {
            QVERIFY(ctl->m_recorder.timeAt(i) > ctl->m_recorder.timeAt(301));
            QVERIFY(ctl->m_recorder.timeAt(i) >= ctl->m_recorder.timeAt(i - 1));
        }
        QVERIFY(std::isfinite(ctl->m_recorder.valueAt(ctl->m_recorder.size() - 1, 1)));
        QVERIFY(ctl->m_timeOffset > oldLast);
        QVERIFY(ctl->m_sessionLoaded);
        QVERIFY(ctl->m_sessionDirty);
        QVERIFY(win->isWindowModified()); // new rows after loading

        disconnectLive();

        // New project with an unsaved recording asks to save the session.
        d.answers << QMessageBox::Cancel;
        win->actions().newProject->trigger();
        QCOMPARE(model()->rowCount(), 11);
        QVERIFY(d.questions.last().contains(QStringLiteral("recording")));
        d.answers << QMessageBox::Save;
        d.saveNames << QString();
        win->actions().newProject->trigger();
        QCOMPARE(model()->rowCount(), 11);

        // Start a new recording is refused while the old one is unsaved and the user cancels.
        d.continueChoices << CC::StartNew;
        d.answers << QMessageBox::Cancel;
        connectButton()->click();
        QTest::qWait(400);
        QVERIFY(ctl->m_state == ConnectionState::Disconnected);
        QVERIFY(ctl->m_sessionLoaded);

        // ... and goes ahead after Discard: the recording starts over from t = 0.
        d.continueChoices << CC::StartNew;
        d.answers << QMessageBox::Discard;
        connectButton()->click();
        waitState(ConnectionState::Connected);
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 3, 8000);
        QVERIFY(!ctl->m_sessionLoaded);
        QVERIFY(ctl->m_recorder.timeAt(0) < 5.0);
        QVERIFY(ctl->m_recorder.size() < 300);
        QCOMPARE(ctl->m_timeOffset, 0.0);
        QVERIFY(win->windowTitle().startsWith(QStringLiteral("Untitled")));
        disconnectLive();
        noUnexpected();
    }

    void startNewRecording()
    {
        const QString file = makeSessionFile(QStringLiteral("n1"));
        restart();
        d.openNames << file;
        win->actions().openSession->trigger();
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301));

        d.continueChoices << CC::StartNew;
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 3, 8000);
        QVERIFY(!ctl->m_sessionLoaded);
        QVERIFY(ctl->m_recorder.timeAt(0) < 5.0);
        QCOMPARE(ctl->m_recorder.capacity(), qsizetype(72000));
        QVERIFY(!win->isWindowModified());
        disconnectLive();
        noUnexpected();
    }

    void openSessionWhileConnectedDropsLiveSamples()
    {
        const QString file = makeSessionFile(QStringLiteral("w1"));
        ctl->m_recorder.clear();
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 3, 8000);

        d.answers << QMessageBox::Discard; // unsaved live recording
        d.openNames << file;
        win->actions().openSession->trigger();
        QVERIFY(ctl->m_sessionLoaded);
        waitState(ConnectionState::Disconnected);
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301));
        QTest::qWait(500);
        QCOMPARE(ctl->m_recorder.size(), qsizetype(301)); // nothing appended after loading
        QVERIFY(!win->isWindowModified());
        QVERIFY(pointsIn(win->trendPlot()->plot()) > 0);
        noUnexpected();
    }

    void liveSessionRoundTrip()
    {
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 20, 10000);
        disconnectLive();
        const qsizetype rows = ctl->m_recorder.size();
        win->trendPlot()->cursors()->setC1(ctl->m_recorder.timeAt(3));
        win->trendPlot()->cursors()->setC2(ctl->m_recorder.timeAt(12));

        d.saveNames << P(QStringLiteral("live"));
        win->actions().saveSession->trigger();
        const QString file = P(QStringLiteral("live.mbsession"));
        QVERIFY(QFileInfo::exists(file));

        restart();
        d.openNames << file;
        win->actions().openSession->trigger();
        QCOMPARE(ctl->m_recorder.size(), rows);
        QVERIFY(pointsIn(win->trendPlot()->plot()) > 0);
        QVERIFY(pointsIn(win->analysisView()->trendPlot()->plot()) > 0);
        QVERIFY(win->trendPlot()->cursors()->hasC1());
        QVERIFY(win->trendPlot()->cursors()->hasC2());
        noUnexpected();
    }

    void closeHandler()
    {
        QVERIFY(!win->isWindowModified());
        model()->addTag();
        QVERIFY(modified());
        // The window's close handler is maybeSave().
        d.answers << QMessageBox::Cancel;
        QVERIFY(!win->close());
        d.answers << QMessageBox::Discard;
        QVERIFY(win->close());
        noUnexpected();
    }
};

QTEST_MAIN(TstAppController)
#include "tst_appcontroller.moc"
