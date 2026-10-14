#include "AddTagDialog.h"

#include "EncodingDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QtTest>

#include <cstring>
#include <functional>

using namespace mb;

namespace {

/// A fake device: answers from `regs`; addresses in `refuseSingle` refuse one-register reads with
/// exception 2; anything not in `regs` gives exception 2. Answers arrive from the event loop.
struct Fake {
    struct Call {
        Area area;
        int address;
        int count;
    };
    QHash<int, quint16> regs;
    QSet<int> refuseSingle;
    QList<Call> calls;
    bool hold = false;
    QList<std::function<void()>> pending;
    ModbusResult failWith; ///< when error != None every read fails with this

    AddTagDialog::ReadFn fn()
    {
        return [this](Area area, quint16 address, int count, AddTagDialog::ReadDone done) {
            calls.append({area, address, count});
            ModbusResult result;
            QList<quint16> values;
            if (failWith.error != ModbusError::None) {
                result = failWith;
            } else if (count == 1 && refuseSingle.contains(address)) {
                result.error = ModbusError::Exception;
                result.exceptionCode = 2;
            } else {
                for (int i = 0; i < count; ++i) {
                    if (!regs.contains(address + i)) {
                        values.clear();
                        result.error = ModbusError::Exception;
                        result.exceptionCode = 2;
                        break;
                    }
                    values.append(regs.value(address + i));
                }
            }
            auto answer = [done, result, values] { done(result, values); };
            if (hold)
                pending.append(answer);
            else
                QTimer::singleShot(0, answer);
        };
    }
    void release()
    {
        const auto list = pending;
        pending.clear();
        for (const auto &f : list)
            f();
    }
    /// Stores 52.3f in registers at `start`, in the given word order (ABCD or CDAB).
    void putFloat(int start, float v, bool wordSwapped)
    {
        quint32 bits = 0;
        std::memcpy(&bits, &v, 4);
        const quint16 hi = quint16(bits >> 16), lo = quint16(bits & 0xFFFF);
        regs[start] = wordSwapped ? lo : hi;
        regs[start + 1] = wordSwapped ? hi : lo;
    }
};

} // namespace

class TstAddTagDialog : public QObject
{
    Q_OBJECT

    static QLineEdit *address(AddTagDialog &d) { return d.findChild<QLineEdit *>("addressEdit"); }
    static QComboBox *area(AddTagDialog &d) { return d.findChild<QComboBox *>("areaCombo"); }
    static QComboBox *type(AddTagDialog &d) { return d.findChild<QComboBox *>("typeCombo"); }
    static QComboBox *order(AddTagDialog &d) { return d.findChild<QComboBox *>("orderCombo"); }
    static QLabel *overlap(AddTagDialog &d) { return d.findChild<QLabel *>("overlapLabel"); }
    static QLabel *preview(AddTagDialog &d) { return d.findChild<QLabel *>("previewLabel"); }
    static QLabel *detect(AddTagDialog &d) { return d.findChild<QLabel *>("detectLabel"); }
    static QPushButton *readButton(AddTagDialog &d) { return d.findChild<QPushButton *>("readButton"); }
    static QPushButton *detectButton(AddTagDialog &d) { return d.findChild<QPushButton *>("detectButton"); }
    static QPushButton *ok(AddTagDialog &d) { return d.m_buttons->button(QDialogButtonBox::Ok); }
    static QString plain(QLabel *label)
    {
        QTextDocument doc;
        doc.setHtml(label->text());
        return doc.toPlainText().simplified();
    }
    static Tag existingFloat(const QString &name, quint16 addr)
    {
        Tag t;
        t.id = 1;
        t.name = name;
        t.address = addr;
        t.type = ValueType::Float32;
        return t;
    }
    /// Connected dialog with area = holding register and the given address typed.
    static void choose(AddTagDialog &d, const QString &addr, int areaIndex = 0)
    {
        area(d)->setCurrentIndex(areaIndex);
        address(d)->setText(addr);
    }

private slots:
    void initialState()
    {
        AddTagDialog d({}, 0, std::nullopt);
        QCOMPARE(d.windowTitle(), QStringLiteral("Add tag"));
        QCOMPARE(area(d)->currentIndex(), -1);
        QVERIFY(address(d)->text().isEmpty());
        QVERIFY(!ok(d)->isEnabled());
        QVERIFY(!readButton(d)->isEnabled());
        QVERIFY(!detectButton(d)->isEnabled());
        QVERIFY(plain(preview(d)).contains(QStringLiteral("Connect to read the register")));
        QVERIFY(!order(d)->isEnabled());
        QCOMPARE(type(d)->currentText(), QStringLiteral("int16"));
        QVERIFY(!d.detectionConfirmed());
        QVERIFY(overlap(d)->isHidden());
        // connected, nothing chosen: empty preview, Read still disabled
        Fake fake;
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        QVERIFY(preview(d)->text().isEmpty());
        QVERIFY(!readButton(d)->isEnabled());
    }

