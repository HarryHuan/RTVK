#pragma once

#include <QMainWindow>
#include <memory>
#include "core/config.h"

class QLabel;
namespace rtvk::render { class GranularVulkanWindow; }
namespace rtvk::sim { class PBDSolver; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void setupMenuBar();
    void setupDockWidgets();
    void startSimulation();

    QWidget *m_vulkanContainer = nullptr;
    QLabel *m_statusLabel = nullptr;
    rtvk::render::GranularVulkanWindow *m_vulkanWindow = nullptr;
    std::unique_ptr<rtvk::sim::PBDSolver> m_solver;
};