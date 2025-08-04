#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>

// 前向声明 VideoProcessor 类，可以减少头文件依赖
// 如果槽函数的参数是自定义类型，则需要包含完整的头文件
#include "videoprocessor.h"

// QByteArray 和 QList 已经通过 videoprocessor.h 间接包含了，但显式包含是好习惯
#include <QByteArray>
#include <QList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT // 必须包含此宏，以支持信号和槽

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    /**
     * @brief 重写窗口关闭事件，以安全地停止后台线程。
     * @param event 关闭事件对象。
     */
    void closeEvent(QCloseEvent *event) override;

public slots:
    /**
     * @brief 响应后台线程的 frameProcessed 信号。
     * 负责将接收到的图像数据和识别结果绘制并更新到UI上。
     * @param jpegData 原始的JPEG图像数据。
     * @param results 识别和追踪结果的列表。
     */
    void updateFrame(const QByteArray &jpegData, const QList<RecognitionResult> &results);

    /**
     * @brief 响应后台线程的 statusMessage 信号。
     * @param message 要显示在状态栏上的文本。
     */
    void updateStatus(const QString &message);

    /**
     * @brief 响应亮度滑块的 valueChanged 信号。
     * @param value 滑块的当前值。
     */
    void onBrightnessChanged(int value);

private:
    // 指向由Qt Designer生成的UI类的指针
    Ui::MainWindow *ui;

    // 用于运行后台任务的工作线程
    QThread m_workerThread;

    // 指向在后台线程中运行的视频处理对象
    VideoProcessor *m_processor;
};

#endif // MAINWINDOW_H
