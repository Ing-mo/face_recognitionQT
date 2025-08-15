#ifndef ALBUMDIALOG_H
#define ALBUMDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class AlbumDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AlbumDialog(const QString &photoPath, QWidget *parent = nullptr);
    ~AlbumDialog();

private slots:
    void onPhotoSelected(QListWidgetItem *item);    
    void onDeletePhotoButtonClicked();              

private:
    void loadPhotos();
    QString m_photoPath;         
    QListWidget *m_listWidget;   
    QLabel *m_imageLabel;        
    QPushButton *m_deleteButton; 
    QPushButton *m_closeButton;  
};

#endif // ALBUMDIALOG_H
