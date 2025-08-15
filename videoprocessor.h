#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>
#include <QPixmap>
#include <QList>
#include <QByteArray>
#include <QStringList>
#include <QTimer> 

#include <atomic>
#include <opencv2/video/tracking.hpp>
// POSIX C 头文件
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
#include "video_manager.h"
#include "face_detector.h"
#include "face_recognizer.h"
}

//声明自定义类型qRegisterMetaType
Q_DECLARE_METATYPE(QList<RecognitionResult>)
// 封装一个被追踪的人脸的所有信息
struct FaceTracker {                
    int active = 0;                   
    FaceRect rect = {0,0,0,0};      
    char name[64] = "Tracking...";  
    float score = 0.0f;                 
    int lifespan = 0;               
    int id = -1;                    
    cv::KalmanFilter kf;            
};

class VideoProcessor : public QObject
{
    Q_OBJECT

public:
    explicit VideoProcessor(QObject *parent = nullptr);
    ~VideoProcessor();

public slots:
    void startProcessing();                        
    void processSingleFrame();                      
    void stop();                                    
    void setBrightness(int value);                
    void takePhoto();                               
    void startRegistration(const QString &name);
    void clearDatabase();                           

signals:
    void frameProcessed(const QByteArray &jpegData, const QList<RecognitionResult> &results);
    void statusMessage(const QString &message);    
    void finished();    

private:
    QTimer *m_timer = nullptr; 
    VideoCaptureDevice *m_cam = nullptr;    
    volatile bool m_stopped = false;        
    std::vector<FaceTracker> m_trackers;   
    int m_nextTrackerId = 0;                
    int m_frameCounter = 0;                 

    QByteArray m_lastFrameJpeg;             
    std::atomic<bool> m_registrationMode{false};    
    QString m_registrationName;             
    int m_photosToTake;                     
    QStringList m_takenPhotoPaths;          
    int m_regCaptureInterval;              

    void initKalmanFilter(cv::KalmanFilter& kf, const FaceRect& initial_rect); 
    float calculate_iou(const FaceRect& r1, const FaceRect& r2);   
    void handleRegistration(VideoFrame *frame, const std::vector<FaceRect> &detected_faces);
    void cleanupRegistration(bool success);
};

#endif // VIDEOPROCESSOR_H
