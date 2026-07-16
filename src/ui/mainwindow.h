#pragma once

#include <QMainWindow>
#include <memory>
#include "core/config.h"

class QLabel;
class QPushButton;
class QTimer;
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
    void setupCentralLayout();
    void setupMenuBar();
    void startSimulation();
    void toggleSimulationPaused();
    void updateSimulationControls();

    QWidget *m_vulkanContainer = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_simulationStateLabel = nullptr;
    QPushButton *m_runPauseButton = nullptr;
    QTimer *m_simTimer = nullptr;
    rtvk::render::GranularVulkanWindow *m_vulkanWindow = nullptr;
    std::unique_ptr<rtvk::sim::PBDSolver> m_solver;
    bool m_simulationPaused = false;
    bool m_simulationReady = false;
};
