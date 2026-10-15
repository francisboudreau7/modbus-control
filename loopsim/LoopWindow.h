#pragma once

#include <QHash>
#include <QMainWindow>

#include <functional>

class QCheckBox;
class QComboBox;
class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class TstLoopWindow;

namespace mbloop {

class Faceplate;
class LoopController;
class SchematicView;

/// The ISA schematic of level loop 101 (QGraphicsScene, scales with the view; ISA-5.1-style
/// symbols drawn with QPainterPath, no images). Items (object names for tests are in brackets,
/// set with QGraphicsItem data(0) = name):
/// - TK-102 base tank (wide, bottom left, constant water) ["TK-102"]; P-101 pump: circle with an
///   inner triangle pointing downstream ["P-101"], label "P-101" and its speed "100 %";
/// - FCV-101: globe valve (two triangles tip to tip) with a diaphragm actuator (dome on a stem)
///   and "FC" under it; label "FCV-101" ["FCV-101"];
/// - TK-101: tall cylinder, water drawn from the bottom to the true level (animated), level
///   graduations 0/25/50/75/100 %, the overflow pipe leaving at 99 % ["TK-101"];
/// - HV-102 at the bottom outlet of TK-101: hand valve (two triangles, T handle) ["HV-102"];
/// - overflow line TK-101 -> TK-102 drawn in the "active" colour while LoopState::overflowing
///   ["overflow"];
/// - process lines solid 2 px with flow arrows; instrument signal lines dashed (electric signal,
///   ISA): LT-101 -> LIC-101 -> FCV-101;
/// - LT-101: field-mounted instrument bubble (circle, no line) at the bottom of TK-101 ["LT-101"];
///   LIC-101: panel-mounted bubble (circle with a horizontal line) ["LIC-101"];
/// - value labels (QGraphicsSimpleTextItem, names for tests): "lt-value" "52.3 %  12.37 mA";
///   "fcv-value" "OUT 50.0 %  12.00 mA" and "Position 48.7 %"; "hv-value" "50 % open";
///   "pump-value" "100 %"; "lic-value" "PV 52.3 %  SP 50.0 %  OUT 50.0 %".
/// - process data (shown only with setProcessDataVisible(true)) ["process-data"]: "Level 0.523 m",
///   "Qin 30.0 L/min", "Qhv 29.6 L/min", "Overflow 0.0 L/min", "Pump 1.00 bar", "Dead time 2.0 s".
/// Numbers: % with 1 decimal, mA with 2, L/min with 1, m with 3, bar with 2.
class SchematicView : public QWidget
{
    Q_OBJECT
public:
    explicit SchematicView(QWidget *parent = nullptr);
    /// Updates every value and the water level from the controller's state.
    void refresh(const LoopController &controller);
    void setProcessDataVisible(bool visible);
    bool processDataVisible() const;
    /// The text of the label named `name` (tests); empty when unknown or hidden.
    QString labelText(const QString &name) const;
    QGraphicsView *view() const;

private:
    friend class ::TstLoopWindow;
    // Implementation-defined; T214 may change the private section freely.
    void build();
    QGraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    bool m_processData = false;
    QHash<QString, QGraphicsSimpleTextItem *> m_labels;
    QGraphicsRectItem *m_water101 = nullptr;
    QGraphicsPathItem *m_overflow = nullptr;
    QGraphicsItem *m_processGroup = nullptr;
};

/// LIC-101 faceplate, like a panel controller in manual: title "LIC-101", "MAN" mode label, a
/// vertical PV bar (0..100 %) with the SP mark, values "PV 52.3 %", "SP 50.0 %", "OUT 50.0 %"
/// and the mA of PV and OUT; buttons "▲" / "▼" ("outUpButton", "outDownButton") change OUT by
/// 1 % (5 % with Shift), "outEdit" (QLineEdit, Enter applies a typed value 0..100), "spEdit"
/// likewise for SP. Out-of-range typed values are clamped.
class Faceplate : public QWidget
{
    Q_OBJECT
public:
    explicit Faceplate(LoopController *controller, QWidget *parent = nullptr);
    /// Shows the controller's current values (called on stateChanged()).
    void refresh();

private:
    friend class ::TstLoopWindow;
    // Implementation-defined; T214 may change the private section freely.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeOut(double delta);
    void applyEdit(bool isOut);
    LoopController *m_controller = nullptr;
    QWidget *m_bar = nullptr;
    QLabel *m_pvLabel = nullptr;
    QLabel *m_pvMaLabel = nullptr;
    QLabel *m_spLabel = nullptr;
    QLabel *m_outLabel = nullptr;
    QLabel *m_outMaLabel = nullptr;
    QLineEdit *m_outEdit = nullptr;
    QLineEdit *m_spEdit = nullptr;
    Qt::KeyboardModifiers m_lastModifiers;
};

/// Main window "Modbus Loop Simulator — Level loop 101": centre SchematicView, right Faceplate,
/// bottom bar: "HV-102" slider ("hvSlider", 0..100 %), "P-101 speed" slider ("pumpSlider"),
/// "Speed" combo ("speedCombo": ×1 ×2 ×5), "Pause"/"Run" toggle ("pauseButton"), simulation
/// time label "t = 123.4 s", "Show process data" check box ("processDataCheck"). Status bar:
/// "Modbus server on 127.0.0.1:1601, 1 client" (or the start error). File menu: "Open
/// scenario...", "Save scenario...", "Reset", "Quit"; Help: "About". The window refreshes at the
/// controller's stateChanged(). File dialogs go through seams (tests).
class LoopWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit LoopWindow(LoopController *controller, QWidget *parent = nullptr);

    SchematicView *schematic() const;
    Faceplate *faceplate() const;
    /// Status text of the server (set by main after LoopController::startServer). While the
    /// controller's server is running ", N client(s)" is appended; when it is not running (failed
    /// start) the text, the error, is shown alone.
    void setServerStatus(const QString &text);

private:
    friend class ::TstLoopWindow;
    // Implementation-defined; T214 may change the private section freely.
    void refresh();
    void syncControls();
    void openScenario();
    void saveScenario();
    LoopController *m_controller = nullptr;
    QSlider *m_hvSlider = nullptr;
    QSlider *m_pumpSlider = nullptr;
    QLabel *m_hvValue = nullptr;
    QLabel *m_pumpValue = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QLabel *m_timeLabel = nullptr;
    QCheckBox *m_processDataCheck = nullptr;
    QString m_serverStatus;
    SchematicView *m_schematic = nullptr;
    Faceplate *m_faceplate = nullptr;
    std::function<QString(const QString &caption)> m_openFileName;
    std::function<QString(const QString &caption)> m_saveFileName;
    std::function<void(const QString &title, const QString &text)> m_warning;
};

} // namespace mbloop
