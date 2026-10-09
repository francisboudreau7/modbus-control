#include "QuickWritePanel.h"
#include "TagTableModel.h"
#include "WriteController.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace mb;

class TstQuickWrite : public QObject
{
    Q_OBJECT
private:
    static Tag makeTag(TagId id, const QString &name, bool writable, std::optional<double> a,
                       std::optional<double> b, const QString &unit = QString())
    {
        Tag t;
        t.id = id;
        t.name = name;
        t.address = quint16(100 + id);
        t.writable = writable;
        t.quickA = a;
        t.quickB = b;
        t.unit = unit;
        return t;
    }

    static QList<Tag> sampleTags()
    {
        return {makeTag(1, "Setpoint", true, 0.0, 1000.0, "%"),
                makeTag(2, "Mode", true, 5.0, std::nullopt),
                makeTag(3, "ReadOnly", false, 1.0, 2.0),
                makeTag(4, "NoPresets", true, std::nullopt, std::nullopt)};
    }

    static QPushButton *button(QWidget *w, const QString &name)
    {
        return w->findChild<QPushButton *>(name);
    }

private slots:
    void rowsAndTexts()
    {
        TagTableModel model;
        model.setTags(sampleTags(), 10);
        QuickWritePanel panel(&model);
        QVERIFY(button(&panel, "quickA_1"));
        QVERIFY(button(&panel, "quickB_1"));
        QCOMPARE(button(&panel, "quickA_1")->text(), QString("A: 0 %"));
        QCOMPARE(button(&panel, "quickB_1")->text(), QString("B: 1000 %"));
        QVERIFY(button(&panel, "quickA_2"));
        QVERIFY(!button(&panel, "quickB_2"));
        QCOMPARE(button(&panel, "quickA_2")->text(), QString("A: 5"));
        QVERIFY(!button(&panel, "quickA_3"));
        QVERIFY(!button(&panel, "quickA_4"));
        QVERIFY(!panel.findChild<QLabel *>("quickHint"));
        QCOMPARE(panel.findChildren<QPushButton *>().size(), 3);
    }

    void hintWhenNone()
    {
        TagTableModel model;
        QuickWritePanel panel(&model);
        auto *hint = panel.findChild<QLabel *>("quickHint");
        QVERIFY(hint);
        QCOMPARE(hint->text(), QString("Set Quick A / Quick B on writable tags in the tag table."));
        QVERIFY(panel.findChildren<QPushButton *>().isEmpty());
    }

