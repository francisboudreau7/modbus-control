#include "QuickWritePanel.h"
#include "TagTableModel.h"
#include "WriteController.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

#include <limits>
#include <memory>

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

    // ---- v1.2.1 write safety ----
private:
    struct Rig {
        TagTableModel model;
        std::unique_ptr<WriteController> wc;
        std::unique_ptr<QSignalSpy> writes;
        QStringList warns;
        QStringList confirms;
        QStringList implausibles;
        bool implausibleAnswer = true;
        int quickCalls = 0;
        QStringList encodingPrompts;
        WriteController::EncodingAnswer encodingAnswer = WriteController::EncodingAnswer::Cancel;
    };

    /// Tag 1: UInt16 "Setpoint" HR 101 limits 0..100 unit C; tag 2: Float32 "Flow" HR 102-103;
    /// tag 3: UInt16 "Next" HR 104; tag 4: UInt16 "Plain" HR 200, no limits.
    /// verified = false: every tag has encodingVerified false, and "Flow" reads as CDAB.
    static void setup(Rig &r, bool verified = true)
    {
        Tag sp = makeTag(1, "Setpoint", true, std::nullopt, std::nullopt, "C");
        sp.type = ValueType::UInt16;
        sp.address = 101;
        sp.writeMin = 0.0;
        sp.writeMax = 100.0;
        Tag fl = makeTag(2, "Flow", true, std::nullopt, std::nullopt);
        fl.type = ValueType::Float32;
        fl.address = 102;
        fl.writeMin = 10.0;
        Tag nx = makeTag(3, "Next", true, std::nullopt, std::nullopt);
        nx.type = ValueType::UInt16;
        nx.address = 104;
        Tag pl = makeTag(4, "Plain", true, std::nullopt, std::nullopt);
        pl.type = ValueType::UInt16;
        pl.address = 200;
        sp.encodingVerified = fl.encodingVerified = nx.encodingVerified = pl.encodingVerified = verified;
        if (!verified)
            fl.byteOrder = ByteOrder::CDAB;
        r.model.setTags({sp, fl, nx, pl}, 0);
        r.wc = std::make_unique<WriteController>(&r.model, nullptr);
        r.writes = std::make_unique<QSignalSpy>(r.wc.get(), &WriteController::writeRegisters);
        Rig *p = &r;
        r.wc->m_confirmEncoding = [p](const QString &t) {
            p->encodingPrompts << t;
            return p->encodingAnswer;
        };
        r.wc->m_warn = [p](const QString &t) { p->warns << t; };
        r.wc->m_confirm = [p](const QString &t) {
            p->confirms << t;
            return true;
        };
        r.wc->m_warnImplausible = [p](const QString &t) {
            p->implausibles << t;
            return p->implausibleAnswer;
        };
        r.wc->m_quickConfirmPrompt = [p](const QString &t) {
            ++p->quickCalls;
            p->confirms << t;
            return WriteController::QuickAnswer{true, false};
        };
        r.wc->setConnected(true);
    }

    static void feed(Rig &r, TagId id, double v, Quality q = Quality::Good)
    {
        SampleBatch b;
        b.samples << TagSample{id, 0, v, q};
        r.model.updateSamples(b);
    }

