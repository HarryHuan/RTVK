#pragma once

#include <QMainWindow>

class QLabel;
namespace rtvk::render { class GranularVulkanWindow; }

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

    QWidget *m_vulkanContainer = nullptr;
    QLabel *m_statusLabel = nullptr;
    rtvk::render::GranularVulkanWindow *m_vulkanWindow = nullptr;
};