#include "LoopWindow.h"

#include "LoopController.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mbloop {

namespace {

constexpr double kSceneW = 1040.0;
constexpr double kSceneH = 650.0;

// TK-101 geometry (scene units).
constexpr double kTkLeft = 710.0;
constexpr double kTkRight = 890.0;
constexpr double kTkTop = 140.0;
constexpr double kTkBottom = 480.0;
constexpr double kTkCx = (kTkLeft + kTkRight) / 2.0;
// TK-102 geometry.
constexpr double kBaseTop = 560.0;
constexpr double kBaseBottom = 620.0;
constexpr double kBaseWaterTop = 578.0;

const QColor kInk(35, 45, 60);
const QColor kWater(70, 150, 220);
const QColor kWaterDark(25, 100, 190);
const QColor kSignal(40, 90, 150);

QString pct1(double v) { return QString::number(v, 'f', 1); }
QString ma2(double pct) { return QString::number(percentToMa(pct), 'f', 2); }

/// QGraphicsView that always shows the whole scene.
class FitView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
    void showEvent(QShowEvent *event) override
    {
        QGraphicsView::showEvent(event);
        fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
};

/// Vertical PV bar 0..100 % with a SP mark, for the faceplate.
class PvBar : public QWidget
{
public:
    explicit PvBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(56, 150);
    }
    void setValues(double pv, double sp)
    {
        m_pv = pv;
        m_sp = sp;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF bar(18, 8, 20, height() - 16.0);
        p.setPen(QPen(QColor(90, 100, 110), 1));
        p.setBrush(QColor(12, 16, 20));
        p.drawRect(bar);
        const double h = bar.height() * std::clamp(m_pv, 0.0, 100.0) / 100.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 220, 120));
        p.drawRect(QRectF(bar.left() + 1, bar.bottom() - h, bar.width() - 1, h));
        p.setPen(QColor(150, 160, 170));
        QFont f = font();
        f.setPixelSize(9);
        p.setFont(f);
        for (int i = 0; i <= 4; ++i) {
            const double y = bar.bottom() - bar.height() * i / 4.0;
            p.drawLine(QPointF(bar.right(), y), QPointF(bar.right() + 4, y));
            p.drawText(QRectF(bar.right() + 5, y - 6, 22, 12), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(i * 25));
        }
        // SP marker: triangle on the left pointing at the bar, line across.
        const double ys = bar.bottom() - bar.height() * std::clamp(m_sp, 0.0, 100.0) / 100.0;
        p.setPen(QPen(QColor(255, 170, 40), 2));
        p.drawLine(QPointF(bar.left(), ys), QPointF(bar.right(), ys));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 170, 40));
        p.drawPolygon(QPolygonF({QPointF(bar.left() - 12, ys - 6), QPointF(bar.left() - 12, ys + 6),
                                 QPointF(bar.left() - 1, ys)}));
    }

private:
    double m_pv = 0.0;
    double m_sp = 0.0;
};

