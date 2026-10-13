#include "EncodingDialog.h"
#include "EncodingMonitor.h"
#include "MetaTypes.h"
#include "TagCodec.h"
#include "TagTableModel.h"

#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <memory>

using namespace mb;

namespace {

constexpr Encoding kF_ABCD{ValueType::Float32, ByteOrder::ABCD};
constexpr Encoding kF_CDAB{ValueType::Float32, ByteOrder::CDAB};
constexpr Encoding kI_ABCD{ValueType::Int32, ByteOrder::ABCD};

Tag mkTag(const QString &name, quint16 address, ValueType type = ValueType::Float32,
          ByteOrder order = ByteOrder::ABCD, const QString &unit = {})
{
    Tag t;
    t.name = name;
    t.area = Area::HoldingRegister;
    t.address = address;
    t.type = type;
    t.byteOrder = order;
    t.unit = unit;
    return t;
}

} // namespace

class TstEncodingMonitor : public QObject
{
    Q_OBJECT

    std::unique_ptr<TagTableModel> model;
    std::unique_ptr<EncodingMonitor> mon;
    std::unique_ptr<QSignalSpy> messages;

    QStringList dialogs;                            ///< window titles of the opened dialogs
    std::function<bool(EncodingDialog &)> onDialog; ///< unset: unexpected, cancelled
    QStringList reviews;                            ///< review prompt texts
    bool reviewAnswer = false;
    QStringList unexpected;

    // Split check: a simulated device behind readRequested, and the split prompt seam.
    struct ReadReq {
        quint64 id = 0;
        Area area = Area::HoldingRegister;
        quint16 address = 0;
        int count = 0;
    };
    struct SplitPrompt {
        QString text;
        QString change;
        QString keep;
    };
    QHash<int, quint16> regs;                          ///< the device's registers (absent: exception)
    QList<ReadReq> reads;                              ///< every readRequested
    bool autoAnswer = true;                            ///< answer reads synchronously
    std::function<bool(quint16, int)> readFails;       ///< make a read fail
    QList<SplitPrompt> splits;                         ///< split prompts shown
    EncodingMonitor::SplitAnswer splitAnswer = EncodingMonitor::SplitAnswer::Later;
    std::function<EncodingMonitor::SplitAnswer()> onSplit; ///< overrides splitAnswer
    QStringList introTexts;                            ///< intro labels of the dialogs (refused reads)
    QStringList registerTexts;                         ///< their registers labels

    void answerRead(const ReadReq &r)
    {
        QList<quint16> values;
        bool ok = !(readFails && readFails(r.address, r.count));
        for (int i = 0; ok && i < r.count; ++i) {
            if (!regs.contains(r.address + i))
                ok = false;
            else
                values.append(regs.value(r.address + i));
        }
        ModbusResult result;
        if (!ok) {
            result.error = ModbusError::Exception;
            result.exceptionCode = 2;
            values.clear();
        }
        mon->onReadFinished(r.id, result, values);
    }

    // ---- helpers -------------------------------------------------------------------------

    /// Adds a tag as the user would (the model emits tagsEdited, the monitor hears it).
    TagId add(const Tag &prototype) { return model->tagAt(model->addTag(prototype)).id; }

    /// Replaces the whole list (like opening a project); ids are 1, 2, ... unless set.
    void load(QList<Tag> tags)
    {
        TagId next = 1;
        for (int i = 0; i < tags.size(); ++i) {
            if (tags[i].id == 0)
                tags[i].id = TagId(i + 1);
            next = qMax(next, TagId(tags[i].id + 1));
        }
        model->setTags(tags, next);
        mon->onTagsReplaced();
    }

    const Tag &tagOf(TagId id) const { return model->tagAt(model->rowOf(id)); }

    /// A sample of `value` as a device with encoding `truth` sends it, decoded by the tag as
    /// currently configured in the model.
    TagSample sample(TagId id, Encoding truth, double value, Quality quality = Quality::Good) const
    {
        const Tag configured = tagOf(id);
        Tag device = configured;
        device.type = truth.type;
        device.byteOrder = truth.order;
        const auto encoded = encodeRegisters(device, value);
        TagSample s;
        s.tagId = id;
        s.quality = quality;
        if (encoded) {
            s.raw = combineRegisters(configured, encoded->at(0), encoded->at(1));
            s.value = decodeValue(configured, s.raw);
        }
        return s;
    }

    void feed(const QList<TagSample> &samples)
    {
        SampleBatch b;
        b.samples = samples;
        mon->onSamples(b);
    }

    void feedOne(TagId id, Encoding truth, double value, Quality quality = Quality::Good)
    {
        feed({sample(id, truth, value, quality)});
    }

    /// Readings 52.1, 52.2, 52.3 of a device with encoding `truth`.
    void feedThree(TagId id, Encoding truth)
    {
        feedOne(id, truth, 52.1);
        feedOne(id, truth, 52.2);
        feedOne(id, truth, 52.3);
    }

    static QString g7(double v) { return QString::number(v, 'g', 7); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ModbusControlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_encodingmonitor"));
        registerMetaTypes();
    }

    void init()
    {
        model = std::make_unique<TagTableModel>();
        mon = std::make_unique<EncodingMonitor>(model.get(), nullptr);
        connect(model.get(), &TagTableModel::tagsEdited, mon.get(), &EncodingMonitor::onTagsChanged);
        messages = std::make_unique<QSignalSpy>(mon.get(), &EncodingMonitor::message);
        dialogs.clear();
        onDialog = nullptr;
        reviews.clear();
        reviewAnswer = false;
        unexpected.clear();
        introTexts.clear();
        registerTexts.clear();
        regs.clear();
        reads.clear();
        autoAnswer = true;
        readFails = nullptr;
        splits.clear();
        splitAnswer = EncodingMonitor::SplitAnswer::Later;
        onSplit = nullptr;
        connect(mon.get(), &EncodingMonitor::readRequested, this,
                [this](quint64 id, Area area, quint16 address, int count) {
                    reads.append(ReadReq{id, area, address, count});
                    if (autoAnswer)
                        answerRead(reads.last());
                });
        mon->m_splitPrompt = [this](const QString &text, const QString &change,
                                    const QString &keep) {
            splits.append(SplitPrompt{text, change, keep});
            return onSplit ? onSplit() : splitAnswer;
        };
        mon->m_runDialog = [this](EncodingDialog &dialog) {
            dialogs << dialog.windowTitle();
            if (!onDialog) {
                unexpected << QStringLiteral("dialog: ") + dialog.windowTitle();
                return false;
            }
            return onDialog(dialog);
        };
        mon->m_reviewPrompt = [this](const QString &text) {
            reviews << text;
            return reviewAnswer;
        };
    }

