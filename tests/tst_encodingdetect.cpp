#include "EncodingDetect.h"
#include "TagCodec.h"

#include <QtTest>

#include <cmath>

using namespace mb;

Q_DECLARE_METATYPE(mb::ByteOrder)

#define QCOMPARE_ENC(actual, expected)     QVERIFY2((actual) == (expected), qPrintable(mb::toString(actual) + QStringLiteral(" != ") + mb::toString(expected)))

namespace {

Tag makeTag(ValueType type, ByteOrder order)
{
    Tag t;
    t.type = type;
    t.byteOrder = order;
    return t;
}

RegisterPair regsFor(const Tag &trueTag, double scaled)
{
    const auto r = encodeRegisters(trueTag, scaled);
    Q_ASSERT(r && r->size() == 2);
    return {(*r)[0], (*r)[1]};
}

const EncodingCandidate *find(const EncodingResult &res, const Encoding &e)
{
    for (const EncodingCandidate &c : res.candidates) {
        if (c.encoding == e)
            return &c;
    }
    return nullptr;
}

constexpr Encoding kF_CDAB{ValueType::Float32, ByteOrder::CDAB};

} // namespace

class TstEncodingDetect : public QObject
{
    Q_OBJECT
private slots:
    void deviceRegistersRoundTrip_data()
    {
        QTest::addColumn<ByteOrder>("order");
        QTest::newRow("ABCD") << ByteOrder::ABCD;
        QTest::newRow("CDAB") << ByteOrder::CDAB;
        QTest::newRow("BADC") << ByteOrder::BADC;
        QTest::newRow("DCBA") << ByteOrder::DCBA;
    }
    void deviceRegistersRoundTrip()
    {
        QFETCH(ByteOrder, order);
        const Tag tag = makeTag(ValueType::UInt32, order);
        QRandomGenerator rng(1234);
        QList<quint32> raws{0u, 1u, 0xFFFFFFFFu, 0x12345678u, 0x80000000u};
        for (int i = 0; i < 300; ++i)
            raws.append(rng.generate());
        for (quint32 raw : raws) {
            const RegisterPair p = deviceRegisters(tag, raw);
            QCOMPARE(combineRegisters(tag, p.r0, p.r1), raw);
        }
        // Concrete layout check: raw 0x11223344.
        const RegisterPair p = deviceRegisters(tag, 0x11223344u);
        switch (order) {
        case ByteOrder::ABCD: QCOMPARE(p, (RegisterPair{0x1122, 0x3344})); break;
        case ByteOrder::CDAB: QCOMPARE(p, (RegisterPair{0x3344, 0x1122})); break;
        case ByteOrder::BADC: QCOMPARE(p, (RegisterPair{0x2211, 0x4433})); break;
        case ByteOrder::DCBA: QCOMPARE(p, (RegisterPair{0x4433, 0x2211})); break;
        }
    }
    void deviceRegisters16Bit()
    {
        const Tag tag = makeTag(ValueType::Int16, ByteOrder::CDAB);
        QCOMPARE(deviceRegisters(tag, 0xABCDu), (RegisterPair{0xABCD, 0}));
    }

