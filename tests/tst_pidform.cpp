#include "PidForm.h"

#include <QtTest>

#include <cmath>
#include <random>

using namespace mb;

namespace {

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

bool rel(double a, double b, double tol = 1e-9)
{
    return std::abs(a - b) <= tol * std::max(std::abs(a), std::abs(b));
}

ControllerSettings cs(PidForm f, GainUnit g = GainUnit::Gain,
                      IntegralUnit i = IntegralUnit::Time)
{
    ControllerSettings s;
    s.form = f;
    s.gain = g;
    s.integral = i;
    return s;
}

IdealPid ideal(double kp, std::optional<double> ti, std::optional<double> td)
{
    IdealPid p;
    p.kp = kp;
    p.ti = ti;
    p.td = td;
    return p;
}

FormValues fv(std::optional<double> g, std::optional<double> i, std::optional<double> d)
{
    FormValues v;
    v.gain = g;
    v.integral = i;
    v.derivative = d;
    return v;
}

} // namespace

class TstPidForm : public QObject
{
    Q_OBJECT
private slots:
    void ideal_isIdentity()
    {
        const FormValues v = toForm(ideal(2, 10, 2), cs(PidForm::Ideal), {}, false);
        QVERIFY(v.available);
        QVERIFY(v.reason.isEmpty());
        QVERIFY(v.gain && v.integral && v.derivative);
        QCOMPARE(*v.gain, 2.0);
        QCOMPARE(*v.integral, 10.0);
        QCOMPARE(*v.derivative, 2.0);
    }

    void ideal_minutes()
    {
        const FormValues v = toForm(ideal(2, 90, 30), cs(PidForm::Ideal), {}, true);
        QVERIFY(near(*v.integral, 1.5, 1e-12));
        QVERIFY(near(*v.derivative, 0.5, 1e-12));
        QCOMPARE(*v.gain, 2.0);
    }

    void ideal_missingActions()
    {
        FormValues v = toForm(ideal(2, std::nullopt, std::nullopt), cs(PidForm::Ideal), {}, false);
        QVERIFY(v.available);
        QVERIFY(!v.integral && !v.derivative);
        v = toForm(ideal(2, 0.0, 0.0), cs(PidForm::Ideal), {}, false);
        QVERIFY(!v.integral && !v.derivative);
        v = toForm(ideal(2, 10, std::nullopt), cs(PidForm::Ideal), {}, false);
        QVERIFY(v.integral && !v.derivative);
    }

    void series_handComputed()
    {
        const FormValues v = toForm(ideal(2, 10, 2), cs(PidForm::Series), {}, false);
        QVERIFY(v.available);
        const double q = std::sqrt(0.2);
        QVERIFY(near(*v.gain, 2.0 * (1.0 + q) / 2.0, 1e-12));
        QVERIFY(near(*v.gain, 1.4472, 1e-4));
        QVERIFY(near(*v.integral, 7.236, 1e-3));
        QVERIFY(near(*v.derivative, 2.764, 1e-3));
        // Identities of the series form.
        const double kc = *v.gain, ti = *v.integral, td = *v.derivative;
        QVERIFY(near(kc * (1.0 + td / ti), 2.0, 1e-12));
        QVERIFY(near(ti + td, 10.0, 1e-12));
        QVERIFY(near(ti * td / (ti + td), 2.0, 1e-12));
    }

    void series_unavailable()
    {
        const FormValues v = toForm(ideal(2, 7, 2), cs(PidForm::Series), {}, false); // 7 < 8
        QVERIFY(!v.available);
        QCOMPARE(v.reason, QStringLiteral("No series equivalent: Ti is less than 4 Td."));
        QVERIFY(!v.gain && !v.integral && !v.derivative);
        // Exactly 4 Td is the limit and available (r = 0).
        const FormValues e = toForm(ideal(2, 8, 2), cs(PidForm::Series), {}, false);
        QVERIFY(e.available);
        QVERIFY(near(*e.integral, 4.0, 1e-12));
        QVERIFY(near(*e.derivative, 4.0, 1e-12));
        QVERIFY(near(*e.gain, 1.0, 1e-12));
    }

    void series_equalsIdealWithoutIOrD()
    {
        for (const auto &p : {ideal(3, 12, std::nullopt), ideal(3, std::nullopt, std::nullopt),
                              ideal(3, std::nullopt, 5.0), ideal(3, 12, 0.0)}) {
            const FormValues a = toForm(p, cs(PidForm::Series), {}, false);
            const FormValues b = toForm(p, cs(PidForm::Ideal), {}, false);
            QVERIFY(a.available);
            QVERIFY(a.gain == b.gain && a.integral == b.integral && a.derivative == b.derivative);
        }
    }

