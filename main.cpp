#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show(); // 在嵌入式设备上通常是全屏显示 w.showFullScreen();
    return a.exec();
}

