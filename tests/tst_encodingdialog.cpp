#include "EncodingDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

#include <memory>

using namespace mb;

namespace {

Tag floatTag()
{
    Tag t;
    t.id = 1;
    t.name = QStringLiteral("Pressure");
    t.area = Area::HoldingRegister;
    t.address = 100;
    t.type = ValueType::Float32;
    t.unit = QStringLiteral("bar");
    return t;
}

/// Another 32-bit tag; unconfirmed unless `verified` (only unconfirmed ones are offered).
Tag otherTag(TagId id, const QString &name, ByteOrder order, bool verified = false)
{
    Tag t = floatTag();
    t.id = id;
    t.name = name;
    t.address = quint16(200 + 2 * id);
    t.byteOrder = order;
    t.encodingVerified = verified;
    return t;
}

struct G {
    QList<Encoding> members;
    double score = 0.0;
    double value = 0.0;
    std::optional<bool> match;
};

Encoding enc(ValueType t, ByteOrder o)
{
    return Encoding{t, o};
}

EncodingResult makeResult(EncodingVerdict verdict, const QList<G> &groups,
                          std::optional<Encoding> best)
{
    EncodingResult r;
    r.verdict = verdict;
    r.best = best;
    r.groupCount = int(groups.size());
    for (int i = 0; i < groups.size(); ++i) {
        for (const Encoding &e : groups.at(i).members) {
            EncodingCandidate c;
            c.encoding = e;
            c.values = {groups.at(i).value + 1.0, groups.at(i).value};
            c.score = groups.at(i).score;
            c.matchesDisplay = groups.at(i).match;
            c.group = i;
            r.candidates.append(c);
        }
    }
    return r;
}

const Encoding kF_ABCD = enc(ValueType::Float32, ByteOrder::ABCD);
const Encoding kF_CDAB = enc(ValueType::Float32, ByteOrder::CDAB);
const Encoding kI_CDAB = enc(ValueType::Int32, ByteOrder::CDAB);
const Encoding kU_CDAB = enc(ValueType::UInt32, ByteOrder::CDAB);
const Encoding kI_ABCD = enc(ValueType::Int32, ByteOrder::ABCD);

} // namespace

class TstEncodingDialog : public QObject
{
    Q_OBJECT

    // Calls recorded by the fake ranking.
    struct Spy {
        QList<RegisterPair> readings;
        EncodingOptions options;
        int calls = 0;
    };

    static void install(EncodingDialog &dlg, const std::shared_ptr<Spy> &spy,
                        const EncodingResult &r)
    {
        dlg.m_detect = [spy, r](const Tag &, const QList<RegisterPair> &readings,
                                const EncodingOptions &options) {
            spy->readings = readings;
            spy->options = options;
            ++spy->calls;
            return r;
        };
    }

    static QTableWidget *table(EncodingDialog &d)
    {
        return d.findChild<QTableWidget *>(QStringLiteral("candidateTable"));
    }
    static QLabel *label(EncodingDialog &d, const char *name)
    {
        return d.findChild<QLabel *>(QString::fromLatin1(name));
    }
    static QCheckBox *others(EncodingDialog &d)
    {
        return d.findChild<QCheckBox *>(QStringLiteral("applyOthersCheck"));
    }
    static QPushButton *okButton(EncodingDialog &d)
    {
        return d.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    }
    static QString cell(EncodingDialog &d, int row, int col)
    {
        return table(d)->item(row, col)->text();
    }

    /// Dialog with a fake ranking applied through setDisplayValue("").
    static std::unique_ptr<EncodingDialog> make(const EncodingResult &r, std::shared_ptr<Spy> spy,
                                                const QList<RegisterPair> &readings = {
                                                    {0x4251, 0x3333}},
                                                const QList<Tag> &oth = {},
                                                std::optional<ByteOrder> devOrder = std::nullopt,
                                                int base = 0, const Tag &tag = floatTag())
    {
        auto d = std::make_unique<EncodingDialog>(tag, readings, oth, devOrder, base);
        install(*d, spy, r);
        d->setDisplayValue(QString());
        return d;
    }