    void parallel_values()
    {
        FormValues v = toForm(ideal(2, 10, 2), cs(PidForm::Parallel), {}, false);
        QVERIFY(v.available);
        QCOMPARE(*v.gain, 2.0);
        QVERIFY(near(*v.integral, 0.2, 1e-12));   // Kp / Ti per s
        QVERIFY(near(*v.derivative, 4.0, 1e-12)); // Kp Td in s
        v = toForm(ideal(2, 10, 2), cs(PidForm::Parallel), {}, true);
        QVERIFY(near(*v.integral, 12.0, 1e-12));         // per min
        QVERIFY(near(*v.derivative, 4.0 / 60.0, 1e-12)); // min
        // Parallel ignores the repeats/min choice.
        const FormValues r = toForm(
            ideal(2, 10, 2),
            cs(PidForm::Parallel, GainUnit::Gain, IntegralUnit::RepeatsPerMinute), {}, false);
        QVERIFY(near(*r.integral, 0.2, 1e-12));
        // No I and no D.
        const FormValues n = toForm(ideal(2, std::nullopt, std::nullopt), cs(PidForm::Parallel),
                                    {}, false);
        QVERIFY(!n.integral && !n.derivative);
    }

    void proportionalBand()
    {
        PidSpans sp;
        sp.pv = 50.0;
        sp.co = 100.0;
        FormValues v = toForm(ideal(2, 10, 2), cs(PidForm::Ideal, GainUnit::ProportionalBand), sp,
                              false);
        QVERIFY(v.available);
        QVERIFY(near(*v.gain, 100.0, 1e-9)); // Kp% = 2 * 50 / 100 = 1 -> PB 100 %
        QCOMPARE(*v.integral, 10.0);

        // Negative gain gives a negative band.
        v = toForm(ideal(-2, 10, 2), cs(PidForm::Ideal, GainUnit::ProportionalBand), sp, false);
        QVERIFY(near(*v.gain, -100.0, 1e-9));

        // Parallel: Ki, Kd use the percent gain.
        v = toForm(ideal(2, 10, 2), cs(PidForm::Parallel, GainUnit::ProportionalBand), sp, false);
        QVERIFY(near(*v.gain, 100.0, 1e-9));
        QVERIFY(near(*v.integral, 0.1, 1e-12));   // 1 / 10
        QVERIFY(near(*v.derivative, 2.0, 1e-12)); // 1 * 2

        // Series with PB uses Kc'.
        v = toForm(ideal(2, 10, 2), cs(PidForm::Series, GainUnit::ProportionalBand), sp, false);
        QVERIFY(near(*v.gain, 100.0 / (1.4472136 * 0.5), 1e-4));

        // Without spans.
        for (const PidSpans &bad : {PidSpans{}, PidSpans{50.0, std::nullopt},
                                    PidSpans{std::nullopt, 100.0}, PidSpans{50.0, 0.0},
                                    PidSpans{0.0, 100.0}}) {
            v = toForm(ideal(2, 10, 2), cs(PidForm::Ideal, GainUnit::ProportionalBand), bad, false);
            QVERIFY(!v.available);
            QCOMPARE(v.reason,
                     QStringLiteral("The proportional band needs the PV and CO ranges."));
            QVERIFY(!v.gain && !v.integral && !v.derivative);
            QVERIFY(!fromForm(fv(100.0, 10.0, 2.0), cs(PidForm::Ideal, GainUnit::ProportionalBand),
                              bad, false));
        }
        // Spans are ignored for the plain gain.
        v = toForm(ideal(2, 10, 2), cs(PidForm::Ideal), PidSpans{}, false);
        QVERIFY(v.available);
    }

    void repeatsPerMinute()
    {
        FormValues v = toForm(
            ideal(2, 20, 2), cs(PidForm::Ideal, GainUnit::Gain, IntegralUnit::RepeatsPerMinute),
            {}, false);
        QVERIFY(near(*v.integral, 3.0, 1e-12));
        QVERIFY(near(*v.derivative, 2.0, 1e-12));
        // Minutes only affect Td; repeats/min stays 60 / Ti with Ti in s.
        v = toForm(ideal(2, 20, 6),
                   cs(PidForm::Ideal, GainUnit::Gain, IntegralUnit::RepeatsPerMinute), {}, true);
        QVERIFY(near(*v.integral, 3.0, 1e-12));
        QVERIFY(near(*v.derivative, 0.1, 1e-12));
        // Series: 60 / Ti'.
        v = toForm(ideal(2, 10, 2),
                   cs(PidForm::Series, GainUnit::Gain, IntegralUnit::RepeatsPerMinute), {}, false);
        QVERIFY(near(*v.integral, 60.0 / 7.2360680, 1e-6));
    }