    void addressBaseOne()
    {
        AddTagDialog d({}, 1, std::nullopt);
        area(d)->setCurrentIndex(0);
        address(d)->setText("0");
        QVERIFY(!ok(d)->isEnabled());
        address(d)->setText("1");
        QVERIFY(ok(d)->isEnabled());
        QCOMPARE(d.tag().address, quint16(0));
        address(d)->setText("111");
        QCOMPARE(d.tag().address, quint16(110));
        address(d)->setText("65536");
        QVERIFY(ok(d)->isEnabled());
        QCOMPARE(d.tag().address, quint16(65535));
        address(d)->setText("65537");
        QVERIFY(!ok(d)->isEnabled());
        address(d)->clear();
        QVERIFY(!ok(d)->isEnabled());
    }

    void overlapFloat32AndLimit()
    {
        AddTagDialog d({existingFloat(QStringLiteral("Setpoint"), 110)}, 0, std::nullopt);
        choose(d, "111");
        QVERIFY(!overlap(d)->isHidden());
        QCOMPARE(overlap(d)->text(), QStringLiteral("Overlaps 'Setpoint' (HR 110-111)."));
        QVERIFY(!ok(d)->isEnabled());
        address(d)->setText("112");
        QVERIFY(overlap(d)->isHidden());
        QVERIFY(ok(d)->isEnabled());
        // a 32-bit new tag at 111 reaches into 112..: free; at 109 it overlaps 110
        address(d)->setText("109");
        type(d)->setCurrentIndex(type(d)->findText("float32"));
        QVERIFY(!overlap(d)->isHidden());
        type(d)->setCurrentIndex(type(d)->findText("int16"));
        QVERIFY(overlap(d)->isHidden());
        // other area: no overlap
        address(d)->setText("110");
        QVERIFY(!overlap(d)->isHidden());
        area(d)->setCurrentIndex(1);
        QVERIFY(overlap(d)->isHidden());
        // past the end
        address(d)->setText("65535");
        QVERIFY(overlap(d)->isHidden());
        type(d)->setCurrentIndex(type(d)->findText("float32"));
        QVERIFY(!overlap(d)->isHidden());
        QCOMPARE(overlap(d)->text(),
                 QStringLiteral("A 32-bit value needs two registers: IR 65535 is the last one."));
        QVERIFY(!ok(d)->isEnabled());
        area(d)->setCurrentIndex(0);
        QCOMPARE(overlap(d)->text(),
                 QStringLiteral("A 32-bit value needs two registers: HR 65535 is the last one."));
    }

    void overlapWithBaseOne()
    {
        AddTagDialog d({existingFloat(QStringLiteral("Setpoint"), 110)}, 1, std::nullopt);
        choose(d, "112");
        QCOMPARE(overlap(d)->text(), QStringLiteral("Overlaps 'Setpoint' (HR 111-112)."));
    }

    void writableOnlyForHolding()
    {
        AddTagDialog d({}, 0, std::nullopt);
        auto *w = d.findChild<QCheckBox *>("writableCheck");
        QVERIFY(!w->isEnabled());
        area(d)->setCurrentIndex(0);
        QVERIFY(w->isEnabled());
        w->setChecked(true);
        address(d)->setText("5");
        QVERIFY(d.tag().writable);
        area(d)->setCurrentIndex(1);
        QVERIFY(!w->isEnabled());
        QVERIFY(!w->isChecked());
        QVERIFY(!d.tag().writable);
    }

