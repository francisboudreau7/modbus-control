#include "AddTagDialog.h"
#include "AnalysisPanel.h"
#include "AppController.h"
#include "ConnectionBar.h"
#include "CursorPanel.h"
#include "EncodingDialog.h"
#include "EncodingMonitor.h"
#include "MainWindow.h"
#include "MetaTypes.h"
#include "ModbusConnection.h"
#include "PlotCursors.h"
#include "QuickWritePanel.h"
#include "Recorder.h"
#include "SessionIO.h"
#include "TagTableModel.h"
#include "TagTableWidget.h"
#include "TrendPlot.h"
#include "WriteController.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
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
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTableWidget>
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

    bool start(const QStringList &extraArgs = {})
    {
        stop();
        m_log.clear();
        m_buffer.clear();
        m_port = 0;
        const QStringList args = QStringList{QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                                             QStringLiteral("--port"), QStringLiteral("0")}
                                 + extraArgs;
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
        QList<bool> implausible;                    ///< m_warnImplausible
        QList<WriteController::EncodingAnswer> encodingAnswers; ///< m_confirmEncoding
        QStringList encodingPrompts;

        QStringList encodingDialogs;                ///< EncodingMonitor::m_runDialog (window titles)
        std::function<bool(EncodingDialog &)> onEncodingDialog; ///< unset: unexpected, cancelled
        QStringList encodingReviews;                ///< EncodingMonitor::m_reviewPrompt texts
        bool allowEncodingReview = false;           ///< otherwise a review prompt is unexpected
        QStringList splitTexts;                     ///< EncodingMonitor::m_splitPrompt texts
        QStringList splitChanges;                   ///< ... the Change labels
        QStringList splitKeeps;                     ///< ... the Keep labels
        QList<EncodingMonitor::SplitAnswer> splitAnswers; ///< empty: unexpected, Later

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
        w->m_confirmEncoding = [this](const QString &text) {
            d.encodingPrompts << text;
            if (d.encodingAnswers.isEmpty()) {
                d.unexpected << QStringLiteral("confirm encoding: ") + text;
                return WriteController::EncodingAnswer::Cancel;
            }
            return d.encodingAnswers.takeFirst();
        };
        w->m_askValues = [this](const QList<Tag> &, const QList<double> &)
            -> std::optional<QList<double>> {
            d.unexpected << QStringLiteral("askValues");
            return std::nullopt;
        };

        ctl->m_encoding->m_runDialog = [this](EncodingDialog &dialog) {
            d.encodingDialogs << dialog.windowTitle();
            if (!d.onEncodingDialog) {
                d.unexpected << QStringLiteral("encoding dialog: ") + dialog.windowTitle();
                return false;
            }
            return d.onEncodingDialog(dialog);
        };
        ctl->m_encoding->m_reviewPrompt = [this](const QString &text) {
            d.encodingReviews << text;
            if (!d.allowEncodingReview)
                d.unexpected << QStringLiteral("encoding review: ") + text;
            return false;
        };
        ctl->m_encoding->m_splitPrompt = [this](const QString &text, const QString &change,
                                                const QString &keep) {
            d.splitTexts << text;
            d.splitChanges << change;
            d.splitKeeps << keep;
            if (d.splitAnswers.isEmpty()) {
                d.unexpected << QStringLiteral("split prompt: ") + text;
                return EncodingMonitor::SplitAnswer::Later;
            }
            return d.splitAnswers.takeFirst();
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

    /// Starts the test server (once) and presses Connect. `strict32`: the server refuses reads and
    /// writes that split a 32-bit value (--strict-32bit, like an ABB CM50).
    void connectLive(bool strict32 = false)
    {
        if (m_server.port() == 0) {
            QVERIFY2(m_server.start(strict32 ? QStringList{QStringLiteral("--strict-32bit")}
                                             : QStringList{}),
                     qPrintable(m_server.log()));
        }
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
        win->analysisPanel()->resetData();
        win->trendPlot()->refresh();
        win->analysisPanel()->refresh();
        win->cursorPanel()->refresh();
    }

    /// feedStep() + cursors + File > Save session to P(name).mbsession; returns the path.
    QString makeSessionFile(const QString &name)
    {
        feedStep();
        win->trendPlot()->cursors()->setC1(5.0);
        win->trendPlot()->cursors()->setC2(20.0);
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

    // ---- Add tag dialog ----------------------------------------------------------------------

    QPushButton *addButton() const
    {
        const auto buttons = win->tagTable()->findChildren<QPushButton *>();
        for (QPushButton *b : buttons)
            if (b->text() == QStringLiteral("Add"))
                return b;
        return nullptr;
    }

    static void chooseTarget(AddTagDialog &dlg, int areaIndex, const QString &address)
    {
        dlg.findChild<QComboBox *>(QStringLiteral("areaCombo"))->setCurrentIndex(areaIndex);
        dlg.findChild<QLineEdit *>(QStringLiteral("addressEdit"))->setText(address);
    }

    /// Detect in the dialog, waits for the answer, accepts the EncodingDialog with 52.3.
    static void detectAndWait(AddTagDialog &dlg)
    {
        dlg.m_runEncodingDialog = [](EncodingDialog &enc) {
            enc.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        auto *detect = dlg.findChild<QPushButton *>(QStringLiteral("detectButton"));
        QTRY_VERIFY_WITH_TIMEOUT(detect->isEnabled(), 5000);
        detect->click();
        auto *label = dlg.findChild<QLabel *>(QStringLiteral("detectLabel"));
        QTRY_VERIFY_WITH_TIMEOUT(!label->text().isEmpty(), 8000);
    }

    void addTagOpensDialogAndCancelAddsNothing()
    {
        const int rows = model()->rowCount();
        bool sawDialog = false;
        d.onExec = [&](QDialog &dialog) {
            sawDialog = qobject_cast<AddTagDialog *>(&dialog) != nullptr;
            dialog.reject();
        };
        QVERIFY(addButton());
        addButton()->click();
        QVERIFY(sawDialog);
        QCOMPARE(d.execCount, 1);
        QCOMPARE(model()->rowCount(), rows);
        noUnexpected();
    }

    void addTagAcceptedAddsTheTagOffline()
    {
        const int rows = model()->rowCount();
        d.onExec = [](QDialog &dialog) {
            auto *dlg = qobject_cast<AddTagDialog *>(&dialog);
            QVERIFY(dlg);
            chooseTarget(*dlg, 0, QStringLiteral("300"));
            dlg->findChild<QLineEdit *>(QStringLiteral("nameEdit"))->setText(QStringLiteral("Mine"));
            dlg->findChild<QLineEdit *>(QStringLiteral("unitEdit"))->setText(QStringLiteral("bar"));
            dlg->accept();
        };
        addButton()->click();
        QCOMPARE(model()->rowCount(), rows + 1);
        const Tag &t = model()->tagAt(rows);
        QCOMPARE(t.name, QStringLiteral("Mine"));
        QCOMPARE(t.unit, QStringLiteral("bar"));
        QVERIFY(t.area == Area::HoldingRegister);
        QCOMPARE(t.address, quint16(300));
        QCOMPARE(win->tagTable()->selectedRows(), QList<int>{rows});
        QVERIFY(modified());
        noUnexpected();
    }

    void addTagAtAddressZeroIsKept()
    {
        // A plain HR 0 int16 tag equals Tag(): it must not be moved after the last row.
        Tag last;
        last.area = Area::InputRegister;
        last.address = 500;
        model()->addTag(last);
        const int rows = model()->rowCount();
        d.onExec = [](QDialog &dialog) {
            auto *dlg = qobject_cast<AddTagDialog *>(&dialog);
            QVERIFY(dlg);
            chooseTarget(*dlg, 0, QStringLiteral("0"));
            dlg->accept();
        };
        addButton()->click();
        QCOMPARE(model()->rowCount(), rows + 1);
        QVERIFY(model()->tagAt(rows).area == Area::HoldingRegister);
        QCOMPARE(model()->tagAt(rows).address, quint16(0));
        noUnexpected();
    }

    void addTagRecheckedAgainstTagsChangedMeanwhile()
    {
        int rows = 0;
        d.onExec = [&](QDialog &dialog) {
            auto *dlg = qobject_cast<AddTagDialog *>(&dialog);
            QVERIFY(dlg);
            chooseTarget(*dlg, 0, QStringLiteral("700"));
            // another tag lands on HR 700 while the dialog is open
            Tag other;
            other.name = QStringLiteral("Other");
            other.address = 699;
            other.type = ValueType::Float32;
            model()->addTag(other);
            rows = model()->rowCount();
            dlg->accept();
        };
        addButton()->click();
        QCOMPARE(model()->rowCount(), rows);
        QCOMPARE(d.warnings.size(), 1);
        QVERIFY2(d.warnings.first().contains(QStringLiteral("would overlap 'Other' (HR 699-700)")),
                 qPrintable(d.warnings.first()));
        d.warnings.clear();
        noUnexpected();
    }

    void addTagDetectsFloat32OnLiveServer()
    {
        connectLive();
        const int rows = model()->rowCount();
        d.onExec = [](QDialog &dialog) {
            auto *dlg = qobject_cast<AddTagDialog *>(&dialog);
            QVERIFY(dlg);
            chooseTarget(*dlg, 0, QStringLiteral("110"));
            detectAndWait(*dlg);
            QVERIFY(dlg->detectionConfirmed());
            dlg->accept();
        };
        addButton()->click();
        QCOMPARE(model()->rowCount(), rows + 1);
        const Tag &t = model()->tagAt(rows);
        QVERIFY(t.type == ValueType::Float32);
        QVERIFY(t.byteOrder == ByteOrder::CDAB);
        QCOMPARE(t.address, quint16(110));
        QVERIFY(t.encodingVerified);
        QVERIFY(!ctl->m_encoding->isPending(t.id));
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(t.id) && qAbs(*model()->goodValue(t.id) - 52.3) < 0.01,
                                 8000);
        QVERIFY(d.encodingDialogs.isEmpty()); // no second dialog from the monitor
        QVERIFY(d.splitTexts.isEmpty());
        QVERIFY(ctl->m_addTagReads.isEmpty());
        noUnexpected();
    }

    void addTagDetectsFloat32OnStrictServer()
    {
        connectLive(true);
        const int rows = model()->rowCount();
        d.onExec = [](QDialog &dialog) {
            auto *dlg = qobject_cast<AddTagDialog *>(&dialog);
            QVERIFY(dlg);
            chooseTarget(*dlg, 0, QStringLiteral("110"));
            detectAndWait(*dlg);
            QVERIFY(dlg->detectionConfirmed());
            dlg->accept();
        };
        addButton()->click();
        QCOMPARE(model()->rowCount(), rows + 1);
        const Tag &t = model()->tagAt(rows);
        QVERIFY(t.type == ValueType::Float32);
        QVERIFY(t.byteOrder == ByteOrder::CDAB);
        QCOMPARE(t.address, quint16(110));
        QVERIFY(t.encodingVerified);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(t.id) && qAbs(*model()->goodValue(t.id) - 52.3) < 0.01,
                                 8000);
        QVERIFY(d.encodingDialogs.isEmpty());
        QVERIFY(d.splitTexts.isEmpty());
        noUnexpected();
    }

    void addTagConnectionLossDoesNotLeakReads()
    {
        connectLive();
        d.onExec = [this](QDialog &dialog) {
            auto *dlg = qobject_cast<AddTagDialog *>(&dialog);
            QVERIFY(dlg);
            chooseTarget(*dlg, 0, QStringLiteral("110"));
            auto *read = dlg->findChild<QPushButton *>(QStringLiteral("readButton"));
            QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
            read->click();
            disconnectLive();
            QTRY_VERIFY_WITH_TIMEOUT(!read->isEnabled(), 5000);
            dlg->reject();
        };
        addButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_addTagReads.isEmpty(), 5000);
        noUnexpected();
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
                // A user edit in the Analysis panel (setSettings does not emit settingsChanged).
                AnalysisSettings a = win->analysisPanel()->settings();
                a.guideLines = !a.guideLines;
                win->analysisPanel()->setSettings(a);
                emit win->analysisPanel()->settingsChanged();
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

        const bool guide = win->analysisPanel()->settings().guideLines;
        const QStringList hidden = win->tagTable()->hiddenColumns();
        stopApp();
        startApp();
        QVERIFY(ctl->openProject(file));
        QCOMPARE(model()->tagAt(2).name, QStringLiteral("Edited"));
        QCOMPARE(model()->addressBase(), 1);
        QVERIFY(win->actions().oneBasedAddresses->isChecked());
        QCOMPARE(win->analysisPanel()->settings().guideLines, guide);
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

    /// Writes `value` as float32 CDAB to HR 150-151 through a verified writable tag and waits
    /// until the poll reads it back. The demo sines are too steep for a 3-reading detection.
    void storeFloatCdab(double value, TagId *idOut = nullptr)
    {
        Tag w;
        w.name = QStringLiteral("Stored");
        w.area = Area::HoldingRegister;
        w.address = 150;
        w.type = ValueType::Float32;
        w.byteOrder = ByteOrder::CDAB;
        w.writable = true;
        const TagId id = model()->tagAt(model()->addTag(w)).id;
        if (idOut)
            *idOut = id;
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id).has_value(), 8000); // a 32-bit write needs a live value
        // A new tag on registers that read 0 cannot be detected: it stays unconfirmed, and the
        // write asks to confirm its encoding first.
        QVERIFY(!model()->tagAt(model()->rowOf(id)).encodingVerified);
        d.encodingAnswers << WriteController::EncodingAnswer::Confirm;
        d.confirms << true;
        QVERIFY(model()->setData(model()->index(model()->rowOf(id), TagTableModel::Value), value));
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id) && qAbs(*model()->goodValue(id) - value) < 0.01,
                                 8000);
    }

    void encodingDetectedOnLiveTag()
    {
        connectLive();
        TagId stored = 0;
        storeFloatCdab(52.3, &stored);
        QVERIFY(model()->tagAt(model()->rowOf(stored)).encodingVerified);
        // A new tag on the same registers that reads them as ABCD is corrected.
        Tag t;
        t.name = QStringLiteral("Probe");
        t.area = Area::HoldingRegister;
        t.address = 150;
        t.type = ValueType::Float32;
        t.byteOrder = ByteOrder::ABCD;
        const TagId id = model()->tagAt(model()->addTag(t)).id;
        QVERIFY(ctl->m_encoding->isPending(id));
        QTRY_VERIFY_WITH_TIMEOUT(model()->tagAt(model()->rowOf(id)).byteOrder == ByteOrder::CDAB, 8000);
        const Tag &now = model()->tagAt(model()->rowOf(id));
        QVERIFY(now.type == ValueType::Float32);
        QVERIFY(!now.encodingVerified);
        QVERIFY(!ctl->m_encoding->isPending(id));
        QVERIFY(modified());
        // The corrected tag reads the stored value once the worker has the new tags.
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id) && qAbs(*model()->goodValue(id) - 52.3) < 0.01,
                                 8000);
        noUnexpected();
        QVERIFY(d.encodingReviews.isEmpty());
    }

    void encodingCheckOnConnect()
    {
        connectLive();
        storeFloatCdab(52.3);
        disconnectLive();
        // A loaded project whose tag has the wrong byte order: flagged unverified on connect.
        QList<Tag> tags = model()->tags();
        Tag t;
        t.id = 40;
        t.name = QStringLiteral("Wrong order");
        t.area = Area::HoldingRegister;
        t.address = 150;
        t.type = ValueType::Float32;
        t.byteOrder = ByteOrder::ABCD;
        tags << t;
        model()->setTags(tags, 41);
        ctl->m_encoding->onTagsReplaced();
        ctl->pushTags();
        d.allowEncodingReview = true;
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(!d.encodingReviews.isEmpty(), 8000);
        const QString text = d.encodingReviews.first();
        QVERIFY2(text.contains(QStringLiteral("<li>'Wrong order' (HR ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral(" as float32 ABCD, <b>52.3</b> as float32 CDAB</li>")),
                 qPrintable(text));
        const Tag &now = model()->tagAt(model()->rowOf(40));
        QVERIFY(!now.encodingVerified);
        QVERIFY(now.byteOrder == ByteOrder::ABCD); // only flagged, not changed
        QCOMPARE(d.encodingReviews.size(), 1);
        QVERIFY(d.encodingDialogs.isEmpty());
        noUnexpected();
    }

    /// An int16 tag at `address` added while connected to the default server (float32 52.3 CDAB at
    /// HR 110-111).
    TagId addInt16(const QString &name, quint16 address)
    {
        Tag t;
        t.name = name;
        t.area = Area::HoldingRegister;
        t.address = address;
        t.type = ValueType::Int16;
        return model()->tagAt(model()->addTag(t)).id;
    }

    void splitCheckChangesToFloat()
    {
        connectLive();
        d.splitAnswers << EncodingMonitor::SplitAnswer::Change;
        const TagId id = addInt16(QStringLiteral("Setpoint"), 110);
        QVERIFY(model()->tagAt(model()->rowOf(id)).encodingVerified); // not flagged while waiting
        QTRY_VERIFY_WITH_TIMEOUT(!d.splitTexts.isEmpty(), 8000);
        const QString split = d.splitTexts.first();
        QVERIFY2(split.contains(QStringLiteral("<b>'Setpoint' is probably half of a float32 value.</b>")),
                 qPrintable(split));
        QVERIFY2(split.contains(QStringLiteral("As int16 (HR 110):</td><td>13107</td>")),
                 qPrintable(split));
        QVERIFY2(split.contains(
                     QStringLiteral("As float32 CDAB (HR 110-111):</td><td><b>52.3</b>")),
                 qPrintable(split));
        QCOMPARE(d.splitChanges.first(), QStringLiteral("Change to float32 CDAB at HR 110"));
        QCOMPARE(d.splitKeeps.first(), QStringLiteral("Keep int16"));
        const Tag &now = model()->tagAt(model()->rowOf(id));
        QVERIFY(now.type == ValueType::Float32);
        QVERIFY(now.byteOrder == ByteOrder::CDAB);
        QCOMPARE(now.address, quint16(110));
        QVERIFY(now.encodingVerified);
        QVERIFY(model()->encodingNote(id).isEmpty());
        QVERIFY(!ctl->m_encoding->isPending(id));
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id) && qAbs(*model()->goodValue(id) - 52.3) < 0.01,
                                 8000);
        QCOMPARE(d.splitTexts.size(), 1);
        QVERIFY(d.encodingDialogs.isEmpty());
        noUnexpected();
    }

    void splitCheckFlagsAndKeeps()
    {
        connectLive();
        d.splitAnswers << EncodingMonitor::SplitAnswer::Later;
        const TagId id = addInt16(QStringLiteral("Setpoint"), 110);
        QTRY_VERIFY_WITH_TIMEOUT(!d.splitTexts.isEmpty(), 8000);
        QVERIFY(!model()->tagAt(model()->rowOf(id)).encodingVerified);
        QCOMPARE(model()->encodingNote(id), QStringLiteral("HR 110-111 read as float32 CDAB give 52.3"));
        QVERIFY(model()->tagAt(model()->rowOf(id)).type == ValueType::Int16);
        // "Detect encoding..." on the flagged tag asks again; Keep verifies it.
        d.splitAnswers << EncodingMonitor::SplitAnswer::Keep;
        emit win->tagTable()->detectEncodingRequested(id);
        QTRY_COMPARE_WITH_TIMEOUT(d.splitTexts.size(), 2, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(model()->tagAt(model()->rowOf(id)).encodingVerified, 8000);
        QVERIFY(model()->encodingNote(id).isEmpty());
        QVERIFY(model()->tagAt(model()->rowOf(id)).type == ValueType::Int16);
        noUnexpected();
    }

    void splitCheckUpperHalfMovesDown()
    {
        connectLive();
        d.splitAnswers << EncodingMonitor::SplitAnswer::Change;
        const TagId id = addInt16(QStringLiteral("Hi"), 111);
        QTRY_VERIFY_WITH_TIMEOUT(!d.splitTexts.isEmpty(), 8000);
        QCOMPARE(d.splitChanges.first(), QStringLiteral("Change to float32 CDAB at HR 110"));
        const Tag &now = model()->tagAt(model()->rowOf(id));
        QCOMPARE(now.address, quint16(110));
        QVERIFY(now.type == ValueType::Float32);
        QVERIFY(now.byteOrder == ByteOrder::CDAB);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id) && qAbs(*model()->goodValue(id) - 52.3) < 0.01,
                                 8000);
        noUnexpected();
    }

    // Keep int16 is remembered in the project: reconnecting raises no new prompt.
    void splitCheckKeepSurvivesReconnect()
    {
        connectLive();
        d.splitAnswers << EncodingMonitor::SplitAnswer::Keep;
        const TagId id = addInt16(QStringLiteral("Setpoint"), 110);
        QTRY_VERIFY_WITH_TIMEOUT(!d.splitTexts.isEmpty(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(model()->tagAt(model()->rowOf(id)).confirmed16Bit, 8000);
        disconnectLive();
        connectLive();
        QTest::qWait(1500); // the on-connect pass is over after 10 batches
        QCOMPARE(d.splitTexts.size(), 1);
        QVERIFY(d.encodingReviews.isEmpty());
        QVERIFY(model()->tagAt(model()->rowOf(id)).encodingVerified);
        noUnexpected();
    }

    // Decide later: the tag stays flagged, but reconnecting in the same session does not ask again.
    void splitCheckLaterNotAskedAgainOnReconnect()
    {
        connectLive();
        d.splitAnswers << EncodingMonitor::SplitAnswer::Later;
        const TagId id = addInt16(QStringLiteral("Setpoint"), 110);
        QTRY_VERIFY_WITH_TIMEOUT(!d.splitTexts.isEmpty(), 8000);
        disconnectLive();
        connectLive();
        QTest::qWait(1500);
        QCOMPARE(d.splitTexts.size(), 1);
        QVERIFY(d.encodingReviews.isEmpty());
        QVERIFY(!model()->tagAt(model()->rowOf(id)).encodingVerified);
        noUnexpected();
    }

    void exampleProjectsRaiseNoSplitPrompt()
    {
        for (const QString &file : {QStringLiteral(MC_EXAMPLES_DIR "/demo.mbproj"),
                                    QStringLiteral(MC_EXAMPLES_DIR "/cm50.mbproj")}) {
            QVERIFY(ctl->openProject(file));
            const QList<Tag> before = model()->tags();
            connectLive();
            // Let every tag read a few times: the on-connect pass is over after 10 batches.
            QTest::qWait(1500);
            QVERIFY2(d.splitTexts.isEmpty(), qPrintable(d.splitTexts.join(QLatin1Char('\n'))));
            QVERIFY2(d.encodingReviews.isEmpty(), qPrintable(d.encodingReviews.join(QLatin1Char('\n'))));
            for (const Tag &tag : model()->tags()) {
                QVERIFY2(tag.encodingVerified, qPrintable(tag.name));
                QVERIFY2(model()->encodingNote(tag.id).isEmpty(), qPrintable(tag.name));
            }
            QVERIFY(model()->tags() == before);
            disconnectLive();
        }
        noUnexpected();
    }

    // A strict server (like a CM50) refuses an int16 tag on half of a float32: the monitor reads
    // the pair and offers the 32-bit encoding.
    void refusedReadOffersTheThirtyTwoBitEncoding()
    {
        connectLive(true);
        QStringList intros;
        d.onEncodingDialog = [&intros](EncodingDialog &dialog) {
            if (auto *intro = dialog.findChild<QLabel *>(QStringLiteral("introLabel")))
                intros << intro->text();
            dialog.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        const TagId id = addInt16(QStringLiteral("Setpoint"), 110);
        QTRY_VERIFY_WITH_TIMEOUT(!d.encodingDialogs.isEmpty(), 8000);
        QCOMPARE(intros.size(), 1);
        QVERIFY2(intros[0].contains(QStringLiteral("'Setpoint' (HR 110, int16) can't be read alone.")),
                 qPrintable(intros[0]));
        QVERIFY2(intros[0].contains(QStringLiteral("HR 110-111 can be read as a 32-bit value")),
                 qPrintable(intros[0]));
        const Tag &now = model()->tagAt(model()->rowOf(id));
        QVERIFY(now.type == ValueType::Float32);
        QVERIFY(now.byteOrder == ByteOrder::CDAB);
        QCOMPARE(now.address, quint16(110));
        QVERIFY(now.encodingVerified);
        QVERIFY(model()->encodingNote(id).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id) && qAbs(*model()->goodValue(id) - 52.3) < 0.01,
                                 8000);
        QCOMPARE(d.encodingDialogs.size(), 1);
        QVERIFY(d.splitTexts.isEmpty());
        noUnexpected();
    }

    // Polled in one block with the refused tag, a plain neighbour keeps reading and gets no dialog.
    void refusedReadDoesNotInvolveBlockMates()
    {
        connectLive(true);
        int dialogsFor = 0;
        d.onEncodingDialog = [&dialogsFor](EncodingDialog &dialog) {
            ++dialogsFor;
            dialog.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        const TagId mate = addInt16(QStringLiteral("Mate"), 100);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(mate).has_value(), 8000);
        const TagId refused = addInt16(QStringLiteral("Setpoint"), 110);
        // The neighbour reads Good all the way through.
        for (int i = 0; i < 30 && model()->tagAt(model()->rowOf(refused)).type == ValueType::Int16; ++i) {
            QVERIFY(model()->goodValue(mate).has_value());
            QTest::qWait(100);
        }
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(refused)
                                     && qAbs(*model()->goodValue(refused) - 52.3) < 0.01,
                                 8000);
        for (int i = 0; i < 10; ++i) {
            QVERIFY(model()->goodValue(mate).has_value());
            QTest::qWait(100);
        }
        QCOMPARE(dialogsFor, 1);
        QCOMPARE(d.encodingDialogs.size(), 1);
        QVERIFY(d.encodingDialogs.first().contains(QStringLiteral("Setpoint")));
        const Tag &m = model()->tagAt(model()->rowOf(mate));
        QVERIFY(m.type == ValueType::Int16);
        QCOMPARE(m.address, quint16(100));
        QVERIFY(m.encodingVerified);
        noUnexpected();
    }

    void refusedReadOfUpperHalfEndsAtTheFirstRegister()
    {
        connectLive(true);
        d.onEncodingDialog = [](EncodingDialog &dialog) {
            dialog.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        const TagId id = addInt16(QStringLiteral("Hi"), 111);
        QTRY_VERIFY_WITH_TIMEOUT(model()->tagAt(model()->rowOf(id)).type == ValueType::Float32, 8000);
        const Tag &now = model()->tagAt(model()->rowOf(id));
        QCOMPARE(now.address, quint16(110));
        QVERIFY(now.byteOrder == ByteOrder::CDAB);
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(id) && qAbs(*model()->goodValue(id) - 52.3) < 0.01,
                                 8000);
        noUnexpected();
    }

    void refusedReadCancelledStaysFlagged()
    {
        connectLive(true);
        d.onEncodingDialog = [](EncodingDialog &) { return false; };
        const TagId id = addInt16(QStringLiteral("Setpoint"), 110);
        QTRY_VERIFY_WITH_TIMEOUT(!d.encodingDialogs.isEmpty(), 8000);
        const Tag &now = model()->tagAt(model()->rowOf(id));
        QVERIFY(now.type == ValueType::Int16);
        QVERIFY(!now.encodingVerified);
        QCOMPARE(model()->encodingNote(id),
                 QStringLiteral("HR 110 can't be read alone; HR 110-111 can be read as a 32-bit value"));
        QTest::qWait(500);
        QCOMPARE(d.encodingDialogs.size(), 1);
        noUnexpected();
    }

    // The example projects map whole 32-bit values: a strict server raises nothing.
    void exampleProjectsRaiseNothingOnAStrictServer()
    {
        bool first = true;
        for (const QString &file : {QStringLiteral(MC_EXAMPLES_DIR "/demo.mbproj"),
                                    QStringLiteral(MC_EXAMPLES_DIR "/cm50.mbproj")}) {
            QVERIFY(ctl->openProject(file));
            const QList<Tag> before = model()->tags();
            connectLive(first);
            first = false;
            QTest::qWait(1500);
            QVERIFY2(d.encodingDialogs.isEmpty(), qPrintable(d.encodingDialogs.join(QLatin1Char('\n'))));
            QVERIFY2(d.splitTexts.isEmpty(), qPrintable(d.splitTexts.join(QLatin1Char('\n'))));
            QVERIFY2(d.encodingReviews.isEmpty(), qPrintable(d.encodingReviews.join(QLatin1Char('\n'))));
            QVERIFY(model()->tags() == before);
            disconnectLive();
        }
        noUnexpected();
    }

    void detectEncodingFromTagTable()
    {
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(model()->goodValue(10).has_value(), 5000);
        QString seen;
        d.onEncodingDialog = [&seen](EncodingDialog &dialog) {
            seen = dialog.windowTitle();
            return false;
        };
        emit win->tagTable()->detectEncodingRequested(10);
        QCOMPARE(d.encodingDialogs.size(), 1);
        QCOMPARE(seen, QStringLiteral("Detect encoding - Float sine (CDAB)"));
        QVERIFY(model()->tagAt(model()->rowOf(10)).encodingVerified);
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

    // A device that rejects FC16: a warning explains it; there is no FC06 fallback any more.
    void fc16RejectionWarns()
    {
        connectLive();
        QTRY_VERIFY_WITH_TIMEOUT(ctl->m_recorder.size() >= 3, 5000);
        WriteController *w = ctl->m_writes;
        const ModbusResult illegal{ModbusError::Exception, 1, QStringLiteral("Illegal function")};

        w->m_pending.insert(900, {11}); // a 32-bit tag
        w->onWriteFinished(900, illegal);
        QCOMPARE(d.writeWarnings.size(), 1);
        QVERIFY(d.writeWarnings.at(0).contains(QStringLiteral("does not accept FC16")));
        QVERIFY(d.writeWarnings.at(0).contains(QStringLiteral("Nothing was written.")));
        QVERIFY(!d.writeWarnings.at(0).contains(QStringLiteral("FC06")));
        QVERIFY(!modified());

        // A 16-bit failure, another exception, or a success never warns.
        w->m_pending.insert(901, {8});
        w->onWriteFinished(901, illegal);
        w->m_pending.insert(902, {11});
        w->onWriteFinished(902, ModbusResult{ModbusError::Timeout, 0, QStringLiteral("Timed out")});
        w->m_pending.insert(903, {8});
        w->onWriteFinished(903, ModbusResult{});
        QCOMPARE(d.writeWarnings.size(), 1);
        d.writeWarnings.clear();
        noUnexpected();
    }

    // The obsolete FC06-only value in the saved settings is ignored and removed on the next connect.
    void legacySingleRegisterWritesSettingIsDropped()
    {
        {
            QSettings q;
            q.setValue(QStringLiteral("connection/lastSingleRegisterWrites"), true);
            q.sync();
        }
        win->actions().newProject->trigger(); // reads the saved connection settings
        connectLive();     // stores them again
        QSettings q;
        QVERIFY(!q.contains(QStringLiteral("connection/lastSingleRegisterWrites")));
        QVERIFY(q.contains(QStringLiteral("connection/lastPort")));
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

        // No tabs any more: the Trend plot is always exported; rejected this time.
        d.onExec = [](QDialog &dlg) { dlg.reject(); };
        win->actions().exportImage->trigger();
        QCOMPARE(d.execCount, 2);

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

    void imageReadoutText()
    {
        feedStep();
        win->trendPlot()->cursors()->setC1(4.0);
        win->trendPlot()->cursors()->setC2(12.0);
        const QString cursorText = win->cursorPanel()->readoutText();
        QVERIFY(!cursorText.isEmpty());
        const QString analysisText = win->analysisPanel()->readoutText();
        // The cursor readout comes first; the analysis readout follows after a newline when it
        // is not empty (the stub panel of T195 returns an empty one, the real panel a summary).
        const QString expected = analysisText.isEmpty() ? cursorText : cursorText + QLatin1Char('\n') + analysisText;
        QCOMPARE(ctl->imageReadoutText(), expected);
        noUnexpected();
    }

    void analysisDockExists()
    {
        QVERIFY(win->analysisPanel());
        auto *dock = win->findChild<QDockWidget *>(QStringLiteral("analysisDock"));
        QVERIFY(dock);
        QVERIFY(dock->widget());
        QVERIFY(dock->isAncestorOf(win->analysisPanel()));
        QVERIFY(!win->findChild<QTabWidget *>(QStringLiteral("centralTabs")));
        QCOMPARE(win->centralWidget(), static_cast<QWidget *>(win->trendPlot()));
        QVERIFY(win->tabifiedDockWidgets(win->findChild<QDockWidget *>(QStringLiteral("cursorsDock")))
                    .contains(dock));
        // The setpoint options have their own tab.
        auto *sp = win->findChild<QDockWidget *>(QStringLiteral("setpointsDock"));
        QVERIFY(sp);
        QCOMPARE(sp->widget(), win->analysisPanel()->setpointsPage());
        QVERIFY(sp->isAncestorOf(sp->findChild<QTableWidget *>(QStringLiteral("setpointTable"))));
        QVERIFY(!dock->findChild<QTableWidget *>(QStringLiteral("setpointTable")));
        QVERIFY(win->tabifiedDockWidgets(dock).contains(sp));
    }

    void restoreStateWithoutAnalysisDock()
    {
        // A window state saved by a version without the Analysis dock: only the old docks.
        QByteArray oldState;
        {
            QMainWindow old;
            old.setCentralWidget(new QWidget(&old));
            old.resize(1200, 800);
            auto *tags = new QDockWidget(QStringLiteral("Tags"), &old);
            tags->setObjectName(QStringLiteral("tagsDock"));
            tags->setWidget(new QWidget(tags));
            old.addDockWidget(Qt::BottomDockWidgetArea, tags);
            auto *cursors = new QDockWidget(QStringLiteral("Cursors"), &old);
            cursors->setObjectName(QStringLiteral("cursorsDock"));
            cursors->setWidget(new QWidget(cursors));
            old.addDockWidget(Qt::RightDockWidgetArea, cursors);
            auto *quick = new QDockWidget(QStringLiteral("Quick write"), &old);
            quick->setObjectName(QStringLiteral("quickWriteDock"));
            quick->setWidget(new QWidget(quick));
            old.addDockWidget(Qt::RightDockWidgetArea, quick);
            old.tabifyDockWidget(cursors, quick);
            old.show();
            oldState = old.saveState();
        }
        QVERIFY(!oldState.isEmpty());
        stopApp();
        QSettings s;
        s.setValue(QStringLiteral("mainWindow/state"), oldState);
        s.sync();

        startApp();
        win->show();
        auto *dock = win->findChild<QDockWidget *>(QStringLiteral("analysisDock"));
        QVERIFY(dock);
        QVERIFY(!dock->isFloating());
        QVERIFY(!dock->isHidden());
        QVERIFY(dock->isVisibleTo(win.get()));
        QVERIFY(win->tabifiedDockWidgets(win->findChild<QDockWidget *>(QStringLiteral("cursorsDock")))
                    .contains(dock));
        win->hide();
    }

    void exportAnalysisResults()
    {
        // Nothing to export yet: no dialog even when called directly.
        QVERIFY(!win->actions().exportAnalysisCsv->isEnabled());
        win->actions().exportAnalysisCsv->trigger(); // disabled action: no-op
        QVERIFY(d.saveCaptions.isEmpty());

        feedStep();
        win->trendPlot()->cursors()->setC1(9.0);
        win->trendPlot()->cursors()->setC2(29.0);
        const QList<StepResult> results = win->analysisPanel()->results();
        QVERIFY(!results.isEmpty());
        QVERIFY2(results.first().valid, "the synthetic step produced no valid analysis result");
        QVERIFY(win->actions().exportAnalysisCsv->isEnabled());

        // Cancelled dialog.
        d.saveNames << QString();
        win->actions().exportAnalysisCsv->trigger();
        QCOMPARE(d.saveCaptions.size(), 1);

        // Written.
        const QString out = P(QStringLiteral("analysis.csv"));
        d.saveNames << out;
        emit win->analysisPanel()->exportResultsRequested();
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
        QVERIFY(win->trendPlot()->cursors()->hasC1());
        QVERIFY(win->trendPlot()->cursors()->hasC2());
        QVERIFY(qAbs(win->trendPlot()->cursors()->c1() - 5.0) < 1e-6);
        QVERIFY(qAbs(win->trendPlot()->cursors()->c2() - 20.0) < 1e-6);
        // The session also stores the cursors in its (legacy) analysis fields.
        {
            Project p;
            Recorder rec;
            SessionCursors sc;
            QVERIFY(loadSession(file, p, rec, sc, nullptr));
            QCOMPARE(sc.analysisC1, sc.trendC1);
            QCOMPARE(sc.analysisC2, sc.trendC2);
        }
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

    void sessionWithoutTrendCursorsFallsBackToAnalysisCursors()
    {
        // A session written before v1.2.6 may only have cursors in the former Analysis tab.
        feedStep();
        SessionCursors old;
        old.analysisC1 = 9.0;
        old.analysisC2 = 29.0;
        const QString file = P(QStringLiteral("legacy.mbsession"));
        QVERIFY(saveSession(file, ctl->currentProject(), ctl->m_recorder, old, nullptr));
        restart();
        d.openNames << file;
        win->actions().openSession->trigger();
        QVERIFY(win->trendPlot()->cursors()->hasC1());
        QVERIFY(win->trendPlot()->cursors()->hasC2());
        QVERIFY(qAbs(win->trendPlot()->cursors()->c1() - 9.0) < 1e-6);
        QVERIFY(qAbs(win->trendPlot()->cursors()->c2() - 29.0) < 1e-6);
        noUnexpected();
    }

    void analysisSettingsRoundTripWithProject()
    {
        QVERIFY(!modified());
        AnalysisSettings a = win->analysisPanel()->settings();
        QCOMPARE(a.pvTagId, TagId(7)); // demo project: Lag
        QCOMPARE(a.deltaCoTagId, TagId(8));
        QCOMPARE(a.setpointTagIds, QList<TagId>{7});
        a.percents = {0.0, 50.0};
        a.guideLines = !a.guideLines;
        win->analysisPanel()->setSettings(a);
        emit win->analysisPanel()->settingsChanged();
        QVERIFY(modified());

        const QString file = P(QStringLiteral("analysis.mbproj"));
        d.saveNames << file;
        win->actions().saveProject->trigger();
        QVERIFY(!modified());
        stopApp();
        startApp();
        QVERIFY(ctl->openProject(file));
        const AnalysisSettings b = win->analysisPanel()->settings();
        QCOMPARE(b.percents, a.percents);
        QCOMPARE(b.guideLines, a.guideLines);
        QCOMPARE(b.pvTagId, a.pvTagId);
        QCOMPARE(b.setpointTagIds, a.setpointTagIds);
        QCOMPARE(b.deltaCoTagId, a.deltaCoTagId);
        QVERIFY(b == a);
        // pvTagId / setpointTagIds travel through ProjectIO once T193 is merged.
        QVERIFY(!modified());
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