private slots:
    void limitRefusedAllPaths()
    {
        Rig r;
        setup(r);
        r.wc->requestWrite(1, 150.0);
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(r.warns.size(), 1);
        QVERIFY(r.warns.at(0).contains("Setpoint"));
        QVERIFY(r.warns.at(0).contains("0 to 100"));
        QVERIFY(r.warns.at(0).contains("150"));
        QVERIFY(r.confirms.isEmpty());

        r.wc->requestQuickWrite(1, -1.0);
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(r.warns.size(), 2);
        QVERIFY(r.warns.at(1).contains("Setpoint"));
        QCOMPARE(r.quickCalls, 0);

        // At the limits: accepted
        r.wc->requestWrite(1, 100.0);
        QCOMPARE(r.writes->size(), 1);
        r.wc->requestQuickWrite(1, 0.0);
        QCOMPARE(r.writes->size(), 2);
        QCOMPARE(r.warns.size(), 2);

        // Selection: tags 1 (HR 101), 2 (HR 102-103, Float32 needs reading), 3 (HR 104)
        feed(r, 2, 50.0);
        int asked = 0;
        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            ++asked;
            return std::optional<QList<double>>(QList<double>{150.0, 5.0, 7.0});
        };
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(asked, 1);
        QCOMPARE(r.writes->size(), 2);
        QCOMPARE(r.warns.size(), 3);
        QVERIFY(r.warns.at(2).contains("Setpoint"));
        QVERIFY(r.warns.at(2).contains("Flow")); // 5 is below min 10: both listed together
        QVERIFY(r.warns.at(2).contains("at least 10"));

        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            return std::optional<QList<double>>(QList<double>{100.0, 10.0, 7.0});
        };
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(r.writes->size(), 3);
        QCOMPARE(r.warns.size(), 3);
    }

    void wideTagNeedsGoodReading()
    {
        Rig r;
        setup(r);
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(r.warns.size(), 1);
        QVERIFY(r.warns.at(0).contains("No valid reading of 'Flow'"));

        feed(r, 2, 0.0, Quality::Timeout); // not Good
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(r.warns.size(), 2);

        feed(r, 2, 50.0);
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.writes->size(), 1);
        QCOMPARE(r.warns.size(), 2);
        QCOMPARE(r.writes->at(0).at(2).value<QList<quint16>>().size(), 2);

        // Quick write path as well
        feed(r, 2, 0.0, Quality::Timeout);
        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(r.writes->size(), 1);
        QCOMPARE(r.warns.size(), 3);

        // Selection with a 32-bit tag lacking a reading: refused before the value dialog
        int asked = 0;
        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            ++asked;
            return std::optional<QList<double>>(QList<double>{1.0, 20.0, 1.0});
        };
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(asked, 0);
        QCOMPARE(r.warns.size(), 4);
        QVERIFY(r.warns.at(3).contains("Flow"));
    }

    void narrowTagWithoutReadingStillAllowed()
    {
        Rig r;
        setup(r);
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.writes->size(), 1);
        QVERIFY(r.warns.isEmpty());
        QCOMPARE(r.confirms.size(), 1);
        QVERIFY(r.confirms.at(0).contains("Current value: unknown"));
        QVERIFY(!r.confirms.at(0).contains("Limits"));
    }

    void implausibleCurrentFloat()
    {
        Rig r;
        setup(r);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double bad[] = {1.0e12, 1.0e-20, -1.0e12, nan, std::numeric_limits<double>::infinity()};
        int expected = 0;
        for (double cur : bad) {
            feed(r, 2, cur);
            r.implausibleAnswer = false;
            const int before = int(r.writes->size());
            r.wc->requestWrite(2, 20.0);
            ++expected;
            QCOMPARE(r.implausibles.size(), expected);
            QCOMPARE(int(r.writes->size()), before); // declined: nothing written
            QVERIFY(r.confirms.isEmpty());           // stopped before the confirmation

            r.implausibleAnswer = true;
            r.wc->requestWrite(2, 20.0);
            ++expected;
            QCOMPARE(r.implausibles.size(), expected);
            QCOMPARE(int(r.writes->size()), before + 1);
            QCOMPARE(r.confirms.size(), 1);
            r.confirms.clear();
        }
        QVERIFY(r.implausibles.at(0).contains("Flow"));

        // Quick write goes through the same check
        feed(r, 2, 1.0e12);
        r.implausibleAnswer = false;
        const int n = int(r.writes->size());
        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(r.implausibles.size(), expected + 1);
        QCOMPARE(int(r.writes->size()), n);

        // Plausible and zero values: no extra prompt
        const int prompts = int(r.implausibles.size());
        for (double cur : {50.0, 0.0, -3.5, 1.0e-6, 1.0e9}) {
            feed(r, 2, cur);
            r.wc->requestWrite(2, 20.0);
            QCOMPARE(r.implausibles.size(), prompts);
        }

        // 16-bit tags never get the float warning
        feed(r, 1, 1.0e12);
        r.wc->requestWrite(1, 5.0);
        QCOMPARE(r.implausibles.size(), prompts);

        // Selection: one prompt listing the affected tag
        feed(r, 2, 1.0e-20);
        r.implausibleAnswer = false;
        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            return std::optional<QList<double>>(QList<double>{1.0, 20.0, 1.0});
        };
        const int w = int(r.writes->size());
        r.confirms.clear();
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(r.implausibles.size(), prompts + 1);
        QVERIFY(r.implausibles.last().contains("Flow"));
        QCOMPARE(int(r.writes->size()), w);
        QVERIFY(r.confirms.isEmpty());
        r.implausibleAnswer = true;
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(r.implausibles.size(), prompts + 2);
        QCOMPARE(int(r.writes->size()), w + 1);
    }

    void confirmationShowsCurrentAndLimits()
    {
        Rig r;
        setup(r);
        feed(r, 1, 12.5);
        r.wc->requestWrite(1, 50.0);
        QCOMPARE(r.confirms.size(), 1);
        QVERIFY(r.confirms.at(0).contains("Current value: 12.5"));
        QVERIFY(r.confirms.at(0).contains("Limits: 0 to 100"));
        QVERIFY(r.confirms.at(0).contains("C")); // unit kept

        // One-sided limit
        feed(r, 2, 30.0);
        r.confirms.clear();
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.confirms.size(), 1);
        QVERIFY(r.confirms.at(0).contains("Limits: at least 10"));
        QVERIFY(r.confirms.at(0).contains("Current value: 30"));
    }

    void quickWriteWideAlwaysConfirms()
    {
        Rig r;
        setup(r);
        feed(r, 2, 30.0);
        r.wc->setQuickConfirm(false);

        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(r.quickCalls, 0);
        QCOMPARE(r.confirms.size(), 1); // normal m_confirm
        QCOMPARE(r.writes->size(), 1);

        // Declined normal confirmation: nothing written
        r.wc->m_confirm = [&](const QString &) { return false; };
        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(r.writes->size(), 1);

        // 16-bit with quickConfirm off: written without asking
        r.confirms.clear();
        r.wc->requestQuickWrite(4, 5.0);
        QCOMPARE(r.writes->size(), 2);
        QCOMPARE(r.quickCalls, 0);
        QVERIFY(r.confirms.isEmpty());

        // quickConfirm on: 32-bit still uses m_confirm, never the check-box prompt
        r.wc->setQuickConfirm(true);
        r.wc->m_confirm = [&](const QString &t) {
            r.confirms << t;
            return true;
        };
        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(r.quickCalls, 0);
        QCOMPARE(r.confirms.size(), 1);
        QCOMPARE(r.writes->size(), 3);
        // and 16-bit uses the quick prompt
        r.wc->requestQuickWrite(4, 5.0);
        QCOMPARE(r.quickCalls, 1);
    }

    // Review fix: the confirmation runs an event loop; what was checked before it must still
    // hold when the write is sent.
    void recheckAfterConfirmation()
    {
        // Tag configuration changed while the dialog was open (byte order of the float tag).
        {
            Rig r;
            setup(r);
            feed(r, 2, 50.0);
            r.wc->m_confirm = [&r](const QString &) {
                r.model.setData(r.model.index(r.model.rowOf(2), TagTableModel::ByteOrder),
                                QStringLiteral("CDAB"));
                return true;
            };
            r.wc->requestWrite(2, 20.0);
            QCOMPARE(r.writes->size(), 0);
            QVERIFY(!r.warns.isEmpty() && r.warns.last().contains("changed"));
        }
        // Disconnected and reconnected while the dialog was open.
        {
            Rig r;
            setup(r);
            feed(r, 2, 50.0);
            r.wc->m_confirm = [&r](const QString &) {
                r.wc->setConnected(false);
                r.wc->setConnected(true);
                feed(r, 2, 50.0);
                return true;
            };
            r.wc->requestWrite(2, 20.0);
            QCOMPARE(r.writes->size(), 0);
        }
        // The 32-bit reading was lost while the dialog was open.
        {
            Rig r;
            setup(r);
            feed(r, 2, 50.0);
            r.wc->m_confirm = [&r](const QString &) {
                r.model.clearLiveValues();
                return true;
            };
            r.wc->requestWrite(2, 20.0);
            QCOMPARE(r.writes->size(), 0);
        }
        // Selection path: a tag edited while the confirmation was open.
        {
            Rig r;
            setup(r);
            feed(r, 2, 50.0);
            r.wc->m_askValues = [](const QList<Tag> &, const QList<double> &) {
                return std::optional<QList<double>>(QList<double>{1.0, 20.0, 1.0});
            };
            r.wc->m_confirm = [&r](const QString &) {
                r.model.setData(r.model.index(r.model.rowOf(3), TagTableModel::Unit),
                                QStringLiteral("kg"));
                return true;
            };
            r.wc->requestWriteSelection({0, 1, 2});
            QCOMPARE(r.writes->size(), 0);
            QVERIFY(!r.warns.isEmpty() && r.warns.last().contains("changed"));
        }
        // Nothing changes: written.
        {
            Rig r;
            setup(r);
            feed(r, 2, 50.0);
            r.wc->requestWrite(2, 20.0);
            QCOMPARE(r.writes->size(), 1);
        }
    }

    // Review fix: a reading decoded with the old configuration must not vouch for a write.
    void configEditForgetsReading()
    {
        Rig r;
        setup(r);
        feed(r, 2, 50.0);
        QVERIFY(r.model.goodValue(2).has_value());
        r.model.setData(r.model.index(r.model.rowOf(2), TagTableModel::Unit), QStringLiteral("m3/h"));
        QVERIFY(r.model.goodValue(2).has_value()); // unit does not change decoding
        r.model.setData(r.model.index(r.model.rowOf(2), TagTableModel::ByteOrder), QStringLiteral("CDAB"));
        QVERIFY(!r.model.goodValue(2).has_value());
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.warns.last().contains("No valid reading of 'Flow'"));
        feed(r, 2, 50.0);
        r.model.setData(r.model.index(r.model.rowOf(2), TagTableModel::Gain), 2.0);
        QVERIFY(!r.model.goodValue(2).has_value());
    }

    // ---- check 3b: unconfirmed 32-bit encoding ----

    void unconfirmedWideCancelWritesNothing()
    {
        Rig r;
        setup(r, false);
        feed(r, 2, 52.3);
        QSignalSpy detects(r.wc.get(), &WriteController::detectEncodingRequested);
        r.encodingAnswer = WriteController::EncodingAnswer::Cancel;
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.encodingPrompts.size(), 1);
        QCOMPARE(r.encodingPrompts.at(0),
                 QStringLiteral("The encoding of 'Flow' is not confirmed: it reads 52.3 as float32 "
                                "CDAB. Writing with a wrong encoding sends a wrong value."));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.confirms.isEmpty());
        QVERIFY(r.warns.isEmpty());
        QCOMPARE(detects.size(), 0);

        // The unit follows the value, with a space
        r.model.setData(r.model.index(r.model.rowOf(2), TagTableModel::Unit), QStringLiteral("C"));
        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(r.encodingPrompts.size(), 2);
        QVERIFY(r.encodingPrompts.at(1).contains(QStringLiteral("reads 52.3 C as float32 CDAB")));
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(r.quickCalls, 0);
    }

    void unconfirmedWideDetectEmitsOnce()
    {
        Rig r;
        setup(r, false);
        feed(r, 2, 52.3);
        QSignalSpy detects(r.wc.get(), &WriteController::detectEncodingRequested);
        r.encodingAnswer = WriteController::EncodingAnswer::Detect;
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.encodingPrompts.size(), 1);
        QCOMPARE(detects.size(), 1);
        QCOMPARE(detects.at(0).at(0).value<TagId>(), TagId(2));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.warns.isEmpty());
        QVERIFY(r.confirms.isEmpty());
    }

    void unconfirmedWideConfirmThenWrites()
    {
        Rig r;
        setup(r, false);
        feed(r, 2, 52.3);
        QSignalSpy detects(r.wc.get(), &WriteController::detectEncodingRequested);
        r.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.encodingPrompts.size(), 1);
        QCOMPARE(r.confirms.size(), 1); // the normal confirmation follows
        QCOMPARE(r.writes->size(), 1);  // the model's stub does not change: the write proceeds
        QCOMPARE(detects.size(), 0);
        QVERIFY(r.warns.isEmpty());
    }

    void encodingPromptOnlyForUnverifiedWide()
    {
        // Verified 32-bit tag: no prompt
        Rig r;
        setup(r);
        feed(r, 2, 52.3);
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.encodingPrompts.size(), 0);
        QCOMPARE(r.writes->size(), 1);

        // 16-bit tag flagged as half of a 32-bit value: prompt (see flagged16Bit*)
        Rig n;
        setup(n, false);
        n.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        n.wc->requestWrite(4, 12.0);
        QCOMPARE(n.encodingPrompts.size(), 1);
        QCOMPARE(n.writes->size(), 1);
    }

    void encodingPromptBeforeConfirmation()
    {
        Rig r;
        setup(r, false);
        feed(r, 2, 52.3);
        QStringList order;
        r.wc->m_confirmEncoding = [&](const QString &) {
            order << QStringLiteral("encoding");
            return WriteController::EncodingAnswer::Confirm;
        };
        r.wc->m_confirm = [&](const QString &) {
            order << QStringLiteral("confirm");
            return true;
        };
        r.wc->requestQuickWrite(2, 20.0);
        QCOMPARE(order, (QStringList{QStringLiteral("encoding"), QStringLiteral("confirm")}));
        QCOMPARE(r.quickCalls, 0);
        QCOMPARE(r.writes->size(), 1);
    }

    void encodingPromptCheckOrder()
    {
        Rig r;
        setup(r, false);
        // Out of limits: refused before the encoding prompt
        feed(r, 2, 52.3);
        r.wc->requestWrite(2, 5.0);
        QCOMPARE(r.warns.size(), 1);
        QVERIFY(r.warns.at(0).contains("'Flow' accepts at least 10"));
        QCOMPARE(r.encodingPrompts.size(), 0);

        // No valid reading: the warning, not the encoding prompt
        feed(r, 2, 0.0, Quality::Timeout);
        r.wc->requestWrite(2, 20.0);
        QCOMPARE(r.warns.size(), 2);
        QVERIFY(r.warns.at(1).contains("No valid reading of 'Flow'"));
        QCOMPARE(r.encodingPrompts.size(), 0);
        QCOMPARE(r.writes->size(), 0);
    }

    void encodingPromptSelection()
    {
        Rig r;
        setup(r, false);
        // Setpoint HR 101 (16-bit), Flow HR 102-103 (float32 CDAB), Count HR 104-105 (int32 ABCD);
        // both 32-bit tags unconfirmed.
        Tag sp = makeTag(1, "Setpoint", true, std::nullopt, std::nullopt);
        sp.type = ValueType::UInt16;
        sp.address = 101;
        Tag fl = makeTag(2, "Flow", true, std::nullopt, std::nullopt);
        fl.type = ValueType::Float32;
        fl.byteOrder = ByteOrder::CDAB;
        fl.address = 102;
        fl.encodingVerified = false;
        Tag cnt = makeTag(3, "Count", true, std::nullopt, std::nullopt);
        cnt.type = ValueType::Int32;
        cnt.address = 104;
        cnt.encodingVerified = false;
        r.model.setTags({sp, fl, cnt}, 0);
        feed(r, 2, 52.3);
        feed(r, 3, 7.0);
        int asked = 0;
        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            ++asked;
            return std::optional<QList<double>>(QList<double>{1.0, 20.0, 1.0});
        };
        QSignalSpy detects(r.wc.get(), &WriteController::detectEncodingRequested);

        // Cancel: one prompt, nothing written
        r.encodingAnswer = WriteController::EncodingAnswer::Cancel;
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(asked, 1);
        QCOMPARE(r.encodingPrompts.size(), 1);
        const QString text = r.encodingPrompts.at(0);
        QVERIFY(text.contains(QStringLiteral("'Flow' (reads 52.3 as float32 CDAB)")));
        QVERIFY(text.contains(QStringLiteral("'Count' (reads 7 as int32 ABCD)")));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.confirms.isEmpty());

        // Detect: emitted for the first unverified tag, nothing written
        r.encodingAnswer = WriteController::EncodingAnswer::Detect;
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(r.encodingPrompts.size(), 2);
        QCOMPARE(detects.size(), 1);
        QCOMPARE(detects.at(0).at(0).value<TagId>(), TagId(2));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.warns.isEmpty());

        // Confirm: the write proceeds after the normal confirmation
        r.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(r.encodingPrompts.size(), 3);
        QCOMPARE(r.confirms.size(), 1);
        QCOMPARE(r.writes->size(), 1);
        QCOMPARE(detects.size(), 1);
        QVERIFY(r.warns.isEmpty());
    }

    void encodingPromptSelectionCheckOrder()
    {
        Rig r;
        setup(r, false);
        feed(r, 2, 52.3);
        // Out of limits: refused before the encoding prompt
        int asked = 0;
        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            ++asked;
            return std::optional<QList<double>>(QList<double>{150.0, 20.0, 7.0});
        };
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(asked, 1);
        QCOMPARE(r.warns.size(), 1);
        QVERIFY(r.warns.at(0).contains("Setpoint"));
        QCOMPARE(r.encodingPrompts.size(), 0);
        QCOMPARE(r.writes->size(), 0);
    }

    // ---- check 3b for 16-bit tags flagged as probably half of a 32-bit value ----

    // "Plain" (UInt16 HR 200, no limits) flagged: the prompt names the note, else the default.
    void flagged16BitPromptText()
    {
        Rig r;
        setup(r);
        r.model.setEncodingVerified(4, false);
        r.model.setEncodingNote(4, QStringLiteral("HR 200-201 read as float32 ABCD give 10"));
        r.encodingAnswer = WriteController::EncodingAnswer::Cancel;
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 1);
        QCOMPARE(r.encodingPrompts.at(0),
                 QStringLiteral("'Plain' is probably half of a 32-bit value: HR 200-201 read as float32 "
                                "ABCD give 10. Writing a 16-bit value there changes half of that value, and "
                                "the device then reads a wrong number."));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.confirms.isEmpty());
        QVERIFY(r.warns.isEmpty());
        QVERIFY(!r.model.tagAt(r.model.rowOf(4)).encodingVerified);

        // No note: the default text with the register number
        r.model.setEncodingNote(4, QString());
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 2);
        QCOMPARE(r.encodingPrompts.at(1),
                 QStringLiteral("'Plain' (HR 200) may be half of a 32-bit value. Writing a 16-bit value "
                                "there changes half of that value, and the device then reads a wrong "
                                "number."));

        // Address base 1: the register is shown as HR 201
        r.model.setAddressBase(1);
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 3);
        QVERIFY(r.encodingPrompts.at(2).contains(QStringLiteral("'Plain' (HR 201) may be half of a 32-bit value.")));
        QCOMPARE(r.writes->size(), 0);
    }

    // Cancel writes nothing, Detect emits once and writes nothing, Confirm verifies and writes.
    void flagged16BitCancelDetectConfirm()
    {
        Rig r;
        setup(r);
        r.model.setEncodingVerified(4, false);
        QSignalSpy detects(r.wc.get(), &WriteController::detectEncodingRequested);

        r.encodingAnswer = WriteController::EncodingAnswer::Cancel;
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 1);
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(detects.size(), 0);

        r.encodingAnswer = WriteController::EncodingAnswer::Detect;
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 2);
        QCOMPARE(detects.size(), 1);
        QCOMPARE(detects.at(0).at(0).value<TagId>(), TagId(4));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.confirms.isEmpty());
        QVERIFY(r.warns.isEmpty());

        r.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 3);
        QCOMPARE(r.confirms.size(), 1); // the normal confirmation follows
        QCOMPARE(r.writes->size(), 1);
        QCOMPARE(detects.size(), 1);
        QVERIFY(r.model.tagAt(r.model.rowOf(4)).encodingVerified);

        // Now verified: no more prompt
        r.wc->requestWrite(4, 13.0);
        QCOMPARE(r.encodingPrompts.size(), 3);
        QCOMPARE(r.writes->size(), 2);
    }

    // An unflagged 16-bit tag gets no prompt.
    void unflagged16BitNoPrompt()
    {
        Rig r;
        setup(r);
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.encodingPrompts.size(), 0);
        QCOMPARE(r.writes->size(), 1);
    }

    // A selection with a flagged 16-bit tag gets one prompt that names it.
    void flagged16BitSelectionOnePrompt()
    {
        Rig r;
        setup(r); // Setpoint HR 101 (16-bit), Flow HR 102-103 (float32), Next HR 104 (16-bit)
        r.model.setEncodingVerified(1, false);
        feed(r, 2, 52.3);
        int asked = 0;
        r.wc->m_askValues = [&](const QList<Tag> &, const QList<double> &) {
            ++asked;
            return std::optional<QList<double>>(QList<double>{50.0, 20.0, 7.0});
        };

        r.encodingAnswer = WriteController::EncodingAnswer::Cancel;
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(asked, 1);
        QCOMPARE(r.encodingPrompts.size(), 1);
        QVERIFY(r.encodingPrompts.at(0).contains(QStringLiteral("'Setpoint' (probably half of a 32-bit value)")));
        QCOMPARE(r.writes->size(), 0);
        QVERIFY(r.confirms.isEmpty());

        r.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        r.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(r.encodingPrompts.size(), 2);
        QCOMPARE(r.confirms.size(), 1);
        QCOMPARE(r.writes->size(), 1);
        QVERIFY(r.model.tagAt(r.model.rowOf(1)).encodingVerified);
        QVERIFY(r.warns.isEmpty());
    }

    // A tag that becomes unverified while the confirmation is open is not written (any width).
    void unverifiedDuringConfirmationNotWritten()
    {
        // 16-bit, single write
        Rig r;
        setup(r);
        r.model.setEncodingVerified(4, false);
        r.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        r.wc->m_confirm = [&r](const QString &) {
            r.model.setEncodingVerified(4, false); // flagged again while the dialog is open
            return true;
        };
        r.wc->requestWrite(4, 12.0);
        QCOMPARE(r.writes->size(), 0);
        QCOMPARE(r.warns.size(), 1);
        QVERIFY(r.warns.at(0).contains(QStringLiteral("'Plain'")));

        // 32-bit, single write
        Rig w;
        setup(w);
        feed(w, 2, 52.3);
        w.model.setEncodingVerified(2, false);
        w.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        w.wc->m_confirm = [&w](const QString &) {
            w.model.setEncodingVerified(2, false);
            return true;
        };
        w.wc->requestWrite(2, 20.0);
        QCOMPARE(w.writes->size(), 0);
        QCOMPARE(w.warns.size(), 1);
        QVERIFY(w.warns.at(0).contains(QStringLiteral("'Flow'")));

        // 16-bit, selection
        Rig s;
        setup(s);
        s.model.setEncodingVerified(1, false);
        feed(s, 2, 52.3);
        s.wc->m_askValues = [](const QList<Tag> &, const QList<double> &) {
            return std::optional<QList<double>>(QList<double>{50.0, 20.0, 7.0});
        };
        s.encodingAnswer = WriteController::EncodingAnswer::Confirm;
        s.wc->m_confirm = [&s](const QString &) {
            s.model.setEncodingVerified(1, false);
            return true;
        };
        s.wc->requestWriteSelection({0, 1, 2});
        QCOMPARE(s.writes->size(), 0);
        QCOMPARE(s.warns.size(), 1);
    }

    // A device without FC16 rejects a 32-bit write with exception 1: offer FC06 only.
    void fc16RejectionOffersSingleWrites()
    {
        auto rejected = [](int code) {
            ModbusResult r;
            r.error = ModbusError::Exception;
            r.exceptionCode = code;
            r.message = QStringLiteral("Modbus exception %1").arg(code);
            return r;
        };
        auto lastRequest = [](Rig &r) { return r.writes->last().at(0).toULongLong(); };

        for (const bool accept : {true, false}) {
            Rig r;
            setup(r);
            int offered = 0;
            r.wc->m_offerSingleWrites = [&](const QString &text) {
                ++offered;
                return accept && text.contains(QStringLiteral("Flow"));
            };
            QSignalSpy requested(r.wc.get(), &WriteController::singleRegisterWritesRequested);
            feed(r, 2, 50.0);
            r.wc->requestWrite(2, 20.0); // float32: two registers, FC16
            QCOMPARE(r.writes->size(), 1);
            r.wc->onWriteFinished(lastRequest(r), rejected(1));
            QCOMPARE(offered, 1);
            QCOMPARE(requested.size(), accept ? 1 : 0);
        }

        // Not offered: other exceptions, single-register writes, or FC06 mode already on.
        Rig r;
        setup(r);
        int offered = 0;
        r.wc->m_offerSingleWrites = [&](const QString &) { ++offered; return true; };
        feed(r, 2, 50.0);
        r.wc->requestWrite(2, 20.0);
        r.wc->onWriteFinished(lastRequest(r), rejected(2));
        r.wc->requestWrite(4, 7.0); // 16-bit tag: FC06 anyway
        r.wc->onWriteFinished(lastRequest(r), rejected(1));
        r.wc->setSingleRegisterWrites(true);
        r.wc->requestWrite(2, 20.0);
        r.wc->onWriteFinished(lastRequest(r), rejected(1));
        QCOMPARE(offered, 0);
    }
};

QTEST_MAIN(TstQuickWrite)
#include "tst_quickwrite.moc"
