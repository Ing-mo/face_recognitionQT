// --- START OF FILE mainwindow.h ---
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include "videoprocessor.h"
#include <QByteArray>
#include <QList>

// [新增]
#include <QSocketNotifier>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

public slots:
    void updateFrame(const QByteArray &jpegData, const QList<RecognitionResult> &results);
    void updateStatus(const QString &message);
    void onBrightnessChanged(int value);

private slots:
    void on_pushButton_clicked();
    void on_registerButton_clicked();
    void on_clearDbButton_clicked();

    // [新增] 用于处理终端输入的槽函数
    void handleTerminalInput();

private:
    Ui::MainWindow *ui;
    QThread m_workerThread;
    VideoProcessor *m_processor;

    // [新增] 用于监视标准输入的Notifier
    QSocketNotifier *m_stdinNotifier;
};

#endif // MAINWINDOW_H
