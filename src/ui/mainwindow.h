#pragma once

#include <QMainWindow>

namespace rtvk::render { class GranularVulkanWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void setupMenuBar();
    void setupDockWidgets();

    rtvk::render::GranularVulkanWindow *m_vulkanWindow = nullptr;
};