    static EncodingResult twoGroups()
    {
        return makeResult(EncodingVerdict::Clear,
                          {G{{kF_CDAB, kI_CDAB}, 0.9, 52.5, std::nullopt},
                           G{{kF_ABCD}, 0.2, 1.5e9, std::nullopt}},
                          kF_CDAB);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ModbusControlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_encodingdialog"));
    }

    void titleAndNoReading()
    {
        EncodingDialog dlg(floatTag(), {}, {}, std::nullopt);
        QCOMPARE(dlg.windowTitle(), QStringLiteral("Detect encoding - Pressure"));
        QCOMPARE(label(dlg, "registersLabel")->text(), QStringLiteral("No reading yet"));
        QVERIFY(!okButton(dlg)->isEnabled() || table(dlg)->rowCount() > 0);
        QCOMPARE(okButton(dlg)->text(), QStringLiteral("Apply"));
        QVERIFY(table(dlg)->editTriggers() == QAbstractItemView::NoEditTriggers);
        QVERIFY(table(dlg)->selectionMode() == QAbstractItemView::SingleSelection);
    }

    void sixteenBitTagIsUndecidable()
    {
        Tag t = floatTag();
        t.type = ValueType::UInt16;
        EncodingDialog dlg(t, {{5, 0}}, {}, std::nullopt);
        QCOMPARE(dlg.result().verdict, EncodingVerdict::Undecidable);
        QCOMPARE(table(dlg)->rowCount(), 0);
        QVERIFY(!okButton(dlg)->isEnabled());
        QVERIFY(label(dlg, "verdictLabel")->text().startsWith(QStringLiteral("Every encoding")));
        QVERIFY(dlg.applyOrderTo().isEmpty());
        QVERIFY(!dlg.chosenEncoding());
    }

    void noReadingsUndecidableSentence()
    {
        EncodingDialog dlg(floatTag(), {}, {}, std::nullopt);
        QCOMPARE(dlg.result().verdict, EncodingVerdict::Undecidable);
        QCOMPARE(label(dlg, "verdictLabel")->text(),
                 QStringLiteral("Every encoding reads the same value (e.g. 0): wait for another "
                                "value or type the value shown on the device."));
    }