    void orderOnlyFor32Bit()
    {
        AddTagDialog d({}, 0, std::nullopt);
        QVERIFY(!order(d)->isEnabled());
        type(d)->setCurrentIndex(type(d)->findText("uint32"));
        QVERIFY(order(d)->isEnabled());
        type(d)->setCurrentIndex(type(d)->findText("uint16"));
        QVERIFY(!order(d)->isEnabled());
    }

    void readFloatCdab()
    {
        Fake fake;
        fake.regs[109] = 0;
        fake.putFloat(110, 52.3f, true);
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QVERIFY(readButton(d)->isEnabled());
        QVERIFY(detectButton(d)->isEnabled());
        QTest::mouseClick(readButton(d), Qt::LeftButton);
        QVERIFY(!readButton(d)->isEnabled()); // probe running
        QTRY_VERIFY(plain(preview(d)).contains(QStringLiteral("HR 110-111 as float32")));
        const QString text = plain(preview(d));
        QVERIFY2(text.contains(QStringLiteral("HR 109: 0x0000")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("HR 110: 0x3333")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("HR 111: 0x4251")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("HR 110 as int16: 13107")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("52.3 (CDAB)")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("HR 109-110 as float32")), qPrintable(text));
        QVERIFY(readButton(d)->isEnabled());
        // sequence: own, below, above
        QCOMPARE(fake.calls.size(), 3);
        QCOMPARE(fake.calls[0].address, 110);
        QCOMPARE(fake.calls[1].address, 109);
        QCOMPARE(fake.calls[2].address, 111);
        QCOMPARE(fake.calls[0].count, 1);
    }

