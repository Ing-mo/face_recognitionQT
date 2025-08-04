#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>
#include <QPixmap>
#include <QList>
#include <QByteArray>
#include <opencv2/video/tracking.hpp> // For KalmanFilter

// 包含你的 C API 头文件
extern "C" {
#include "video_manager.h"
#include "face_detector.h"
#include "face_recognizer.h"
}

// 提前声明 RecognitionResult 结构体，以便在 QList 中使用
// 这样可以避免在头文件中包含完整的定义，减少编译依赖
// 不过，由于我们要在信号中使用它，完整的定义是必需的。
// face_recognizer.h 中已经有了定义。

// 为了能在信号槽系统中传递 QList<RecognitionResult>，需要注册这个元类型。
// 在 .h 文件中声明，在 .cpp 或 main.cpp 中注册(qRegisterMetaType)。
Q_DECLARE_METATYPE(QList<RecognitionResult>)


// 用于追踪人脸的内部结构体
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
    Q_OBJECT // 必须包含此宏，以支持信号和槽

public:
    explicit VideoProcessor(QObject *parent = nullptr);
    ~VideoProcessor();

public slots:
    /**
     * @brief 主要的处理循环。
     * 这个槽函数应该在工作线程启动后被调用。
     */
    void process();

    /**
     * @brief 请求停止处理循环。
     * 这是一个线程安全的槽，可以从任何线程调用。
     */
    void stop();

    /**
     * @brief 设置摄像头的亮度。
     * 这是一个线程安全的槽，可以从主线程调用。
     * @param value 亮度值 (通常为 0-255)。
     */
    void setBrightness(int value);

signals:
    /**
     * @brief 当一帧视频处理完毕后发射此信号。
     * @param jpegData 包含原始摄像头JPEG数据的字节数组。
     * @param results 一个列表，包含了所有被识别或追踪到的人脸的结果（位置、名字等）。
     */
    void frameProcessed(const QByteArray &jpegData, const QList<RecognitionResult> &results);

    /**
     * @brief 发射一条状态信息，用于在UI上显示。
     * @param message 要显示的状态文本。
     */
    void statusMessage(const QString &message);

private:
    VideoCaptureDevice *m_cam = nullptr;
    volatile bool m_stopped = false;

    // 从你旧的 main.cpp 中移植过来的变量
    std::vector<FaceTracker> m_trackers;
    int m_nextTrackerId = 0;

    // 私有辅助函数
    void initKalmanFilter(cv::KalmanFilter& kf, const FaceRect& initial_rect);
    float calculate_iou(const FaceRect& r1, const FaceRect& r2);
};

#endif // VIDEOPROCESSOR_H