    void fromForm_refusals()
    {
        const ControllerSettings id = cs(PidForm::Ideal);
        QVERIFY(fromForm(fv(2, 10, 2), id, {}, false).has_value());
        QVERIFY(!fromForm(fv(std::nullopt, 10, 2), id, {}, false));
        QVERIFY(!fromForm(fv(0.0, 10, 2), id, {}, false));
        QVERIFY(!fromForm(fv(2, -1, 2), id, {}, false));
        QVERIFY(!fromForm(fv(2, 10, -1), id, {}, false));
        const auto rpm = cs(PidForm::Ideal, GainUnit::Gain, IntegralUnit::RepeatsPerMinute);
        QVERIFY(!fromForm(fv(2, 0.0, 2), rpm, {}, false));
        QVERIFY(!fromForm(fv(2, -3.0, 2), rpm, {}, false));
        // PB 0 and PB without spans.
        const PidSpans sp{50.0, 100.0};
        const auto pb = cs(PidForm::Ideal, GainUnit::ProportionalBand);
        QVERIFY(!fromForm(fv(0.0, 10, 2), pb, sp, false));
        QVERIFY(!fromForm(fv(100.0, 10, 2), pb, {}, false));
        QVERIFY(fromForm(fv(100.0, 10, 2), pb, sp, false).has_value());
        // Unavailable values.
        FormValues u = fv(2, 10, 2);
        u.available = false;
        QVERIFY(!fromForm(u, id, {}, false));
        // Parallel with a negative time (Ki of the opposite sign to Kp).
        QVERIFY(!fromForm(fv(2, -0.2, 4), cs(PidForm::Parallel), {}, false));
        QVERIFY(!fromForm(fv(2, 0.2, -4), cs(PidForm::Parallel), {}, false));
        // Zero or missing times mean no action.
        const auto p = fromForm(fv(2, 0.0, std::nullopt), id, {}, false);
        QVERIFY(p && !p->ti && !p->td);
    }

    void fromForm_handComputed()
    {
        const auto p = fromForm(fv(1.4472135955, 7.2360679775, 2.7639320225), cs(PidForm::Series),
                                {}, false);
        QVERIFY(p);
        QVERIFY(rel(p->kp, 2.0, 1e-8));
        QVERIFY(rel(*p->ti, 10.0, 1e-8));
        QVERIFY(rel(*p->td, 2.0, 1e-8));
        // PB 100 %, spans 50 / 100 -> Kp 2.
        const auto q = fromForm(fv(100.0, 10.0, std::nullopt),
                                cs(PidForm::Ideal, GainUnit::ProportionalBand),
                                PidSpans{50.0, 100.0}, false);
        QVERIFY(q && near(q->kp, 2.0, 1e-12) && !q->td);
    }

    void roundTrips_data()
    {
        QTest::addColumn<int>("form");
        QTest::addColumn<int>("gain");
        QTest::addColumn<int>("integral");
        QTest::addColumn<bool>("minutes");
        for (int f = 0; f < 3; ++f)
            for (int g = 0; g < 2; ++g)
                for (int i = 0; i < 2; ++i)
                    for (int m = 0; m < 2; ++m)
                        QTest::addRow("f%d_g%d_i%d_m%d", f, g, i, m) << f << g << i << bool(m);
    }

    void roundTrips()
    {
        QFETCH(int, form);
        QFETCH(int, gain);
        QFETCH(int, integral);
        QFETCH(bool, minutes);
        const ControllerSettings s = cs(PidForm(form), GainUnit(gain), IntegralUnit(integral));
        std::mt19937 gen(12345 + unsigned(form * 8 + gain * 4 + integral * 2 + int(minutes)));
        std::uniform_real_distribution<double> kp(0.05, 20.0), ti(1.0, 600.0), frac(0.01, 0.2),
            span(1.0, 5000.0);
        std::bernoulli_distribution coin(0.5), flip(0.25);
        for (int n = 0; n < 300; ++n) {
            IdealPid p;
            p.kp = kp(gen) * (flip(gen) ? -1.0 : 1.0);
            const double tiVal = ti(gen);
            if (coin(gen))
                p.ti = tiVal;
            if (coin(gen))
                p.td = tiVal * frac(gen); // Td < Ti / 4 so the series form exists
            const PidSpans sp{span(gen), span(gen)};
            const FormValues v = toForm(p, s, sp, minutes);
            QVERIFY(v.available);
            const auto back = fromForm(v, s, sp, minutes);
            QVERIFY(back.has_value());
            QVERIFY2(rel(back->kp, p.kp), qPrintable(QString::number(back->kp, 'g', 17)));
            QCOMPARE(back->ti.has_value(), p.ti.has_value());
            QCOMPARE(back->td.has_value(), p.td.has_value());
            if (p.ti)
                QVERIFY(rel(*back->ti, *p.ti));
            if (p.td)
                QVERIFY(rel(*back->td, *p.td));
        }
    }

    void roundTrip_nearSeriesLimit()
    {
        const IdealPid p = ideal(1.5, 8.0001, 2.0);
        const ControllerSettings s = cs(PidForm::Series);
        const FormValues v = toForm(p, s, {}, false);
        QVERIFY(v.available);
        const auto back = fromForm(v, s, {}, false);
        QVERIFY(back && rel(back->kp, 1.5, 1e-7) && rel(*back->ti, 8.0001, 1e-7)
                && rel(*back->td, 2.0, 1e-7));
    }

    void toStrings()
    {
        QVERIFY(!toString(PidForm::Series).isEmpty());
        QCOMPARE(toString(GainUnit::Gain), QStringLiteral("Kp"));
    }
};

QTEST_GUILESS_MAIN(TstPidForm)
#include "tst_pidform.moc"