    void disabledUntilConnectedAndClick()
    {
        TagTableModel model;
        model.setTags(sampleTags(), 10);
        QuickWritePanel panel(&model);
        QSignalSpy spy(&panel, &QuickWritePanel::quickWriteRequested);
        QVERIFY(!button(&panel, "quickB_1")->isEnabled());
        panel.setConnected(true);
        QVERIFY(button(&panel, "quickB_1")->isEnabled());
        QVERIFY(button(&panel, "quickA_2")->isEnabled());
        button(&panel, "quickB_1")->click();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).value<TagId>(), TagId(1));
        QCOMPARE(spy.at(0).at(1).toDouble(), 1000.0);
        panel.setConnected(false);
        QVERIFY(!button(&panel, "quickB_1")->isEnabled());
    }

    void rebuildOnChanges()
    {
        TagTableModel model;
        model.setTags(sampleTags(), 10);
        QuickWritePanel panel(&model);
        panel.setConnected(true);

        // Rebuilt buttons keep the connected state.
        QList<Tag> tags = sampleTags();
        tags[3].quickB = 7.5;
        model.setTags(tags, 10);
        QVERIFY(button(&panel, "quickB_4"));
        QCOMPARE(button(&panel, "quickB_4")->text(), QString("B: 7.5"));
        QVERIFY(button(&panel, "quickB_4")->isEnabled());

        // tagsEdited: e.g. a new tag
        Tag proto = makeTag(0, "New", true, 3.0, std::nullopt);
        const int row = model.addTag(proto);
        const TagId newId = model.tagAt(row).id;
        QVERIFY(button(&panel, QString("quickA_%1").arg(newId)));

        // Row removal
        model.removeRows(row, 1);
        QVERIFY(!button(&panel, QString("quickA_%1").arg(newId)));

        // Editing a preset through the model (tagsEdited path)
        model.setData(model.index(0, TagTableModel::Name), "Renamed");
        QVERIFY(button(&panel, "quickA_1"));
    }

    void liveValueFollowsModel()
    {
        TagTableModel model;
        model.setTags(sampleTags(), 10);
        QuickWritePanel panel(&model);
        auto *label = panel.findChild<QLabel *>("quickValue_1");
        QVERIFY(label);
        QCOMPARE(label->text(), QString(QChar(0x2014)));
        SampleBatch b;
        b.samples << TagSample{1, 42, 42.0, Quality::Good};
        model.updateSamples(b);
        QCOMPARE(label->text(), model.data(model.index(0, TagTableModel::Value)).toString());
        QCOMPARE(label->text(), QString("42"));
        // Same label object survives (no rebuild)
        QCOMPARE(panel.findChild<QLabel *>("quickValue_1"), label);
    }

    // ---- WriteController ----

    void controllerQuick()
    {
        TagTableModel model;
        model.setTags(sampleTags(), 10);
        WriteController wc(&model, nullptr);
        QSignalSpy writes(&wc, &WriteController::writeRegisters);
        QSignalSpy changed(&wc, &WriteController::quickConfirmChanged);
        QSignalSpy messages(&wc, &WriteController::message);
        int quickCalls = 0;
        int normalCalls = 0;
        WriteController::QuickAnswer answer;
        wc.m_quickConfirmPrompt = [&](const QString &q) {
            ++quickCalls;
            Q_UNUSED(q)
            return answer;
        };
        wc.m_confirm = [&](const QString &) {
            ++normalCalls;
            return true;
        };
        QVERIFY(wc.quickConfirm());

        // Not connected
        wc.requestQuickWrite(1, 1000.0);
        QCOMPARE(writes.size(), 0);
        QCOMPARE(quickCalls, 0);
        QVERIFY(!messages.isEmpty());

        wc.setConnected(true);

        // Declined
        answer = {false, true};
        wc.requestQuickWrite(1, 1000.0);
        QCOMPARE(quickCalls, 1);
        QCOMPARE(writes.size(), 0);
        QVERIFY(wc.quickConfirm());
        QCOMPARE(changed.size(), 0);

        // Confirmed without "don't ask"
        answer = {true, false};
        wc.requestQuickWrite(1, 1000.0);
        QCOMPARE(quickCalls, 2);
        QCOMPARE(writes.size(), 1);
        QVERIFY(wc.quickConfirm());

        // Confirmed with "don't ask again"
        answer = {true, true};
        wc.requestQuickWrite(1, 0.0);
        QCOMPARE(quickCalls, 3);
        QCOMPARE(writes.size(), 2);
        QVERIFY(!wc.quickConfirm());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).toBool(), false);
        QCOMPARE(writes.at(1).at(2).value<QList<quint16>>(), QList<quint16>{0});

        // No further prompt
        wc.requestQuickWrite(1, 1000.0);
        QCOMPARE(quickCalls, 3);
        QCOMPARE(writes.size(), 3);
        QCOMPARE(writes.at(2).at(1).value<quint16>(), quint16(101));
        QCOMPARE(writes.at(2).at(2).value<QList<quint16>>(), QList<quint16>{1000});

        // Normal write still always asks the normal confirmation
        wc.requestWrite(1, 5.0);
        QCOMPARE(normalCalls, 1);
        QCOMPARE(quickCalls, 3);
        QCOMPARE(writes.size(), 4);

        // Re-enable
        wc.setQuickConfirm(true);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(changed.at(1).at(0).toBool(), true);
        answer = {false, false};
        wc.requestQuickWrite(1, 1.0);
        QCOMPARE(quickCalls, 4);
        QCOMPARE(writes.size(), 4);
    }

    void controllerQuickRangeError()
    {
        TagTableModel model;
        model.setTags(sampleTags(), 10);
        WriteController wc(&model, nullptr);
        QSignalSpy writes(&wc, &WriteController::writeRegisters);
        int warns = 0;
        int quickCalls = 0;
        wc.m_warn = [&](const QString &) { ++warns; };
        wc.m_quickConfirmPrompt = [&](const QString &) {
            ++quickCalls;
            return WriteController::QuickAnswer{true, false};
        };
        wc.setConnected(true);
        wc.requestQuickWrite(1, 1.0e9); // out of int16 range
        QCOMPARE(warns, 1);
        QCOMPARE(quickCalls, 0);
        QCOMPARE(writes.size(), 0);
    }
};

QTEST_MAIN(TstQuickWrite)
#include "tst_quickwrite.moc"
