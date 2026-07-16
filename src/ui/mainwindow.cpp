#include "mainwindow.h"
#include "render/vulkan_window.h"
#include "sim/pbd_solver.h"
#include <glm/glm.hpp>

#include <QFrame>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QLabel>
#include <QList>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupMenuBar();
    setupCentralLayout();
    statusBar()->showMessage("正在启动...");
    resize(1600, 900);

    QMetaObject::invokeMethod(this, [this]() {
        m_vulkanWindow = new rtvk::render::GranularVulkanWindow(this);

        QObject::connect(
            m_vulkanWindow,
            &rtvk::render::GranularVulkanWindow::vulkanReady,
            this, [this]() {
                m_statusLabel->hide();
                statusBar()->showMessage("Vulkan 已就绪");
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
                statusBar()->showMessage("Vulkan 错误");
            });

        m_vulkanWindow->initialize(m_vulkanContainer);
    }, Qt::QueuedConnection);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupCentralLayout()
{
    auto *central = new QWidget(this);
    central->setObjectName("appSurface");

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(14);

    auto *controlPanel = new QFrame(central);
    controlPanel->setObjectName("controlPanel");
    controlPanel->setFixedWidth(260);

    auto *controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(18, 18, 18, 18);
    controlLayout->setSpacing(10);

    auto *titleLabel = new QLabel("RTVK", controlPanel);
    titleLabel->setObjectName("titleLabel");
    auto *subtitleLabel = new QLabel("颗粒仿真控制", controlPanel);
    subtitleLabel->setObjectName("subtitleLabel");

    m_simulationStateLabel = new QLabel("初始化中", controlPanel);
    m_simulationStateLabel->setObjectName("stateLabel");

    m_runPauseButton = new QPushButton("运行/暂停", controlPanel);
    m_runPauseButton->setObjectName("primaryButton");
    m_runPauseButton->setMinimumHeight(44);
    m_runPauseButton->setEnabled(false);
    connect(m_runPauseButton, &QPushButton::clicked, this, &MainWindow::toggleSimulationPaused);

    auto *resetButton = new QPushButton("重置模拟", controlPanel);
    auto *materialButton = new QPushButton("材质参数", controlPanel);
    auto *debugButton = new QPushButton("调试信息", controlPanel);
    auto *captureButton = new QPushButton("截取画面", controlPanel);
    const QList<QPushButton *> pendingButtons = {
        resetButton,
        materialButton,
        debugButton,
        captureButton,
    };
    for (auto *button : pendingButtons)
    {
        button->setMinimumHeight(38);
        button->setEnabled(false);
    }

    controlLayout->addWidget(titleLabel);
    controlLayout->addWidget(subtitleLabel);
    controlLayout->addSpacing(12);
    controlLayout->addWidget(m_simulationStateLabel);
    controlLayout->addSpacing(8);
    controlLayout->addWidget(m_runPauseButton);
    controlLayout->addWidget(resetButton);
    controlLayout->addWidget(materialButton);
    controlLayout->addWidget(debugButton);
    controlLayout->addWidget(captureButton);
    controlLayout->addStretch();

    auto *viewportFrame = new QFrame(central);
    viewportFrame->setObjectName("viewportFrame");
    auto *viewportLayout = new QVBoxLayout(viewportFrame);
    viewportLayout->setContentsMargins(1, 1, 1, 1);
    viewportLayout->setSpacing(0);

    m_vulkanContainer = new QWidget(viewportFrame);
    m_vulkanContainer->setObjectName("vulkanContainer");
    m_vulkanContainer->setMinimumSize(640, 420);

    m_statusLabel = new QLabel("正在初始化 Vulkan...", m_vulkanContainer);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    auto *vulkanLayout = new QVBoxLayout(m_vulkanContainer);
    vulkanLayout->setContentsMargins(0, 0, 0, 0);
    vulkanLayout->addWidget(m_statusLabel);

    viewportLayout->addWidget(m_vulkanContainer);
    rootLayout->addWidget(controlPanel);
    rootLayout->addWidget(viewportFrame, 1);

    central->setStyleSheet(R"(
        QWidget#appSurface {
            background: #101418;
        }
        QFrame#controlPanel {
            background: #182027;
            border: 1px solid #2b3843;
            border-radius: 8px;
        }
        QLabel#titleLabel {
            color: #f1f5f7;
            font-size: 28px;
            font-weight: 700;
        }
        QLabel#subtitleLabel {
            color: #8fa2ad;
            font-size: 13px;
        }
        QLabel#stateLabel {
            color: #c9d6dc;
            background: #101820;
            border: 1px solid #2a3944;
            border-radius: 6px;
            padding: 9px 10px;
        }
        QPushButton {
            color: #dce7eb;
            background: #22303a;
            border: 1px solid #344550;
            border-radius: 6px;
            padding: 8px 12px;
            text-align: left;
        }
        QPushButton:hover:enabled {
            background: #2b3b46;
            border-color: #496170;
        }
        QPushButton:disabled {
            color: #64747d;
            background: #1a242b;
            border-color: #26333b;
        }
        QPushButton#primaryButton {
            color: #061015;
            background: #72d6a0;
            border-color: #72d6a0;
            font-weight: 700;
            text-align: center;
        }
        QPushButton#primaryButton:hover:enabled {
            background: #86e6b1;
            border-color: #86e6b1;
        }
        QFrame#viewportFrame {
            background: #05070a;
            border: 1px solid #26333b;
            border-radius: 8px;
        }
        QWidget#vulkanContainer {
            background: #080b0f;
        }
        QWidget#vulkanContainer QLabel {
            color: #dce7eb;
            background: #101418;
            font-size: 18px;
        }
    )");

    setCentralWidget(central);
}

