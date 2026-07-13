#include "mainwindow.h"
#include "render/vulkan_window.h"

#include <QMenuBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *statusLabel = new QLabel("Initializing Vulkan...");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("background-color: #1a1a2e; color: #e0e0e0; font-size: 18px;");
    setCentralWidget(statusLabel);

    setupMenuBar();
    setupDockWidgets();
    statusBar()->showMessage("Starting...");
    resize(1600, 900);

    // Vulkan creates its own Win32 window; Qt MainWindow is the control panel
    QMetaObject::invokeMethod(this, [this, statusLabel]() {
        m_vulkanWindow = new rtvk::render::GranularVulkanWindow(this);

        QObject::connect(m_vulkanWindow, &rtvk::render::GranularVulkanWindow::vulkanReady,
            this, [this, statusLabel]() {
                statusLabel->setText("Vulkan Ready — see RTVK Vulkan window");
                statusBar()->showMessage("Vulkan Ready");
            });

        QObject::connect(m_vulkanWindow, &rtvk::render::GranularVulkanWindow::vulkanError,
            this, [this, statusLabel](const QString &msg) {
                statusLabel->setText("Vulkan Error:\n" + msg);
                statusLabel->setStyleSheet("background-color: #2e1a1a; color: #ff6060; font-size: 13px; padding: 20px;");
                statusBar()->showMessage("Vulkan Error");
            });

        m_vulkanWindow->initialize();
    }, Qt::QueuedConnection);
}

MainWindow::~MainWindow() = default;

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