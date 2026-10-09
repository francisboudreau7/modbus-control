#include "MainWindow.h"

#include "ConnectionBar.h"
#include "CursorPanel.h"
#include "TagTableModel.h"
#include "TagTableWidget.h"
#include "TrendPlot.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>

namespace mb {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_connectionBar(new ConnectionBar(this))
    , m_tagModel(new TagTableModel(this))
    , m_tagTable(new TagTableWidget(m_tagModel, this))
    , m_trendPlot(new TrendPlot(this))
    , m_cursorPanel(new CursorPanel(this))
    , m_connectionLabel(new QLabel(this))
{
    setCentralWidget(m_trendPlot);
    resize(1200, 800);

    // Toolbar
    auto *connectionToolBar = addToolBar(tr("Connection"));
    connectionToolBar->setObjectName(QStringLiteral("connectionToolBar"));
    connectionToolBar->setMovable(false);
    connectionToolBar->addWidget(m_connectionBar);

    // Docks
    const QDockWidget::DockWidgetFeatures dockFeatures = QDockWidget::DockWidgetClosable
        | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;

    auto *tagsDock = new QDockWidget(tr("Tags"), this);
    tagsDock->setObjectName(QStringLiteral("tagsDock"));
    tagsDock->setFeatures(dockFeatures);
    tagsDock->setWidget(m_tagTable);
    addDockWidget(Qt::BottomDockWidgetArea, tagsDock);

    auto *cursorsDock = new QDockWidget(tr("Cursors"), this);
    cursorsDock->setObjectName(QStringLiteral("cursorsDock"));
    cursorsDock->setFeatures(dockFeatures);
    cursorsDock->setWidget(m_cursorPanel);
    addDockWidget(Qt::RightDockWidgetArea, cursorsDock);

    // File menu
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    m_actions.newProject = fileMenu->addAction(tr("&New"));
    m_actions.newProject->setShortcut(QKeySequence::New);
    m_actions.openProject = fileMenu->addAction(tr("&Open..."));
    m_actions.openProject->setShortcut(QKeySequence::Open);
    m_actions.saveProject = fileMenu->addAction(tr("&Save"));
    m_actions.saveProject->setShortcut(QKeySequence::Save);
    m_actions.saveProjectAs = fileMenu->addAction(tr("Save &As..."));
    m_actions.saveProjectAs->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    m_actions.quit = fileMenu->addAction(tr("&Quit"));
    m_actions.quit->setShortcut(QKeySequence::Quit);
    connect(m_actions.quit, &QAction::triggered, this, &QWidget::close);

    // Export menu
    auto *exportMenu = menuBar()->addMenu(tr("&Export"));
    m_actions.exportImage = exportMenu->addAction(tr("Graph &image..."));
    m_actions.exportImage->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_actions.exportCsv = exportMenu->addAction(tr("&Data (CSV)..."));
    m_actions.exportCsv->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

    // View menu
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tagsDock->toggleViewAction());
    viewMenu->addAction(cursorsDock->toggleViewAction());

    // Options menu
    auto *optionsMenu = menuBar()->addMenu(tr("&Options"));
    m_actions.oneBasedAddresses = optionsMenu->addAction(tr("Use &1-based register numbers"));
    m_actions.oneBasedAddresses->setCheckable(true);
    m_actions.oneBasedAddresses->setToolTip(
        tr("Show and enter register numbers as printed in device manuals (first register = 1)"));

    // Help menu
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    m_actions.about = helpMenu->addAction(tr("&About"));
    connect(m_actions.about, &QAction::triggered, this, [this] {
        const QString name = QApplication::applicationName().isEmpty()
            ? tr("Modbus Control")
            : QApplication::applicationName();
        QMessageBox::about(this, tr("About %1").arg(name),
                           tr("<b>%1</b> %2<br><br>Built with Qt, Qwt and libmodbus")
                               .arg(name, QApplication::applicationVersion()));
    });

    // Status bar
    m_connectionLabel->setObjectName(QStringLiteral("connectionLabel"));
    statusBar()->addPermanentWidget(m_connectionLabel);
    setConnectionStatus(ConnectionState::Disconnected, QString());

    // Restore geometry and dock state
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
}

MainWindow::~MainWindow() = default;
ConnectionBar *MainWindow::connectionBar() const { return m_connectionBar; }
TagTableModel *MainWindow::tagModel() const { return m_tagModel; }
TagTableWidget *MainWindow::tagTable() const { return m_tagTable; }
TrendPlot *MainWindow::trendPlot() const { return m_trendPlot; }
CursorPanel *MainWindow::cursorPanel() const { return m_cursorPanel; }
const MainWindow::Actions &MainWindow::actions() const { return m_actions; }
// Stubs created by T90; T96 builds the tab widget.
AnalysisView *MainWindow::analysisView() const { return m_analysisView; }
bool MainWindow::isAnalysisTabActive() const { return false; }
void MainWindow::setAnalysisTabActive(bool) {}

void MainWindow::setConnectionStatus(ConnectionState state, const QString &detail)
{
    // The worker's details already read as a sentence ("Connected to ..."), so they replace
    // the bare state name instead of being appended to it.
    m_connectionLabel->setText(detail.isEmpty() ? toString(state) : detail);
}

void MainWindow::showStatusMessage(const QString &text, int timeoutMs)
{
    statusBar()->showMessage(text, timeoutMs);
}

void MainWindow::setDocumentTitle(const QString &name, bool modified)
{
    setWindowTitle(name + QStringLiteral("[*] — Modbus Control"));
    setWindowModified(modified);
}

void MainWindow::setCloseHandler(std::function<bool()> handler) { m_closeHandler = std::move(handler); }

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeHandler && !m_closeHandler()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
    event->accept();
}

} // namespace mb