    void readRefusedSingleCm50()
    {
        Fake fake;
        fake.refuseSingle.insert(110);
        fake.regs[109] = 0;
        fake.putFloat(110, 52.3f, true);
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::mouseClick(readButton(d), Qt::LeftButton);
        QTRY_VERIFY(plain(preview(d)).contains(QStringLiteral("can't be read alone")));
        const QString text = plain(preview(d));
        QVERIFY2(text.contains(QStringLiteral("HR 110 can't be read alone (illegal data address).")),
                 qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("HR 110-111 as float32")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("52.3 (CDAB)")), qPrintable(text));
        QVERIFY(!text.contains(QStringLiteral("as int16")));
        QCOMPARE(fake.calls.size(), 3); // (110,1), (110,2), (109,2)
        QCOMPARE(fake.calls[1].address, 110);
        QCOMPARE(fake.calls[1].count, 2);
        QCOMPARE(fake.calls[2].address, 109);
        QCOMPARE(fake.calls[2].count, 2);
    }

    void readOtherError()
    {
        Fake fake;
        fake.failWith.error = ModbusError::Timeout;
        fake.failWith.message = QStringLiteral("Connection timed out");
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::mouseClick(readButton(d), Qt::LeftButton);
        QTRY_VERIFY(plain(preview(d)).contains(QStringLiteral("could not be read")));
        QVERIFY(plain(preview(d)).contains(QStringLiteral("HR 110 could not be read: Connection timed out.")));
        QCOMPARE(fake.calls.size(), 1);
    }

    void autoProbeAfterDelay()
    {
        Fake fake;
        fake.regs[109] = 1;
        fake.regs[110] = 2;
        fake.regs[111] = 3;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::qWait(100);
        QVERIFY(fake.calls.isEmpty());
        QTRY_VERIFY(plain(preview(d)).contains(QStringLiteral("HR 110: 0x0002")));
        // not read while disconnected
        fake.calls.clear();
        d.setConnected(false);
        address(d)->setText("111");
        QTest::qWait(600);
        QVERIFY(fake.calls.isEmpty());
        QVERIFY(plain(preview(d)).contains(QStringLiteral("Connect to read")));
        QVERIFY(!readButton(d)->isEnabled());
    }

    void lateAnswersAreIgnored()
    {
        Fake fake;
        fake.regs[109] = 1;
        fake.regs[110] = 2;
        fake.regs[111] = 3;
        fake.regs[120] = 9;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        fake.hold = true;
        QTest::mouseClick(readButton(d), Qt::LeftButton);
        QCOMPARE(fake.pending.size(), 1);
        address(d)->setText("120"); // cancels
        fake.release();
        QTest::qWait(50);
        QVERIFY(preview(d)->text().isEmpty());
        QCOMPARE(fake.calls.size(), 1); // no follow-up reads of the cancelled probe
        QVERIFY(readButton(d)->isEnabled());
        // disconnect also cancels
        fake.hold = true;
        QTest::mouseClick(readButton(d), Qt::LeftButton);
        d.setConnected(false);
        fake.release();
        QTest::qWait(50);
        QVERIFY(plain(preview(d)).contains(QStringLiteral("Connect to read")));
    }

    void answerAfterDialogDestroyed()
    {
        Fake fake;
        fake.regs[110] = 2;
        fake.hold = true;
        {
            AddTagDialog d({}, 0, std::nullopt);
            d.setReadFunction(fake.fn());
            d.setConnected(true);
            choose(d, "110");
            QTest::mouseClick(readButton(d), Qt::LeftButton);
        }
        fake.release(); // must not crash
        QVERIFY(true);
    }

    void detectSixteen()
    {
        Fake fake;
        fake.regs[109] = 5;
        fake.regs[110] = 1234;
        fake.regs[111] = 7;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        type(d)->setCurrentIndex(type(d)->findText("float32")); // wrong guess, Detect corrects it
        bool asked = false;
        d.m_runEncodingDialog = [&](EncodingDialog &) { asked = true; return true; };
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(d.detectionConfirmed());
        QVERIFY(!asked);
        QCOMPARE(type(d)->currentText(), QStringLiteral("int16"));
        QVERIFY(d.tag().type == ValueType::Int16);
        QVERIFY2(plain(detect(d)).contains(
                     QStringLiteral("HR 110 holds a 16-bit value (1234): no float32 around it.")),
                 qPrintable(plain(detect(d))));
        // a later change clears the flag
        address(d)->setText("111");
        QVERIFY(!d.detectionConfirmed());
        QVERIFY(detect(d)->text().isEmpty());
    }

    void detectSixteenKeepsUint16()
    {
        Fake fake;
        fake.regs[109] = 5;
        fake.regs[110] = 40000;
        fake.regs[111] = 7;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        type(d)->setCurrentIndex(type(d)->findText("uint16"));
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(d.detectionConfirmed());
        QVERIFY(d.tag().type == ValueType::UInt16);
        QVERIFY(plain(detect(d)).contains(QStringLiteral("(40000)")));
    }

    void typeChangeClearsConfirmed()
    {
        Fake fake;
        fake.regs[109] = 5;
        fake.regs[110] = 1234;
        fake.regs[111] = 7;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(d.detectionConfirmed());
        type(d)->setCurrentIndex(type(d)->findText("uint16"));
        QVERIFY(!d.detectionConfirmed());
    }

    void detectThirtyTwoMovesAddress()
    {
        Fake fake;
        fake.regs[108] = 0;
        fake.putFloat(109, 52.3f, false); // ABCD at 109-110, user typed 110 (the low word)
        fake.regs[111] = 0;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        EncodingDialog *seen = nullptr;
        d.m_runEncodingDialog = [&](EncodingDialog &dlg) {
            seen = &dlg;
            return true; // best group is preselected
        };
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(d.detectionConfirmed());
        QVERIFY(seen);
        QCOMPARE(address(d)->text(), QStringLiteral("109"));
        QCOMPARE(d.tag().address, quint16(109));
        QVERIFY(d.tag().type == ValueType::Float32);
        QVERIFY(d.tag().byteOrder == ByteOrder::ABCD);
        QVERIFY(order(d)->isEnabled());
        const QString text = plain(detect(d));
        QVERIFY2(text.contains(QStringLiteral("float32 ABCD, HR 109-110.")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("The value starts one register earlier: the address is now HR 109.")),
                 qPrintable(text));
        // the address change by Detect did not schedule a new probe that would reset the answer
        QTest::qWait(600);
        QVERIFY(d.detectionConfirmed());
        // editing afterwards clears it
        order(d)->setCurrentIndex(order(d)->findText("CDAB"));
        QVERIFY(!d.detectionConfirmed());
    }

    void detectThirtyTwoRefusedAddsIntro()
    {
        Fake fake;
        fake.refuseSingle.insert(110);
        fake.regs[109] = 0;
        fake.putFloat(110, 52.3f, true);
        AddTagDialog d({existingFloat(QStringLiteral("Other"), 200)}, 0, ByteOrder::CDAB);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QString intro;
        d.m_runEncodingDialog = [&](EncodingDialog &dlg) {
            auto *label = dlg.findChild<QLabel *>("introLabel");
            intro = label ? label->text() : QString();
            return true;
        };
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(d.detectionConfirmed());
        QVERIFY2(intro.contains(QStringLiteral("HR 110 can't be read alone.")), qPrintable(intro));
        QVERIFY2(intro.contains(QStringLiteral("Choose the encoding of HR 110-111")), qPrintable(intro));
        QCOMPARE(address(d)->text(), QStringLiteral("110"));
        QVERIFY(d.tag().type == ValueType::Float32);
        QVERIFY(d.tag().byteOrder == ByteOrder::CDAB);
        QVERIFY(!plain(detect(d)).contains(QStringLiteral("earlier")));
    }

    void detectThirtyTwoCancelled()
    {
        Fake fake;
        fake.regs[108] = 0;
        fake.putFloat(109, 52.3f, false);
        fake.regs[111] = 0;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        bool asked = false;
        d.m_runEncodingDialog = [&](EncodingDialog &) {
            asked = true;
            return false;
        };
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(asked);
        QTest::qWait(50);
        QVERIFY(!d.detectionConfirmed());
        QCOMPARE(address(d)->text(), QStringLiteral("110"));
        QVERIFY(d.tag().type == ValueType::Int16);
        QVERIFY(detectButton(d)->isEnabled());
    }

    void detectOwnZero()
    {
        Fake fake;
        fake.regs[109] = 0;
        fake.regs[110] = 0;
        fake.regs[111] = 0;
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(!detect(d)->text().isEmpty());
        QVERIFY(plain(detect(d)).contains(QStringLiteral("HR 110 reads 0: can't tell")));
        QVERIFY(!d.detectionConfirmed());
    }

    void detectRefusedNoPair()
    {
        Fake fake; // nothing readable
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(!detect(d)->text().isEmpty());
        QVERIFY(plain(detect(d)).contains(
            QStringLiteral("HR 110 can't be read, alone or as part of a 32-bit value")));
        QVERIFY(!d.detectionConfirmed());
    }

    void detectNotRead()
    {
        Fake fake;
        fake.failWith.error = ModbusError::Timeout;
        fake.failWith.message = QStringLiteral("timeout");
        AddTagDialog d({}, 0, std::nullopt);
        d.setReadFunction(fake.fn());
        d.setConnected(true);
        choose(d, "110");
        QTest::mouseClick(detectButton(d), Qt::LeftButton);
        QTRY_VERIFY(!detect(d)->text().isEmpty());
        QVERIFY(plain(detect(d)).contains(QStringLiteral("HR 110 could not be read: timeout.")));
    }

    void tagContents()
    {
        AddTagDialog d({}, 0, std::nullopt);
        d.findChild<QLineEdit *>("nameEdit")->setText(QStringLiteral("  Flow  "));
        d.findChild<QLineEdit *>("unitEdit")->setText(QStringLiteral("m3/h"));
        choose(d, "42", 0);
        type(d)->setCurrentIndex(type(d)->findText("float32"));
        order(d)->setCurrentIndex(order(d)->findText("BADC"));
        d.findChild<QCheckBox *>("writableCheck")->setChecked(true);
        Tag t = d.tag();
        QCOMPARE(t.id, TagId(0));
        QCOMPARE(t.name, QStringLiteral("Flow"));
        QVERIFY(t.area == Area::HoldingRegister);
        QCOMPARE(t.address, quint16(42));
        QVERIFY(t.type == ValueType::Float32);
        QVERIFY(t.byteOrder == ByteOrder::BADC);
        QCOMPARE(t.unit, QStringLiteral("m3/h"));
        QVERIFY(t.writable);
        QVERIFY(t.encodingVerified);
        area(d)->setCurrentIndex(1);
        t = d.tag();
        QVERIFY(t.area == Area::InputRegister);
        QVERIFY(!t.writable);
        d.findChild<QLineEdit *>("nameEdit")->clear();
        QVERIFY(d.tag().name.isEmpty());
    }
};

QTEST_MAIN(TstAddTagDialog)
#include "tst_addtagdialog.moc"
