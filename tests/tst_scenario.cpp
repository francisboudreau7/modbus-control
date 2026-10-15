#include "Scenario.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace mbloop;

namespace {

Scenario sampleScenario()
{
    Scenario s;
    s.params.pumpSpeedPct = 80.0;
    s.params.hvOpeningPct = 35.5;
    s.params.deadTimeS = 3.25;
    s.params.ltNoisePct = 0.5;
    s.params.ltDampingS = 2.0;
    s.params.fcvStrokeS = 10.0;
    s.speedFactor = 5.0;
    return s;
}

// A document with the given settings/state members spliced in; `extra` is raw JSON members.
QByteArray doc(const QString &settings, const QString &state = {}, const QString &top = {})
{
    QString text = QStringLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":1,"
                                  "\"settings\":%1").arg(settings);
    if (!state.isEmpty())
        text += QStringLiteral(",\"state\":%1").arg(state);
    if (!top.isEmpty())
        text += QStringLiteral(",%1").arg(top);
    text += QLatin1Char('}');
    return text.toUtf8();
}

} // namespace

class TstScenario : public QObject
{
    Q_OBJECT
private slots:
    void roundTripWithoutState();
    void roundTripWithState();
    void jsonHasExpectedKeys();
    void defaultsWhenKeysMissing();
    void rangeAndTypeErrors_data();
    void rangeAndTypeErrors();
    void unknownKeysRejected_data();
    void unknownKeysRejected();
    void wrongFormatOrVersionRejected_data();
    void wrongFormatOrVersionRejected();
    void speedFactorRestricted();
    void saveAndLoadFiles();
    void loadMissingFileReportsPath();
    void applyScenarioSetsParams();
    void applyScenarioWithState();
    void applyScenarioWithoutStateKeepsSteadyStart();
};

void TstScenario::roundTripWithoutState()
{
    const Scenario original = sampleScenario();
    QString error;
    const std::optional<Scenario> back = scenarioFromJson(scenarioToJson(original), &error);
    QVERIFY2(back.has_value(), qPrintable(error));
    QCOMPARE(*back, original);
    QVERIFY(!back->state.has_value());
}

void TstScenario::roundTripWithState()
{
    Scenario original = sampleScenario();
    original.state = Scenario::State{.levelPct = 37.25, .outPct = 12.5, .spPct = 66.0, .stemPct = 9.75};
    QString error;
    const std::optional<Scenario> back = scenarioFromJson(scenarioToJson(original), &error);
    QVERIFY2(back.has_value(), qPrintable(error));
    QCOMPARE(*back, original);
    QVERIFY(back->state.has_value());
}

void TstScenario::jsonHasExpectedKeys()
{
    Scenario s;
    const QByteArray withoutState = scenarioToJson(s);
    QVERIFY(withoutState.contains("\"format\": \"modbus-loop-scenario\""));
    QVERIFY(withoutState.contains("\"version\": 1"));
    QVERIFY(withoutState.contains("\"deadTimeS\""));
    QVERIFY(withoutState.contains("\"speedFactor\""));
    QVERIFY(!withoutState.contains("\"state\""));
    QVERIFY(withoutState.contains('\n')); // indented

    s.state = Scenario::State{};
    QVERIFY(scenarioToJson(s).contains("\"state\""));
}

void TstScenario::defaultsWhenKeysMissing()
{
    QString error;
    const std::optional<Scenario> s = scenarioFromJson(
        QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":1}"), &error);
    QVERIFY2(s.has_value(), qPrintable(error));
    QCOMPARE(s->params, LoopParams{});
    QCOMPARE(s->speedFactor, 1.0);
    QVERIFY(!s->state.has_value());

    const std::optional<Scenario> partial = scenarioFromJson(
        doc(QStringLiteral("{\"deadTimeS\":7}"), QStringLiteral("{\"outPct\":20}")), &error);
    QVERIFY2(partial.has_value(), qPrintable(error));
    QCOMPARE(partial->params.deadTimeS, 7.0);
    QCOMPARE(partial->params.pumpSpeedPct, LoopParams{}.pumpSpeedPct);
    QVERIFY(partial->state.has_value());
    QCOMPARE(partial->state->outPct, 20.0);
    QCOMPARE(partial->state->levelPct, 50.0);
    QCOMPARE(partial->state->spPct, 50.0);
    QCOMPARE(partial->state->stemPct, 50.0);
}