    void cleanup()
    {
        messages.reset();
        mon.reset();
        model.reset();
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join(QLatin1Char('\n'))));
    }

    // ---- new tags ------------------------------------------------------------------------

    void clearDetectionChangesOrder()
    {
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110, ValueType::Float32, ByteOrder::ABCD,
                                   QStringLiteral("°C")));
        QVERIFY(mon->isPending(id));
        feedOne(id, kF_CDAB, 52.1);
        feedOne(id, kF_CDAB, 52.2);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD); // two readings are not enough
        QVERIFY(messages->isEmpty());
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(id).encodingVerified);
        QVERIFY(!mon->isPending(id));
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Detected float32 CDAB for 'Temp' (reads 52.3 °C). "
                                "Confirm it before writing."));
        // The monitor's own change is not a new trigger, and more readings change nothing.
        feedThree(id, kF_CDAB);
        QCOMPARE(messages->size(), 1);
        QVERIFY(dialogs.isEmpty());
    }

    void clearSameEncodingChangesNothing()
    {
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110, ValueType::Float32, ByteOrder::CDAB));
        feedThree(id, kF_CDAB);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(!mon->isPending(id));
        QVERIFY(messages->isEmpty());
        QVERIFY(dialogs.isEmpty());
    }

    void ambiguousOpensDialogAccepted()
    {
        const TagId id = add(mkTag(QStringLiteral("Counter"), 110, ValueType::Float32, ByteOrder::CDAB));
        int size = -1;
        EncodingVerdict verdict = EncodingVerdict::Clear;
        onDialog = [&](EncodingDialog &dlg) {
            size = int(dlg.result().candidates.first().values.size());
            verdict = dlg.result().verdict;
            dlg.setDisplayValue(QStringLiteral("100"));
            return dlg.chosenEncoding() == std::optional<Encoding>(kI_ABCD);
        };
        feedOne(id, kI_ABCD, 100);
        feedOne(id, kI_ABCD, 100);
        QVERIFY(dialogs.isEmpty());
        feedOne(id, kI_ABCD, 100);
        QCOMPARE(dialogs, QStringList{QStringLiteral("Detect encoding - Counter")});
        QCOMPARE(size, 3);
        QVERIFY(verdict == EncodingVerdict::Ambiguous);
        QVERIFY(tagOf(id).type == ValueType::Int32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(!mon->isPending(id));
        feedThree(id, kI_ABCD);
        QCOMPARE(dialogs.size(), 1);
    }

    // A new 32-bit tag is unconfirmed until detection has a result; readings that fit its own
    // encoding confirm it.
    void newTagUnconfirmedUntilReadingsAgree()
    {
        const TagId id = add(mkTag(QStringLiteral("Sp"), 110, ValueType::Float32, ByteOrder::CDAB));
        QVERIFY(mon->isPending(id));
        QVERIFY(!tagOf(id).encodingVerified);
        feedThree(id, kF_CDAB);
        QVERIFY(!mon->isPending(id));
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(dialogs.isEmpty());
        QVERIFY(messages->isEmpty());
    }

    // Registers at 0 never decide: the tag stays pending and unconfirmed (writes ask first).
    void zerosKeepTagUnconfirmed()
    {
        const TagId id = add(mkTag(QStringLiteral("Sp"), 110, ValueType::Float32, ByteOrder::ABCD));
        for (int i = 0; i < 6; ++i)
            feedOne(id, kF_CDAB, 0.0);
        QVERIFY(mon->isPending(id));
        QVERIFY(!tagOf(id).encodingVerified);
        QVERIFY(dialogs.isEmpty());
    }

    // A plausible current reading is not overridden: int32 17000 (0x0000 0x4268) also reads as
    // float32 58.0 in CDAB order; the user decides.
    void plausibleCurrentEncodingAsks()
    {
        const TagId id = add(mkTag(QStringLiteral("Count"), 110, ValueType::Int32, ByteOrder::ABCD));
        onDialog = [](EncodingDialog &) { return false; };
        for (int i = 0; i < 3; ++i)
            feedOne(id, kI_ABCD, 17000);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(id).type == ValueType::Int32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(messages->isEmpty());
    }

    void ambiguousCancelled()
    {
        const TagId id = add(mkTag(QStringLiteral("Counter"), 110, ValueType::Float32, ByteOrder::CDAB));
        onDialog = [](EncodingDialog &) { return false; };
        for (int i = 0; i < 3; ++i)
            feedOne(id, kI_ABCD, 100);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(id).encodingVerified); // still to be confirmed
        QVERIFY(!mon->isPending(id));
        for (int i = 0; i < 3; ++i)
            feedOne(id, kI_ABCD, 100);
        QCOMPARE(dialogs.size(), 1);
    }

    void undecidableStaysPending()
    {
        const TagId id = add(mkTag(QStringLiteral("Idle"), 110));
        for (int i = 0; i < 5; ++i)
            feedOne(id, kF_CDAB, 0.0);
        QVERIFY(mon->isPending(id));
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(messages->isEmpty());
        QVERIFY(dialogs.isEmpty());
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(!mon->isPending(id));
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(id).encodingVerified);
        QCOMPARE(messages->size(), 1);
    }

    void onlyGoodSamplesCount()
    {
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110));
        for (int i = 0; i < 6; ++i)
            feedOne(id, kF_CDAB, 52.0 + i, Quality::Timeout);
        feedOne(id, kF_CDAB, 52.1);
        feedOne(id, kF_CDAB, 52.2, Quality::Exception);
        feedOne(id, kF_CDAB, 52.2);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(mon->isPending(id));
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
    }

    // ---- what makes a tag pending ----------------------------------------------------------

    void pendingRules()
    {
        // 16-bit tags never.
        const TagId i16 = add(mkTag(QStringLiteral("Small"), 5, ValueType::Int16));
        QVERIFY(!mon->isPending(i16));
        // 16-bit -> 32-bit: pending.
        model->setData(model->index(model->rowOf(i16), TagTableModel::Type), QStringLiteral("float32"));
        QVERIFY(mon->isPending(i16));
        // A type change among 32-bit types, or a byte order change alone, ends it.
        model->setData(model->index(model->rowOf(i16), TagTableModel::Type), QStringLiteral("int32"));
        QVERIFY(!mon->isPending(i16));

        load({mkTag(QStringLiteral("A"), 100), mkTag(QStringLiteral("B"), 110, ValueType::Int32)});
        const TagId a = 1;
        const TagId b = 2;
        QVERIFY(!mon->isPending(a));
        QVERIFY(!mon->isPending(b));
        model->setData(model->index(model->rowOf(a), TagTableModel::ByteOrder), QStringLiteral("CDAB"));
        QVERIFY(!mon->isPending(a));
        model->setData(model->index(model->rowOf(a), TagTableModel::Type), QStringLiteral("uint32"));
        QVERIFY(!mon->isPending(a));
        // Address (and area) change: pending.
        model->setData(model->index(model->rowOf(a), TagTableModel::Address), 120);
        QVERIFY(mon->isPending(a));
        model->setData(model->index(model->rowOf(b), TagTableModel::Area), QStringLiteral("Input"));
        QVERIFY(mon->isPending(b));
        // Replacing the list clears everything.
        mon->onTagsReplaced();
        QVERIFY(!mon->isPending(a));
        QVERIFY(!mon->isPending(b));
        // Removing a pending tag forgets it.
        model->setData(model->index(model->rowOf(a), TagTableModel::Address), 130);
        QVERIFY(mon->isPending(a));
        model->removeRows(model->rowOf(a), 1);
        QVERIFY(!mon->isPending(a));
    }

    // ---- on-connect check ------------------------------------------------------------------

    void connectCheckFlagsSuspiciousTag()
    {
        load({mkTag(QStringLiteral("P"), 110, ValueType::Float32, ByteOrder::ABCD, QStringLiteral("bar")),
              mkTag(QStringLiteral("Q"), 120, ValueType::Float32, ByteOrder::ABCD)});
        mon->onConnected();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(reviews.isEmpty());
            feed({sample(1, kF_CDAB, 52.1 + 0.1 * i), sample(2, kF_ABCD, 10.0 + i)});
        }
        QCOMPARE(reviews.size(), 1);
        const Tag p = tagOf(1);
        const quint32 raw = sample(1, kF_CDAB, 52.1 + 0.2).raw;
        const QString expected =
            QStringLiteral("<p><b>1 tag reads an implausible value with its current encoding:</b></p>"
                           "<ul><li>'P' (HR %3-%4): %1 bar as float32 ABCD, <b>%2 bar</b> as float32 "
                           "CDAB</li></ul><p>Writes to them ask first until you confirm their "
                           "encoding. Review them now?</p>")
                .arg(g7(decodeValue(p, raw)), g7(52.3))
                .arg(p.address).arg(p.address + 1);
        QCOMPARE(reviews.first(), expected);
        QVERIFY(!tagOf(1).encodingVerified);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::ABCD);
        QVERIFY(tagOf(2).encodingVerified);
        QVERIFY(dialogs.isEmpty());
        // Once only.
        feed({sample(1, kF_CDAB, 52.5), sample(2, kF_ABCD, 14.0)});
        QCOMPARE(reviews.size(), 1);
    }

    void connectCheckReviewOpensDialog()
    {
        load({mkTag(QStringLiteral("P"), 110, ValueType::Float32, ByteOrder::ABCD)});
        reviewAnswer = true;
        onDialog = [](EncodingDialog &) { return false; };
        mon->onConnected();
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(dialogs, QStringList{QStringLiteral("Detect encoding - P")});
    }

    void connectCheckSkipsSilentTagsAfterTenBatches()
    {
        load({mkTag(QStringLiteral("Silent"), 100), mkTag(QStringLiteral("P"), 110)});
        mon->onConnected();
        for (int i = 0; i < 9; ++i) {
            feed({sample(1, kF_ABCD, 1.0, Quality::Timeout), sample(2, kF_CDAB, 52.1 + 0.1 * i)});
            QVERIFY(reviews.isEmpty());
        }
        feed({sample(1, kF_ABCD, 1.0, Quality::Timeout), sample(2, kF_CDAB, 53.0)});
        QCOMPARE(reviews.size(), 1);
        QVERIFY(reviews.first().contains(QStringLiteral("<li>'P' (HR ")));
        QVERIFY(!reviews.first().contains(QStringLiteral("Silent")));
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(!tagOf(2).encodingVerified);
    }

    void connectCheckIgnoresPendingTags()
    {
        load({mkTag(QStringLiteral("P"), 110)});
        mon->onConnected();
        const TagId extra = add(mkTag(QStringLiteral("New"), 120));
        QVERIFY(mon->isPending(extra));
        for (int i = 0; i < 3; ++i)
            feed({sample(1, kF_ABCD, 52.1 + 0.1 * i), sample(extra, kF_CDAB, 52.1 + 0.1 * i)});
        QVERIFY(reviews.isEmpty());
        QVERIFY(tagOf(extra).byteOrder == ByteOrder::CDAB); // handled as a new tag
    }

    void disconnectForgetsReadings()
    {
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110));
        mon->onConnected();
        feedOne(id, kF_CDAB, 52.1);
        feedOne(id, kF_CDAB, 52.2);
        mon->onDisconnected();
        QVERIFY(mon->isPending(id));
        mon->onConnected();
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD); // the two earlier readings are gone
        feedOne(id, kF_CDAB, 52.4);
        feedOne(id, kF_CDAB, 52.5);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
    }

    // ---- device order ------------------------------------------------------------------------

    void deviceOrder()
    {
        QVERIFY(!mon->deviceOrder());
        Tag a = mkTag(QStringLiteral("A"), 100, ValueType::Float32, ByteOrder::CDAB);
        Tag b = mkTag(QStringLiteral("B"), 110, ValueType::Int32, ByteOrder::CDAB);
        Tag c = mkTag(QStringLiteral("C"), 120, ValueType::Float32, ByteOrder::ABCD);
        Tag s = mkTag(QStringLiteral("S"), 5, ValueType::Int16, ByteOrder::BADC);
        load({s});
        QVERIFY(!mon->deviceOrder());
        load({a, s});
        QVERIFY(mon->deviceOrder() == std::optional<ByteOrder>(ByteOrder::CDAB));
        load({a, b, s});
        QVERIFY(mon->deviceOrder() == std::optional<ByteOrder>(ByteOrder::CDAB));
        load({a, b, c});
        QVERIFY(!mon->deviceOrder());
        c.encodingVerified = false; // unverified tags do not vote
        load({a, b, c});
        QVERIFY(mon->deviceOrder() == std::optional<ByteOrder>(ByteOrder::CDAB));
        a.encodingVerified = false;
        b.encodingVerified = false;
        load({a, b, c});
        QVERIFY(!mon->deviceOrder());
    }

    // ---- detectNow ---------------------------------------------------------------------------

    void detectNowUsesRecentAndLiveReadings()
    {
        load({mkTag(QStringLiteral("T"), 110)});
        for (int i = 0; i < 4; ++i)
            feedOne(1, kF_ABCD, 20.0 + i);
        int before = -1;
        int after = -1;
        onDialog = [&](EncodingDialog &dlg) {
            before = int(dlg.result().candidates.first().values.size());
            feedOne(1, kF_ABCD, 30.0);
            after = int(dlg.result().candidates.first().values.size());
            return false;
        };
        mon->detectNow(1);
        QCOMPARE(dialogs, QStringList{QStringLiteral("Detect encoding - T")});
        QCOMPARE(before, 4);
        QCOMPARE(after, 5);
    }

    void detectNowKeepsTenReadings()
    {
        load({mkTag(QStringLiteral("T"), 110)});
        for (int i = 0; i < 15; ++i)
            feedOne(1, kF_ABCD, 20.0 + i);
        int size = -1;
        onDialog = [&](EncodingDialog &dlg) {
            size = int(dlg.result().candidates.first().values.size());
            return false;
        };
        mon->detectNow(1);
        QCOMPARE(size, 10);
    }

    void detectNowIgnoresUnknownAndSmallTags()
    {
        load({mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        mon->detectNow(1);
        mon->detectNow(99);
        QVERIFY(dialogs.isEmpty());
    }

    void acceptedAppliesOrderToOthers()
    {
        Tag other = mkTag(QStringLiteral("Other"), 130, ValueType::Int32, ByteOrder::ABCD);
        other.encodingVerified = false;
        Tag third = mkTag(QStringLiteral("Third"), 140, ValueType::Float32, ByteOrder::ABCD);
        load({mkTag(QStringLiteral("T"), 110), other, third, mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::ABCD); // loaded tag: no automatic detection
        QList<TagId> others;
        onDialog = [&](EncodingDialog &dlg) {
            others = dlg.applyOrderTo();
            return dlg.chosenEncoding() == std::optional<Encoding>(kF_CDAB);
        };
        mon->detectNow(1);
        QCOMPARE(others, QList<TagId>{2}); // only the unconfirmed tag is offered
        QVERIFY(tagOf(1).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(1).type == ValueType::Float32);
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(tagOf(2).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(2).type == ValueType::Int32);
        QVERIFY(!tagOf(2).encodingVerified); // its order was chosen for it: still to be confirmed
        QVERIFY(tagOf(3).byteOrder == ByteOrder::ABCD); // confirmed: never changed
        QVERIFY(tagOf(3).type == ValueType::Float32);
        QVERIFY(tagOf(3).encodingVerified);
        QVERIFY(tagOf(4).byteOrder == ByteOrder::ABCD);
        QVERIFY(!mon->isPending(1));
        QVERIFY(!mon->isPending(2));
        QVERIFY(!mon->isPending(3));
    }

    // ---- 16-bit split check ----------------------------------------------------------------

    /// A device with float32 52.3 CDAB at HR 110-111, 17.5 CDAB at HR 120-121, and plain
    /// small values at HR 4-6.
    void splitDevice()
    {
        regs.clear();
        regs[109] = 0;
        regs[110] = 0x3333;
        regs[111] = 0x4251;
        regs[112] = 0;
        regs[120] = 0;
        regs[121] = 0x418C;
        regs[4] = 7;
        regs[5] = 8;
        regs[6] = 9;
    }

    TagSample good16(TagId id, quint16 raw, Quality quality = Quality::Good) const
    {
        TagSample s;
        s.tagId = id;
        s.quality = quality;
        s.raw = raw;
        s.value = decodeValue(tagOf(id), raw);
        return s;
    }

    /// Adds an int16 tag while connected and gives it its first Good sample.
    TagId addSplitTag(const QString &name, quint16 address, ValueType type = ValueType::Int16)
    {
        const TagId id = add(mkTag(name, address, type));
        feed({good16(id, regs.value(address))});
        return id;
    }

    void splitFlagsAndPrompts()
    {
        splitDevice();
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16));
        QVERIFY(tagOf(id).encodingVerified); // not unverified while waiting for the check
        QVERIFY(reads.isEmpty());
        feed({good16(id, 0x3333)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reads[0].address, quint16(109));
        QCOMPARE(reads[0].count, 3);
        QVERIFY(reads[0].area == Area::HoldingRegister);
        QVERIFY(!tagOf(id).encodingVerified);
        QCOMPARE(model->encodingNote(id), QStringLiteral("HR 110-111 read as float32 CDAB give 52.3"));
        QCOMPARE(splits.size(), 1);
        QCOMPARE(splits[0].text,
                 QStringLiteral("<p><b>'Setpoint' is probably half of a float32 value.</b></p>"
                                "<table cellspacing=\"0\" cellpadding=\"3\">"
                                "<tr><td>As int16 (HR 110):</td><td>13107</td></tr>"
                                "<tr><td>As float32 CDAB (HR 110-111):</td><td><b>52.3</b>"
                                "</td></tr></table>"
                                "<p>Writing a 16-bit value here would change half of the float.</p>"));
        QCOMPARE(splits[0].change, QStringLiteral("Change to float32 CDAB at HR 110"));
        QCOMPARE(splits[0].keep, QStringLiteral("Keep int16"));
        // Later: stays flagged, and is not checked again.
        QVERIFY(!tagOf(id).encodingVerified);
        feed({good16(id, 0x3333)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(splits.size(), 1);
        QVERIFY(dialogs.isEmpty());
    }

    void splitChangeMovesAndRetypes()
    {
        splitDevice();
        splitAnswer = EncodingMonitor::SplitAnswer::Change;
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("Setpoint"), 110);
        const Tag t = tagOf(id);
        QVERIFY(t.type == ValueType::Float32);
        QVERIFY(t.byteOrder == ByteOrder::CDAB);
        QCOMPARE(t.address, quint16(110));
        QVERIFY(t.encodingVerified);
        QVERIFY(model->encodingNote(id).isEmpty());
        QVERIFY(!mon->isPending(id));
        // Not re-checked, not detected again.
        feed({sample(id, kF_CDAB, 52.3)});
        feed({sample(id, kF_CDAB, 52.3)});
        feed({sample(id, kF_CDAB, 52.3)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(splits.size(), 1);
        QVERIFY(dialogs.isEmpty());
        QVERIFY(messages->isEmpty());
        QVERIFY(tagOf(id).encodingVerified);
    }

    void splitChangeIsNotAppliedWhenARegisterWasTaken()
    {
        splitDevice();
        mon->onConnected();
        onSplit = [&] {
            add(mkTag(QStringLiteral("Y"), 111, ValueType::Int16));
            return EncodingMonitor::SplitAnswer::Change;
        };
        const TagId id = addSplitTag(QStringLiteral("Setpoint"), 110);
        QCOMPARE(splits.size(), 1);
        QCOMPARE(tagOf(id).address, quint16(110));
        QVERIFY(tagOf(id).type == ValueType::Int16);
        QVERIFY(!tagOf(id).encodingVerified); // stays flagged
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Not applied: HR 110-111 is now used by 'Y'."));
    }

    void splitKeepVerifiesAndClearsNote()
    {
        splitDevice();
        splitAnswer = EncodingMonitor::SplitAnswer::Keep;
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("Setpoint"), 110);
        QVERIFY(tagOf(id).type == ValueType::Int16);
        QCOMPARE(tagOf(id).address, quint16(110));
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(tagOf(id).confirmed16Bit); // remembered in the project
        QVERIFY(model->encodingNote(id).isEmpty());
        feed({good16(id, 0x3333)});
        QCOMPARE(reads.size(), 1);
    }

    void splitUpperHalfMovesDown()
    {
        splitDevice();
        splitAnswer = EncodingMonitor::SplitAnswer::Change;
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("Hi"), 111);
        QCOMPARE(splits.size(), 1);
        QVERIFY(splits[0].text.contains(QStringLiteral("<b>'Hi' is probably half of a float32 value.</b>")));
        QVERIFY(splits[0].text.contains(
            QStringLiteral("As int16 (HR 111):</td><td>16977</td>")));
        QVERIFY(splits[0].text.contains(
            QStringLiteral("As float32 CDAB (HR 110-111):</td><td><b>52.3</b>")));
        QCOMPARE(splits[0].change, QStringLiteral("Change to float32 CDAB at HR 110"));
        QCOMPARE(tagOf(id).address, quint16(110));
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(id).encodingVerified);
    }

    void splitUint16AndAddressBase()
    {
        splitDevice();
        model->setAddressBase(1);
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("U"), 110, ValueType::UInt16);
        QCOMPARE(splits.size(), 1);
        QCOMPARE(splits[0].keep, QStringLiteral("Keep uint16"));
        QCOMPARE(splits[0].change, QStringLiteral("Change to float32 CDAB at HR 111"));
        QVERIFY(splits[0].text.contains(
            QStringLiteral("As uint16 (HR 111):</td><td>13107</td>")));
        QVERIFY(splits[0].text.contains(
            QStringLiteral("As float32 CDAB (HR 111-112):</td><td><b>52.3</b>")));
        QCOMPARE(model->encodingNote(id), QStringLiteral("HR 111-112 read as float32 CDAB give 52.3"));
    }

    void splitInputRegisterPrefix()
    {
        splitDevice();
        mon->onConnected();
        Tag t = mkTag(QStringLiteral("IR"), 110, ValueType::Int16);
        t.area = Area::InputRegister;
        const TagId id = add(t);
        feed({good16(id, 0x3333)});
        QVERIFY(reads[0].area == Area::InputRegister);
        QCOMPARE(model->encodingNote(id), QStringLiteral("IR 110-111 read as float32 CDAB give 52.3"));
    }

    void splitWholeReadFailsReadsEachSide()
    {
        splitDevice();
        readFails = [](quint16, int count) { return count == 3; };
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("S"), 110);
        QCOMPARE(reads.size(), 3);
        QCOMPARE(reads[0].count, 3);
        QCOMPARE(reads[1].address, quint16(109));
        QCOMPARE(reads[1].count, 2);
        QCOMPARE(reads[2].address, quint16(110));
        QCOMPARE(reads[2].count, 2);
        QVERIFY(!tagOf(id).encodingVerified);
        QCOMPARE(splits.size(), 1);
    }

    void splitFailingSideCountsAsAbsent()
    {
        splitDevice();
        readFails = [](quint16 first, int count) { return count == 3 || first == 109; };
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("S"), 110);
        QCOMPARE(reads.size(), 3);
        QVERIFY(!tagOf(id).encodingVerified); // the above side alone is enough
        // And the other way: the tag at 121 (17.5 at HR 120-121) with its above side failing.
        readFails = [](quint16 first, int count) { return count == 3 || first == 121; };
        const TagId hi = addSplitTag(QStringLiteral("Hi"), 121);
        QVERIFY(!tagOf(hi).encodingVerified);
        // Everything failing: nothing is known, nothing flagged.
        readFails = [](quint16, int) { return true; };
        regs[300] = 1;
        const TagId none = addSplitTag(QStringLiteral("N"), 300);
        QVERIFY(tagOf(none).encodingVerified);
        QCOMPARE(splits.size(), 2);
    }

    void splitClipsAtMapEdges()
    {
        splitDevice();
        regs[0] = 0x3333;
        regs[1] = 0x4251;
        regs[65534] = 0x3333;
        regs[65535] = 0x4251;
        splitAnswer = EncodingMonitor::SplitAnswer::Change;
        mon->onConnected();
        const TagId low = addSplitTag(QStringLiteral("Low"), 0);
        QCOMPARE(reads[0].address, quint16(0));
        QCOMPARE(reads[0].count, 2);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(tagOf(low).address, quint16(0));
        QVERIFY(tagOf(low).type == ValueType::Float32);
        const TagId top = addSplitTag(QStringLiteral("Top"), 65535);
        QCOMPARE(reads[1].address, quint16(65534));
        QCOMPARE(reads[1].count, 2);
        QCOMPARE(reads.size(), 2);
        QCOMPARE(tagOf(top).address, quint16(65534));
        QVERIFY(tagOf(top).type == ValueType::Float32);
        QVERIFY(tagOf(top).byteOrder == ByteOrder::CDAB);
    }

    void splitClippedReadFailureHasNoSideReads()
    {
        splitDevice();
        readFails = [](quint16, int) { return true; };
        mon->onConnected();
        addSplitTag(QStringLiteral("Low"), 0);
        QCOMPARE(reads.size(), 1);
        QVERIFY(splits.isEmpty());
    }

    void splitCleanNeighboursDoNothing()
    {
        splitDevice();
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("Count"), 5);
        QCOMPARE(reads.size(), 1);
        QVERIFY(splits.isEmpty());
        QVERIFY(reviews.isEmpty());
        QVERIFY(messages->isEmpty());
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(model->encodingNote(id).isEmpty());
        feed({good16(id, 8)});
        QCOMPARE(reads.size(), 1); // checked once
    }

    void splitWaitsForConnection()
    {
        splitDevice();
        const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        feed({good16(id, 0x3333)}); // not connected: ignored
        QVERIFY(reads.isEmpty());
        mon->onConnected();
        QVERIFY(reads.isEmpty());
        feed({good16(id, 0x3333, Quality::Timeout)});
        QVERIFY(reads.isEmpty());
        feed({good16(id, 0x3333)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(splits.size(), 1);
    }

    void splitMovedTagIsCheckedAgain()
    {
        splitDevice();
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("S"), 5);
        QCOMPARE(reads.size(), 1);
        model->setData(model->index(model->rowOf(id), TagTableModel::Address), 110);
        QVERIFY(tagOf(id).encodingVerified);
        feed({good16(id, 0x3333)});
        QCOMPARE(reads.size(), 2);
        QCOMPARE(splits.size(), 1);
    }

    void splitResultDroppedWhenTagRemovedOrMoved()
    {
        splitDevice();
        autoAnswer = false;
        mon->onConnected();
        const TagId a = addSplitTag(QStringLiteral("A"), 110);
        QCOMPARE(reads.size(), 1);
        model->removeRows(model->rowOf(a), 1);
        answerRead(reads.last());
        QVERIFY(splits.isEmpty());
        QCOMPARE(model->rowCount(), 0);

        const TagId b = add(mkTag(QStringLiteral("B"), 110, ValueType::Int16));
        feed({good16(b, 0x3333)});
        QCOMPARE(reads.size(), 2);
        model->setData(model->index(model->rowOf(b), TagTableModel::Address), 120);
        answerRead(reads.last());
        QVERIFY(splits.isEmpty());
        QVERIFY(tagOf(b).encodingVerified);
        QVERIFY(model->encodingNote(b).isEmpty());
    }

    void splitResultDroppedAcrossDisconnectAndReplace()
    {
        splitDevice();
        autoAnswer = false;
        mon->onConnected();
        const TagId a = addSplitTag(QStringLiteral("A"), 110);
        QCOMPARE(reads.size(), 1);
        mon->onDisconnected();
        answerRead(reads.last());
        QVERIFY(splits.isEmpty());
        QVERIFY(tagOf(a).encodingVerified);

        // Reconnect: the same old answer is not accepted either.
        mon->onConnected();
        answerRead(reads.last());
        QVERIFY(splits.isEmpty());

        // A replaced tag list drops it too.
        load({mkTag(QStringLiteral("A"), 110, ValueType::Int16)});
        mon->onDisconnected();
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QVERIFY(reads.size() >= 2);
        const ReadReq last = reads.last();
        load({mkTag(QStringLiteral("A"), 110, ValueType::Int16)});
        answerRead(last);
        QVERIFY(splits.isEmpty());
        QVERIFY(tagOf(1).encodingVerified);
    }

    void splitPromptsWaitForDialog()
    {
        splitDevice();
        const TagId f = add(mkTag(QStringLiteral("F"), 200, ValueType::Float32, ByteOrder::CDAB));
        const TagId s = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        mon->onConnected();
        int splitsDuringDialog = -1;
        onDialog = [&](EncodingDialog &) {
            feed({good16(s, 0x3333)}); // the probe is answered; the prompt must wait
            splitsDuringDialog = int(splits.size());
            return false;
        };
        for (int i = 0; i < 3; ++i)
            feed({sample(f, kI_ABCD, 100)});
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(splitsDuringDialog, 0);
        QCOMPARE(splits.size(), 1);
        QVERIFY(!tagOf(s).encodingVerified);
    }

    void dialogsWaitForSplitPrompt()
    {
        splitDevice();
        const TagId f = add(mkTag(QStringLiteral("F"), 200, ValueType::Float32, ByteOrder::CDAB));
        const TagId s = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        mon->onConnected();
        int dialogsDuringPrompt = -1;
        onSplit = [&] {
            for (int i = 0; i < 3; ++i)
                feed({sample(f, kI_ABCD, 100)}); // the dialog is needed, but must wait
            dialogsDuringPrompt = int(dialogs.size());
            return EncodingMonitor::SplitAnswer::Later;
        };
        onDialog = [](EncodingDialog &) { return false; };
        feed({good16(s, 0x3333)});
        QCOMPARE(dialogsDuringPrompt, 0);
        QCOMPARE(dialogs.size(), 1);
    }

    void splitRevalidatedAfterPrompt()
    {
        splitDevice();
        mon->onConnected();
        TagId id = 0;
        onSplit = [&] {
            model->setData(model->index(model->rowOf(id), TagTableModel::Address), 5);
            return EncodingMonitor::SplitAnswer::Change;
        };
        id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        feed({good16(id, 0x3333)});
        QCOMPARE(splits.size(), 1);
        QCOMPARE(tagOf(id).address, quint16(5)); // the answer no longer applied
        QVERIFY(tagOf(id).type == ValueType::Int16);
    }

    void splitConnectPassListsSuspiciousTagsOnce()
    {
        splitDevice();
        load({mkTag(QStringLiteral("X"), 110, ValueType::Int16),
              mkTag(QStringLiteral("Y"), 121, ValueType::Int16),
              mkTag(QStringLiteral("Z"), 5, ValueType::Int16)});
        mon->onConnected();
        feed({good16(1, 0x3333), good16(2, 0x418C)});
        QVERIFY(reviews.isEmpty()); // Z has not read yet
        feed({good16(3, 8)});
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(reviews[0],
                 QStringLiteral("<p><b>2 tags are probably half of a float32 value:</b></p><ul>"
                                "<li>'X' (HR 110): 13107 as int16, <b>52.3</b> as float32 CDAB (HR 110-111)</li>"
                                "<li>'Y' (HR 121): 16780 as int16, <b>17.5</b> as float32 CDAB (HR 120-121)</li>"
                                "</ul><p>Writes to them ask first. Review them now?</p>"));
        QVERIFY(!tagOf(1).encodingVerified);
        QVERIFY(!tagOf(2).encodingVerified);
        QVERIFY(tagOf(3).encodingVerified);
        QVERIFY(splits.isEmpty()); // no individual prompts
        feed({good16(1, 0x3333), good16(2, 0x418C), good16(3, 8)});
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(reads.size(), 5); // X: 1, Y: whole read and both sides (HR 122 is absent), Z: 1
    }

    void splitConnectPassReviewPromptsEach()
    {
        splitDevice();
        load({mkTag(QStringLiteral("X"), 110, ValueType::Int16),
              mkTag(QStringLiteral("Y"), 121, ValueType::Int16)});
        reviewAnswer = true;
        splitAnswer = EncodingMonitor::SplitAnswer::Change;
        mon->onConnected();
        feed({good16(1, 0x3333), good16(2, 0x418C)});
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(splits.size(), 2);
        QVERIFY(splits[0].text.contains(QStringLiteral("<b>'X' is probably half")));
        QVERIFY(splits[0].text.contains(QStringLiteral("As int16 (HR 110):")));
        QVERIFY(splits[1].text.contains(QStringLiteral("<b>'Y' is probably half")));
        QVERIFY(splits[1].text.contains(QStringLiteral("As int16 (HR 121):")));
        QVERIFY(tagOf(1).type == ValueType::Float32);
        QVERIFY(tagOf(2).type == ValueType::Float32);
        QCOMPARE(tagOf(2).address, quint16(120));
        QVERIFY(tagOf(2).encodingVerified);
    }

    void splitConnectPassSkipsSilentTags()
    {
        splitDevice();
        load({mkTag(QStringLiteral("Silent"), 5, ValueType::Int16),
              mkTag(QStringLiteral("X"), 110, ValueType::Int16)});
        mon->onConnected();
        for (int i = 0; i < 9; ++i) {
            feed({good16(1, 0, Quality::Timeout), good16(2, 0x3333)});
            QVERIFY(reviews.isEmpty());
        }
        feed({good16(1, 0, Quality::Timeout), good16(2, 0x3333)});
        QCOMPARE(reviews.size(), 1);
        QVERIFY(reviews[0].contains(QStringLiteral("'X'")));
        QVERIFY(!reviews[0].contains(QStringLiteral("Silent")));
        QCOMPARE(reads.size(), 1);
    }

    void splitConnectPassLeavesPendingTagsToThePendingPath()
    {
        splitDevice();
        load({mkTag(QStringLiteral("X"), 110, ValueType::Int16)});
        mon->onConnected();
        const TagId q = add(mkTag(QStringLiteral("Q"), 110, ValueType::Int16));
        feed({good16(1, 0x3333), good16(q, 0x3333)});
        QCOMPARE(reviews.size(), 1);
        QVERIFY(reviews[0].contains(QStringLiteral("'X'")));
        QVERIFY(!reviews[0].contains(QStringLiteral("'Q'")));
        QCOMPARE(splits.size(), 1); // Q's own prompt
        QVERIFY(splits[0].text.contains(QStringLiteral("<b>'Q' is probably half")));
        QVERIFY(splits[0].text.contains(QStringLiteral("As int16 (HR 110):")));
        QCOMPARE(reads.size(), 2);
    }

    void splitSeparateFromThirtyTwoBitReview()
    {
        splitDevice();
        load({mkTag(QStringLiteral("P"), 200, ValueType::Float32, ByteOrder::ABCD),
              mkTag(QStringLiteral("X"), 110, ValueType::Int16)});
        mon->onConnected();
        for (int i = 0; i < 3; ++i)
            feed({sample(1, kF_CDAB, 52.1 + 0.1 * i), good16(2, 0x3333)});
        QCOMPARE(reviews.size(), 2);
        QVERIFY(reviews[0].contains(QStringLiteral("half of a float32"))
                != reviews[1].contains(QStringLiteral("half of a float32")));
        QVERIFY(reviews[0].contains(QStringLiteral("'X'")) != reviews[1].contains(QStringLiteral("'X'")));
    }

    void splitDetectNowDisconnected()
    {
        splitDevice();
        load({mkTag(QStringLiteral("S"), 110, ValueType::Int16)});
        mon->detectNow(1);
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(), QStringLiteral("Connect to check 'S'."));
        QVERIFY(reads.isEmpty());
    }

    void splitDetectNowChecksUnflaggedTag()
    {
        splitDevice();
        load({mkTag(QStringLiteral("S"), 110, ValueType::Int16)});
        mon->onConnected();
        mon->detectNow(1);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(splits.size(), 1);
        QVERIFY(!tagOf(1).encodingVerified);
        QCOMPARE(model->encodingNote(1), QStringLiteral("HR 110-111 read as float32 CDAB give 52.3"));
    }

    // detectNow on an unflagged tag with clean neighbours: a message only.
    void splitDetectNowUnflaggedCleanTagSaysSo()
    {
        splitDevice();
        load({mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        mon->onConnected();
        mon->detectNow(1);
        QVERIFY(tagOf(1).encodingVerified);
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("'S' (HR 5) looks like a 16-bit value: no float32 found around it."));
        QVERIFY(splits.isEmpty());
    }

    // detectNow on a flagged tag that finds nothing now: the flag stays; the split prompt offers
    // only Keep and Later.
    void splitDetectNowKeepsFlagWhenNothingFound()
    {
        splitDevice();
        load({mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        model->setEncodingVerified(1, false);
        model->setEncodingNote(1, QStringLiteral("HR 5-6 read as float32 CDAB give 1"));
        mon->onConnected();
        mon->detectNow(1);
        QCOMPARE(splits.size(), 1);
        QCOMPARE(splits[0].text,
                 QStringLiteral("<p><b>No float32 found around 'S' (HR 5) right now.</b></p>"
                                "<p>It stays flagged. Choose <b>Keep int16</b> if it is a 16-bit "
                                "value.</p>"));
        QVERIFY(splits[0].change.isEmpty());
        QCOMPARE(splits[0].keep, QStringLiteral("Keep int16"));
        QVERIFY(messages->isEmpty());
        // Later: nothing changes.
        QVERIFY(!tagOf(1).encodingVerified);
        QVERIFY(!model->encodingNote(1).isEmpty());
        QVERIFY(!tagOf(1).confirmed16Bit);
        // Keep: verified and remembered.
        splitAnswer = EncodingMonitor::SplitAnswer::Keep;
        mon->detectNow(1);
        QCOMPARE(splits.size(), 2);
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(tagOf(1).confirmed16Bit);
        QVERIFY(model->encodingNote(1).isEmpty());
    }

    // A Change answer cannot happen without a Change button; it is treated as Later.
    void splitNothingFoundPromptIgnoresChange()
    {
        splitDevice();
        load({mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        model->setEncodingVerified(1, false);
        splitAnswer = EncodingMonitor::SplitAnswer::Change;
        mon->onConnected();
        mon->detectNow(1);
        QCOMPARE(splits.size(), 1);
        QVERIFY(tagOf(1).type == ValueType::Int16);
        QVERIFY(!tagOf(1).encodingVerified);
    }

    // An automatic check that finds nothing clears a stale flag (no note), but not a real one.
    void splitAutomaticCheckClearsStaleFlag()
    {
        splitDevice();
        regs[99] = 1;
        regs[100] = 2;
        regs[101] = 3;
        load({mkTag(QStringLiteral("Stale"), 5, ValueType::Int16),
              mkTag(QStringLiteral("Noted"), 100, ValueType::Int16)});
        model->setEncodingVerified(1, false);
        model->setEncodingVerified(2, false);
        model->setEncodingNote(2, QStringLiteral("a note"));
        mon->onConnected();
        feed({good16(1, 8), good16(2, 2)});
        QCOMPARE(reads.size(), 2);
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(!tagOf(2).encodingVerified);
        QVERIFY(splits.isEmpty());
        QVERIFY(reviews.isEmpty());
        QVERIFY(messages->isEmpty());
    }

    // The pending path clears a stale flag too: a flagged tag that was moved to a clean place.
    void splitPendingCheckClearsStaleFlag()
    {
        splitDevice();
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("S"), 110, ValueType::Int16);
        QVERIFY(!tagOf(id).encodingVerified);
        model->setData(model->index(model->rowOf(id), TagTableModel::Address), 5);
        feed({good16(id, 8)});
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(messages->isEmpty());
    }

    // Own register 0 is never flagged, even when the neighbour makes a clean float (0x4268 reads
    // 58.0 with a 0 low word).
    void splitOwnRegisterZeroIsNotFlagged()
    {
        regs[299] = 0;
        regs[300] = 0;
        regs[301] = 0x4268;
        regs[302] = 0;
        mon->onConnected();
        const TagId id = addSplitTag(QStringLiteral("Z"), 300);
        QCOMPARE(reads.size(), 1);
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(splits.isEmpty());
    }

    // A neighbour that another tag maps counts as absent (and is not read).
    void splitNeighbourUsedByOtherTagIsAbsent()
    {
        splitDevice();
        regs[112] = 0x1234;
        load({mkTag(QStringLiteral("S"), 110, ValueType::Int16),
              mkTag(QStringLiteral("O"), 111, ValueType::Int16)});
        mon->onConnected();
        feed({good16(1, 0x3333), good16(2, 0x4251)});
        QVERIFY(splits.isEmpty()); // 111 is O's: S sees only 109
        QVERIFY(reviews.isEmpty());
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(tagOf(2).encodingVerified);
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reads[0].address, quint16(109)); // S: 109..110, 111 not read
        QCOMPARE(reads[0].count, 2);
        QCOMPARE(reads[1].address, quint16(111)); // O: 111..112, 110 not read
        QCOMPARE(reads[1].count, 2);

        // Once O is gone, a manual check of S finds the float.
        model->removeRows(model->rowOf(2), 1);
        mon->detectNow(1);
        QCOMPARE(splits.size(), 1);
        QVERIFY(!tagOf(1).encodingVerified);
    }

    void splitNeighbourInsideThirtyTwoBitTagIsAbsent()
    {
        splitDevice();
        // F covers HR 111-112; S at 110 must not see 111.
        load({mkTag(QStringLiteral("S"), 110, ValueType::Int16),
              mkTag(QStringLiteral("F"), 111, ValueType::Float32, ByteOrder::CDAB)});
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QVERIFY(splits.isEmpty());
        QVERIFY(tagOf(1).encodingVerified);
        // The second register of F counts too: S2 at 113 must not see 112 (a clean float with it).
        regs[112] = 0x3333;
        regs[113] = 0x4251;
        regs[114] = 0x1234;
        const TagId s2 = add(mkTag(QStringLiteral("S2"), 113, ValueType::Int16));
        feed({good16(s2, 0x4251)});
        QVERIFY(splits.isEmpty());
        QVERIFY(tagOf(s2).encodingVerified);
    }

    // Tags with confirmed16Bit are not checked automatically; detectNow still checks them.
    void splitConfirmedTagsAreSkipped()
    {
        splitDevice();
        Tag t = mkTag(QStringLiteral("Kept"), 110, ValueType::Int16);
        t.confirmed16Bit = true;
        load({t});
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QVERIFY(reads.isEmpty());
        QVERIFY(reviews.isEmpty());
        // A confirmed tag added later (pending path) is skipped too.
        Tag u = mkTag(QStringLiteral("Kept2"), 110, ValueType::Int16);
        u.confirmed16Bit = true;
        const TagId id = add(u);
        feed({good16(id, 0x3333)});
        QVERIFY(reads.isEmpty());
        // detectNow checks anyway.
        mon->detectNow(1);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(splits.size(), 1);
        // Moving the tag clears the confirmation and checks it again.
        model->setData(model->index(0, TagTableModel::Address), 111);
        QVERIFY(!tagOf(1).confirmed16Bit);
        feed({good16(1, 0x4251)});
        QCOMPARE(reads.size(), 2);
    }

    // The on-connect checks run once per tag per session.
    void splitConnectChecksOnceAcrossReconnects()
    {
        splitDevice();
        load({mkTag(QStringLiteral("X"), 110, ValueType::Int16),
              mkTag(QStringLiteral("Z"), 5, ValueType::Int16)});
        mon->onConnected();
        feed({good16(1, 0x3333), good16(2, 8)});
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(reads.size(), 2);
        mon->onDisconnected();
        mon->onConnected();
        feed({good16(1, 0x3333), good16(2, 8)});
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(reads.size(), 2);
        QVERIFY(splits.isEmpty());
        // A new session (the list was replaced) checks again.
        load({mkTag(QStringLiteral("X"), 110, ValueType::Int16)});
        mon->onDisconnected();
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QCOMPARE(reviews.size(), 2);
    }

    // A tag that never read Good was not checked: the next connect tries again.
    void splitSilentTagIsCheckedOnReconnect()
    {
        splitDevice();
        load({mkTag(QStringLiteral("X"), 110, ValueType::Int16)});
        mon->onConnected();
        for (int i = 0; i < 10; ++i)
            feed({good16(1, 0, Quality::Timeout)});
        QVERIFY(reads.isEmpty());
        mon->onDisconnected();
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reviews.size(), 1);
    }

    // A tag that moves is handled by the pending path and not checked again on reconnect.
    void splitMovedTagIsNotCheckedByTheConnectPassAgain()
    {
        splitDevice();
        load({mkTag(QStringLiteral("X"), 5, ValueType::Int16)});
        mon->onConnected();
        feed({good16(1, 8)});
        QCOMPARE(reads.size(), 1);
        model->setData(model->index(0, TagTableModel::Address), 110);
        feed({good16(1, 0x3333)}); // the pending path checks the moved tag
        QCOMPARE(reads.size(), 2);
        QCOMPARE(splits.size(), 1);
        mon->onDisconnected();
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reviews.size(), 0);
    }

    void thirtyTwoBitConnectCheckRunsOncePerSession()
    {
        load({mkTag(QStringLiteral("P"), 200, ValueType::Float32, ByteOrder::ABCD)});
        mon->onConnected();
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QCOMPARE(reviews.size(), 1);
        QVERIFY(!tagOf(1).encodingVerified);
        model->setEncodingVerified(1, true);
        mon->onDisconnected();
        mon->onConnected();
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QCOMPARE(reviews.size(), 1); // not flagged or asked again in this session
        QVERIFY(tagOf(1).encodingVerified);
        // A replaced list starts a new session.
        load({mkTag(QStringLiteral("P"), 200, ValueType::Float32, ByteOrder::ABCD)});
        mon->onDisconnected();
        mon->onConnected();
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QCOMPARE(reviews.size(), 2);
    }

    // The 16-bit on-connect review waits while a split prompt is open.
    void splitConnectReviewWaitsForSplitPrompt()
    {
        splitDevice();
        load({mkTag(QStringLiteral("A"), 110, ValueType::Int16),
              mkTag(QStringLiteral("B"), 5, ValueType::Int16)});
        mon->onConnected();
        feed({good16(1, 0x3333)}); // the pass still waits for B
        QVERIFY(reviews.isEmpty());
        int reviewsDuringPrompt = -1;
        onSplit = [&] {
            feed({good16(2, 8)}); // the pass finishes under the open prompt
            reviewsDuringPrompt = int(reviews.size());
            return EncodingMonitor::SplitAnswer::Later;
        };
        const TagId q = add(mkTag(QStringLiteral("Q"), 110, ValueType::Int16));
        feed({good16(q, 0x3333)});
        QCOMPARE(splits.size(), 1);
        QCOMPARE(reviewsDuringPrompt, 0);
        QCOMPARE(reviews.size(), 1);
        QVERIFY(reviews[0].contains(QStringLiteral("'A'")));
    }

    // ... and the 32-bit one waits while an encoding dialog is open.
    void thirtyTwoBitConnectReviewWaitsForDialog()
    {
        load({mkTag(QStringLiteral("P"), 200, ValueType::Float32, ByteOrder::ABCD)});
        mon->onConnected();
        const TagId r = add(mkTag(QStringLiteral("R"), 300, ValueType::Float32, ByteOrder::CDAB));
        int reviewsDuringDialog = -1;
        onDialog = [&](EncodingDialog &) {
            for (int i = 0; i < 3; ++i)
                feedOne(1, kF_CDAB, 52.1 + 0.1 * i); // P's check completes under the open dialog
            reviewsDuringDialog = int(reviews.size());
            return false;
        };
        for (int i = 0; i < 3; ++i)
            feedOne(r, kI_ABCD, 100);
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(reviewsDuringDialog, 0);
        QCOMPARE(reviews.size(), 1);
        QVERIFY(reviews[0].contains(QStringLiteral("'P'")));
    }

    // A timed-out probe stops all split probes until the list is replaced.
    void splitTimeoutStopsProbes()
    {
        splitDevice();
        autoAnswer = false;
        load({mkTag(QStringLiteral("A"), 110, ValueType::Int16),
              mkTag(QStringLiteral("B"), 5, ValueType::Int16)});
        mon->onConnected();
        feed({good16(1, 0x3333), good16(2, 8)});
        QCOMPARE(reads.size(), 1); // B waits behind A
        ModbusResult timeout;
        timeout.error = ModbusError::Timeout;
        mon->onReadFinished(reads[0].id, timeout, {});
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Stopped checking 16-bit tags: the device did not answer a read "
                                "next to 'A'."));
        QCOMPARE(reads.size(), 1); // B's probe was dropped
        // No more probes, no more messages.
        const TagId c = add(mkTag(QStringLiteral("C"), 6, ValueType::Int16));
        feed({good16(c, 9)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(messages->size(), 1);
        QVERIFY(splits.isEmpty());
        QVERIFY(reviews.isEmpty());
        // A replaced list starts probing again.
        autoAnswer = true;
        load({mkTag(QStringLiteral("A"), 110, ValueType::Int16)});
        mon->onDisconnected();
        mon->onConnected();
        feed({good16(1, 0x3333)});
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reviews.size(), 1);
    }

    void splitConnectionLostStopsProbes()
    {
        splitDevice();
        autoAnswer = false;
        mon->onConnected();
        addSplitTag(QStringLiteral("A"), 110);
        QCOMPARE(reads.size(), 1);
        ModbusResult lost;
        lost.error = ModbusError::ConnectionLost;
        mon->onReadFinished(reads[0].id, lost, {});
        QCOMPARE(messages->size(), 1);
        addSplitTag(QStringLiteral("B"), 5);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(messages->size(), 1);
    }

    // A Modbus exception is not a stop: the side counts as absent.
    void splitExceptionDoesNotStopProbes()
    {
        splitDevice();
        mon->onConnected();
        addSplitTag(QStringLiteral("A"), 300); // HR 299-301 do not exist: exceptions
        addSplitTag(QStringLiteral("B"), 5);
        QVERIFY(messages->isEmpty());
        QCOMPARE(reads.last().address, quint16(4));
    }

    // The split prompt mentions what Change resets.
    void splitPromptMentionsScalingReset()
    {
        splitDevice();
        const QString extra = QStringLiteral("<p><small>Change also resets its gain and offset to 1 "
                                             "and 0 and clears its quick values.</small></p>");
        mon->onConnected();
        const TagId p = add(mkTag(QStringLiteral("P"), 110, ValueType::Int16));
        feed({good16(p, 0x3333)});
        QCOMPARE(splits.size(), 1);
        QVERIFY(!splits[0].text.contains(QStringLiteral("Change also resets")));

        Tag g = mkTag(QStringLiteral("G"), 110, ValueType::Int16);
        g.gain = 0.1;
        feed({good16(add(g), 0x3333)});
        QCOMPARE(splits.size(), 2);
        QVERIFY(splits[1].text.endsWith(extra));

        Tag o = mkTag(QStringLiteral("O"), 110, ValueType::Int16);
        o.offset = 5;
        feed({good16(add(o), 0x3333)});
        QCOMPARE(splits.size(), 3);
        QVERIFY(splits[2].text.endsWith(extra));

        Tag q = mkTag(QStringLiteral("Q"), 110, ValueType::Int16);
        q.quickB = 3;
        feed({good16(add(q), 0x3333)});
        QCOMPARE(splits.size(), 4);
        QVERIFY(splits[3].text.endsWith(extra));
    }

    // The default prompt has no Change button when the label is empty.
    void defaultSplitPromptOmitsEmptyChange()
    {
        EncodingMonitor fresh(model.get(), nullptr);
        QStringList buttonTexts;
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box);
            QPushButton *keep = nullptr;
            const auto buttons = box->buttons();
            for (QAbstractButton *b : buttons) {
                buttonTexts << b->text();
                if (b->text() == QStringLiteral("Keep int16"))
                    keep = qobject_cast<QPushButton *>(b);
            }
            QVERIFY(keep);
            keep->click();
        });
        const auto answer = fresh.m_splitPrompt(QStringLiteral("text"), QString(),
                                                QStringLiteral("Keep int16"));
        QVERIFY(answer == EncodingMonitor::SplitAnswer::Keep);
        QCOMPARE(buttonTexts.size(), 2);
        QVERIFY(buttonTexts.contains(QStringLiteral("Decide later")));

        buttonTexts.clear();
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box);
            const auto buttons = box->buttons();
            for (QAbstractButton *b : buttons)
                buttonTexts << b->text();
            buttons.first()->click();
        });
        fresh.m_splitPrompt(QStringLiteral("text"), QStringLiteral("Change it"),
                            QStringLiteral("Keep int16"));
        QCOMPARE(buttonTexts.size(), 3);
    }

    // ---- refused 16-bit reads (a device that refuses half of a 32-bit value) ---------------------

    /// Like a CM50: HR 110-111 hold float32 CDAB 52.3; every other register is absent, so a read
    /// that touches only half of it, or goes outside the pair, is refused.
    void strictDevice()
    {
        regs.clear();
        regs[110] = 0x3333;
        regs[111] = 0x4251;
        // Alone, one register of the pair is refused.
        readFails = [](quint16 address, int count) {
            return count == 1 && (address == 110 || address == 111);
        };
    }

    TagSample exception16(TagId id) const { return good16(id, 0, Quality::Exception); }

    static QString refusedIntro(const QString &name, const QString &type, int address, int start)
    {
        return QStringLiteral("<p><b>'%1' (HR %2, %3) can't be read alone.</b></p>"
                              "<p>The device answered &quot;Illegal data address&quot;. HR %4-%5 can be "
                              "read as a 32-bit value: choose its encoding, or type the value shown "
                              "on the device.</p>")
            .arg(name, QString::number(address), type).arg(start).arg(start + 1);
    }

    /// Dialog seam: remembers the intro and the registers, types the value shown on the device.
    void answerWith(const QString &display, bool accept = true)
    {
        onDialog = [this, display, accept](EncodingDialog &dlg) {
            if (auto *intro = dlg.findChild<QLabel *>(QStringLiteral("introLabel")))
                introTexts << intro->text();
            if (auto *regsLabel = dlg.findChild<QLabel *>(QStringLiteral("registersLabel")))
                registerTexts << regsLabel->text();
            dlg.setDisplayValue(display);
            return accept;
        };
    }

    void refusedReadOpensDialogAndMovesTag()
    {
        strictDevice();
        introTexts.clear();
        registerTexts.clear();
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        Tag proto = mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16);
        proto.gain = 2;
        proto.offset = 1;
        proto.quickA = 5;
        const TagId id = add(proto);
        QVERIFY(reads.isEmpty());
        feed({exception16(id)});
        QCOMPARE(reads.size(), 3);
        QCOMPARE(reads[0].address, quint16(110)); // the own register alone, first
        QCOMPARE(reads[0].count, 1);
        QCOMPARE(reads[1].address, quint16(110));
        QCOMPARE(reads[1].count, 2);
        QCOMPARE(reads[2].address, quint16(109));
        QCOMPARE(reads[2].count, 2);
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(introTexts, QStringList{refusedIntro(QStringLiteral("Setpoint"), QStringLiteral("int16"),
                                                      110, 110)});
        QCOMPARE(registerTexts.size(), 1);
        QVERIFY(registerTexts[0].contains(QStringLiteral("HR 110-111")));
        QVERIFY(registerTexts[0].contains(QStringLiteral("0x3333")));
        const Tag t = tagOf(id);
        QVERIFY(t.type == ValueType::Float32);
        QVERIFY(t.byteOrder == ByteOrder::CDAB);
        QCOMPARE(t.address, quint16(110));
        QVERIFY(t.encodingVerified);
        QCOMPARE(t.gain, 1.0);
        QCOMPARE(t.offset, 0.0);
        QVERIFY(!t.quickA);
        QVERIFY(model->encodingNote(id).isEmpty());
        QVERIFY(!mon->isPending(id));
        // Now a 32-bit tag: its readings are not detected again, and nothing else is asked.
        feed({sample(id, kF_CDAB, 52.3)});
        feed({sample(id, kF_CDAB, 52.3)});
        feed({sample(id, kF_CDAB, 52.3)});
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(reads.size(), 3);
        QVERIFY(messages->isEmpty());
        QVERIFY(reviews.isEmpty());
        QVERIFY(splits.isEmpty());
    }

    void refusedReadFlagsBeforeTheDialogAnswers()
    {
        strictDevice();
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16));
        QString note;
        bool verified = true;
        onDialog = [&](EncodingDialog &) {
            note = model->encodingNote(id);
            verified = tagOf(id).encodingVerified;
            return false;
        };
        feed({exception16(id)});
        QCOMPARE(note, QStringLiteral("HR 110 can't be read alone; HR 110-111 can be read as a "
                                      "32-bit value"));
        QVERIFY(!verified);
    }

    void refusedReadUpperHalfMovesDown()
    {
        strictDevice();
        introTexts.clear();
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Upper"), 111, ValueType::UInt16));
        feed({exception16(id)});
        QCOMPARE(reads.size(), 3);
        QCOMPARE(reads[0].count, 1);
        QCOMPARE(reads[1].address, quint16(111)); // refused: HR 112 does not exist
        QCOMPARE(reads[2].address, quint16(110));
        QCOMPARE(introTexts,
                 QStringList{refusedIntro(QStringLiteral("Upper"), QStringLiteral("uint16"), 111, 110)});
        QCOMPARE(tagOf(id).address, quint16(110));
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(id).encodingVerified);
    }

    void refusedReadAddressBaseOneShowsOneBasedRegisters()
    {
        strictDevice();
        model->setAddressBase(1);
        introTexts.clear();
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(introTexts, QStringList{refusedIntro(QStringLiteral("S"), QStringLiteral("int16"), 111, 111)});
        QCOMPARE(tagOf(id).address, quint16(110));
    }

    void refusedReadInputRegisterPrefix()
    {
        strictDevice();
        introTexts.clear();
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        Tag proto = mkTag(QStringLiteral("S"), 110, ValueType::Int16);
        proto.area = Area::InputRegister;
        const TagId id = add(proto);
        feed({exception16(id)});
        QVERIFY(reads[0].area == Area::InputRegister);
        QCOMPARE(introTexts.size(), 1);
        QVERIFY(introTexts[0].contains(QStringLiteral("IR 110, int16")));
        QVERIFY(introTexts[0].contains(QStringLiteral("IR 110-111")));
        QVERIFY(model->encodingNote(id).isEmpty()); // moved and verified
        QVERIFY(tagOf(id).area == Area::InputRegister);
    }

    // Both pairs read: the one whose best group scores higher.
    void refusedReadHigherScoreWins()
    {
        regs.clear();
        regs[109] = 0x4251; // below pair (109, 110): 52.3 as float32 ABCD
        regs[110] = 0x3333;
        regs[111] = 0x7FFF; // own pair (110, 111): nothing plausible
        readFails = [](quint16, int count) { return count == 1; };
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(reads.size(), 3);
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(tagOf(id).address, quint16(109));
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
    }

    // Same score: the pair that starts at the tag's own address.
    void refusedReadTieStartsAtOwnAddress()
    {
        regs.clear();
        regs[109] = 0x4251; // (109, 110): 52.3 ABCD
        regs[110] = 0x3333;
        regs[111] = 0x4251; // (110, 111): 52.3 CDAB
        readFails = [](quint16, int count) { return count == 1; };
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(reads.size(), 3);
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(tagOf(id).address, quint16(110));
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
    }

    // The view the dialog ranks uses gain 1 and the device's byte order; the tag keeps its own
    // gain when the dialog is cancelled.
    void refusedReadCancelKeepsGain()
    {
        strictDevice();
        mon->onConnected();
        Tag proto = mkTag(QStringLiteral("S"), 110, ValueType::Int16);
        proto.gain = 100;
        const TagId id = add(proto);
        std::optional<Encoding> chosen;
        onDialog = [&](EncodingDialog &dlg) {
            dlg.setDisplayValue(QStringLiteral("52.3"));
            chosen = dlg.chosenEncoding();
            return false;
        };
        feed({exception16(id)});
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(chosen);
        QVERIFY(chosen->order == ByteOrder::CDAB);
        QCOMPARE(tagOf(id).gain, 100.0);
        QVERIFY(tagOf(id).type == ValueType::Int16);
    }

    void refusedReadNeitherPairReadsDoesNothingAutomatically()
    {
        strictDevice();
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Gone"), 300, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(reads.size(), 3);
        QCOMPARE(reads[0].address, quint16(300));
        QCOMPARE(reads[0].count, 1);
        QCOMPARE(reads[1].address, quint16(300));
        QCOMPARE(reads[2].address, quint16(299));
        QVERIFY(dialogs.isEmpty());
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(model->encodingNote(id).isEmpty());
        QVERIFY(messages->isEmpty());
        QVERIFY(splits.isEmpty());
        // detectNow says so.
        mon->detectNow(id);
        QCOMPARE(reads.size(), 6);
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("'Gone' (HR 300) can't be read, alone or as part of a 32-bit value: "
                                "check the register number."));
        QVERIFY(dialogs.isEmpty());
    }

    void refusedReadCancelKeepsFlagAndDetectNowRetries()
    {
        strictDevice();
        answerWith(QStringLiteral("52.3"), false);
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(id).type == ValueType::Int16);
        QCOMPARE(tagOf(id).address, quint16(110));
        QVERIFY(!tagOf(id).encodingVerified);
        QVERIFY(!model->encodingNote(id).isEmpty());
        // Not asked again by itself.
        feed({exception16(id)});
        feed({exception16(id)});
        QCOMPARE(reads.size(), 3);
        QCOMPARE(dialogs.size(), 1);
        // detectNow runs the flow again.
        answerWith(QStringLiteral("52.3"));
        mon->detectNow(id);
        QCOMPARE(reads.size(), 6);
        QCOMPARE(dialogs.size(), 2);
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(model->encodingNote(id).isEmpty());
    }

    void refusedReadSkipsConfirmed16BitTagsExceptForDetectNow()
    {
        strictDevice();
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16));
        model->setConfirmed16Bit(id, true);
        feed({exception16(id)});
        QVERIFY(reads.isEmpty());
        QVERIFY(dialogs.isEmpty());
        mon->detectNow(id);
        QCOMPARE(reads.size(), 3);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(id).type == ValueType::Float32);
    }

    void refusedReadDetectNowWithoutExceptionUsesTheSplitCheck()
    {
        strictDevice();
        regs[109] = 0; // the plain split check reads HR 109-111
        regs[112] = 0;
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16));
        feed({good16(id, 0x3333)});
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reads[0].count, 3);
        QCOMPARE(splits.size(), 1);
        // A later Good sample keeps it that way.
        reads.clear();
        mon->detectNow(id);
        QCOMPARE(reads.size(), 1);
        QCOMPARE(reads[0].count, 3);
        QVERIFY(dialogs.isEmpty());
    }

    void refusedReadOtherQualitiesDoNotTrigger()
    {
        strictDevice();
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Setpoint"), 110, ValueType::Int16));
        feed({good16(id, 0, Quality::Timeout)});
        feed({good16(id, 0, Quality::Error)});
        feed({good16(id, 0, Quality::Disconnected)});
        QVERIFY(reads.isEmpty());
        mon->detectNow(id); // not Exception: the split check (the whole read is refused here)
        QVERIFY(!reads.isEmpty());
        QCOMPARE(reads[0].address, quint16(109));
        QCOMPARE(reads[0].count, 3);
        QVERIFY(dialogs.isEmpty());
    }

    void refusedReadPairOverlappingAnotherTagIsSkipped()
    {
        regs.clear();
        regs[109] = 0x4251;
        regs[110] = 0x3333;
        regs[111] = 0x4251;
        readFails = [](quint16, int count) { return count == 1; };
        answerWith(QStringLiteral("52.3"), false);
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        add(mkTag(QStringLiteral("Next"), 111, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reads[1].address, quint16(109)); // (110, 2) overlaps HR 111
        QCOMPARE(dialogs.size(), 1);

        reads.clear();
        mon->onTagsReplaced();
        load({mkTag(QStringLiteral("S"), 110, ValueType::Int16),
              mkTag(QStringLiteral("Prev"), 109, ValueType::Int16)});
        mon->onDisconnected();
        mon->onConnected();
        feed({exception16(1)});
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reads[1].address, quint16(110)); // (109, 2) overlaps HR 109
    }

    void refusedReadSkipsPairsOutsideTheAddressSpace()
    {
        strictDevice();
        mon->onConnected();
        const TagId low = add(mkTag(QStringLiteral("Low"), 0, ValueType::Int16));
        const TagId high = add(mkTag(QStringLiteral("High"), 65535, ValueType::Int16));
        feed({exception16(low)});
        QCOMPARE(reads.size(), 2); // alone, then (0, 2)
        QCOMPARE(reads[1].address, quint16(0));
        feed({exception16(high)});
        QCOMPARE(reads.size(), 4); // alone, then (65534, 2)
        QCOMPARE(reads[3].address, quint16(65534));
    }

    void refusedReadTimeoutStopsProbes()
    {
        strictDevice();
        autoAnswer = false;
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        feed({exception16(a)});
        QCOMPARE(reads.size(), 1);
        ModbusResult timeout;
        timeout.error = ModbusError::Timeout;
        mon->onReadFinished(reads[0].id, timeout, {});
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Stopped checking 16-bit tags: the device did not answer a read "
                                "next to 'A'."));
        QCOMPARE(reads.size(), 1); // the second pair is not tried
        const TagId b = add(mkTag(QStringLiteral("B"), 5, ValueType::Int16));
        feed({exception16(b)});
        QCOMPARE(reads.size(), 1);
        QVERIFY(dialogs.isEmpty());
    }

    void refusedReadConnectionLostStopsProbes()
    {
        strictDevice();
        autoAnswer = false;
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        feed({exception16(a)});
        ModbusResult lost;
        lost.error = ModbusError::ConnectionLost;
        mon->onReadFinished(reads[0].id, lost, {});
        QCOMPARE(messages->size(), 1);
        QCOMPARE(reads.size(), 1);
    }

    void refusedReadStaleResultIsDropped()
    {
        strictDevice();
        autoAnswer = false;
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        feed({exception16(a)});
        QCOMPARE(reads.size(), 1);
        // The tag moves meanwhile: neither the answer nor the second pair matter.
        model->setData(model->index(0, TagTableModel::Address), 300);
        answerRead(reads[0]);
        QCOMPARE(reads.size(), 1);
        QVERIFY(dialogs.isEmpty());
        QVERIFY(tagOf(a).encodingVerified);
        QVERIFY(model->encodingNote(a).isEmpty());
    }

    void refusedReadAnswerIsNotAppliedWhenTagMovedInTheDialog()
    {
        strictDevice();
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        onDialog = [&](EncodingDialog &dlg) {
            dlg.setDisplayValue(QStringLiteral("52.3"));
            model->setData(model->index(0, TagTableModel::Address), 300);
            return true;
        };
        feed({exception16(a)});
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(tagOf(a).address, quint16(300));
        QVERIFY(tagOf(a).type == ValueType::Int16);
    }

    void refusedReadWaitsForOpenDialog()
    {
        strictDevice();
        regs[120] = 0x0000; // a float32 CDAB 100.0 at HR 120-121
        regs[121] = 0x42C8;
        readFails = [](quint16 address, int count) {
            return count == 1 && (address == 110 || address == 111 || address == 120 || address == 121);
        };
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        const TagId b = add(mkTag(QStringLiteral("B"), 120, ValueType::Int16));
        int depth = 0;
        int maxDepth = 0;
        onDialog = [&](EncodingDialog &dlg) {
            ++depth;
            maxDepth = qMax(maxDepth, depth);
            if (dialogs.size() == 1)
                feed({exception16(b)}); // while A's dialog is open
            dlg.setDisplayValue(dialogs.size() == 1 ? QStringLiteral("52.3") : QStringLiteral("100"));
            --depth;
            return true;
        };
        feed({exception16(a)});
        QCOMPARE(dialogs.size(), 2);
        QCOMPARE(maxDepth, 1);
        QVERIFY(tagOf(a).type == ValueType::Float32);
        QVERIFY(tagOf(b).type == ValueType::Float32);
        QCOMPARE(tagOf(b).address, quint16(120));
    }

    void refusedReadConnectPassHandlesTheTagOnceAndCompletes()
    {
        strictDevice();
        regs[4] = 7;
        regs[5] = 8;
        regs[6] = 9;
        load({mkTag(QStringLiteral("Refused"), 110, ValueType::Int16),
              mkTag(QStringLiteral("Plain"), 5, ValueType::Int16)});
        answerWith(QStringLiteral("52.3"), false);
        mon->onConnected();
        feed({exception16(1)});
        QVERIFY(mon->m_passActive); // Plain has not read yet
        QCOMPARE(reads.size(), 3);
        QCOMPARE(dialogs.size(), 1);
        feed({good16(2, 8)});
        QVERIFY(!mon->m_passActive);
        QCOMPARE(reads.size(), 4); // Plain's split check
        QVERIFY(reviews.isEmpty());
        QCOMPARE(dialogs.size(), 1);
        // Reconnect: the same tag is not handled again.
        mon->onDisconnected();
        mon->onConnected();
        feed({exception16(1), good16(2, 8)});
        QCOMPARE(reads.size(), 4);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(!mon->m_passActive);
    }

    void refusedReadConnectPassCountsExceptionAsSampled()
    {
        strictDevice();
        load({mkTag(QStringLiteral("Refused"), 110, ValueType::Int16)});
        answerWith(QStringLiteral("52.3"));
        mon->onConnected();
        feed({exception16(1)});
        QVERIFY(!mon->m_passActive);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(1).type == ValueType::Float32);
    }

    void refusedReadConnectPassSkipsConfirmedTags()
    {
        strictDevice();
        Tag t = mkTag(QStringLiteral("Refused"), 110, ValueType::Int16);
        t.confirmed16Bit = true;
        load({t});
        mon->onConnected();
        QVERIFY(mon->m_connectWait.isEmpty());
        feed({exception16(1)});
        QVERIFY(reads.isEmpty());
        QVERIFY(dialogs.isEmpty());
    }

    void refusedReadDialogGetsNoLiveFeed()
    {
        strictDevice();
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        qsizetype readings = -1;
        onDialog = [&](EncodingDialog &dlg) {
            feed({exception16(a), good16(a, 5)});
            readings = dlg.result().candidates.first().values.size();
            return false;
        };
        feed({exception16(a)});
        QCOMPARE(readings, qsizetype(1)); // just the probed pair
    }

    void refusedReadAppliesByteOrderToOtherUnconfirmedTags()
    {
        strictDevice();
        load({mkTag(QStringLiteral("Other"), 200, ValueType::Float32, ByteOrder::ABCD)});
        model->setEncodingVerified(1, false);
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        onDialog = [&](EncodingDialog &dlg) {
            dlg.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        feed({exception16(a)});
        QVERIFY(tagOf(a).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(1).encodingVerified); // only the order was chosen for it
    }


    // The tag's own register is read alone first; only exception 2 continues to the pair probes.
    void checkOtherErrorStopsAfterTheOwnRead(quint8 code)
    {
        {
            strictDevice();
            autoAnswer = false;
            mon->onConnected();
            const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
            feed({exception16(id)});
            QCOMPARE(reads.size(), 1);
            QCOMPARE(reads[0].count, 1);
            ModbusResult busy;
            busy.error = ModbusError::Exception;
            busy.exceptionCode = code;
            busy.message = QStringLiteral("boom");
            mon->onReadFinished(reads[0].id, busy, {});
            QCOMPARE(reads.size(), 1); // no pair probes
            QVERIFY(dialogs.isEmpty());
            QVERIFY(tagOf(id).encodingVerified);
            QVERIFY(model->encodingNote(id).isEmpty());
            QVERIFY(messages->isEmpty());
            mon->detectNow(id);
            QCOMPARE(reads.size(), 2);
            mon->onReadFinished(reads[1].id, busy, {});
            QCOMPARE(reads.size(), 2);
            QCOMPARE(messages->size(), 1);
            QCOMPARE(messages->first().first().toString(),
                     QStringLiteral("'S' (HR 110) could not be read: boom."));
            QVERIFY(dialogs.isEmpty());
        }
    }
    void refusedReadExceptionCode3StopsAfterTheOwnRead() { checkOtherErrorStopsAfterTheOwnRead(3); }
    void refusedReadExceptionCode4StopsAfterTheOwnRead() { checkOtherErrorStopsAfterTheOwnRead(4); }
    void refusedReadExceptionCode6StopsAfterTheOwnRead() { checkOtherErrorStopsAfterTheOwnRead(6); }

    // The own register reads fine: not a refused read, the normal split check runs instead.
    void refusedReadOwnReadOkFallsBackToTheSplitCheck()
    {
        regs[109] = 0;
        regs[110] = 5;
        regs[111] = 0;
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("S"), 110, ValueType::Int16));
        feed({exception16(id)});
        QCOMPARE(reads.size(), 2);
        QCOMPARE(reads[0].address, quint16(110));
        QCOMPARE(reads[0].count, 1);
        QCOMPARE(reads[1].address, quint16(109)); // the split check's neighbourhood read
        QCOMPARE(reads[1].count, 3);
        QVERIFY(dialogs.isEmpty());
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(model->encodingNote(id).isEmpty());
        QVERIFY(messages->isEmpty());
        // detectNow: the own read is not repeated by the monitor on its own, and still no dialog.
        mon->detectNow(id);
        QVERIFY(dialogs.isEmpty());
    }

    // A failed block marks every tag of it Exception; only the tag the device really refuses is
    // flagged and gets the dialog.
    void refusedReadBlockMateIsNotFlagged()
    {
        strictDevice();
        regs[105] = 1;
        answerWith(QStringLiteral("52.3"));
        introTexts.clear();
        mon->onConnected();
        const TagId mate = add(mkTag(QStringLiteral("Mate"), 105, ValueType::Int16));
        const TagId refused = add(mkTag(QStringLiteral("Refused"), 110, ValueType::Int16));
        feed({exception16(mate), exception16(refused)});
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(introTexts.size(), 1);
        QVERIFY(introTexts[0].contains(QStringLiteral("'Refused'")));
        QCOMPARE(tagOf(mate).address, quint16(105));
        QVERIFY(tagOf(mate).type == ValueType::Int16);
        QVERIFY(tagOf(mate).encodingVerified);
        QVERIFY(model->encodingNote(mate).isEmpty());
        QVERIFY(tagOf(refused).type == ValueType::Float32);
    }

    // B's pair was readable when probed, but A's dialog (open meanwhile) then took one register.
    void refusedReadPairTakenWhileQueuedIsNotApplied()
    {
        regs[110] = 0x3333;
        regs[111] = 0x4251;
        regs[112] = 0x1234;
        readFails = [](quint16, int count) { return count == 1; };
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        const TagId b = add(mkTag(QStringLiteral("B"), 112, ValueType::Int16));
        onDialog = [&](EncodingDialog &dlg) {
            if (dialogs.size() == 1)
                feed({exception16(b)}); // B's pair is (111, 112) while A is still at HR 110
            dlg.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        feed({exception16(a)});
        QCOMPARE(dialogs.size(), 1); // B's dialog is not opened
        QVERIFY(tagOf(a).type == ValueType::Float32);
        QCOMPARE(tagOf(b).address, quint16(112));
        QVERIFY(tagOf(b).type == ValueType::Int16);
        QVERIFY(!tagOf(b).encodingVerified); // still flagged
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Not applied: HR 111-112 is now used by 'A'."));
    }

    // A tag added while the dialog is open takes the register the answer would use.
    void refusedReadPairTakenInTheDialogIsNotApplied()
    {
        strictDevice();
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        onDialog = [&](EncodingDialog &dlg) {
            add(mkTag(QStringLiteral("Y"), 111, ValueType::Int16));
            dlg.setDisplayValue(QStringLiteral("52.3"));
            return true;
        };
        feed({exception16(a)});
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(tagOf(a).address, quint16(110));
        QVERIFY(tagOf(a).type == ValueType::Int16);
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Not applied: HR 110-111 is now used by 'Y'."));
    }

    void refusedReadQueuedDialogIsDroppedAfterReconnect()
    {
        strictDevice();
        regs[120] = 0x0000;
        regs[121] = 0x42C8;
        readFails = [](quint16 address, int count) {
            return count == 1 && (address == 110 || address == 111 || address == 120 || address == 121);
        };
        mon->onConnected();
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Int16));
        const TagId b = add(mkTag(QStringLiteral("B"), 120, ValueType::Int16));
        onDialog = [&](EncodingDialog &) {
            if (dialogs.size() == 1) {
                feed({exception16(b)}); // queued behind A's dialog
                mon->onDisconnected();
                mon->onConnected();
            }
            return false;
        };
        feed({exception16(a)});
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(b).type == ValueType::Int16);
        QCOMPARE(tagOf(b).address, quint16(120));
    }

    // ---- one dialog at a time ----------------------------------------------------------------

    void dialogsAreQueued()
    {
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Float32, ByteOrder::CDAB));
        const TagId b = add(mkTag(QStringLiteral("B"), 120, ValueType::Float32, ByteOrder::CDAB));
        int depth = 0;
        int maxDepth = 0;
        QStringList order;
        int liveSize = -1;
        int openedWhileFirst = -1;
        onDialog = [&](EncodingDialog &dlg) {
            ++depth;
            maxDepth = qMax(maxDepth, depth);
            order << dlg.windowTitle();
            if (order.size() == 1) {
                // Samples keep arriving while the first dialog is open.
                feed({sample(a, kI_ABCD, 100), sample(b, kI_ABCD, 100)});
                liveSize = int(dlg.result().candidates.first().values.size());
                openedWhileFirst = int(dialogs.size());
            }
            --depth;
            return true;
        };
        for (int i = 0; i < 3; ++i)
            feed({sample(a, kI_ABCD, 100), sample(b, kI_ABCD, 100)});
        QCOMPARE(order, (QStringList{QStringLiteral("Detect encoding - A"),
                                     QStringLiteral("Detect encoding - B")}));
        QCOMPARE(maxDepth, 1);
        QCOMPARE(liveSize, 4);
        QCOMPARE(openedWhileFirst, 1);
        QVERIFY(!mon->isPending(a));
        QVERIFY(!mon->isPending(b));
    }

    void tagRemovedWhileDialogOpen()
    {
        load({mkTag(QStringLiteral("T"), 110), mkTag(QStringLiteral("U"), 120, ValueType::Int32)});
        onDialog = [&](EncodingDialog &) {
            model->removeRows(model->rowOf(1), 1);
            return true;
        };
        mon->detectNow(1);
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(tagOf(2).type == ValueType::Int32);
        QVERIFY(messages->isEmpty());
    }

    void tagReconfiguredWhileDialogOpen()
    {
        load({mkTag(QStringLiteral("T"), 110)});
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        bool chosen = false;
        onDialog = [&](EncodingDialog &dlg) {
            model->setData(model->index(0, TagTableModel::Address), 150);
            chosen = dlg.chosenEncoding() == std::optional<Encoding>(kF_CDAB);
            return true;
        };
        mon->detectNow(1);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(chosen);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::ABCD); // not applied to the new address
        QVERIFY(!tagOf(1).encodingVerified);             // re-addressed: to be confirmed
        QVERIFY(mon->isPending(1));
    }
};

QTEST_MAIN(TstEncodingMonitor)
#include "tst_encodingmonitor.moc"
