#include "mainwindow.h"
#include <QApplication>
#include "videoprocessor.h"

int main(int argc, char *argv[])
{
    // --- 在创建 QApplication 之前添加这一行 ---
    qRegisterMetaType<QList<RecognitionResult>>("QList<RecognitionResult>");

    QApplication a(argc, argv);
    MainWindow w;
    w.show(); // 在嵌入式设备上通常是全屏显示 w.showFullScreen();
    return a.exec();
}