void TstScenario::rangeAndTypeErrors_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("key");

    QTest::newRow("pump high") << doc(QStringLiteral("{\"pumpSpeedPct\":101}")) << QStringLiteral("settings.pumpSpeedPct");
    QTest::newRow("pump low") << doc(QStringLiteral("{\"pumpSpeedPct\":-1}")) << QStringLiteral("settings.pumpSpeedPct");
    QTest::newRow("hv high") << doc(QStringLiteral("{\"hvOpeningPct\":100.5}")) << QStringLiteral("settings.hvOpeningPct");
    QTest::newRow("deadTime high") << doc(QStringLiteral("{\"deadTimeS\":31}")) << QStringLiteral("settings.deadTimeS");
    QTest::newRow("deadTime low") << doc(QStringLiteral("{\"deadTimeS\":-0.1}")) << QStringLiteral("settings.deadTimeS");
    QTest::newRow("noise high") << doc(QStringLiteral("{\"ltNoisePct\":5.1}")) << QStringLiteral("settings.ltNoisePct");
    QTest::newRow("damping high") << doc(QStringLiteral("{\"ltDampingS\":61}")) << QStringLiteral("settings.ltDampingS");
    QTest::newRow("stroke high") << doc(QStringLiteral("{\"fcvStrokeS\":121}")) << QStringLiteral("settings.fcvStrokeS");
    QTest::newRow("settings wrong type") << doc(QStringLiteral("{\"deadTimeS\":\"2\"}")) << QStringLiteral("settings.deadTimeS");
    QTest::newRow("settings bool") << doc(QStringLiteral("{\"pumpSpeedPct\":true}")) << QStringLiteral("settings.pumpSpeedPct");
    QTest::newRow("level 100") << doc(QStringLiteral("{}"), QStringLiteral("{\"levelPct\":100}")) << QStringLiteral("state.levelPct");
    QTest::newRow("out high") << doc(QStringLiteral("{}"), QStringLiteral("{\"outPct\":101}")) << QStringLiteral("state.outPct");
    QTest::newRow("sp low") << doc(QStringLiteral("{}"), QStringLiteral("{\"spPct\":-5}")) << QStringLiteral("state.spPct");
    QTest::newRow("stem high") << doc(QStringLiteral("{}"), QStringLiteral("{\"stemPct\":150}")) << QStringLiteral("state.stemPct");
    QTest::newRow("state wrong type") << doc(QStringLiteral("{}"), QStringLiteral("{\"outPct\":\"x\"}")) << QStringLiteral("state.outPct");
    QTest::newRow("speed 3") << doc(QStringLiteral("{\"speedFactor\":3}")) << QStringLiteral("settings.speedFactor");
    QTest::newRow("speed string") << doc(QStringLiteral("{\"speedFactor\":\"2\"}")) << QStringLiteral("settings.speedFactor");
    QTest::newRow("settings not object") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":1,\"settings\":[]}") << QStringLiteral("settings");
    QTest::newRow("state not object") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":1,\"state\":5}") << QStringLiteral("state");
}

void TstScenario::rangeAndTypeErrors()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, key);
    QString error;
    QVERIFY(!scenarioFromJson(json, &error).has_value());
    QVERIFY2(error.contains(key), qPrintable(error));
}

void TstScenario::unknownKeysRejected_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("key");

    QTest::newRow("settings unknown")
        << doc(QStringLiteral("{\"pumpSpeed\":50}")) << QStringLiteral("settings.pumpSpeed");
    QTest::newRow("state unknown")
        << doc(QStringLiteral("{}"), QStringLiteral("{\"levelM\":0.5}")) << QStringLiteral("state.levelM");
    QTest::newRow("top unknown")
        << doc(QStringLiteral("{}"), {}, QStringLiteral("\"comment\":\"hi\"")) << QStringLiteral("comment");
}

