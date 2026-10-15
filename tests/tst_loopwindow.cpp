#include "LoopController.h"
#include "LoopWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include <modbus.h>

using namespace mbloop;

class TstLoopWindow : public QObject
{
    Q_OBJECT
private:
    static QGraphicsItem *named(const SchematicView &v, const QString &name)
    {
        for (QGraphicsItem *item : v.view()->scene()->items()) {
            if (item->data(0).toString() == name && !dynamic_cast<QGraphicsSimpleTextItem *>(item)) {
                return item;
            }
        }
        return nullptr;
    }

private slots:
    void namedItemsExist()
    {
        LoopController c;
        LoopWindow w(&c);
        const QStringList names{"TK-102", "P-101", "FCV-101", "TK-101", "HV-102", "overflow",
                                "LT-101", "LIC-101", "process-data"};
        for (const QString &n : names) {
            QVERIFY2(named(*w.schematic(), n) != nullptr, qPrintable(n));
        }
    }

    void labelFormats()
    {
        LoopController c;
        LoopWindow w(&c);
        c.tickFor(1.0);
        const LoopState &s = c.model().state();
        const SchematicView &v = *w.schematic();
        QCOMPARE(v.labelText("lt-value"),
                 QString("%1 %  %2 mA").arg(s.pvPct, 0, 'f', 1).arg(percentToMa(s.pvPct), 0, 'f', 2));
        QVERIFY(v.labelText("lt-value").contains("%"));
        QVERIFY(v.labelText("lt-value").contains("mA"));
        QCOMPARE(v.labelText("fcv-value"),
                 QString("OUT %1 %  %2 mA\nPosition %3 %")
                     .arg(s.outPct, 0, 'f', 1)
                     .arg(percentToMa(s.outPct), 0, 'f', 2)
                     .arg(s.stemPct, 0, 'f', 1));
        QCOMPARE(v.labelText("hv-value"), QString("50 % open"));
        QCOMPARE(v.labelText("pump-value"), QString("100 %"));
        QCOMPARE(v.labelText("lic-value"),
                 QString("PV %1 %  SP 50.0 %  OUT 50.0 %").arg(s.pvPct, 0, 'f', 1));
        QCOMPARE(v.labelText("nope"), QString());
    }

    void processDataToggle()
    {
        LoopController c;
        LoopWindow w(&c);
        c.tickFor(1.0);
        auto *check = w.findChild<QCheckBox *>("processDataCheck");
        QVERIFY(check);
        QVERIFY(!w.schematic()->processDataVisible());
        QVERIFY(w.schematic()->labelText("process-data").isEmpty());
        check->click();
        QVERIFY(w.schematic()->processDataVisible());
        const QString t = w.schematic()->labelText("process-data");
        QVERIFY(t.contains("Level "));
        QVERIFY(t.contains(QString("Level %1 m").arg(c.model().state().levelM, 0, 'f', 3)));
        QVERIFY(t.contains("Dead time 2.0 s"));
        QVERIFY(t.contains("L/min"));
        QVERIFY(t.contains("Pump 1.00 bar"));
        check->click();
        QVERIFY(w.schematic()->labelText("process-data").isEmpty());
    }

