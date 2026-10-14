#include "Tuning.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <optional>

using namespace mb;

namespace {

bool near(std::optional<double> a, std::optional<double> b)
{
    if (!a || !b)
        return !a && !b;
    return std::abs(*a - *b) < 1e-9;
}

void expectRow(const TuningRow &row, ControllerKind kind, bool available,
               std::optional<double> kp, std::optional<double> ti, std::optional<double> td)
{
    QVERIFY(row.kind == kind);
    QVERIFY(row.available == available);
    QVERIFY(near(row.kp, kp));
    QVERIFY(near(row.ti, ti));
    QVERIFY(near(row.td, td));
}

} // namespace

class TstTuning : public QObject
{
    Q_OBJECT
private slots:
    void names()
    {
        QCOMPARE(toString(TuningRule::CohenCoon), QStringLiteral("Cohen-Coon"));
        QCOMPARE(effectiveLambda(2.0, 0.0), 2.0);
    }

    // K = 2, L = 1, T = 4, r = L / T = 0.25, so T / (K L) = 2.
    void ziegler_nichols()
    {
        const auto rows = tune(2.0, 1.0, 4.0, TuningRule::ZieglerNichols);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, true, 4.0 / (2.0 * 1.0), std::nullopt, std::nullopt);
        expectRow(rows[1], ControllerKind::PI, true, 0.9 * 4.0 / (2.0 * 1.0), 1.0 / 0.3,
                  std::nullopt);
        expectRow(rows[2], ControllerKind::PID, true, 1.2 * 4.0 / (2.0 * 1.0), 2.0 * 1.0,
                  0.5 * 1.0);
    }

    void cohen_coon()
    {
        const auto rows = tune(2.0, 1.0, 4.0, TuningRule::CohenCoon);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, true, (4.0 / (2.0 * 1.0)) * (1.0 + 0.25 / 3.0),
                  std::nullopt, std::nullopt);
        expectRow(rows[1], ControllerKind::PI, true,
                  (4.0 / (2.0 * 1.0)) * (0.9 + 0.25 / 12.0),
                  1.0 * (30.0 + 3.0 * 0.25) / (9.0 + 20.0 * 0.25), std::nullopt);
        expectRow(rows[2], ControllerKind::PID, true,
                  (4.0 / (2.0 * 1.0)) * (4.0 / 3.0 + 0.25 / 4.0),
                  1.0 * (32.0 + 6.0 * 0.25) / (13.0 + 8.0 * 0.25),
                  4.0 * 1.0 / (11.0 + 2.0 * 0.25));
    }

    void imc_lambda_default_lambda_is_L()
    {
        // lam = L = 1 when lambda = 0.
        const auto rows = tune(2.0, 1.0, 4.0, TuningRule::ImcLambda, 0.0);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, false, std::nullopt, std::nullopt, std::nullopt);
        expectRow(rows[1], ControllerKind::PI, true, 4.0 / (2.0 * (1.0 + 1.0)), 4.0,
                  std::nullopt);
        expectRow(rows[2], ControllerKind::PID, true, (4.0 + 0.5) / (2.0 * (1.0 + 0.5)),
                  4.0 + 0.5, 4.0 * 1.0 / (2.0 * 4.0 + 1.0));
    }

    void imc_lambda_explicit_lambda()
    {
        // lam = 3.
        const auto rows = tune(2.0, 1.0, 4.0, TuningRule::ImcLambda, 3.0);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, false, std::nullopt, std::nullopt, std::nullopt);
        expectRow(rows[1], ControllerKind::PI, true, 4.0 / (2.0 * (3.0 + 1.0)), 4.0,
                  std::nullopt);
        expectRow(rows[2], ControllerKind::PID, true, (4.0 + 0.5) / (2.0 * (3.0 + 0.5)),
                  4.0 + 0.5, 4.0 * 1.0 / (2.0 * 4.0 + 1.0));
    }

    void simc_default_lambda_small_T()
    {
        // lam = L = 1; Ti = min(T, 4 (lam + L)) = min(4, 8) = 4.
        const auto rows = tune(2.0, 1.0, 4.0, TuningRule::Simc, 0.0);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, true, 4.0 / (2.0 * (1.0 + 1.0)), std::nullopt,
                  std::nullopt);
        expectRow(rows[1], ControllerKind::PI, true, 4.0 / (2.0 * (1.0 + 1.0)), 4.0,
                  std::nullopt);
        expectRow(rows[2], ControllerKind::PID, false, std::nullopt, std::nullopt, std::nullopt);
    }

    void simc_large_T_takes_4_lam_plus_L()
    {
        // lam = L = 1; Ti = min(100, 8) = 8.
        const auto rows = tune(2.0, 1.0, 100.0, TuningRule::Simc, 0.0);
        QVERIFY(rows.size() == 3);
        expectRow(rows[1], ControllerKind::PI, true, 100.0 / (2.0 * (1.0 + 1.0)), 8.0,
                  std::nullopt);
    }

    void simc_explicit_lambda()
    {
        // lam = 3; P Kp = T / (K (lam + L)) = 4 / (2 * 4); PI Ti = min(4, 16) = 4.
        const auto rows = tune(2.0, 1.0, 4.0, TuningRule::Simc, 3.0);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, true, 4.0 / (2.0 * (3.0 + 1.0)), std::nullopt,
                  std::nullopt);
        expectRow(rows[1], ControllerKind::PI, true, 4.0 / (2.0 * (3.0 + 1.0)), 4.0,
                  std::nullopt);
    }

    void negative_gain_gives_negative_kp()
    {
        const auto rows = tune(-2.0, 1.0, 4.0, TuningRule::ZieglerNichols);
        QVERIFY(rows.size() == 3);
        expectRow(rows[0], ControllerKind::P, true, -2.0, std::nullopt, std::nullopt);
        expectRow(rows[2], ControllerKind::PID, true, -2.4, 2.0, 0.5);
    }

    void invalid_input_gives_empty()
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (auto rule : {TuningRule::ZieglerNichols, TuningRule::CohenCoon,
                          TuningRule::ImcLambda, TuningRule::Simc}) {
            QVERIFY(tune(0.0, 1.0, 4.0, rule).isEmpty());
            QVERIFY(tune(2.0, 0.0, 4.0, rule).isEmpty());
            QVERIFY(tune(2.0, 1.0, -1.0, rule).isEmpty());
            QVERIFY(tune(2.0, 1.0, 0.0, rule).isEmpty());
            QVERIFY(tune(nan, 1.0, 4.0, rule).isEmpty());
            QVERIFY(tune(2.0, nan, 4.0, rule).isEmpty());
            QVERIFY(tune(2.0, 1.0, nan, rule).isEmpty());
            QVERIFY(tune(inf, 1.0, 4.0, rule).isEmpty());
            QVERIFY(tune(2.0, inf, 4.0, rule).isEmpty());
            QVERIFY(tune(2.0, 1.0, inf, rule).isEmpty());
        }
    }

    void row_kinds_and_order()
    {
        for (auto rule : {TuningRule::ZieglerNichols, TuningRule::CohenCoon,
                          TuningRule::ImcLambda, TuningRule::Simc}) {
            const auto rows = tune(2.0, 1.0, 4.0, rule);
            QVERIFY(rows.size() == 3);
            QVERIFY(rows[0].kind == ControllerKind::P);
            QVERIFY(rows[1].kind == ControllerKind::PI);
            QVERIFY(rows[2].kind == ControllerKind::PID);
            // P rows never carry Ti or Td; PI rows never carry Td.
            QVERIFY(!rows[0].ti && !rows[0].td);
            QVERIFY(!rows[1].td);
        }
    }

    void unavailable_rows_have_no_values()
    {
        const auto imc = tune(2.0, 1.0, 4.0, TuningRule::ImcLambda);
        QVERIFY(!imc[0].available && !imc[0].kp && !imc[0].ti && !imc[0].td);
        const auto simc = tune(2.0, 1.0, 4.0, TuningRule::Simc);
        QVERIFY(!simc[2].available && !simc[2].kp && !simc[2].ti && !simc[2].td);
    }
};

QTEST_MAIN(TstTuning)
#include "tst_tuning.moc"