void MainWindow::startSimulation()
{
    // 配置用于实时仿真的小型颗粒堆。
    rtvk::SimParams params;
    params.particleRadius = 0.03f;
    params.constraintIterations = 5;
    params.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    params.domainMin = glm::vec3(-2.0f, 0.0f, -2.0f);
    params.domainMax = glm::vec3(2.0f, 3.0f, 2.0f);

    m_solver = std::make_unique<rtvk::sim::PBDSolver>(params);

    // 上传第一帧初始位置。
    std::vector<glm::vec3> positions;
    for (const auto &p : m_solver->particles())
        positions.push_back(p.position);
    m_vulkanWindow->updateParticles(positions);

    // 初始化 GPU 仿真资源。
    m_vulkanWindow->initGpuSimulation(
        (uint32_t)m_solver->particleCount(), params.particleRadius, params);

    // 上传初始颗粒数据到 GPU。
    std::vector<rtvk::render::GpuParticle> gpuParticles;
    gpuParticles.reserve(m_solver->particleCount());
    for (const auto &p : m_solver->particles())
    {
        rtvk::render::GpuParticle gp{};
        gp.posX = p.position.x; gp.posY = p.position.y; gp.posZ = p.position.z;
        gp.velX = p.velocity.x; gp.velY = p.velocity.y; gp.velZ = p.velocity.z;
        gp.predX = p.predictedPosition.x; gp.predY = p.predictedPosition.y; gp.predZ = p.predictedPosition.z;
        gp.invMass = p.invMass; gp.radius = p.radius;
        gpuParticles.push_back(gp);
    }
    m_vulkanWindow->uploadParticleData(gpuParticles);

    m_solver.reset();

    statusBar()->showMessage(
        QString("颗粒: %1  |  GPU Compute + CPU render")
            .arg(m_vulkanWindow->gpuSimParticleCount()));

    // GPU 仿真步进和 CPU 读回分离成成员 timer，便于界面暂停。
    m_simTimer = new QTimer(this);
    connect(m_simTimer, &QTimer::timeout, this, [this]() {
        m_vulkanWindow->gpuSimStep(1.0f / 60.0f);
        auto pos = m_vulkanWindow->gpuSimGetPositions();
        if (!pos.empty())
            m_vulkanWindow->updateParticles(pos);
    });
    m_simTimer->start(16);

    m_simulationReady = true;
    m_simulationPaused = false;
    updateSimulationControls();
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

void MainWindow::toggleSimulationPaused()
{
    if (!m_simulationReady || !m_simTimer)
    {
        return;
    }

    m_simulationPaused = !m_simulationPaused;
    if (m_simulationPaused)
    {
        m_simTimer->stop();
    }
    else
    {
        m_simTimer->start(16);
    }

    updateSimulationControls();
}

void MainWindow::updateSimulationControls()
{
    if (!m_runPauseButton || !m_simulationStateLabel)
    {
        return;
    }

    m_runPauseButton->setEnabled(m_simulationReady);
    if (!m_simulationReady)
    {
        m_simulationStateLabel->setText("初始化中");
        return;
    }

    if (m_simulationPaused)
    {
        m_simulationStateLabel->setText("模拟已暂停");
        statusBar()->showMessage("模拟已暂停");
    }
    else
    {
        m_simulationStateLabel->setText("模拟运行中");
        statusBar()->showMessage(
            QString("颗粒: %1  |  GPU Compute + CPU render")
                .arg(m_vulkanWindow->gpuSimParticleCount()));
    }
}
