#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>
#include <QPixmap>
#include <QList>
#include <QByteArray>
#include <QStringList>
#include <atomic>
#include <opencv2/video/tracking.hpp>
#include <QTimer> // <-- [新增] 引入QTimer头文件

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
#include "video_manager.h"
#include "face_detector.h"
#include "face_recognizer.h"
}

Q_DECLARE_METATYPE(QList<RecognitionResult>)

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
    void startProcessing();    // <-- [修改] 用于启动整个流程和定时器
    void processSingleFrame(); // <-- [新增] 由定时器触发，处理单帧图像
    void stop();
    void setBrightness(int value);
    void takePhoto();
    void startRegistration(const QString &name);
    void clearDatabase();


signals:
    void frameProcessed(const QByteArray &jpegData, const QList<RecognitionResult> &results);
    void statusMessage(const QString &message);
    void finished(); // <-- [新增] 可以用来通知主线程处理已结束

private:
    QTimer *m_timer = nullptr; // <-- [新增] 定时器指针
    VideoCaptureDevice *m_cam = nullptr;
    volatile bool m_stopped = false;
    std::vector<FaceTracker> m_trackers;
    int m_nextTrackerId = 0;
    int m_frameCounter = 0; // <-- [新增] 用于替代局部变量的帧计数器

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
