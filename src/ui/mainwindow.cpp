#include "mainwindow.h"
#include "render/vulkan_window.h"
#include "sim/pbd_solver.h"

#include <QMenuBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_vulkanContainer = new QWidget();
    m_vulkanContainer->setMinimumSize(400, 300);

    m_statusLabel = new QLabel("Initializing Vulkan...", m_vulkanContainer);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(
        "background-color: #1a1a2e; color: #e0e0e0; font-size: 18px;");

    auto *layout = new QVBoxLayout(m_vulkanContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_statusLabel);

    setCentralWidget(m_vulkanContainer);
    setupMenuBar();
    setupDockWidgets();
    statusBar()->showMessage("Starting...");
    resize(1600, 900);

    QMetaObject::invokeMethod(this, [this]() {
        m_vulkanWindow = new rtvk::render::GranularVulkanWindow(this);

        QObject::connect(
            m_vulkanWindow,
            &rtvk::render::GranularVulkanWindow::vulkanReady,
            this, [this]() {
                m_statusLabel->hide();
                statusBar()->showMessage("Vulkan Ready");
                startSimulation();
            });

        QObject::connect(
            m_vulkanWindow,
            &rtvk::render::GranularVulkanWindow::vulkanError,
            this, [this](const QString &msg) {
                m_statusLabel->setText("Vulkan Error:\n" + msg);
                m_statusLabel->setStyleSheet(
                    "background-color: #2e1a1a; color: #ff6060; "
                    "font-size: 13px; padding: 20px;");
                m_statusLabel->show();
                statusBar()->showMessage("Vulkan Error");
            });

        m_vulkanWindow->initialize(m_vulkanContainer);
    }, Qt::QueuedConnection);
}

MainWindow::~MainWindow() = default;

void MainWindow::startSimulation()
{
    // Configure a small particle pile for real-time CPU simulation
    rtvk::SimParams params;
    params.particleRadius = 0.03f;
    params.constraintIterations = 5;
    params.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    params.domainMin = glm::vec3(-2.0f, 0.0f, -2.0f);
    params.domainMax = glm::vec3(2.0f, 3.0f, 2.0f);

    m_solver = std::make_unique<rtvk::sim::PBDSolver>(params);

    // Push initial positions to renderer
    std::vector<glm::vec3> positions;
    for (const auto &p : m_solver->particles())
    {
        positions.push_back(p.position);
    }
    m_vulkanWindow->updateParticles(positions);

    statusBar()->showMessage(
        QString("Particles: %1  |  PBD Solver running")
            .arg(m_solver->particleCount()));

    // Simulation timer ~60fps
    auto *simTimer = new QTimer(this);
    connect(simTimer, &QTimer::timeout, this, [this]() {
        m_solver->step(1.0f / 60.0f);

        std::vector<glm::vec3> positions;
        positions.reserve(m_solver->particleCount());
        for (const auto &p : m_solver->particles())
        {
            positions.push_back(p.position);
        }
        m_vulkanWindow->updateParticles(positions);
    });
    simTimer->start(16);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    if (m_vulkanWindow)
    {
        m_vulkanWindow->resize();
    }
}

void MainWindow::setupMenuBar()
{
    auto *fm = menuBar()->addMenu(tr("&File"));
    fm->addAction(tr("E&xit"), this, &QWidget::close);
    auto *vm = menuBar()->addMenu(tr("&View"));
    vm->addAction(tr("&Toggle Debug Overlay"));
}

void MainWindow::setupDockWidgets()
{
    auto *pd = new QDockWidget(tr("Parameters"), this);
    pd->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, pd);

    auto *ld = new QDockWidget(tr("LLM Assistant"), this);
    ld->setAllowedAreas(Qt::BottomDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, ld);

    auto *sd = new QDockWidget(tr("Statistics"), this);
    sd->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, sd);
}