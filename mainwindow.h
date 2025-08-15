#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "videoprocessor.h"
#include "albumdialog.h" 

#include <QMainWindow>
#include <QThread>
#include <QByteArray>
#include <QList>
#include <QSocketNotifier> 

// 前向声明 MainWindow 类继承自 QMainWindow
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT// Qt宏，用于支持信号和槽

public:
    MainWindow(QWidget *parent = nullptr);  
    ~MainWindow();                          

protected:
    void closeEvent(QCloseEvent *event) override;

public slots:
    void updateFrame(const QByteArray &jpegData, const QList<RecognitionResult> &results);
    void updateStatus(const QString &message);
    void onBrightnessChanged(int value);
//分别为：视频帧数据和识别结果,状态信息字符,亮度滑块

private slots:
    void on_pushButton_clicked();
    void on_albumButton_clicked();
    void on_registerButton_clicked();
    void on_clearDbButton_clicked();
    void handleTerminalInput();
//分别为：拍照按钮，相册按钮，注册人脸按钮，清除数据库按钮，终端输入

private:
    Ui::MainWindow *ui;
    QThread m_workerThread;
    VideoProcessor *m_processor;
    QSocketNotifier *m_stdinNotifier;
   //分别为:Ui::MainWindow 对象的指针，工作线程对象，视频处理器对象，监视终端输入对象
};

#endif // MAINWINDOW_H