    void parseDisplay()
    {
        auto p = parseDisplayValue(QStringLiteral("52.3"));
        QVERIFY(p);
        QCOMPARE(p->first, 52.3);
        QCOMPARE(p->second, 0.2615);
        p = parseDisplayValue(QStringLiteral("1013"));
        QVERIFY(p);
        QCOMPARE(p->first, 1013.0);
        QCOMPARE(p->second, 5.065);
        p = parseDisplayValue(QStringLiteral("0.05"));
        QVERIFY(p);
        QCOMPARE(p->first, 0.05);
        QCOMPARE(p->second, 0.005);
        p = parseDisplayValue(QStringLiteral("1,5"));
        QVERIFY(p);
        QCOMPARE(p->first, 1.5);
        QCOMPARE(p->second, 0.05); // half of 0.1 = 0.05 beats 0.5 % of 1.5
        p = parseDisplayValue(QStringLiteral("  -3 "));
        QVERIFY(p);
        QCOMPARE(p->first, -3.0);
        QCOMPARE(p->second, 0.5);
        p = parseDisplayValue(QStringLiteral("+2.50"));
        QVERIFY(p);
        QCOMPARE(p->first, 2.5);
        QCOMPARE(p->second, 0.0125);
        p = parseDisplayValue(QStringLiteral("5.23e1"));
        QVERIFY(p);
        QCOMPARE(p->first, 52.3);
        QCOMPARE(p->second, 0.2615);
        p = parseDisplayValue(QStringLiteral("5.23E-1"));
        QVERIFY(p);
        QCOMPARE(p->first, 0.523);
        QCOMPARE(p->second, 0.002615);
        p = parseDisplayValue(QStringLiteral(".5"));
        QVERIFY(p);
        QCOMPARE(p->first, 0.5);
        p = parseDisplayValue(QStringLiteral("0e5"));
        QVERIFY(p);
        QCOMPARE(p->first, 0.0);
        QCOMPARE(p->second, 50000.0);

        // A unit after the number, spaces between digits and a thousands separator are fine.
        p = parseDisplayValue(QString::fromUtf8("52.3 °C"));
        QVERIFY(p);
        QCOMPARE(p->first, 52.3);
        p = parseDisplayValue(QStringLiteral("75%"));
        QVERIFY(p);
        QCOMPARE(p->first, 75.0);
        p = parseDisplayValue(QStringLiteral("1 013,25"));
        QVERIFY(p);
        QCOMPARE(p->first, 1013.25);
        p = parseDisplayValue(QStringLiteral("1,013.25"));
        QVERIFY(p);
        QCOMPARE(p->first, 1013.25);
        p = parseDisplayValue(QStringLiteral("1.013,25"));
        QVERIFY(p);
        QCOMPARE(p->first, 1013.25);
        p = parseDisplayValue(QStringLiteral("1,013")); // one separator: decimal
        QVERIFY(p);
        QCOMPARE(p->first, 1.013);

        for (const char *bad : {"abc", "inf", "-inf", "nan", "", "  ", "1.2.3", "1e", "5.", "3-", "--1",
                                "1e999", "0x10", ".", "°C"})
            QVERIFY2(!parseDisplayValue(QString::fromLatin1(bad)), bad);
    }

    void toStringAndHelpers()
    {
        QCOMPARE(toString(kF_CDAB), QStringLiteral("float32 CDAB"));
        const Tag t = withEncoding(makeTag(ValueType::Int16, ByteOrder::ABCD),
                                   {ValueType::UInt32, ByteOrder::DCBA});
        QCOMPARE_ENC(encodingOf(t), (Encoding{ValueType::UInt32, ByteOrder::DCBA}));
    }