QLabel *panelLabel(const QString &name, QWidget *parent, int px = 15, bool bright = true)
{
    auto *l = new QLabel(parent);
    l->setObjectName(name);
    l->setStyleSheet(QStringLiteral("color: %1; font: bold %2px 'Consolas';")
                         .arg(bright ? QStringLiteral("#6dfc9a") : QStringLiteral("#9aa7b3"))
                         .arg(px));
    return l;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// SchematicView
// ---------------------------------------------------------------------------------------------

SchematicView::SchematicView(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(0, 0, kSceneW, kSceneH);
    m_view = new FitView(m_scene, this);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    m_view->setBackgroundBrush(QColor(246, 248, 250));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setMinimumSize(320, 200);
    layout->addWidget(m_view);
    build();
}

void SchematicView::build()
{
    const QPen pipePen(kInk, 2, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
    const QPen signalPen(kSignal, 1.5, Qt::DashLine);

    auto text = [this](const QString &s, double x, double y, int px, bool bold = false,
                       const QString &name = QString()) {
        auto *t = m_scene->addSimpleText(s);
        QFont f(QStringLiteral("Segoe UI"));
        f.setPixelSize(px);
        f.setBold(bold);
        t->setFont(f);
        t->setBrush(kInk);
        t->setPos(x, y);
        t->setZValue(10);
        if (!name.isEmpty()) {
            t->setData(0, name);
            m_labels.insert(name, t);
        }
        return t;
    };
    auto pipe = [&](std::initializer_list<QPointF> pts, const QPen &pen) {
        QPainterPath path;
        bool first = true;
        for (const QPointF &pt : pts) {
            if (first) {
                path.moveTo(pt);
                first = false;
            } else {
                path.lineTo(pt);
            }
        }
        auto *item = m_scene->addPath(path, pen);
        item->setZValue(2);
        return item;
    };
    auto arrow = [&](QPointF c, QPointF dir, const QColor &col = kInk) {
        const double len = std::hypot(dir.x(), dir.y());
        const QPointF d(dir.x() / len, dir.y() / len);
        const QPointF n(-d.y(), d.x());
        QPolygonF poly;
        poly << c + d * 7.0 << c - d * 5.0 + n * 5.0 << c - d * 5.0 - n * 5.0;
        auto *a = m_scene->addPolygon(poly, Qt::NoPen, col);
        a->setZValue(3);
        return a;
    };
    auto bubble = [&](double cx, double cy, const QString &l1, const QString &l2, bool panel,
                      const QString &name) {
        auto *c = m_scene->addEllipse(cx - 26, cy - 26, 52, 52, QPen(kInk, 1.8), QColor(255, 255, 255));
        c->setData(0, name);
        c->setZValue(4);
        if (panel) {
            auto *ln = m_scene->addLine(cx - 26, cy, cx + 26, cy, QPen(kInk, 1.8));
            ln->setZValue(5);
        }
        auto *t1 = text(l1, 0, 0, 14, true);
        t1->setPos(cx - t1->boundingRect().width() / 2.0, cy - 3 - t1->boundingRect().height() + 1);
        auto *t2 = text(l2, 0, 0, 14, true);
        t2->setPos(cx - t2->boundingRect().width() / 2.0, cy + 2);
        t1->setZValue(6);
        t2->setZValue(6);
        return c;
    };

    // --- TK-102 base tank: open top U shape, constant water ---------------------------------
    auto *baseWater = m_scene->addRect(QRectF(40, kBaseWaterTop, 900, kBaseBottom - kBaseWaterTop),
                                       Qt::NoPen, QColor(kWater.red(), kWater.green(), kWater.blue(), 150));
    baseWater->setZValue(0);
    QPainterPath base;
    base.moveTo(40, kBaseTop);
    base.lineTo(40, kBaseBottom);
    base.lineTo(940, kBaseBottom);
    base.lineTo(940, kBaseTop);
    auto *baseItem = m_scene->addPath(base, QPen(kInk, 3, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    baseItem->setData(0, QStringLiteral("TK-102"));
    baseItem->setZValue(1);
    text(QStringLiteral("TK-102"), 820, 590, 14, true);
    text(QStringLiteral("Base tank"), 50, 590, 11);

    // --- Process lines ----------------------------------------------------------------------
    pipe({{110, 610}, {110, 492}}, pipePen);                         // suction
    pipe({{132, 470}, {240, 470}, {240, 110}, {338, 110}}, pipePen); // pump -> FCV
    pipe({{382, 110}, {kTkCx, 110}, {kTkCx, kTkTop}}, pipePen);       // FCV -> TK-101
    pipe({{kTkCx, kTkBottom}, {kTkCx, 511}}, pipePen);                // outlet -> HV
    pipe({{kTkCx, 539}, {kTkCx, kBaseWaterTop}}, pipePen);            // HV -> TK-102
    arrow({110, 545}, {0, -1});
    arrow({190, 470}, {1, 0});
    arrow({240, 290}, {0, -1});
    arrow({560, 110}, {1, 0});
    arrow({kTkCx, 126}, {0, 1});
    arrow({kTkCx, 496}, {0, 1});
    arrow({kTkCx, 558}, {0, 1});

    // Overflow line (highlighted while overflowing).
    {
        QPainterPath ov;
        ov.moveTo(kTkLeft, 143.6);
        ov.lineTo(640, 143.6);
        ov.lineTo(640, kBaseWaterTop);
        m_overflow = m_scene->addPath(ov, pipePen);
        m_overflow->setData(0, QStringLiteral("overflow"));
        m_overflow->setData(1, false);
        m_overflow->setZValue(2);
        arrow({670, 143.6}, {-1, 0});
        arrow({640, 360}, {0, 1});
        text(QStringLiteral("overflow"), 648, 300, 11);
    }

    // --- P-101 pump -------------------------------------------------------------------------
    {
        auto *c = m_scene->addEllipse(88, 448, 44, 44, QPen(kInk, 2), QColor(255, 255, 255));
        c->setData(0, QStringLiteral("P-101"));
        c->setZValue(4);
        auto *tri = m_scene->addPolygon(QPolygonF({QPointF(100, 456), QPointF(100, 484), QPointF(126, 470)}),
                                        QPen(kInk, 1.5), kInk);
        tri->setZValue(5);
        text(QStringLiteral("P-101"), 62, 418, 14, true);
        text(QString(), 124, 500, 12, false, QStringLiteral("pump-value"));
    }

    // --- FCV-101 ---------------------------------------------------------------------------
    {
        const double x = 360.0;
        const double y = 110.0;
        QPainterPath body;
        body.addPolygon(QPolygonF({QPointF(x - 22, y - 14), QPointF(x, y), QPointF(x - 22, y + 14),
                                   QPointF(x - 22, y - 14)}));
        body.addPolygon(QPolygonF({QPointF(x + 22, y - 14), QPointF(x, y), QPointF(x + 22, y + 14),
                                   QPointF(x + 22, y - 14)}));
        auto *b = m_scene->addPath(body, QPen(kInk, 2), QColor(255, 255, 255));
        b->setData(0, QStringLiteral("FCV-101"));
        b->setZValue(4);
        auto *stem = m_scene->addLine(x, y, x, y - 20, QPen(kInk, 2));
        stem->setZValue(3);
        QPainterPath dome;
        dome.moveTo(x - 20, y - 20);
        dome.arcTo(QRectF(x - 20, y - 20 - 20, 40, 40), 180, -180);
        dome.closeSubpath();
        auto *d = m_scene->addPath(dome, QPen(kInk, 2), QColor(255, 255, 255));
        d->setZValue(4);
        text(QStringLiteral("FC"), x - 9, y + 17, 13, true);
        text(QStringLiteral("FCV-101"), 262, 66, 14, true);
        text(QString(), 300, 150, 12, false, QStringLiteral("fcv-value"));
    }

    // --- TK-101 -----------------------------------------------------------------------------
    {
        m_water101 = m_scene->addRect(QRectF(kTkLeft, kTkBottom, kTkRight - kTkLeft, 0), Qt::NoPen,
                                      QColor(kWater.red(), kWater.green(), kWater.blue(), 170));
        m_water101->setZValue(0.5);
        QPainterPath tankPath;
        tankPath.addRoundedRect(QRectF(kTkLeft, kTkTop, kTkRight - kTkLeft, kTkBottom - kTkTop), 6, 6);
        auto *tank = m_scene->addPath(tankPath, QPen(kInk, 3), Qt::NoBrush);
        tank->setData(0, QStringLiteral("TK-101"));
        tank->setZValue(1);
        for (int i = 0; i <= 4; ++i) {
            const double y = kTkBottom - (kTkBottom - kTkTop) * i / 4.0;
            auto *tick = m_scene->addLine(kTkLeft + 1.5, y, kTkLeft + 14, y, QPen(kInk, 1.2));
            tick->setZValue(3);
            auto *t = text(QStringLiteral("%1 %").arg(i * 25), kTkLeft + 18, y - 8, 11);
            if (i == 4) {
                t->setY(y + 1); // keep clear of the overflow nozzle just above
            } else if (i == 0) {
                t->setY(y - 17);
            }
        }
        text(QStringLiteral("TK-101"), kTkCx + 14, kTkTop + 8, 14, true);
        // Pressure tap and LT-101.
        pipe({{kTkRight, 470}, {934, 470}}, pipePen);
        auto *lt = bubble(960, 470, QStringLiteral("LT"), QStringLiteral("101"), false,
                          QStringLiteral("LT-101"));
        lt->setZValue(4);
        text(QString(), 905, 502, 12, false, QStringLiteral("lt-value"));
    }

    // --- HV-102 -----------------------------------------------------------------------------
    {
        const double x = kTkCx;
        const double y = 525.0;
        QPainterPath body;
        body.addPolygon(QPolygonF({QPointF(x - 12, y - 14), QPointF(x, y), QPointF(x + 12, y - 14),
                                   QPointF(x - 12, y - 14)}));
        body.addPolygon(QPolygonF({QPointF(x - 12, y + 14), QPointF(x, y), QPointF(x + 12, y + 14),
                                   QPointF(x - 12, y + 14)}));
        auto *b = m_scene->addPath(body, QPen(kInk, 2), QColor(255, 255, 255));
        b->setData(0, QStringLiteral("HV-102"));
        b->setZValue(4);
        // T handle on the left.
        auto *stem = m_scene->addLine(x, y, x - 22, y, QPen(kInk, 2));
        stem->setZValue(3);
        auto *bar = m_scene->addLine(x - 22, y - 9, x - 22, y + 9, QPen(kInk, 3));
        bar->setZValue(3);
        text(QStringLiteral("HV-102"), kTkCx - 100, y - 24, 14, true);
        text(QString(), kTkCx - 100, y - 6, 12, false, QStringLiteral("hv-value"));
    }

    // --- LIC-101 and signal lines -----------------------------------------------------------
    {
        auto *lic = bubble(620, 45, QStringLiteral("LIC"), QStringLiteral("101"), true,
                           QStringLiteral("LIC-101"));
        lic->setZValue(4);
        text(QString(), 500, 76, 12, false, QStringLiteral("lic-value"));
        // Dashed electric signals: LT-101 -> LIC-101 -> FCV-101 actuator.
        pipe({{960, 444}, {960, 45}, {646, 45}}, signalPen);
        pipe({{594, 45}, {360, 45}, {360, 70}}, signalPen);
        arrow({960, 250}, {0, -1}, kSignal);
        arrow({800, 45}, {-1, 0}, kSignal);
        arrow({480, 45}, {-1, 0}, kSignal);
        arrow({360, 62}, {0, 1}, kSignal);
    }

    // --- Process data (hidden by default) ---------------------------------------------------
    {
        auto *group = new QGraphicsItemGroup;
        m_scene->addItem(group);
        group->setData(0, QStringLiteral("process-data"));
        auto *bg = new QGraphicsRectItem(-8, -6, 188, 126);
        bg->setBrush(QColor(255, 255, 255, 230));
        bg->setPen(QPen(QColor(150, 160, 175), 1));
        group->addToGroup(bg);
        auto *t = text(QString(), 0, 0, 12, false, QStringLiteral("process-data"));
        t->setData(0, QStringLiteral("process-data"));
        group->addToGroup(t);
        group->setPos(290, 232);
        group->setZValue(8);
        group->setVisible(false);
        m_processGroup = group;
    }
}

void SchematicView::refresh(const LoopController &controller)
{
    const LoopState &s = controller.model().state();
    const LoopParams &p = controller.model().params();

    auto set = [this](const char *name, const QString &str) {
        if (auto *t = m_labels.value(QString::fromLatin1(name))) {
            t->setText(str);
        }
    };
    set("lt-value", QStringLiteral("%1 %  %2 mA").arg(pct1(s.pvPct), ma2(s.pvPct)));
    set("fcv-value", QStringLiteral("OUT %1 %  %2 mA\nPosition %3 %")
                         .arg(pct1(s.outPct), ma2(s.outPct), pct1(s.stemPct)));
    set("hv-value", QStringLiteral("%1 % open").arg(p.hvOpeningPct, 0, 'f', 0));
    set("pump-value", QStringLiteral("%1 %").arg(p.pumpSpeedPct, 0, 'f', 0));
    set("lic-value", QStringLiteral("PV %1 %  SP %2 %  OUT %3 %")
                         .arg(pct1(s.pvPct), pct1(s.spPct), pct1(s.outPct)));
    const QString proc = QStringLiteral("Level %1 m\nQin %2 L/min\nQhv %3 L/min\nOverflow %4 L/min\n"
                                        "Pump %5 bar\nDead time %6 s")
                             .arg(s.levelM, 0, 'f', 3)
                             .arg(s.qIn * 60000.0, 0, 'f', 1)
                             .arg(s.qHv * 60000.0, 0, 'f', 1)
                             .arg(s.qOverflow * 60000.0, 0, 'f', 1)
                             .arg(s.pumpPressureBar, 0, 'f', 2)
                             .arg(p.deadTimeS, 0, 'f', 1);
    set("process-data", proc);

    // Water in TK-101.
    const double h = std::clamp(s.levelM / loop101::kTankHeightM, 0.0, 1.0) * (kTkBottom - kTkTop);
    m_water101->setRect(kTkLeft + 1.5, kTkBottom - h, kTkRight - kTkLeft - 3.0, h);

    // Overflow highlight.
    m_overflow->setData(1, s.overflowing);
    QPen pen = m_overflow->pen();
    pen.setWidthF(s.overflowing ? 6.0 : 2.0);
    pen.setColor(s.overflowing ? kWaterDark : kInk);
    m_overflow->setPen(pen);
}

void SchematicView::setProcessDataVisible(bool visible)
{
    m_processData = visible;
    if (m_processGroup) {
        m_processGroup->setVisible(visible);
    }
}

bool SchematicView::processDataVisible() const { return m_processData; }

QString SchematicView::labelText(const QString &name) const
{
    const auto *t = m_labels.value(name, nullptr);
    if (!t || !t->isVisible()) {
        return {};
    }
    return t->text();
}

QGraphicsView *SchematicView::view() const { return m_view; }

// ---------------------------------------------------------------------------------------------
// Faceplate
// ---------------------------------------------------------------------------------------------

Faceplate::Faceplate(LoopController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller)
{
    setObjectName(QStringLiteral("faceplate"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(210);
    setStyleSheet(QStringLiteral(
        "#faceplate { background: #1d2227; border: 2px solid #4a545e; border-radius: 6px; }"
        "QPushButton { background: #36404a; color: #e8eef4; border: 1px solid #66717c;"
        " border-radius: 3px; font-size: 16px; min-height: 30px; }"
        "QPushButton:pressed { background: #55606b; }"
        "QPushButton:disabled { color: #6b7782; }"
        "QLineEdit { background: #0c1014; color: #6dfc9a; border: 1px solid #66717c;"
        " font: bold 15px 'Consolas'; padding: 2px; }"
        "QLineEdit:disabled { color: #3f6b51; }"
        "QComboBox { background: #0c1014; color: #e8eef4; border: 1px solid #66717c;"
        " padding: 2px 6px; min-height: 22px; }"
        "QComboBox QAbstractItemView { background: #1d2227; color: #e8eef4;"
        " selection-background-color: #36404a; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(6);

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("LIC-101"), this);
    title->setStyleSheet(QStringLiteral("color: #e8eef4; font: bold 18px 'Segoe UI';"));
    m_modeButton = new QPushButton(QStringLiteral("MAN"), this);
    m_modeButton->setObjectName(QStringLiteral("modeButton"));
    m_modeButton->setFocusPolicy(Qt::NoFocus);
    m_modeButton->setToolTip(QStringLiteral("Switch LIC-101 between Manual and Auto (PID)"));
    connect(m_modeButton, &QPushButton::clicked, this, [this] {
        if (m_controller) {
            m_controller->setAuto(!m_controller->model().isAuto());
        }
        refresh();
    });
    head->addWidget(title);
    head->addStretch();
    head->addWidget(m_modeButton);
    root->addLayout(head);

    auto *mid = new QHBoxLayout;
    auto *bar = new PvBar(this);
    m_bar = bar;
    mid->addWidget(bar);
    auto *vals = new QVBoxLayout;
    m_pvLabel = panelLabel(QStringLiteral("pvLabel"), this);
    m_pvMaLabel = panelLabel(QStringLiteral("pvMaLabel"), this, 13, false);
    m_spLabel = panelLabel(QStringLiteral("spLabel"), this);
    m_spLabel->setStyleSheet(m_spLabel->styleSheet() + QStringLiteral("color: #ffb428;"));
    vals->addWidget(m_pvLabel);
    vals->addWidget(m_pvMaLabel);
    vals->addSpacing(8);
    vals->addWidget(m_spLabel);
    vals->addStretch();
    mid->addLayout(vals, 1);
    root->addLayout(mid, 1);

    m_outLabel = panelLabel(QStringLiteral("outLabel"), this, 17);
    m_outMaLabel = panelLabel(QStringLiteral("outMaLabel"), this, 13, false);
    root->addWidget(m_outLabel);
    root->addWidget(m_outMaLabel);

    auto *btns = new QHBoxLayout;
    auto *up = new QPushButton(QStringLiteral("▲"), this);
    up->setObjectName(QStringLiteral("outUpButton"));
    up->setToolTip(QStringLiteral("OUT +1 % (Shift: +5 %)"));
    up->setFocusPolicy(Qt::NoFocus);
    auto *down = new QPushButton(QStringLiteral("▼"), this);
    down->setObjectName(QStringLiteral("outDownButton"));
    down->setToolTip(QStringLiteral("OUT -1 % (Shift: -5 %)"));
    down->setFocusPolicy(Qt::NoFocus);
    m_outUp = up;
    m_outDown = down;
    btns->addWidget(up);
    btns->addWidget(down);
    root->addLayout(btns);
    up->installEventFilter(this);
    down->installEventFilter(this);
    connect(up, &QPushButton::clicked, this, [this] {
        const bool shift = (m_lastModifiers | QGuiApplication::keyboardModifiers()) & Qt::ShiftModifier;
        changeOut(shift ? 5.0 : 1.0);
    });
    connect(down, &QPushButton::clicked, this, [this] {
        const bool shift = (m_lastModifiers | QGuiApplication::keyboardModifiers()) & Qt::ShiftModifier;
        changeOut(shift ? -5.0 : -1.0);
    });

    auto *form = new QHBoxLayout;
    auto *outCaption = new QLabel(QStringLiteral("OUT"), this);
    outCaption->setStyleSheet(QStringLiteral("color: #9aa7b3; font: 12px 'Segoe UI';"));
    m_outEdit = new QLineEdit(this);
    m_outEdit->setObjectName(QStringLiteral("outEdit"));
    m_outEdit->setToolTip(QStringLiteral("Type OUT in % (0..100) and press Enter"));
    form->addWidget(outCaption);
    form->addWidget(m_outEdit);
    root->addLayout(form);
    auto *form2 = new QHBoxLayout;
    auto *spCaption = new QLabel(QStringLiteral("SP"), this);
    spCaption->setStyleSheet(QStringLiteral("color: #9aa7b3; font: 12px 'Segoe UI';"));
    spCaption->setMinimumWidth(outCaption->sizeHint().width());
    m_spEdit = new QLineEdit(this);
    m_spEdit->setObjectName(QStringLiteral("spEdit"));
    m_spEdit->setToolTip(QStringLiteral("Type SP in % (0..100) and press Enter"));
    form2->addWidget(spCaption);
    form2->addWidget(m_spEdit);
    root->addLayout(form2);

    // Tuning (v1.3.1): PID parameters and the action, applied with Enter / on selection.
    auto *tuningCaption = new QLabel(QStringLiteral("Tuning"), this);
    tuningCaption->setStyleSheet(QStringLiteral("color: #e8eef4; font: bold 12px 'Segoe UI';"));
    root->addWidget(tuningCaption);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    const QString captionStyle = QStringLiteral("color: #9aa7b3; font: 12px 'Segoe UI';");
    auto *kpCaption = new QLabel(QStringLiteral("Kp"), this);
    auto *tiCaption = new QLabel(QStringLiteral("Ti s"), this);
    auto *tdCaption = new QLabel(QStringLiteral("Td s"), this);
    auto *actCaption = new QLabel(QStringLiteral("Action"), this);
    for (QLabel *c : {kpCaption, tiCaption, tdCaption, actCaption}) {
        c->setStyleSheet(captionStyle);
    }
    m_kpEdit = new QLineEdit(this);
    m_kpEdit->setObjectName(QStringLiteral("kpEdit"));
    m_kpEdit->setToolTip(QStringLiteral("Proportional gain Kp, 0..100 (Enter applies)"));
    m_tiEdit = new QLineEdit(this);
    m_tiEdit->setObjectName(QStringLiteral("tiEdit"));
    m_tiEdit->setToolTip(QStringLiteral("Integral time Ti in s, 0..3600 (0 = no I) (Enter applies)"));
    m_tdEdit = new QLineEdit(this);
    m_tdEdit->setObjectName(QStringLiteral("tdEdit"));
    m_tdEdit->setToolTip(QStringLiteral("Derivative time Td in s, 0..600 (0 = no D) (Enter applies)"));
    m_actionCombo = new QComboBox(this);
    m_actionCombo->setObjectName(QStringLiteral("actionCombo"));
    m_actionCombo->setToolTip(QStringLiteral("Reverse: PV up -> OUT down; Direct: PV up -> OUT up"));
    m_actionCombo->addItem(QStringLiteral("Reverse"));
    m_actionCombo->addItem(QStringLiteral("Direct"));
    grid->addWidget(kpCaption, 0, 0);
    grid->addWidget(m_kpEdit, 0, 1);
    grid->addWidget(tiCaption, 1, 0);
    grid->addWidget(m_tiEdit, 1, 1);
    grid->addWidget(tdCaption, 2, 0);
    grid->addWidget(m_tdEdit, 2, 1);
    grid->addWidget(actCaption, 3, 0);
    grid->addWidget(m_actionCombo, 3, 1);
    root->addLayout(grid);

    connect(m_outEdit, &QLineEdit::returnPressed, this, [this] { applyEdit(true); });
    connect(m_spEdit, &QLineEdit::returnPressed, this, [this] { applyEdit(false); });
    connect(m_kpEdit, &QLineEdit::returnPressed, this,
            [this] { applyTuning(m_kpEdit, &PidSettings::kp, 100.0); });
    connect(m_tiEdit, &QLineEdit::returnPressed, this,
            [this] { applyTuning(m_tiEdit, &PidSettings::tiS, 3600.0); });
    connect(m_tdEdit, &QLineEdit::returnPressed, this,
            [this] { applyTuning(m_tdEdit, &PidSettings::tdS, 600.0); });
    connect(m_actionCombo, &QComboBox::currentIndexChanged, this, [this](int i) { applyAction(i); });

    applyMode(controller && controller->model().isAuto());
    refresh();
}

namespace {
/// Number typed in a faceplate field (comma or point decimal); false when not a finite number.
double parseFieldNumber(const QString &text, bool *ok)
{
    QString str = text.trimmed();
    str.replace(QLatin1Char(','), QLatin1Char('.'));
    const double v = QLocale::c().toDouble(str, ok);
    if (!std::isfinite(v)) {
        *ok = false;
    }
    return v;
}

/// Shortest faithful text of a tuning value (no rounding of what was typed).
QString tuningText(double v)
{
    return QString::number(v, 'g', 6);
}
} // namespace

void Faceplate::applyMode(bool isAuto)
{
    m_modeButton->setText(isAuto ? QStringLiteral("AUTO") : QStringLiteral("MAN"));
    m_modeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #1d2227; background: %1; border: none; border-radius: 3px;"
        " font: bold 14px 'Consolas'; padding: 1px 8px; min-height: 0px; }"
        "QPushButton:pressed { background: %1; }")
                                    .arg(isAuto ? QStringLiteral("#3ccf6a")
                                                : QStringLiteral("#ffb428")));
    // In Auto the PID computes OUT: the OUT buttons and the OUT edit are disabled.
    m_outUp->setEnabled(!isAuto);
    m_outDown->setEnabled(!isAuto);
    m_outEdit->setEnabled(!isAuto);
}

void Faceplate::applyTuning(QLineEdit *edit, double PidSettings::*member, double max)
{
    if (!m_controller) {
        return;
    }
    bool ok = false;
    const double v = parseFieldNumber(edit->text(), &ok);
    if (ok) {
        PidSettings pid = m_controller->model().pid();
        pid.*member = std::clamp(v, 0.0, max);
        m_controller->setPid(pid); // the model clamps too; emits stateChanged()
    }
    // Show what is in effect (the typed text is replaced by the applied, clamped value).
    edit->setText(tuningText(m_controller->model().pid().*member));
    refresh();
}

void Faceplate::applyAction(int index)
{
    if (!m_controller) {
        return;
    }
    const PidSettings &current = m_controller->model().pid();
    if (current.reverse == (index == 0)) {
        return; // already so (e.g. refresh-driven change): nothing to send
    }
    PidSettings pid = current;
    pid.reverse = (index == 0);
    m_controller->setPid(pid);
    refresh();
}

bool Faceplate::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        m_lastModifiers = static_cast<QMouseEvent *>(event)->modifiers();
    }
    return QWidget::eventFilter(watched, event);
}

void Faceplate::changeOut(double delta)
{
    if (!m_controller) {
        return;
    }
    m_controller->setOut(std::clamp(m_controller->model().state().outPct + delta, 0.0, 100.0));
    // The out edit may show an old typed value: a button press always shows the new OUT.
    m_outEdit->setText(pct1(m_controller->model().state().outPct));
    refresh();
    emit m_controller->stateChanged(); // the schematic and the status follow
}

void Faceplate::applyEdit(bool isOut)
{
    if (!m_controller) {
        return;
    }
    QLineEdit *edit = isOut ? m_outEdit : m_spEdit;
    QString str = edit->text().trimmed();
    str.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = QLocale::c().toDouble(str, &ok);
    if (ok && std::isfinite(v)) {
        const double c = std::clamp(v, 0.0, 100.0);
        if (isOut) {
            m_controller->setOut(c);
        } else {
            m_controller->setSp(c);
        }
    }
    const LoopState &s = m_controller->model().state();
    edit->setText(pct1(isOut ? s.outPct : s.spPct));
    refresh();
    emit m_controller->stateChanged();
}

void Faceplate::refresh()
{
    if (!m_controller) {
        return;
    }
    const LoopState &s = m_controller->model().state();
    m_pvLabel->setText(QStringLiteral("PV %1 %").arg(pct1(s.pvPct)));
    m_pvMaLabel->setText(QStringLiteral("%1 mA").arg(ma2(s.pvPct)));
    m_spLabel->setText(QStringLiteral("SP %1 %").arg(pct1(s.spPct)));
    m_outLabel->setText(QStringLiteral("OUT %1 %").arg(pct1(s.outPct)));
    m_outMaLabel->setText(QStringLiteral("%1 mA").arg(ma2(s.outPct)));
    static_cast<PvBar *>(m_bar)->setValues(s.pvPct, s.spPct);
    if (!m_outEdit->hasFocus()) {
        m_outEdit->setText(pct1(s.outPct));
    }
    if (!m_spEdit->hasFocus()) {
        m_spEdit->setText(pct1(s.spPct));
    }

    const bool isAuto = m_controller->model().isAuto();
    if (m_modeButton->text() != (isAuto ? QStringLiteral("AUTO") : QStringLiteral("MAN"))) {
        applyMode(isAuto);
    }
    const PidSettings &pid = m_controller->model().pid();
    if (!m_kpEdit->hasFocus()) {
        m_kpEdit->setText(tuningText(pid.kp));
    }
    if (!m_tiEdit->hasFocus()) {
        m_tiEdit->setText(tuningText(pid.tiS));
    }
    if (!m_tdEdit->hasFocus()) {
        m_tdEdit->setText(tuningText(pid.tdS));
    }
    {
        // Shows the controller's action without emitting currentIndexChanged (no feedback).
        const QSignalBlocker b(m_actionCombo);
        m_actionCombo->setCurrentIndex(pid.reverse ? 0 : 1);
    }
}

// ---------------------------------------------------------------------------------------------
// LoopWindow
// ---------------------------------------------------------------------------------------------

LoopWindow::LoopWindow(LoopController *controller, QWidget *parent)
    : QMainWindow(parent), m_controller(controller)
{
    setWindowTitle(QStringLiteral("Modbus Loop Simulator — Level loop 101"));
    const QString filter = QStringLiteral("Loop scenario (*.mbloop)");
    m_openFileName = [this, filter](const QString &caption) {
        return QFileDialog::getOpenFileName(this, caption, QString(), filter);
    };
    m_saveFileName = [this, filter](const QString &caption) {
        return QFileDialog::getSaveFileName(this, caption, QString(), filter);
    };
    m_warning = [this](const QString &title, const QString &text) {
        QMessageBox::warning(this, title, text);
    };

    auto *central = new QWidget(this);
    auto *col = new QVBoxLayout(central);
    auto *row = new QHBoxLayout;
    m_schematic = new SchematicView(central);
    m_faceplate = new Faceplate(controller, central);
    row->addWidget(m_schematic, 1);
    row->addWidget(m_faceplate);
    col->addLayout(row, 1);

    // Control bar.
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("HV-102"), central));
    m_hvSlider = new QSlider(Qt::Horizontal, central);
    m_hvSlider->setObjectName(QStringLiteral("hvSlider"));
    m_hvSlider->setRange(0, 100);
    m_hvSlider->setMinimumWidth(110);
    m_hvValue = new QLabel(central);
    m_hvValue->setMinimumWidth(48);
    bar->addWidget(m_hvSlider, 1);
    bar->addWidget(m_hvValue);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(QStringLiteral("P-101 speed"), central));
    m_pumpSlider = new QSlider(Qt::Horizontal, central);
    m_pumpSlider->setObjectName(QStringLiteral("pumpSlider"));
    m_pumpSlider->setRange(0, 100);
    m_pumpSlider->setMinimumWidth(110);
    m_pumpValue = new QLabel(central);
    m_pumpValue->setMinimumWidth(48);
    bar->addWidget(m_pumpSlider, 1);
    bar->addWidget(m_pumpValue);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(QStringLiteral("Speed"), central));
    m_speedCombo = new QComboBox(central);
    m_speedCombo->setObjectName(QStringLiteral("speedCombo"));
    for (int k : {1, 2, 5}) {
        m_speedCombo->addItem(QStringLiteral("×%1").arg(k), static_cast<double>(k));
    }
    bar->addWidget(m_speedCombo);
    m_pauseButton = new QPushButton(QStringLiteral("Pause"), central);
    m_pauseButton->setObjectName(QStringLiteral("pauseButton"));
    m_pauseButton->setCheckable(true);
    bar->addWidget(m_pauseButton);
    m_timeLabel = new QLabel(QStringLiteral("t = 0.0 s"), central);
    m_timeLabel->setObjectName(QStringLiteral("timeLabel"));
    m_timeLabel->setMinimumWidth(90);
    bar->addWidget(m_timeLabel);
    m_processDataCheck = new QCheckBox(QStringLiteral("Show process data"), central);
    m_processDataCheck->setObjectName(QStringLiteral("processDataCheck"));
    bar->addWidget(m_processDataCheck);
    col->addLayout(bar);
    setCentralWidget(central);

    // Menus.
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *open = file->addAction(QStringLiteral("&Open scenario..."));
    open->setObjectName(QStringLiteral("openAction"));
    QAction *save = file->addAction(QStringLiteral("&Save scenario..."));
    save->setObjectName(QStringLiteral("saveAction"));
    file->addSeparator();
    QAction *reset = file->addAction(QStringLiteral("&Reset"));
    reset->setObjectName(QStringLiteral("resetAction"));
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("&Quit"));
    quit->setObjectName(QStringLiteral("quitAction"));
    quit->setShortcut(QKeySequence::Quit);
    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction *about = help->addAction(QStringLiteral("&About"));
    about->setObjectName(QStringLiteral("aboutAction"));

    connect(open, &QAction::triggered, this, [this] { openScenario(); });
    connect(save, &QAction::triggered, this, [this] { saveScenario(); });
    connect(reset, &QAction::triggered, this, [this] {
        m_controller->resetState();
        refresh();
    });
    connect(quit, &QAction::triggered, this, &QWidget::close);
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, QStringLiteral("About Modbus Loop Simulator"),
            QStringLiteral("<b>Modbus Loop Simulator</b> %1<br>Level loop 101 with the LIC-101 "
                           "regulator as a Modbus TCP server, for instrumentation courses.")
                .arg(QCoreApplication::applicationVersion()));
    });

    // Controls -> controller.
    connect(m_hvSlider, &QSlider::valueChanged, this, [this](int v) {
        m_controller->setHvOpening(v);
        m_hvValue->setText(QStringLiteral("%1 %").arg(v));
        refresh();
    });
    connect(m_pumpSlider, &QSlider::valueChanged, this, [this](int v) {
        m_controller->setPumpSpeed(v);
        m_pumpValue->setText(QStringLiteral("%1 %").arg(v));
        refresh();
    });
    connect(m_speedCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0) {
            m_controller->setSpeedFactor(m_speedCombo->itemData(i).toDouble());
        }
    });
    connect(m_pauseButton, &QPushButton::toggled, this, [this](bool on) {
        m_controller->setPaused(on);
        m_pauseButton->setText(on ? QStringLiteral("Run") : QStringLiteral("Pause"));
    });
    connect(m_processDataCheck, &QCheckBox::toggled, this,
            [this](bool on) { m_schematic->setProcessDataVisible(on); refresh(); });
    // Controller -> window.
    connect(controller, &LoopController::stateChanged, this, [this] { refresh(); });
    connect(controller, &LoopController::pausedChanged, this, [this] { syncControls(); });
    connect(controller, &LoopController::speedFactorChanged, this, [this] { syncControls(); });

    statusBar()->showMessage(QString());
    syncControls();
    refresh();
    resize(1200, 760);
}

