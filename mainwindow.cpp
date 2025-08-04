#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCloseEvent>
#include <QDebug>
#include <QPainter>
// --- 添加缺失的Qt控件头文件 ---
#include <QSlider> // <--- 添加这一行
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("i.MX6Ull 人脸识别");

    // 1. 创建 VideoProcessor 实例
    m_processor = new VideoProcessor();
    // 2. 将 processor 移动到工作线程
    m_processor->moveToThread(&m_workerThread);

    // 3. 连接信号与槽 (使用最终确定的、正确的信号和槽名称)

    // 当线程启动时，开始处理
    connect(&m_workerThread, &QThread::started, m_processor, &VideoProcessor::process);

    // 当处理完成一帧时，更新UI (使用新的信号和槽)
    connect(m_processor, &VideoProcessor::frameProcessed, this, &MainWindow::updateFrame);

    // 更新状态栏信息
    connect(m_processor, &VideoProcessor::statusMessage, this, &MainWindow::updateStatus);

    // 线程结束后，自动删除 processor 对象
    connect(&m_workerThread, &QThread::finished, m_processor, &QObject::deleteLater);

    // 连接UI控件
    connect(ui->brightnessSlider, &QSlider::valueChanged, this, &MainWindow::onBrightnessChanged);

    // 4. 启动线程
    m_workerThread.start();

    // 初始化亮度滑块 (假设亮度范围是0-255，默认128)
    ui->brightnessSlider->setRange(-255, 255);
    ui->brightnessSlider->setValue(0);
}

MainWindow::~MainWindow()
{
    // 在窗口关闭时，我们通过 closeEvent 确保线程已经停止，
    // deleteLater 会在事件循环中安全删除 ui，所以这里通常是空的。
    delete ui;
}

// 这是响应 closeEvent 的实现，确保安全退出
void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "Closing application...";
    if (m_workerThread.isRunning()) {
        // 请求停止后台循环
        QMetaObject::invokeMethod(m_processor, "stop", Qt::QueuedConnection);
        // 退出线程的事件循环
        m_workerThread.quit();
        // 等待线程完全结束，最多等待3秒
        m_workerThread.wait(3000);
    }
    event->accept(); // 接受关闭事件，窗口现在可以关闭了
}

// 这是 updateFrame 槽的正确实现，参数与 .h 文件中声明的完全匹配
void MainWindow::updateFrame(const QByteArray &jpegData, const QList<RecognitionResult> &results)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(jpegData, "JPEG")) {
        qWarning() << "Failed to load pixmap from data in main thread!";
        return;
    }

    // 在主线程中进行绘图
    QPainter painter(&pixmap);
    for (const auto &result : results) {
        // 根据识别结果选择颜色
        QColor color = (strcmp(result.name, "Tracking...") != 0 && strcmp(result.name, "Unknown") != 0) ? Qt::green : Qt::red;

        painter.setPen(QPen(color, 2)); // 2像素宽的画笔
        painter.drawRect(result.rect.x, result.rect.y, result.rect.width, result.rect.height);

        // 在框上方绘制名字
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(result.rect.x, result.rect.y - 5, QString(result.name));
    }

    // 将最终绘制好的图像显示出来
    ui->videoLabel->setPixmap(pixmap);
}

// 这是 updateStatus 槽的实现
void MainWindow::updateStatus(const QString &message)
{
    ui->statusLabel->setText(message);
}

// 这是 onBrightnessChanged 槽的实现
void MainWindow::onBrightnessChanged(int value)
{
    // 使用 QMetaObject::invokeMethod 安全地跨线程调用 processor 的槽函数
    QMetaObject::invokeMethod(m_processor, "setBrightness", Qt::QueuedConnection, Q_ARG(int, value));
}
