#include "mainwindow.h"
#include "render/vulkan_window.h"

#include <QMenuBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Container widget that will host the Vulkan child HWND
    m_vulkanContainer = new QWidget();
    m_vulkanContainer->setMinimumSize(400, 300);

    // Status label overlay inside container
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

    // Delay Vulkan init so MainWindow has a valid native HWND
    QMetaObject::invokeMethod(this, [this]() {
        m_vulkanWindow = new rtvk::render::GranularVulkanWindow(this);

        QObject::connect(m_vulkanWindow, &rtvk::render::GranularVulkanWindow::vulkanReady,
            this, [this]() {
                m_statusLabel->hide();   // hide overlay, show Vulkan
                statusBar()->showMessage("Vulkan Ready");
            });

        QObject::connect(m_vulkanWindow, &rtvk::render::GranularVulkanWindow::vulkanError,
            this, [this](const QString &msg) {
                m_statusLabel->setText("Vulkan Error:\n" + msg);
                m_statusLabel->setStyleSheet(
                    "background-color: #2e1a1a; color: #ff6060; font-size: 13px; padding: 20px;");
                m_statusLabel->show();
                statusBar()->showMessage("Vulkan Error");
            });

        m_vulkanWindow->initialize(m_vulkanContainer);
    }, Qt::QueuedConnection);
}

MainWindow::~MainWindow() = default;

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    if (m_vulkanWindow)
        m_vulkanWindow->resize();
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