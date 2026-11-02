#include "LoopController.h"
#include "LoopWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QApplication>
#include <QPalette>
#include <QImage>
#include <QtTest>

#include <modbus.h>

using namespace mbloop;

// Declared in src/core/Language.h (mbcore is linked, but its headers are not on the include path).
namespace mb {
bool installTranslators(const QString &code);
}

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
    void initTestCase()
    {
        // The English catalog supplies the plural forms ("1 client", "2 clients").
        mb::installTranslators(QStringLiteral("en"));
    }

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
        c.setSpeedFactor(5);
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
        c.setSpeedFactor(2);
        QCOMPARE(speed->currentText(), QString::fromUtf8("×2"));
        QCOMPARE(speed->count(), 3); // ×1 ×2 ×5
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

    void faceplateModeButton()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *mb = w.faceplate()->findChild<QPushButton *>("modeButton");
        QVERIFY(mb);
        QCOMPARE(mb->text(), QString("MAN"));
        QVERIFY(!c.model().isAuto());
        QTest::mouseClick(mb, Qt::LeftButton);
        QVERIFY(c.model().isAuto());
        QVERIFY(c.model().state().autoMode);
        QCOMPARE(mb->text(), QString("AUTO"));
        QTest::mouseClick(mb, Qt::LeftButton);
        QVERIFY(!c.model().isAuto());
        QCOMPARE(mb->text(), QString("MAN"));
    }

    void faceplateAutoDisablesOut()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *up = w.faceplate()->findChild<QPushButton *>("outUpButton");
        auto *down = w.faceplate()->findChild<QPushButton *>("outDownButton");
        auto *outEdit = w.faceplate()->findChild<QLineEdit *>("outEdit");
        auto *spEdit = w.faceplate()->findChild<QLineEdit *>("spEdit");
        QVERIFY(up && down && outEdit && spEdit);
        QVERIFY(up->isEnabled() && down->isEnabled() && outEdit->isEnabled());
        c.setAuto(true);
        QVERIFY(!up->isEnabled());
        QVERIFY(!down->isEnabled());
        QVERIFY(!outEdit->isEnabled());
        QVERIFY(spEdit->isEnabled());
        c.setAuto(false);
        QVERIFY(up->isEnabled() && down->isEnabled() && outEdit->isEnabled());
    }

    void faceplateTuningEdits()
    {
        LoopController c;
        LoopWindow w(&c);
        w.show();
        auto *kp = w.faceplate()->findChild<QLineEdit *>("kpEdit");
        auto *ti = w.faceplate()->findChild<QLineEdit *>("tiEdit");
        auto *td = w.faceplate()->findChild<QLineEdit *>("tdEdit");
        QVERIFY(kp && ti && td);
        QCOMPARE(kp->text(), QString("1"));
        QCOMPARE(ti->text(), QString("30"));
        QCOMPARE(td->text(), QString("0"));
        kp->clear();
        QTest::keyClicks(kp, "2.5");
        QTest::keyClick(kp, Qt::Key_Return);
        QCOMPARE(c.model().pid().kp, 2.5);
        ti->clear();
        QTest::keyClicks(ti, "150");
        QTest::keyClick(ti, Qt::Key_Return);
        QCOMPARE(c.model().pid().tiS, 150.0);
        td->clear();
        QTest::keyClicks(td, "12");
        QTest::keyClick(td, Qt::Key_Return);
        QCOMPARE(c.model().pid().tdS, 12.0);
        // Out of range values are clamped to the PidSettings ranges.
        kp->clear();
        QTest::keyClicks(kp, "250");
        QTest::keyClick(kp, Qt::Key_Return);
        QCOMPARE(c.model().pid().kp, 100.0);
        QCOMPARE(kp->text(), QString("100"));
        ti->clear();
        QTest::keyClicks(ti, "9000");
        QTest::keyClick(ti, Qt::Key_Return);
        QCOMPARE(c.model().pid().tiS, 3600.0);
        td->clear();
        QTest::keyClicks(td, "-4");
        QTest::keyClick(td, Qt::Key_Return);
        QCOMPARE(c.model().pid().tdS, 0.0);
        QCOMPARE(td->text(), QString("0"));
        // Invalid text restores the current value.
        kp->clear();
        QTest::keyClicks(kp, "abc");
        QTest::keyClick(kp, Qt::Key_Return);
        QCOMPARE(c.model().pid().kp, 100.0);
        QCOMPARE(kp->text(), QString("100"));
    }

    void faceplateActionCombo()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *combo = w.faceplate()->findChild<QComboBox *>("actionCombo");
        QVERIFY(combo);
        QCOMPARE(combo->currentText(), QString("Reverse"));
        QVERIFY(c.model().pid().reverse);
        combo->setCurrentIndex(combo->findText("Direct"));
        QVERIFY(!c.model().pid().reverse);
        combo->setCurrentIndex(combo->findText("Reverse"));
        QVERIFY(c.model().pid().reverse);
    }

    void faceplateRefreshFromController()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *mb = w.faceplate()->findChild<QPushButton *>("modeButton");
        auto *kp = w.faceplate()->findChild<QLineEdit *>("kpEdit");
        auto *ti = w.faceplate()->findChild<QLineEdit *>("tiEdit");
        auto *td = w.faceplate()->findChild<QLineEdit *>("tdEdit");
        auto *combo = w.faceplate()->findChild<QComboBox *>("actionCombo");
        auto *outEdit = w.faceplate()->findChild<QLineEdit *>("outEdit");
        PidSettings p = c.model().pid();
        p.kp = 3.25;
        p.tiS = 60;
        p.tdS = 5;
        p.reverse = false;
        c.setPid(p);
        QCOMPARE(kp->text(), QString("3.25"));
        QCOMPARE(ti->text(), QString("60"));
        QCOMPARE(td->text(), QString("5"));
        QCOMPARE(combo->currentText(), QString("Direct"));
        // The refresh from the controller side does not send anything back.
        QCOMPARE(c.model().pid().kp, 3.25);
        c.setAuto(true);
        QCOMPARE(mb->text(), QString("AUTO"));
        QVERIFY(!outEdit->isEnabled());
        c.setAuto(false);
        QCOMPARE(mb->text(), QString("MAN"));
        QVERIFY(outEdit->isEnabled());
    }

    void faceplateFocusedTuningKept()
    {
        LoopController c;
        LoopWindow w(&c);
        w.show();
        w.activateWindow();
        (void)QTest::qWaitForWindowActive(&w);
        auto *kp = w.faceplate()->findChild<QLineEdit *>("kpEdit");
        kp->setFocus();
        if (!kp->hasFocus()) {
            QSKIP("no keyboard focus on this platform");
        }
        kp->setText("7");
        c.tickFor(0.5);
        QCOMPARE(kp->text(), QString("7"));
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
        c.setSpeedFactor(5);
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
        // server not running (failed start): the error alone, no client count
        w.setServerStatus("Port 1601 is already in use.");
        QCOMPARE(w.statusBar()->currentMessage(), QString("Port 1601 is already in use."));
        c.tickFor(0.1);
        QCOMPARE(w.statusBar()->currentMessage(), QString("Port 1601 is already in use."));
        QString error;
        QVERIFY2(c.startServer("127.0.0.1", 0, &error), qPrintable(error));
        w.setServerStatus("Modbus server on 127.0.0.1:1601");
        QCOMPARE(w.statusBar()->currentMessage(), QString("Modbus server on 127.0.0.1:1601, 0 clients"));
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

    void modbusPanelRunning()
    {
        LoopController c;
        QString error;
        QVERIFY2(c.startServer("127.0.0.1", 0, &error), qPrintable(error));
        LoopWindow w(&c);
        w.setServerStatus("Modbus server on 127.0.0.1");
        w.show();
        QWidget *panel = w.findChild<QWidget *>("modbusPanel");
        QVERIFY(panel);
        QVERIFY(!panel->isVisible()); // hidden by default
        w.findChild<QCheckBox *>("modbusCheck")->setChecked(true);
        QVERIFY(panel->isVisible());
        QVERIFY(w.rect().contains(panel->mapTo(&w, panel->rect().bottomRight())));
        auto *conn = w.findChild<QLabel *>("modbusConnLabel");
        QVERIFY(conn);
        QVERIFY(conn->text().contains(QString("127.0.0.1 : %1").arg(c.server().port())));
        QVERIFY(conn->text().contains("Unit 1 (any unit answered)"));
        QVERIFY(conn->text().contains("0 clients connected"));
        QCOMPARE(c.server().host(), QString("127.0.0.1"));
        auto *t = w.findChild<QTableWidget *>("modbusTable");
        QVERIFY(t);
        QCOMPARE(t->rowCount(), 4);
        QVERIFY(!t->verticalHeader()->isVisible());
        QCOMPARE(t->item(0, 1)->text(), QString("Input register 1-2"));
        QCOMPARE(t->item(0, 2)->text(), QString("IR 0"));
        QCOMPARE(t->item(0, 3)->text(), QString("float32 ABCD (high word first)"));
        QCOMPARE(t->item(0, 4)->text(), QString("FC04"));
        QCOMPARE(t->item(0, 5)->text(), QString("Read only"));
        QCOMPARE(t->item(1, 1)->text(), QString("Holding register 1-2"));
        QCOMPARE(t->item(1, 2)->text(), QString("HR 0"));
        QCOMPARE(t->item(1, 4)->text(), QString("FC03 read, FC16 write"));
        QCOMPARE(t->item(2, 1)->text(), QString("Holding register 3-4"));
        QCOMPARE(t->item(2, 2)->text(), QString("HR 2"));
        QVERIFY(t->item(0, 0)->text().startsWith("PV"));
        QVERIFY(t->item(1, 0)->text().startsWith("SP"));
        QVERIFY(t->item(2, 0)->text().startsWith("OUT"));
        QVERIFY(!t->item(0, 1)->toolTip().isEmpty());
        QVERIFY(w.findChild<QLabel *>("modbusNote"));
        // Live values follow the model.
        c.setOut(70);
        c.tickFor(1.0);
        const LoopState &s = c.model().state();
        QCOMPARE(t->item(0, 6)->text(), QString("%1 %").arg(s.pvPct, 0, 'f', 1));
        QCOMPARE(t->item(1, 6)->text(), QString("%1 %").arg(s.spPct, 0, 'f', 1));
        QCOMPARE(t->item(2, 6)->text(), QString("70.0 %"));
        // Auto changes the OUT access text.
        QCOMPARE(t->item(2, 5)->text(), QString("Read / write (0–100); read only in AUTO"));
        c.setAuto(true);
        QCOMPARE(t->item(2, 5)->text(), QString("Read only (AUTO)"));
        c.setAuto(false);
        QCOMPARE(t->item(2, 5)->text(), QString("Read / write (0–100); read only in AUTO"));
        QVERIFY(w.findChild<QAction *>("modbusHelpAction"));

        // Coil row: A/M, shown 1-based like the others.
        QVERIFY(t->item(3, 0)->text().startsWith("LIC-101 A/M"));
        QCOMPARE(t->item(3, 1)->text(), QString("Coil 1"));
        QCOMPARE(t->item(3, 2)->text(), QString("CO 0"));
        QCOMPARE(t->item(3, 4)->text(), QString("FC01 read, FC05 write"));
        QCOMPARE(t->item(3, 6)->text(), QString("1 = Manual"));
        c.setAuto(true);
        QCOMPARE(t->item(3, 6)->text(), QString("0 = Auto"));
        c.setAuto(false);
        QCOMPARE(t->item(3, 6)->text(), QString("1 = Manual"));
    }

    void coilWriteSwitchesModeAndFaceplate()
    {
        LoopController c;
        QString error;
        QVERIFY2(c.startServer("127.0.0.1", 0, &error), qPrintable(error));
        LoopWindow w(&c);
        w.show();
        w.findChild<QCheckBox *>("modbusCheck")->setChecked(true);
        auto *t = w.findChild<QTableWidget *>("modbusTable");
        QVERIFY(t);
        QVERIFY(!c.model().isAuto());
        QCOMPARE(t->item(3, 6)->text(), QString("1 = Manual"));

        modbus_t *ctx = modbus_new_tcp("127.0.0.1", c.server().port());
        QVERIFY(ctx);
        QVERIFY(modbus_connect(ctx) == 0);
        QCOMPARE(modbus_write_bit(ctx, 0, 0), 1); // Auto
        c.tickFor(0.05);
        QVERIFY(c.model().isAuto());
        QCOMPARE(t->item(3, 6)->text(), QString("0 = Auto"));
        QCOMPARE(t->item(2, 5)->text(), QString("Read only (AUTO)"));
        auto *mode = w.faceplate()->findChild<QPushButton *>("modeButton");
        QVERIFY(mode);
        QCOMPARE(mode->text(), QString("AUTO"));

        QCOMPARE(modbus_write_bit(ctx, 0, 1), 1); // Manual
        c.tickFor(0.05);
        QVERIFY(!c.model().isAuto());
        QCOMPARE(t->item(3, 6)->text(), QString("1 = Manual"));
        QCOMPARE(t->item(2, 5)->text(), QString("Read / write (0–100); read only in AUTO"));
        QCOMPARE(mode->text(), QString("MAN"));
        modbus_close(ctx);
        modbus_free(ctx);
    }

    void modbusTableReadableInDarkTheme()
    {
        // Windows dark mode gives white text by default: the panel forces its own text colour.
        const QPalette old = QApplication::palette();
        QPalette dark = old;
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Base, QColor(0x20, 0x20, 0x20));
        dark.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
        QApplication::setPalette(dark);
        {
            LoopController c;
            LoopWindow w(&c);
            w.resize(1200, 760);
            w.show();
            w.findChild<QCheckBox *>("modbusCheck")->setChecked(true);
            auto *t = w.findChild<QTableWidget *>("modbusTable");
            QVERIFY(t);
            const QRect cell = t->visualItemRect(t->item(0, 1));
            const QImage img = t->viewport()->grab(cell).toImage();
            int darkPixels = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qGray(img.pixel(x, y)) < 110)
                        ++darkPixels;
            QVERIFY2(darkPixels > 20, "the register text is not drawn in a dark colour");
        }
        QApplication::setPalette(old);
    }

    void modbusPanelNotRunning()
    {
        LoopController c;
        LoopWindow w(&c);
        w.setServerStatus("Port 1601 is already in use.");
        w.show();
        auto *conn = w.findChild<QLabel *>("modbusConnLabel");
        QVERIFY(conn);
        QVERIFY(conn->text().contains("Server not running: Port 1601 is already in use."));
        QVERIFY(conn->text().contains("c0392b"));
        w.findChild<QCheckBox *>("modbusCheck")->setChecked(true);
        QVERIFY(w.findChild<QWidget *>("modbusPanel")->isVisible());
        w.findChild<QCheckBox *>("modbusCheck")->setChecked(false);
        QVERIFY(!w.findChild<QWidget *>("modbusPanel")->isVisible());
    }

    void snapshot()
    {
        const QByteArray out = qgetenv("LOOPSIM_SNAPSHOT");
        if (out.isEmpty()) {
            QSKIP("set LOOPSIM_SNAPSHOT=<dir> to write PNGs");
        }
        LoopController c;
        QVERIFY(c.startServer("127.0.0.1", 0));
        LoopWindow w(&c);
        w.setServerStatus("Modbus server on 127.0.0.1:1601");
        w.resize(1200, 760);
        w.show();
        w.findChild<QCheckBox *>("processDataCheck")->setChecked(true);
        c.setSpeedFactor(5);
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

    // v1.3.3: faceplate fields take "72,3" and "72.3"; "1,234.5" is refused.
    void faceplateAcceptsCommaAndPoint()
    {
        LoopController c;
        LoopWindow w(&c);
        auto *outEdit = w.faceplate()->findChild<QLineEdit *>("outEdit");
        auto *spEdit = w.faceplate()->findChild<QLineEdit *>("spEdit");
        auto *kp = w.faceplate()->findChild<QLineEdit *>("kpEdit");
        QVERIFY(outEdit && spEdit && kp);
        for (const char *text : {"62,5", "62.5"}) {
            outEdit->setText(QString::fromLatin1(text));
            emit outEdit->returnPressed();
            QCOMPARE(c.model().state().outPct, 62.5);
            QCOMPARE(outEdit->text(), QString("62.5"));
            c.setOut(10.0);
        }
        spEdit->setText(QStringLiteral("35,5"));
        emit spEdit->returnPressed();
        QCOMPARE(c.model().state().spPct, 35.5);
        QCOMPARE(spEdit->text(), QString("35.5"));
        kp->setText(QStringLiteral("2,5"));
        emit kp->returnPressed();
        QCOMPARE(c.model().pid().kp, 2.5);
        QCOMPARE(kp->text(), QString("2.5"));
        // Thousands separators are not numbers: nothing changes.
        spEdit->setText(QStringLiteral("1,234.5"));
        emit spEdit->returnPressed();
        QCOMPARE(c.model().state().spPct, 35.5);
        kp->setText(QStringLiteral("1,234.5"));
        emit kp->returnPressed();
        QCOMPARE(c.model().pid().kp, 2.5);
    }
};

QTEST_MAIN(TstLoopWindow)
#include "tst_loopwindow.moc"
