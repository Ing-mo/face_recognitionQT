#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "albumdialog.h"

#include <QCloseEvent>
#include <QDebug>
#include <QPainter>
#include <QSlider>  
#include <QMessageBox>
#include <QTextStream>
#include <unistd.h> 

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("i.MX6Ull 人脸识别系统 (核心功能演示)");

    m_processor = new VideoProcessor();
    m_processor->moveToThread(&m_workerThread);

    // --- 连接信号与槽 ---
    connect(&m_workerThread, &QThread::started, m_processor, &VideoProcessor::startProcessing);
    connect(&m_workerThread, &QThread::finished, m_processor, &QObject::deleteLater);

    connect(m_processor, &VideoProcessor::frameProcessed, this, &MainWindow::updateFrame);
    connect(m_processor, &VideoProcessor::statusMessage, this, &MainWindow::updateStatus);

    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::on_pushButton_clicked);
    connect(ui->albumButton, &QPushButton::clicked, this, &MainWindow::on_albumButton_clicked);
    connect(ui->registerButton, &QPushButton::clicked, this, &MainWindow::on_registerButton_clicked);
    connect(ui->clearDbButton, &QPushButton::clicked, this, &MainWindow::on_clearDbButton_clicked);
    connect(ui->brightnessSlider, &QSlider::valueChanged, this, &MainWindow::onBrightnessChanged);

    // --- 初始化标准输入监视器 ---
    m_stdinNotifier = new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
    connect(m_stdinNotifier, &QSocketNotifier::activated, this, &MainWindow::handleTerminalInput);
    m_stdinNotifier->setEnabled(false);

    // --- 初始化UI控件 ---
    ui->brightnessSlider->setRange(-100, 100);
    ui->brightnessSlider->setValue(0);
    qDebug() << "应用程序已启动。";
    qInfo() << "\n======================================================";
    qInfo() << "UI界面已在LCD上显示。";
    qInfo() << "注册等操作请在终端中按提示输入。";
    qInfo() << "======================================================";

    // --- 应用QSS进行界面美化 ---
    this->setStyleSheet(R"(
        /* 全局样式 */
        QWidget {
            background-color: #2D2D2D;
            color: #F0F0F0;
            font-size: 14px;
        }

        /* 按钮样式 */
        QPushButton {
            background-color: #0078D7;
            color: white;
            border: 1px solid #444;
            padding: 8px;
            border-radius: 8px; /* 让按钮也更圆润 */
            outline: none;      /* 移除焦点时的虚线框 */
        }
        QPushButton:hover {
            background-color: #005A9E;
        }
        QPushButton:pressed {
            background-color: #004578;
            border: 1px solid #888;
        }

        /* 标签样式 */
        QLabel {
            background: transparent;
        }
        #statusLabel {
            font-weight: bold;
            color: #33CC33;
        }

        /* 滑块样式 */
        QSlider::groove:horizontal {
            border: 1px solid #4A4A4A;
            height: 8px;          /* 增加凹槽高度，使其更明显 */
            background: #5A5A5A;
            margin: 2px 0;
            border-radius: 4px;   /*  让凹槽两端也变成圆形 */
        }

        QSlider::handle:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #888, stop:1 #ddd); /* <-- 增加渐变，更有质感 */
            border: 1px solid #5c5c5c;
            width: 22px;          /* 增加手柄宽度 */
            height: 22px;         /* 增加手柄高度 */
            margin: -8px 0;       /* 调整margin以保持垂直居中 */
            border-radius: 11px;  /* 核心：让手柄变成一个完美的圆形 (半径为 宽度/2) */
        }
    )");

    m_workerThread.start();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// 创建相册对话框，并传入照片路径
void MainWindow::on_albumButton_clicked()
{
    qDebug() << "'查看相册' button clicked.";
    AlbumDialog albumDialog("/root/photos/", this);
    albumDialog.exec(); 
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "正在关闭应用程序...";
    if (m_workerThread.isRunning()) {
        QMetaObject::invokeMethod(m_processor, "stop", Qt::QueuedConnection);
        m_workerThread.quit();
        if (!m_workerThread.wait(3000)) {
            qWarning("后台线程无法在3秒内正常退出，将强制终止。");
            m_workerThread.terminate();//强制终止
            m_workerThread.wait();
        }
    }
    event->accept();
}

void MainWindow::updateFrame(const QByteArray &jpegData, const QList<RecognitionResult> &results)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(jpegData, "JPEG")) {
        qWarning() << "主线程加载pixmap失败!";
        return;
    }

    QPainter painter(&pixmap);
    for (const auto &result : results) {
        QColor color = Qt::red; // 默认为红色 (Unknown)
        if (strcmp(result.name, "Positioning...") == 0) {
            color = Qt::yellow; // 注册时为黄色
        } else if (strcmp(result.name, "Tracking...") != 0 && strcmp(result.name, "Unknown") != 0) {
            color = Qt::green; // 识别成功为绿色
        }

        painter.setPen(QPen(color, 2));
        painter.drawRect(result.rect.x, result.rect.y, result.rect.width, result.rect.height);

        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(result.rect.x, result.rect.y - 5, QString(result.name));
    }
    ui->videoLabel->setPixmap(pixmap.scaled(ui->videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::updateStatus(const QString &message)
{
    ui->statusLabel->setText(message);
}

void MainWindow::onBrightnessChanged(int value)
{
    ui->brightnessLabel->setText(QString("亮度: %1").arg(value));
    QMetaObject::invokeMethod(m_processor, "setBrightness", Qt::QueuedConnection, Q_ARG(int, value));
}

void MainWindow::on_pushButton_clicked()
{
    qDebug() << "'拍照' button clicked.";
    QMetaObject::invokeMethod(m_processor, "takePhoto", Qt::QueuedConnection);
}

void MainWindow::on_registerButton_clicked()
{
    qDebug() << "'注册' button clicked.";

    // 在终端打印提示信息
    qInfo().noquote() << "\n[INPUT REQUIRED] Please enter the name for registration in this terminal and press Enter:";
    updateStatus("等待终端输入姓名...");

    // 启用标准输入监视器
    m_stdinNotifier->setEnabled(true);
}

void MainWindow::on_clearDbButton_clicked()
{
    qDebug() << "'清空数据库' button clicked.";
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认操作", "您确定要清空所有已注册的人脸数据吗？\n此操作不可恢复！",
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        qDebug() << "User confirmed to clear database.";
        QMetaObject::invokeMethod(m_processor, "clearDatabase", Qt::QueuedConnection);
    } else {
        qDebug() << "User cancelled database clearing.";
    }
}

void MainWindow::handleTerminalInput()
{
    m_stdinNotifier->setEnabled(false);

    QTextStream stream(stdin);
    QString name = stream.readLine().trimmed();

    if (!name.isEmpty()) {
        qInfo().noquote() << "[OK] Name received:" << name << ". Starting registration process on the device...";
        QMetaObject::invokeMethod(m_processor, "startRegistration", Qt::QueuedConnection, Q_ARG(QString, name));
    } else {
        qWarning() << "[CANCELLED] Empty name received. Registration cancelled.";
        updateStatus("注册已取消");
    }
}