    void overflowHighlight()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *ov = qgraphicsitem_cast<QGraphicsPathItem *>(named(*w.schematic(), "overflow"));
        QVERIFY(ov);
        const qreal normal = ov->pen().widthF();
        c.setSpeedFactor(10);
        c.setOut(100);
        for (int i = 0; i < 400 && !c.model().state().overflowing; ++i) {
            c.tickFor(0.5);
        }
        QVERIFY(c.model().state().overflowing);
        QVERIFY(ov->pen().widthF() > normal);
        c.setOut(0);
        for (int i = 0; i < 2000 && c.model().state().overflowing; ++i) {
            c.tickFor(0.5);
        }
        QVERIFY(!c.model().state().overflowing);
        QCOMPARE(ov->pen().widthF(), normal);
    }

    void slidersAndCombos()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *hv = w.findChild<QSlider *>("hvSlider");
        auto *pump = w.findChild<QSlider *>("pumpSlider");
        auto *speed = w.findChild<QComboBox *>("speedCombo");
        QVERIFY(hv && pump && speed);
        QCOMPARE(hv->value(), 50);
        hv->setValue(30);
        QCOMPARE(c.model().params().hvOpeningPct, 30.0);
        QCOMPARE(w.schematic()->labelText("hv-value"), QString("30 % open"));
        pump->setValue(80);
        QCOMPARE(c.model().params().pumpSpeedPct, 80.0);
        QCOMPARE(w.schematic()->labelText("pump-value"), QString("80 %"));
        speed->setCurrentIndex(speed->findText(QString::fromUtf8("×5")));
        QCOMPARE(c.speedFactor(), 5.0);
        // Controller-side change updates the control without feeding back.
        c.setSpeedFactor(10);
        QCOMPARE(speed->currentText(), QString::fromUtf8("×10"));
    }

    void pauseButton()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *pause = w.findChild<QPushButton *>("pauseButton");
        QVERIFY(pause);
        QCOMPARE(pause->text(), QString("Pause"));
        QTest::mouseClick(pause, Qt::LeftButton);
        QVERIFY(c.isPaused());
        QCOMPARE(pause->text(), QString("Run"));
        QTest::mouseClick(pause, Qt::LeftButton);
        QVERIFY(!c.isPaused());
        QCOMPARE(pause->text(), QString("Pause"));
        c.setPaused(true);
        QCOMPARE(pause->text(), QString("Run"));
        QVERIFY(pause->isChecked());
    }

    void timeLabel()
    {
        LoopController c;
        LoopWindow w(&c);
        c.tickFor(0.4);
        auto *t = w.findChild<QLabel *>("timeLabel");
        QVERIFY(t);
        QCOMPARE(t->text(), QString("t = %1 s").arg(c.model().state().timeS, 0, 'f', 1));
        QVERIFY(t->text().startsWith("t = "));
    }

    void faceplateButtons()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *up = w.faceplate()->findChild<QPushButton *>("outUpButton");
        auto *down = w.faceplate()->findChild<QPushButton *>("outDownButton");
        QVERIFY(up && down);
        QCOMPARE(c.model().state().outPct, 50.0);
        QTest::mouseClick(up, Qt::LeftButton);
        QCOMPARE(c.model().state().outPct, 51.0);
        QTest::mouseClick(up, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(c.model().state().outPct, 56.0);
        QTest::mouseClick(down, Qt::LeftButton);
        QCOMPARE(c.model().state().outPct, 55.0);
        QTest::mouseClick(down, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(c.model().state().outPct, 50.0);
        QCOMPARE(w.schematic()->labelText("lic-value").contains("OUT 50.0 %"), true);
        auto *outEdit = w.faceplate()->findChild<QLineEdit *>("outEdit");
        QCOMPARE(outEdit->text(), QString("50.0"));
    }

    void faceplateEdits()
    {
        LoopController c;
        LoopWindow w(&c);
        w.show();
        auto *outEdit = w.faceplate()->findChild<QLineEdit *>("outEdit");
        auto *spEdit = w.faceplate()->findChild<QLineEdit *>("spEdit");
        QVERIFY(outEdit && spEdit);
        outEdit->clear();
        QTest::keyClicks(outEdit, "62.5");
        QTest::keyClick(outEdit, Qt::Key_Return);
        QCOMPARE(c.model().state().outPct, 62.5);
        outEdit->clear();
        QTest::keyClicks(outEdit, "150");
        QTest::keyClick(outEdit, Qt::Key_Enter);
        QCOMPARE(c.model().state().outPct, 100.0);
        QCOMPARE(outEdit->text(), QString("100.0"));
        outEdit->clear();
        QTest::keyClicks(outEdit, "-5");
        QTest::keyClick(outEdit, Qt::Key_Return);
        QCOMPARE(c.model().state().outPct, 0.0);
        spEdit->clear();
        QTest::keyClicks(spEdit, "35");
        QTest::keyClick(spEdit, Qt::Key_Return);
        QCOMPARE(c.model().state().spPct, 35.0);
        QVERIFY(w.schematic()->labelText("lic-value").contains("SP 35.0 %"));
    }

    void refreshKeepsTypedText()
    {
        LoopController c;
        LoopWindow w(&c);
        w.show();
        w.activateWindow();
        (void)QTest::qWaitForWindowActive(&w);
        auto *outEdit = w.faceplate()->findChild<QLineEdit *>("outEdit");
        outEdit->setFocus();
        if (!outEdit->hasFocus()) {
            QSKIP("no keyboard focus on this platform");
        }
        outEdit->setText("7");
        c.tickFor(0.5);
        QCOMPARE(outEdit->text(), QString("7"));
    }

    void saveOpenResetScenario()
    {
        LoopController c;
        LoopWindow w(&c);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("a.mbloop");
        w.m_saveFileName = [&](const QString &) { return path; };
        w.m_openFileName = [&](const QString &) { return path; };
        QStringList warnings;
        w.m_warning = [&](const QString &, const QString &text) { warnings << text; };

        w.findChild<QSlider *>("hvSlider")->setValue(20);
        w.findChild<QSlider *>("pumpSlider")->setValue(70);
        c.setSpeedFactor(5);
        w.findChild<QAction *>("saveAction")->trigger();
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(warnings.isEmpty());

        w.findChild<QSlider *>("hvSlider")->setValue(90);
        w.findChild<QSlider *>("pumpSlider")->setValue(100);
        c.setSpeedFactor(1);
        w.findChild<QAction *>("openAction")->trigger();
        QVERIFY(warnings.isEmpty());
        QCOMPARE(c.model().params().hvOpeningPct, 20.0);
        QCOMPARE(c.model().params().pumpSpeedPct, 70.0);
        QCOMPARE(c.speedFactor(), 5.0);
        QCOMPARE(w.findChild<QSlider *>("hvSlider")->value(), 20);
        QCOMPARE(w.findChild<QSlider *>("pumpSlider")->value(), 70);
        QCOMPARE(w.findChild<QComboBox *>("speedCombo")->currentData().toDouble(), 5.0);
        QCOMPARE(w.schematic()->labelText("hv-value"), QString("20 % open"));

        // A bad file produces a warning and leaves the loop alone.
        QFile bad(dir.filePath("bad.mbloop"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ nonsense");
        bad.close();
        w.m_openFileName = [&](const QString &) { return dir.filePath("bad.mbloop"); };
        w.findChild<QAction *>("openAction")->trigger();
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(c.model().params().hvOpeningPct, 20.0);

        // Cancelled dialogs do nothing.
        w.m_openFileName = [](const QString &) { return QString(); };
        w.findChild<QAction *>("openAction")->trigger();
        QCOMPARE(warnings.size(), 1);

        // Reset: back to OUT = SP = 50 %, time 0.
        c.setSpeedFactor(10);
        c.tickFor(2.0);
        c.setOut(80);
        w.findChild<QAction *>("resetAction")->trigger();
        QCOMPARE(c.model().state().outPct, 50.0);
        QCOMPARE(c.model().state().timeS, 0.0);
        QCOMPARE(w.findChild<QLabel *>("timeLabel")->text(), QString("t = 0.0 s"));
    }

    void statusWithClients()
    {
        LoopController c;
        LoopWindow w(&c);
        w.setServerStatus("Modbus server on 127.0.0.1:1601");
        QCOMPARE(w.statusBar()->currentMessage(), QString("Modbus server on 127.0.0.1:1601, 0 clients"));
        QString error;
        QVERIFY2(c.startServer("127.0.0.1", 0, &error), qPrintable(error));
        QCOMPARE(c.server().clientCount(), 0);
        // One real client: a plain TCP connection is counted by the server loop.
        modbus_t *ctx = modbus_new_tcp("127.0.0.1", c.server().port());
        QVERIFY(ctx);
        QVERIFY(modbus_connect(ctx) == 0);
        QTRY_COMPARE_WITH_TIMEOUT(c.server().clientCount(), 1, 3000);
        c.tickFor(0.1);
        const QString msg = w.statusBar()->currentMessage();
        modbus_close(ctx);
        modbus_free(ctx);
        QCOMPARE(msg, QString("Modbus server on 127.0.0.1:1601, 1 client"));
    }

    void snapshot()
    {
        const QByteArray out = qgetenv("LOOPSIM_SNAPSHOT");
        if (out.isEmpty()) {
            QSKIP("set LOOPSIM_SNAPSHOT=<dir> to write PNGs");
        }
        LoopController c;
        LoopWindow w(&c);
        w.setServerStatus("Modbus server on 127.0.0.1:1601");
        w.resize(1200, 760);
        w.show();
        w.findChild<QCheckBox *>("processDataCheck")->setChecked(true);
        c.setSpeedFactor(10);
        c.setOut(70);
        for (int i = 0; i < 60; ++i) {
            c.tickFor(0.5);
        }
        QVERIFY(w.grab().save(QString::fromLocal8Bit(out) + "/loop_normal.png"));
        w.resize(700, 520);
        QVERIFY(w.grab().save(QString::fromLocal8Bit(out) + "/loop_small.png"));
        w.resize(1200, 760);
        c.setOut(100);
        for (int i = 0; i < 400 && !c.model().state().overflowing; ++i) {
            c.tickFor(0.5);
        }
        QVERIFY(w.grab().save(QString::fromLocal8Bit(out) + "/loop_overflow.png"));
    }
};

QTEST_MAIN(TstLoopWindow)
#include "tst_loopwindow.moc"