SchematicView *LoopWindow::schematic() const { return m_schematic; }
Faceplate *LoopWindow::faceplate() const { return m_faceplate; }

void LoopWindow::setServerStatus(const QString &text)
{
    m_serverStatus = text;
    refresh();
}

void LoopWindow::syncControls()
{
    const LoopParams &p = m_controller->model().params();
    {
        const QSignalBlocker b(m_hvSlider);
        if (!m_hvSlider->isSliderDown()) {
            m_hvSlider->setValue(static_cast<int>(std::lround(p.hvOpeningPct)));
        }
    }
    {
        const QSignalBlocker b(m_pumpSlider);
        if (!m_pumpSlider->isSliderDown()) {
            m_pumpSlider->setValue(static_cast<int>(std::lround(p.pumpSpeedPct)));
        }
    }
    m_hvValue->setText(QStringLiteral("%1 %").arg(m_hvSlider->value()));
    m_pumpValue->setText(QStringLiteral("%1 %").arg(m_pumpSlider->value()));
    {
        const QSignalBlocker b(m_speedCombo);
        const int idx = m_speedCombo->findData(m_controller->speedFactor());
        if (idx >= 0) {
            m_speedCombo->setCurrentIndex(idx);
        }
    }
    {
        const QSignalBlocker b(m_pauseButton);
        m_pauseButton->setChecked(m_controller->isPaused());
        m_pauseButton->setText(m_controller->isPaused() ? QStringLiteral("Run") : QStringLiteral("Pause"));
    }
}

