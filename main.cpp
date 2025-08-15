#include "mainwindow.h"
#include "videoprocessor.h" 
#include <QApplication>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<RecognitionResult>>("QList<RecognitionResult>");
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}