    // 1. Floats in each byte order are recognised from a wrong current encoding.
    void floatsAllOrders_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<ByteOrder>("order");
        for (double v : {52.3, 1013.25, -17.5}) {
            for (ByteOrder o : {ByteOrder::ABCD, ByteOrder::CDAB, ByteOrder::BADC, ByteOrder::DCBA}) {
                QTest::newRow(qPrintable(QStringLiteral("%1 %2").arg(v).arg(toString(o))))
                    << v << o;
            }
        }
    }
    void floatsAllOrders()
    {
        QFETCH(double, value);
        QFETCH(ByteOrder, order);
        const Tag truth = makeTag(ValueType::Float32, order);
        const RegisterPair r = regsFor(truth, value);
        const Encoding wrong{ValueType::Int32, order == ByteOrder::ABCD ? ByteOrder::DCBA : ByteOrder::ABCD};
        const Tag cur = withEncoding(truth, wrong);

        const EncodingResult res = detectEncoding(cur, {r});
        QCOMPARE(res.verdict, EncodingVerdict::Clear);
        QVERIFY(res.best);
        QCOMPARE_ENC(*res.best, encodingOf(truth));
        QCOMPARE(res.candidates.size(), 12);
        QVERIFY(res.groupCount >= 2);
        // Suspicious exactly when the wrong current encoding scores below 0.5 (e.g. a small
        // plausible int32 such as 49548 does not trigger it).
        QCOMPARE(currentEncodingSuspicious(cur, res), find(res, wrong)->score < 0.5);
        // Sorted by group, groups numbered from 0.
        QCOMPARE(res.candidates.first().group, 0);
        for (qsizetype i = 1; i < res.candidates.size(); ++i)
            QVERIFY(res.candidates[i].group >= res.candidates[i - 1].group);
        QCOMPARE_ENC(res.candidates.first().encoding, *res.best);
        QVERIFY(res.candidates.first().score > 0.6); // 0.95 with <= 4 digits, else 0.7
        for (const EncodingCandidate &c : res.candidates) {
            QVERIFY(!c.matchesDisplay);
            QVERIFY(c.score >= 0.0 && c.score <= 1.0);
            QCOMPARE(c.values.size(), 1);
        }
        QCOMPARE(find(res, encodingOf(truth))->values.first(), static_cast<double>(static_cast<float>(value)));
    }

    // 2. A smooth float series.
    void sineSeries()
    {
        const Tag truth = makeTag(ValueType::Float32, ByteOrder::CDAB);
        QList<RegisterPair> readings;
        for (int i = 0; i < 20; ++i)
            readings.append(regsFor(truth, 50.0 + 30.0 * std::sin(0.3 * i)));
        const Tag cur = makeTag(ValueType::Float32, ByteOrder::ABCD);
        const EncodingResult res = detectEncoding(cur, readings);
        QCOMPARE(res.verdict, EncodingVerdict::Clear);
        QCOMPARE_ENC(*res.best, kF_CDAB);
        QVERIFY(currentEncodingSuspicious(cur, res));
        QCOMPARE(find(res, kF_CDAB)->values.size(), 20);
        QVERIFY(find(res, kF_CDAB)->score > 0.5);
        QVERIFY(find(res, encodingOf(cur))->score < 0.3);
        // The same series read with the right order is not suspicious.
        QVERIFY(!currentEncodingSuspicious(truth, detectEncoding(truth, readings)));
    }

    // A fast signal around 0 (the test server's float sine: amplitude 123.456, period 5 s), seen
    // through only 3 readings at the poll period, from every phase of a period and in every byte
    // order: the right encoding is Clear, and a wrong float order is flagged on connect.
    void fastSineThreeReadings_data()
    {
        QTest::addColumn<int>("order");
        QTest::addColumn<double>("pollS");
        const char *names[] = {"ABCD", "CDAB", "BADC", "DCBA"};
        for (int o = 0; o < 4; ++o) {
            QTest::newRow(qPrintable(QStringLiteral("%1 100ms").arg(names[o]))) << o << 0.1;
            QTest::newRow(qPrintable(QStringLiteral("%1 200ms").arg(names[o]))) << o << 0.2;
        }
    }

    void fastSineThreeReadings()
    {
        QFETCH(int, order);
        QFETCH(double, pollS);
        const Encoding truthEnc{ValueType::Float32, ByteOrder(order)};
        const Tag truth = makeTag(ValueType::Float32, ByteOrder(order));
        const Tag wrong = makeTag(ValueType::Float32, ByteOrder((order + 1) % 4));
        constexpr double kPi = 3.14159265358979323846;
        for (int start = 0; start < 50; ++start) {
            QList<RegisterPair> readings;
            for (int i = 0; i < 3; ++i) {
                const double t = 0.013 + (start + i) * pollS; // never exactly on a zero crossing
                readings.append(regsFor(truth, 123.456 * std::sin(2 * kPi * t / 5.0)));
            }
            const EncodingResult res = detectEncoding(wrong, readings);
            const QString where = QStringLiteral("start %1").arg(start);
            if (res.verdict == EncodingVerdict::Undecidable)
                continue; // all three readings exactly 0 cannot happen, but be safe
            QVERIFY2(res.verdict == EncodingVerdict::Clear, qPrintable(where));
            QVERIFY2(*res.best == truthEnc, qPrintable(where + QStringLiteral(": ") + toString(*res.best)));
            QVERIFY2(currentEncodingSuspicious(wrong, res), qPrintable(where));
        }
    }

    // 3. int32 100 in ABCD.
    void int32Ambiguous()
    {
        const Tag truth = makeTag(ValueType::Int32, ByteOrder::ABCD);
        const RegisterPair r = regsFor(truth, 100);
        QCOMPARE(r, (RegisterPair{0, 100}));
        const Tag cur = makeTag(ValueType::Float32, ByteOrder::DCBA);

        // BADC reads 25600 and CDAB 6553600: BADC is as plausible as ABCD, so no Clear verdict.
        const EncodingResult res = detectEncoding(cur, {r});
        QCOMPARE(res.verdict, EncodingVerdict::Ambiguous);
        QVERIFY(res.best);
        QCOMPARE(find(res, {ValueType::Int32, ByteOrder::BADC})->values.first(), 25600.0);
        QCOMPARE(find(res, {ValueType::Int32, ByteOrder::CDAB})->values.first(), 6553600.0);

        // Device order only breaks the tie; the verdict stays Ambiguous but best is ABCD.
        EncodingOptions opt;
        opt.deviceOrder = ByteOrder::ABCD;
        const EncodingResult res2 = detectEncoding(cur, {r}, opt);
        QCOMPARE(res2.verdict, EncodingVerdict::Ambiguous);
        QCOMPARE_ENC(*res2.best, (Encoding{ValueType::Int32, ByteOrder::ABCD}));
        QVERIFY(!currentEncodingSuspicious(cur, res2));

        // With BADC as the device order, BADC wins the tie instead.
        opt.deviceOrder = ByteOrder::BADC;
        QCOMPARE_ENC(*detectEncoding(cur, {r}, opt).best, (Encoding{ValueType::Int32, ByteOrder::BADC}));
    }

    // 4. Everything zero.
    void allZero()
    {
        const Tag cur = makeTag(ValueType::Int32, ByteOrder::BADC);
        const EncodingResult res = detectEncoding(cur, {RegisterPair{0, 0}, RegisterPair{0, 0}});
        QCOMPARE(res.verdict, EncodingVerdict::Undecidable);
        QCOMPARE(res.groupCount, 1);
        QCOMPARE(res.candidates.size(), 12);
        QCOMPARE_ENC(*res.best, encodingOf(cur));
        for (const EncodingCandidate &c : res.candidates)
            QCOMPARE(c.group, 0);
        QCOMPARE_ENC(res.candidates.first().encoding, encodingOf(cur));
        QVERIFY(!currentEncodingSuspicious(cur, res));
    }

    // 5. Display value.
    void displayValue()
    {
        const Tag truth = makeTag(ValueType::Float32, ByteOrder::CDAB);
        const RegisterPair r = regsFor(truth, 52.3);
        const Tag cur = makeTag(ValueType::Int32, ByteOrder::ABCD);
        EncodingOptions opt;
        opt.displayValue = QStringLiteral("52.3");
        const EncodingResult res = detectEncoding(cur, {r}, opt);
        QCOMPARE(res.verdict, EncodingVerdict::Clear);
        QCOMPARE_ENC(*res.best, kF_CDAB);
        QCOMPARE(res.candidates.first().group, 0);
        for (const EncodingCandidate &c : res.candidates) {
            QVERIFY(c.matchesDisplay.has_value());
            QCOMPARE(*c.matchesDisplay, c.encoding == kF_CDAB);
        }

        opt.displayValue = QStringLiteral("52,4"); // within 0.5 %
        QCOMPARE(detectEncoding(cur, {r}, opt).verdict, EncodingVerdict::Clear);

        opt.displayValue = QStringLiteral("999");
        const EncodingResult none = detectEncoding(cur, {r}, opt);
        QCOMPARE(none.verdict, EncodingVerdict::NoMatch);
        QVERIFY(!none.best);
        QCOMPARE(none.candidates.size(), 12);
        for (const EncodingCandidate &c : none.candidates)
            QCOMPARE(*c.matchesDisplay, false);

        // An unparsable display value is ignored.
        opt.displayValue = QStringLiteral("abc");
        const EncodingResult ign = detectEncoding(cur, {r}, opt);
        QCOMPARE(ign.verdict, EncodingVerdict::Clear);
        QVERIFY(!ign.candidates.first().matchesDisplay);
    }

    // The display matches the latest reading only.
    void displayUsesLatestReading()
    {
        const Tag truth = makeTag(ValueType::Float32, ByteOrder::ABCD);
        const QList<RegisterPair> rs{regsFor(truth, 10.5), regsFor(truth, 20.5)};
        EncodingOptions opt;
        opt.displayValue = QStringLiteral("20.5");
        const EncodingResult res = detectEncoding(makeTag(ValueType::Int32, ByteOrder::ABCD), rs, opt);
        QCOMPARE(res.verdict, EncodingVerdict::Clear);
        opt.displayValue = QStringLiteral("10.5");
        QCOMPARE(detectEncoding(makeTag(ValueType::Int32, ByteOrder::ABCD), rs, opt).verdict,
                 EncodingVerdict::NoMatch);
    }

    // 6. Single group with a display value: Undecidable (matches) / NoMatch (does not).
    void singleGroupWithDisplay()
    {
        const Tag cur = makeTag(ValueType::Float32, ByteOrder::ABCD);
        EncodingOptions opt;
        opt.displayValue = QStringLiteral("0");
        const EncodingResult res = detectEncoding(cur, {RegisterPair{0, 0}}, opt);
        QCOMPARE(res.groupCount, 1);
        QCOMPARE(res.verdict, EncodingVerdict::Undecidable);
        QCOMPARE_ENC(*res.best, encodingOf(cur));
        QCOMPARE(*res.candidates.first().matchesDisplay, true);

        opt.displayValue = QStringLiteral("52.3");
        const EncodingResult res2 = detectEncoding(cur, {RegisterPair{0, 0}}, opt);
        QCOMPARE(res2.verdict, EncodingVerdict::NoMatch);
        QVERIFY(!res2.best);
    }

    // Several groups match the display value.
    void displayMatchesSeveralGroups()
    {
        const Tag truth = makeTag(ValueType::Int32, ByteOrder::ABCD);
        const RegisterPair r = regsFor(truth, 100);
        const Tag cur = makeTag(ValueType::Float32, ByteOrder::DCBA);
        EncodingOptions opt;
        opt.displayValue = QStringLiteral("0e5"); // 0 +- 50000: matches 100 and 25600
        opt.deviceOrder = ByteOrder::ABCD;
        const EncodingResult res = detectEncoding(cur, {r}, opt);
        QCOMPARE(res.verdict, EncodingVerdict::Ambiguous);
        QVERIFY(res.groupCount >= 3);
        QVERIFY(res.best);
        QCOMPARE_ENC(*res.best, (Encoding{ValueType::Int32, ByteOrder::ABCD}));
    }

    // 7. NaN and infinity never score.
    void nanAndInfinity()
    {
        for (quint32 pattern : {0x7FC00000u, 0x7F800000u, 0xFF800000u, 0x7F800001u}) {
            for (ByteOrder o : {ByteOrder::ABCD, ByteOrder::CDAB, ByteOrder::BADC, ByteOrder::DCBA}) {
                const Tag tag = makeTag(ValueType::Float32, o);
                const RegisterPair r = deviceRegisters(tag, pattern);
                const EncodingResult res = detectEncoding(tag, {r});
                const EncodingCandidate *c = find(res, encodingOf(tag));
                QVERIFY(c);
                QVERIFY(!std::isfinite(c->values.first()));
                QCOMPARE(c->score, 0.0);
                QVERIFY(res.best);
                QVERIFY(*res.best != encodingOf(tag));
            }
        }
        // NaN values with identical bit patterns share a group.
        const Tag abcd = makeTag(ValueType::Float32, ByteOrder::ABCD);
        const EncodingResult res = detectEncoding(abcd, {RegisterPair{0x7FC0, 0}});
        const EncodingCandidate *f = find(res, {ValueType::Float32, ByteOrder::ABCD});
        QCOMPARE(f->score, 0.0);
    }

    // 8. Gain and offset.
    void gainAndOffset()
    {
        Tag truth = makeTag(ValueType::Float32, ByteOrder::CDAB);
        truth.gain = 0.1;
        truth.offset = 5.0;
        const RegisterPair r = regsFor(truth, 10.23); // raw 52.3
        Tag cur = truth;
        cur.type = ValueType::Int32;
        cur.byteOrder = ByteOrder::ABCD;

        EncodingOptions opt;
        opt.displayValue = QStringLiteral("10.2");
        const EncodingResult res = detectEncoding(cur, {r}, opt);
        QCOMPARE(res.verdict, EncodingVerdict::Clear);
        QCOMPARE_ENC(*res.best, kF_CDAB);
        QVERIFY(qAbs(find(res, kF_CDAB)->values.first() - 10.23) < 1e-4);
        QCOMPARE(*find(res, kF_CDAB)->matchesDisplay, true);

        opt.displayValue = QStringLiteral("52.3"); // the unscaled number does not match
        QCOMPARE(detectEncoding(cur, {r}, opt).verdict, EncodingVerdict::NoMatch);

        // Plausibility is judged on the unscaled value: still Clear without a display value.
        const EncodingResult plain = detectEncoding(cur, {r});
        QCOMPARE(plain.verdict, EncodingVerdict::Clear);
        QCOMPARE_ENC(*plain.best, kF_CDAB);
    }

    // 9. 16-bit tag and no readings.
    void sixteenBitAndEmpty()
    {
        const Tag t16 = makeTag(ValueType::Int16, ByteOrder::ABCD);
        const EncodingResult res = detectEncoding(t16, {RegisterPair{1, 2}});
        QCOMPARE(res.verdict, EncodingVerdict::Undecidable);
        QVERIFY(res.candidates.isEmpty());
        QVERIFY(!res.best);
        QVERIFY(!currentEncodingSuspicious(t16, res));

        const Tag t32 = makeTag(ValueType::UInt32, ByteOrder::BADC);
        const EncodingResult none = detectEncoding(t32, {});
        QCOMPARE(none.verdict, EncodingVerdict::Undecidable);
        QVERIFY(none.candidates.isEmpty());
        QCOMPARE(none.groupCount, 0);
        QCOMPARE_ENC(*none.best, encodingOf(t32));
    }

    // 11. Suspicious current encoding.
    void suspicious()
    {
        const Tag truth = makeTag(ValueType::Float32, ByteOrder::CDAB);
        const RegisterPair r = regsFor(truth, 52.3);
        const Tag wrong = makeTag(ValueType::Float32, ByteOrder::ABCD);
        const EncodingResult res = detectEncoding(wrong, {r});
        QCOMPARE(res.verdict, EncodingVerdict::Clear);
        QVERIFY(find(res, encodingOf(wrong))->score < 0.5);
        QVERIFY(currentEncodingSuspicious(wrong, res));
        QVERIFY(!currentEncodingSuspicious(truth, detectEncoding(truth, {r})));

        // Not Clear -> not suspicious.
        EncodingResult amb = res;
        amb.verdict = EncodingVerdict::Ambiguous;
        QVERIFY(!currentEncodingSuspicious(wrong, amb));
    }

    // 12. Preference within a group and between tied groups.
    void preference()
    {
        const Tag truth = makeTag(ValueType::Int32, ByteOrder::ABCD);
        const RegisterPair r = regsFor(truth, 100);

        // Current encoding uint32 ABCD is a member of the winning group -> best.
        const Tag curU = makeTag(ValueType::UInt32, ByteOrder::ABCD);
        const EncodingResult a = detectEncoding(curU, {r});
        QCOMPARE_ENC(*a.best, encodingOf(curU));
        QCOMPARE_ENC(a.candidates.first().encoding, encodingOf(curU));
        QCOMPARE_ENC(a.candidates[1].encoding, (Encoding{ValueType::Int32, ByteOrder::ABCD}));
        QCOMPARE(a.candidates[0].group, a.candidates[1].group);

        // Current encoding not in the group: Int32 ABCD (int32 > uint32).
        const Tag curF = makeTag(ValueType::Float32, ByteOrder::DCBA);
        const EncodingResult b = detectEncoding(curF, {r});
        QCOMPARE_ENC(*b.best, (Encoding{ValueType::Int32, ByteOrder::ABCD}));
        QCOMPARE(find(b, {ValueType::Int32, ByteOrder::ABCD})->group,
                 find(b, {ValueType::UInt32, ByteOrder::ABCD})->group);
        QCOMPARE_ENC(b.candidates[0].encoding, (Encoding{ValueType::Int32, ByteOrder::ABCD}));
        QCOMPARE_ENC(b.candidates[1].encoding, (Encoding{ValueType::UInt32, ByteOrder::ABCD}));

        // Negative ints do not share a group with uint32.
        const RegisterPair neg = regsFor(truth, -5);
        const EncodingResult c = detectEncoding(curF, {neg});
        QVERIFY(find(c, {ValueType::Int32, ByteOrder::ABCD})->group
                != find(c, {ValueType::UInt32, ByteOrder::ABCD})->group);
    }
};

QTEST_MAIN(TstEncodingDetect)
#include "tst_encodingdetect.moc"