void LoopWindow::refresh()
{
    m_schematic->refresh(*m_controller);
    m_faceplate->refresh();
    syncControls();
    m_timeLabel->setText(
        QStringLiteral("t = %1 s").arg(m_controller->model().state().timeS, 0, 'f', 1));
    if (!m_serverStatus.isEmpty()) {
        QString msg = m_serverStatus;
        if (m_controller->server().port() != 0) { // running: a client count only means something then
            const int n = m_controller->server().clientCount();
            msg = QStringLiteral("%1, %2 %3")
                      .arg(m_serverStatus)
                      .arg(n)
                      .arg(n == 1 ? QStringLiteral("client") : QStringLiteral("clients"));
        }
        if (statusBar()->currentMessage() != msg) {
            statusBar()->showMessage(msg);
        }
    }
}

void LoopWindow::openScenario()
{
    const QString path = m_openFileName(QStringLiteral("Open scenario"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const auto scenario = loadScenario(path, &error);
    if (!scenario) {
        m_warning(QStringLiteral("Open scenario"), error);
        return;
    }
    m_controller->loadScenario(*scenario);
    refresh();
}

void LoopWindow::saveScenario()
{
    QString path = m_saveFileName(QStringLiteral("Save scenario"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".mbloop");
    }
    QString error;
    if (!mbloop::saveScenario(path, m_controller->currentScenario(), &error)) {
        m_warning(QStringLiteral("Save scenario"), error);
    }
}

} // namespace mbloop
