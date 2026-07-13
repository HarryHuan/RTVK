#include <QApplication>
#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("RTVK");
    app.setApplicationVersion("0.1.0");

    MainWindow window;
    window.setWindowTitle("RTVK — Granular Simulation & Rendering");
    window.resize(1600, 900);
    window.show();

    return app.exec();
}