void TstScenario::unknownKeysRejected()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, key);
    QString error;
    QVERIFY(!scenarioFromJson(json, &error).has_value());
    QVERIFY2(error.contains(key), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("unknown")), qPrintable(error));
}

void TstScenario::wrongFormatOrVersionRejected_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("missing format") << QByteArrayLiteral("{\"version\":1}");
    QTest::newRow("other format") << QByteArrayLiteral("{\"format\":\"modbus-control-project\",\"version\":1}");
    QTest::newRow("missing version") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\"}");
    QTest::newRow("version 2") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":2}");
    QTest::newRow("version 0") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":0}");
    QTest::newRow("version fraction") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":1.5}");
    QTest::newRow("version string") << QByteArrayLiteral("{\"format\":\"modbus-loop-scenario\",\"version\":\"1\"}");
    QTest::newRow("array") << QByteArrayLiteral("[1,2]");
    QTest::newRow("bad json") << QByteArrayLiteral("{\"format\":");
}

void TstScenario::wrongFormatOrVersionRejected()
{
    QFETCH(QByteArray, json);
    QString error;
    QVERIFY(!scenarioFromJson(json, &error).has_value());
    QVERIFY(!error.isEmpty());
}

void TstScenario::speedFactorRestricted()
{
    for (double ok : {1.0, 2.0, 5.0}) {
        QString error;
        const QByteArray json = doc(QStringLiteral("{\"speedFactor\":%1}").arg(ok));
        const std::optional<Scenario> s = scenarioFromJson(json, &error);
        QVERIFY2(s.has_value(), qPrintable(error));
        QCOMPARE(s->speedFactor, ok);
    }
    QString error;
    QVERIFY(!scenarioFromJson(doc(QStringLiteral("{\"speedFactor\":3}")), &error).has_value());
    QCOMPARE(error, QStringLiteral("settings.speedFactor: must be 1, 2 or 5"));
    QVERIFY(!scenarioFromJson(doc(QStringLiteral("{\"speedFactor\":10}")), &error).has_value()); // removed in 1.3.0
}

void TstScenario::saveAndLoadFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("loop.mbloop"));

    Scenario original = sampleScenario();
    original.state = Scenario::State{.levelPct = 61.0, .outPct = 40.0, .spPct = 55.5, .stemPct = 38.0};
    QString error;
    QVERIFY2(saveScenario(path, original, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));

    const std::optional<Scenario> loaded = loadScenario(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, original);

    // Overwriting with a scenario without state works too.
    const Scenario plain = sampleScenario();
    QVERIFY2(saveScenario(path, plain, &error), qPrintable(error));
    const std::optional<Scenario> again = loadScenario(path, &error);
    QVERIFY2(again.has_value(), qPrintable(error));
    QCOMPARE(*again, plain);
}

void TstScenario::loadMissingFileReportsPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("does-not-exist.mbloop"));
    QString error;
    QVERIFY(!loadScenario(path, &error).has_value());
    QVERIFY2(error.contains(path), qPrintable(error));
}

void TstScenario::applyScenarioSetsParams()
{
    LoopModel model;
    const Scenario s = sampleScenario();
    applyScenario(model, s);
    QCOMPARE(model.params(), s.params);
}

void TstScenario::applyScenarioWithState()
{
    LoopModel model;
    Scenario s = sampleScenario();
    s.state = Scenario::State{.levelPct = 42.0, .outPct = 30.0, .spPct = 44.0, .stemPct = 28.0};
    applyScenario(model, s);
    QCOMPARE(model.params(), s.params);
    QCOMPARE(model.state().outPct, 30.0);
    QCOMPARE(model.state().spPct, 44.0);
}

void TstScenario::applyScenarioWithoutStateKeepsSteadyStart()
{
    LoopModel model;
    Scenario s = sampleScenario();
    applyScenario(model, s);
    // Without state the model is reset with OUT = SP = 50.
    QCOMPARE(model.state().outPct, 50.0);
    QCOMPARE(model.state().spPct, 50.0);
}

QTEST_MAIN(TstScenario)
#include "tst_scenario.moc"