    void registersLabelAndBestSelected()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        QCOMPARE(label(*d, "registersLabel")->text(),
                 QStringLiteral("HR 100-101: 0x4251 0x3333"));
        QCOMPARE(table(*d)->rowCount(), 2);
        QCOMPARE(table(*d)->currentRow(), 0);
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kF_CDAB));
        QVERIFY(okButton(*d)->isEnabled());
    }

    void inputRegisterAndAddressBase()
    {
        Tag t = floatTag();
        t.area = Area::InputRegister;
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy, {{0x00AB, 0xFFFF}}, {}, std::nullopt, 1, t);
        QCOMPARE(label(*d, "registersLabel")->text(),
                 QStringLiteral("IR 101-102: 0x00AB 0xFFFF"));
    }

    void verdictSentences()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(makeResult(EncodingVerdict::Clear, {G{{kF_CDAB}, 0.9, 1, {}}}, kF_CDAB), spy);
        QCOMPARE(label(*d, "verdictLabel")->text(), QStringLiteral("float32 CDAB fits best."));

        install(*d, spy, makeResult(EncodingVerdict::Ambiguous, {G{{kF_CDAB}, 0.6, 1, {}}}, kF_CDAB));
        d->setDisplayValue(QString());
        QCOMPARE(label(*d, "verdictLabel")->text(),
                 QStringLiteral("Several encodings are possible: type the value shown on the "
                                "device to decide."));

        install(*d, spy, makeResult(EncodingVerdict::Undecidable, {G{{kF_CDAB}, 0.0, 0, {}}}, kF_CDAB));
        d->setDisplayValue(QString());
        QCOMPARE(label(*d, "verdictLabel")->text(),
                 QStringLiteral("Every encoding reads the same value (e.g. 0): wait for another "
                                "value or type the value shown on the device."));

        install(*d, spy, makeResult(EncodingVerdict::NoMatch, {G{{kF_CDAB}, 0.6, 1, false}}, std::nullopt));
        d->setDisplayValue(QStringLiteral("99"));
        QCOMPARE(label(*d, "verdictLabel")->text(),
                 QStringLiteral("No encoding gives the value shown on the device: check the "
                                "register number."));
    }

    void tableContents()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(makeResult(EncodingVerdict::Ambiguous,
                                 {G{{kF_CDAB, kI_CDAB}, 0.75, 52.5, {}},
                                  G{{kF_ABCD}, 0.5, 1234567.89, {}},
                                  G{{kI_ABCD}, 0.1, 3, {}},
                                  G{{kU_CDAB}, 0.0, 4, {}}},
                                 kF_CDAB),
                      spy);
        QCOMPARE(table(*d)->rowCount(), 4);
        QCOMPARE(table(*d)->columnCount(), 4);
        QCOMPARE(cell(*d, 0, 0), QStringLiteral("float32 CDAB"));
        QCOMPARE(cell(*d, 0, 1), QStringLiteral("52.5 bar"));
        QCOMPARE(cell(*d, 1, 1), QStringLiteral("1234568 bar"));
        QCOMPARE(cell(*d, 0, 2), QStringLiteral("Good"));
        QCOMPARE(cell(*d, 1, 2), QStringLiteral("Possible"));
        QCOMPARE(cell(*d, 2, 2), QStringLiteral("Unlikely"));
        QCOMPARE(cell(*d, 3, 2), QStringLiteral("Implausible"));
        for (int r = 0; r < 4; ++r)
            QCOMPARE(cell(*d, r, 3), QString());
        QVERIFY(cell(*d, 0, 0) != cell(*d, 1, 0));
    }

    void valueWithoutUnit()
    {
        Tag t = floatTag();
        t.unit.clear();
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy, {{1, 2}}, {}, std::nullopt, 0, t);
        QCOMPARE(cell(*d, 0, 1), QStringLiteral("52.5"));
    }

    void matchColumnAndOrdering()
    {
        auto spy = std::make_shared<Spy>();
        // Best group by score does not match; the second one does and must be listed first.
        auto d = make(makeResult(EncodingVerdict::Clear,
                                 {G{{kF_ABCD}, 0.9, 10, false},
                                  G{{kF_CDAB, kI_CDAB}, 0.4, 52.3, true},
                                  G{{kU_CDAB}, 0.3, 7, false}},
                                 kF_CDAB),
                      spy);
        d->setDisplayValue(QStringLiteral("52.3"));
        QCOMPARE(table(*d)->rowCount(), 3);
        QCOMPARE(cell(*d, 0, 0), QStringLiteral("float32 CDAB"));
        QCOMPARE(cell(*d, 0, 3), QString(QChar(0x2713)));
        QCOMPARE(cell(*d, 1, 0), QStringLiteral("float32 ABCD"));
        QCOMPARE(cell(*d, 1, 3), QStringLiteral("No"));
        QCOMPARE(cell(*d, 2, 0), QStringLiteral("uint32 CDAB"));
        QCOMPARE(cell(*d, 2, 3), QStringLiteral("No"));
        // The matching group is selected after the re-ranking and counts as confirmed.
        QCOMPARE(table(*d)->currentRow(), 0);
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kF_CDAB));
        QVERIFY(d->matchedDisplayValue());
        d->setDisplayValue(QString());
        QCOMPARE(spy->options.displayValue, QString());
    }

    void tooltipListsEquivalents()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        const QString tip = table(*d)->item(0, 0)->toolTip();
        QVERIFY(tip.contains(QStringLiteral("float32 CDAB")));
        QVERIFY(tip.contains(QStringLiteral("int32 CDAB")));
        QVERIFY(!tip.contains(QStringLiteral("float32 ABCD")));
        QVERIFY(table(*d)->item(1, 0)->toolTip().contains(QStringLiteral("float32 ABCD")));
    }

    void userSelectionKeptAcrossAddReading()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        table(*d)->selectRow(1);
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kF_ABCD));
        d->addReading({1, 2});
        QCOMPARE(table(*d)->currentRow(), 1);
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kF_ABCD));
        // Groups reordered: the choice follows its group.
        install(*d, spy,
                makeResult(EncodingVerdict::Clear,
                           {G{{kU_CDAB}, 0.9, 1, {}}, G{{kF_ABCD}, 0.5, 2, {}}}, kU_CDAB));
        d->addReading({3, 4});
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kF_ABCD));
        QCOMPARE(table(*d)->currentRow(), 1);
        // Its group is gone: back to the best group.
        install(*d, spy, makeResult(EncodingVerdict::Clear, {G{{kU_CDAB}, 0.9, 1, {}}}, kU_CDAB));
        d->addReading({5, 6});
        // The encoding no longer exists anywhere: best group selected.
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kU_CDAB));
    }

    void bestSelectedWhenUserDidNotPick()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        install(*d, spy,
                makeResult(EncodingVerdict::Clear,
                           {G{{kU_CDAB}, 0.9, 1, {}}, G{{kF_ABCD}, 0.5, 2, {}}}, kU_CDAB));
        d->addReading({3, 4});
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kU_CDAB));
    }

    void okDisabledWithoutSelection()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        QVERIFY(okButton(*d)->isEnabled());
        table(*d)->clearSelection();
        QVERIFY(!okButton(*d)->isEnabled());
        QVERIFY(!d->chosenEncoding());
        QVERIFY(!d->matchedDisplayValue());
        QVERIFY(d->applyOrderTo().isEmpty());
        d->setDisplayValue(QStringLiteral("5"));
        QVERIFY(okButton(*d)->isEnabled()); // re-ranking selects the best again
    }

    void acceptAndReject()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        okButton(*d)->click();
        QCOMPARE(d->result().candidates, twoGroups().candidates);
        QCOMPARE(d->QDialog::result(), int(QDialog::Accepted));
        auto d2 = make(twoGroups(), spy);
        d2->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(d2->QDialog::result(), int(QDialog::Rejected));
    }

    void applyNeedsMatchingRowWithDisplayValue()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(makeResult(EncodingVerdict::Ambiguous,
                                 {G{{kF_CDAB}, 0.9, 52.3, true}, G{{kU_CDAB}, 0.4, 7, false}},
                                 kF_CDAB),
                      spy);
        QPushButton *ok = d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        table(*d)->selectRow(1); // does not match the display value
        QVERIFY(!ok->isEnabled());
        table(*d)->selectRow(0);
        QVERIFY(ok->isEnabled());

        auto none = make(makeResult(EncodingVerdict::NoMatch,
                                    {G{{kF_CDAB}, 0.9, 52.3, false}, G{{kU_CDAB}, 0.4, 7, false}},
                                    std::nullopt),
                         spy);
        QVERIFY(!none->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
    }

    void unreadableDisplayValueIsReported()
    {
        const Tag tag = floatTag();
        EncodingDialog d(tag, {{0x4251, 0x3333}}, {}, std::nullopt);
        d.setDisplayValue(QStringLiteral("abc"));
        QCOMPARE(d.findChild<QLabel *>(QStringLiteral("verdictLabel"))->text(),
                 QStringLiteral("Can't read \"abc\" as a number: type it like 52.3."));
        d.setDisplayValue(QStringLiteral("52.3 °C"));
        QVERIFY(!d.findChild<QLabel *>(QStringLiteral("verdictLabel"))->text().startsWith(
            QStringLiteral("Can't read")));
    }

    void matchedDisplayValueFollowsSelection()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(makeResult(EncodingVerdict::Ambiguous,
                                 {G{{kF_CDAB}, 0.9, 52.3, true}, G{{kU_CDAB}, 0.4, 7, false}},
                                 kF_CDAB),
                      spy);
        QVERIFY(d->matchedDisplayValue());
        table(*d)->selectRow(1);
        QVERIFY(!d->matchedDisplayValue());
        QCOMPARE(d->chosenEncoding(), std::optional<Encoding>(kU_CDAB));
    }

    void applyOthersCheck()
    {
        auto spy = std::make_shared<Spy>();
        const QList<Tag> oth = {otherTag(2, "A", ByteOrder::ABCD),
                                otherTag(3, "B", ByteOrder::BADC)};
        auto d = make(twoGroups(), spy, {{1, 2}}, oth);
        QVERIFY(!others(*d)->isHidden());
        QVERIFY(others(*d)->isChecked());
        QCOMPARE(others(*d)->text(),
                 QStringLiteral("Also use CDAB for the other unconfirmed 32-bit tags: A, B"));
        QCOMPARE(d->applyOrderTo(), (QList<TagId>{2, 3}));
        others(*d)->setChecked(false);
        QVERIFY(d->applyOrderTo().isEmpty());
        others(*d)->setChecked(true);
        // The order follows the selected row.
        table(*d)->selectRow(1);
        QCOMPARE(others(*d)->text(),
                 QStringLiteral("Also use ABCD for the other unconfirmed 32-bit tags: B"));
        QCOMPARE(d->applyOrderTo(), (QList<TagId>{3}));
    }

    void applyOthersHidden()
    {
        auto spy = std::make_shared<Spy>();
        auto none = make(twoGroups(), spy);
        QVERIFY(others(*none)->isHidden());
        QVERIFY(none->applyOrderTo().isEmpty());

        const QList<Tag> same = {otherTag(2, "A", ByteOrder::CDAB), otherTag(3, "B", ByteOrder::CDAB)};
        auto all = make(twoGroups(), spy, {{1, 2}}, same);
        QVERIFY(others(*all)->isHidden());
        QVERIFY(all->applyOrderTo().isEmpty());

        // Tags the user set or confirmed are never offered.
        const QList<Tag> confirmed = {otherTag(2, "A", ByteOrder::ABCD, true),
                                      otherTag(3, "B", ByteOrder::BADC, true)};
        auto conf = make(twoGroups(), spy, {{1, 2}}, confirmed);
        QVERIFY(others(*conf)->isHidden());
        QVERIFY(conf->applyOrderTo().isEmpty());
        const QList<Tag> mixed = {otherTag(2, "A", ByteOrder::ABCD, true),
                                  otherTag(3, "B", ByteOrder::BADC)};
        auto mix = make(twoGroups(), spy, {{1, 2}}, mixed);
        QCOMPARE(mix->applyOrderTo(), QList<TagId>{3});
    }

    void applyOthersManyNames()
    {
        auto spy = std::make_shared<Spy>();
        QList<Tag> oth;
        for (int i = 0; i < 8; ++i)
            oth.append(otherTag(TagId(10 + i), QString(QChar('A' + i)), ByteOrder::ABCD));
        auto d = make(twoGroups(), spy, {{1, 2}}, oth);
        QCOMPARE(others(*d)->text(),
                 QStringLiteral("Also use CDAB for the other unconfirmed 32-bit tags: A, B, C, D, E and 3 more"));
        QCOMPARE(d->applyOrderTo().size(), 8);

        QList<Tag> five = oth.mid(0, 5);
        auto d5 = make(twoGroups(), spy, {{1, 2}}, five);
        QCOMPARE(others(*d5)->text(),
                 QStringLiteral("Also use CDAB for the other unconfirmed 32-bit tags: A, B, C, D, E"));
    }

    void keepsLastTenReadings()
    {
        auto spy = std::make_shared<Spy>();
        QList<RegisterPair> initial;
        for (quint16 i = 0; i < 4; ++i)
            initial.append({i, 0});
        auto d = make(twoGroups(), spy, initial);
        QCOMPARE(spy->readings.size(), 4);
        for (quint16 i = 4; i < 15; ++i)
            d->addReading({i, 0});
        QCOMPARE(spy->readings.size(), 10);
        QCOMPARE(spy->readings.first().r0, quint16(5));
        QCOMPARE(spy->readings.last().r0, quint16(14));
        QCOMPARE(label(*d, "registersLabel")->text(), QStringLiteral("HR 100-101: 0x000E 0x0000"));
    }

    void constructorTrimsReadings()
    {
        QList<RegisterPair> many;
        for (quint16 i = 0; i < 25; ++i)
            many.append({i, 0});
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy, many);
        QCOMPARE(spy->readings.size(), 10);
        QCOMPARE(spy->readings.first().r0, quint16(15));
    }

    void displayValueAndDeviceOrderPassedThrough()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy, {{1, 2}}, {}, ByteOrder::BADC);
        QCOMPARE(spy->options.deviceOrder, std::optional<ByteOrder>(ByteOrder::BADC));
        QVERIFY(spy->options.displayValue.isEmpty());
        const int before = spy->calls;
        d->setDisplayValue(QStringLiteral("52,3"));
        QCOMPARE(spy->options.displayValue, QStringLiteral("52,3"));
        QCOMPARE(spy->options.deviceOrder, std::optional<ByteOrder>(ByteOrder::BADC));
        QCOMPARE(spy->calls, before + 1);
        // Typing into the edit re-ranks as well.
        auto *edit = d->findChild<QLineEdit *>(QStringLiteral("displayValueEdit"));
        QVERIFY(edit);
        QCOMPARE(edit->text(), QStringLiteral("52,3"));
        edit->setText(QStringLiteral("7"));
        QCOMPARE(spy->options.displayValue, QStringLiteral("7"));
        QCOMPARE(spy->calls, before + 2);
        // Setting the same text again still re-ranks.
        d->setDisplayValue(QStringLiteral("7"));
        QCOMPARE(spy->calls, before + 3);
    }

    void addressBaseShiftsRegisters()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy, {{0x4251, 0x3333}}, {}, std::nullopt, 1);
        QCOMPARE(label(*d, "registersLabel")->text(),
                 QStringLiteral("HR 101-102: 0x4251 0x3333"));
    }

    void introLabel()
    {
        auto spy = std::make_shared<Spy>();
        auto d = make(twoGroups(), spy);
        QLabel *intro = label(*d, "introLabel");
        QVERIFY(intro);
        QVERIFY(intro->isHidden());
        QCOMPARE(intro->textFormat(), Qt::RichText);
        QVERIFY(intro->wordWrap());
        // First item of the dialog's layout: above "registersLabel".
        QCOMPARE(d->layout()->itemAt(0)->widget(), intro);

        const QString rich = QStringLiteral("<b>'Setpoint' (HR 110, int16)</b> can't be read alone.");
        d->setIntro(rich);
        QVERIFY(!intro->isHidden());
        QCOMPARE(intro->text(), rich);

        d->setIntro(QString());
        QVERIFY(intro->isHidden());
        QVERIFY(intro->text().isEmpty());
    }

    void realDetectionSmoke()
    {
        // Only relies on the contract: 12 candidates and a selected best group when implemented.
        const QList<RegisterPair> readings = {{0x4251, 0x3333}, {0x4252, 0x0000}};
        if (detectEncoding(floatTag(), readings).candidates.isEmpty())
            QSKIP("detection not implemented yet");
        EncodingDialog dlg(floatTag(), readings, {}, std::nullopt);
        QCOMPARE(dlg.result().candidates.size(), 12);
        QVERIFY(table(dlg)->rowCount() >= 1);
        QCOMPARE(table(dlg)->rowCount(), dlg.result().groupCount);
        QCOMPARE(table(dlg)->currentRow(), 0);
        QVERIFY(dlg.chosenEncoding().has_value());
    }
};

QTEST_MAIN(TstEncodingDialog)
#include "tst_encodingdialog.moc"
