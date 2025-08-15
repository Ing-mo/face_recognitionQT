#include "albumdialog.h"
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QMessageBox>
#include <QDebug>

AlbumDialog::AlbumDialog(const QString &photoPath, QWidget *parent)
    : QDialog(parent), m_photoPath(photoPath)
{
    this->setWindowTitle("相册");
    this->setMinimumSize(780, 460);  

    // --- 创建控件 ---
    m_listWidget = new QListWidget;
    m_listWidget->setFlow(QListView::LeftToRight);    
    m_listWidget->setWrapping(true);                  
    m_listWidget->setViewMode(QListView::IconMode);   
    m_listWidget->setIconSize(QSize(100, 100));       
    m_listWidget->setSpacing(10);                     
    m_listWidget->setFixedWidth(240);                 

    m_imageLabel = new QLabel("请选择一张照片");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background-color: black;");

    m_deleteButton = new QPushButton("删除照片");
    m_closeButton  = new QPushButton("关闭");

    // --- 布局 ---
    QHBoxLayout *mainLayout   = new QHBoxLayout(this);
    QVBoxLayout *rightLayout  = new QVBoxLayout;
    QHBoxLayout *buttonLayout = new QHBoxLayout;

    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addWidget(m_closeButton);

    rightLayout->addWidget(m_imageLabel);
    rightLayout->addLayout(buttonLayout);

    mainLayout->addWidget(m_listWidget);
    mainLayout->addLayout(rightLayout);

    // --- 美化样式 ---
    this->setStyleSheet(R"(
        QDialog { background-color: #2D2D2D; color: #F0F0F0; }
        QListWidget { border: 1px solid #444; }
        QPushButton {
            background-color: #0078D7; color: white; border: 1px solid #444;
            padding: 8px; border-radius: 8px;
        }
        QPushButton:hover { background-color: #005A9E; }
        QPushButton:pressed { background-color: #004578; }
    )");


    // --- 连接信号和槽 ---
    connect(m_listWidget,   &QListWidget::itemClicked, this, &AlbumDialog::onPhotoSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &AlbumDialog::onDeletePhotoButtonClicked);
    connect(m_closeButton,  &QPushButton::clicked, this, &QDialog::accept);

    // --- 加载照片 ---
    loadPhotos();
}

AlbumDialog::~AlbumDialog()
{
}

void AlbumDialog::loadPhotos()
{
    m_listWidget->clear();
    m_imageLabel->setText("请选择一张照片");

    QDir dir(m_photoPath);
    // 只查找 jpg 和 png 文件
    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png";
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Time); 

    QFileInfoList list = dir.entryInfoList();
    for (const QFileInfo &fileInfo : list) {
        QListWidgetItem *item = new QListWidgetItem(QIcon(fileInfo.filePath()), fileInfo.fileName());
        item->setData(Qt::UserRole, fileInfo.filePath()); 
        m_listWidget->addItem(item);
    }

    if(m_listWidget->count() == 0) {
        m_imageLabel->setText("相册为空");
        m_deleteButton->setEnabled(false);
    } else {
        m_deleteButton->setEnabled(true);
    }
}

void AlbumDialog::onPhotoSelected(QListWidgetItem *item)
{
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();
    QPixmap pixmap(filePath);
    if (pixmap.isNull()) {
        m_imageLabel->setText("无法加载图片");
        return;
    }
    // 缩放图片以适应QLabel
    m_imageLabel->setPixmap(pixmap.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void AlbumDialog::onDeletePhotoButtonClicked()
{
    QListWidgetItem *currentItem = m_listWidget->currentItem();
    if (!currentItem) {
        QMessageBox::warning(this, "提示", "请先选择一张要删除的照片。");
        return;
    }

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认删除", "您确定要永久删除这张照片吗？\n" + currentItem->text(),
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QString filePath = currentItem->data(Qt::UserRole).toString();
        QFile file(filePath);
        if (file.remove()) {
            qDebug() << "Deleted:" << filePath;
            // 从列表中移除并刷新
            delete m_listWidget->takeItem(m_listWidget->row(currentItem));
            if (m_listWidget->count() > 0) {
                 m_listWidget->setCurrentRow(0); 
                 onPhotoSelected(m_listWidget->currentItem());
            } else {
                m_imageLabel->setText("相册为空");
                m_deleteButton->setEnabled(false);
            }
        } else {
            QMessageBox::critical(this, "错误", "删除文件失败！\n" + file.errorString());
        }
    }
}
