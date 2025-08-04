#include "videoprocessor.h"
#include <QPainter>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <vector>
// --- 添加缺失的Qt头文件 ---
#include <QThread> // <--- 添加这一行
// --- 添加缺失的头文件 ---
#include <sys/ioctl.h>  // 为了 ioctl

// --- 从你旧的main.cpp中复制的定义 ---
#define TRACKER_LIFESPAN 25
#define MAX_TRACKERS 5
#define RECOGNITION_INTERVAL 10 // 增加识别间隔，给追踪和检测更多机会
#define DETECTION_INTERVAL 3   // 每3帧检测一次人脸
#define IOU_MATCH_THRESHOLD 0.3f


VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent)
{
    // 初始化所有模块
    const char *cascade_file = "/root/lbpcascade_frontalface.xml";
    const char *onnx_model_file = "/root/models/mobilefacenet.onnx";
    const char *database_file = "/root/face_database.db";

    if (face_detector_init(cascade_file) != 0) {
        emit statusMessage("人脸检测器初始化失败!");
        return;
    }
    if (face_recognizer_init(onnx_model_file, database_file) != 0) {
        face_detector_cleanup();
        emit statusMessage("人脸识别器初始化失败!");
        return;
    }

    // 注册逻辑保持不变
    const char* person1_image_paths[] = { "/root/face_database/yy/001.jpg", "/root/face_database/yy/002.jpg", "/root/face_database/yy/003.jpg", "/root/face_database/yy/004.jpg", "/root/face_database/yy/005.jpg", "/root/face_database/yy/006.jpg", "/root/face_database/yy/007.jpg", "/root/face_database/yy/008.jpg", "/root/face_database/yy/009.jpg", "/root/face_database/yy/010.jpg" };
    int num_images = sizeof(person1_image_paths) / sizeof(person1_image_paths[0]);
    if (num_images >= 3) {
        face_recognizer_register_faces_from_paths(person1_image_paths, num_images, "yy");
    }

    m_trackers.resize(MAX_TRACKERS);
}

VideoProcessor::~VideoProcessor()
{
    if (m_cam) {
        video_capture_cleanup(m_cam);
    }
    face_recognizer_cleanup();
    face_detector_cleanup();
    qDebug() << "VideoProcessor cleaned up.";
}

void VideoProcessor::process()
{
    m_cam = video_capture_init("/dev/video1", 640, 480, V4L2_PIX_FMT_MJPEG);
    if (!m_cam) {
        emit statusMessage("摄像头初始化失败!");
        return;
    }

    emit statusMessage("视频流已启动...");
    qDebug() << "DEBUG: Video stream started, entering main loop...";

    int frame_count = 0;

    while (!m_stopped) {
        VideoFrame *frame = video_capture_get_frame(m_cam);
        if (!frame) {
             qDebug() << "DEBUG: Loop" << frame_count << "- Failed to get frame, continuing.";
             continue;
        }

        // 1. 预测 (每一帧都做，开销极小)
        for (auto& tracker : m_trackers) {
            if (tracker.active) {
                cv::Mat prediction = tracker.kf.predict();
                tracker.rect.width = prediction.at<float>(2);
                tracker.rect.height = prediction.at<float>(3);
                tracker.rect.x = prediction.at<float>(0) - tracker.rect.width / 2;
                tracker.rect.y = prediction.at<float>(1) - tracker.rect.height / 2;
            }
        }

        // 2. 检测 (周期性进行以提升流畅度)
        std::vector<FaceRect> detected_faces;
        if (frame_count % DETECTION_INTERVAL == 0) {
            FaceRect *detected_faces_ptr = nullptr;
            int num_detected = face_detector_detect((unsigned char*)frame->start, frame->length, &detected_faces_ptr);
            if (num_detected > 0) {
                detected_faces.assign(detected_faces_ptr, detected_faces_ptr + num_detected);
            }
            if(detected_faces_ptr) free(detected_faces_ptr);
        }

        // 3. 关联与更新 (只有在当前帧进行了检测时才执行)
        if (!detected_faces.empty()) {
            std::vector<bool> detection_used(detected_faces.size(), false);
            for (auto& tracker : m_trackers) {
                if (!tracker.active) continue;

                float best_iou = 0.0f;
                int best_match_idx = -1;
                for (size_t i = 0; i < detected_faces.size(); ++i) {
                    if (detection_used[i]) continue;
                    float iou = calculate_iou(tracker.rect, detected_faces[i]);
                    if (iou > best_iou) {
                        best_iou = iou;
                        best_match_idx = i;
                    }
                }

                if (best_iou > IOU_MATCH_THRESHOLD) {
                    const auto& matched_rect = detected_faces[best_match_idx];
                    cv::Mat measurement = (cv::Mat_<float>(4, 1) <<
                        matched_rect.x + matched_rect.width / 2.0f,
                        matched_rect.y + matched_rect.height / 2.0f,
                        matched_rect.width,
                        matched_rect.height);
                    tracker.kf.correct(measurement);
                    tracker.lifespan = TRACKER_LIFESPAN; // 匹配成功，重置生命
                    detection_used[best_match_idx] = true;
                } else {
                    tracker.lifespan--; // 未匹配到，减少生命
                }
            }
        } else {
             // 在没有进行检测的帧，也减少生命周期
             for (auto& tracker : m_trackers) {
                if (tracker.active) tracker.lifespan--;
            }
        }

        // 4. 处理识别任务 (使用更长的间隔，并且只有在检测到人脸时)
        if (frame_count % RECOGNITION_INTERVAL == 0 && !detected_faces.empty()) {
            face_recognizer_submit_task(
                (const unsigned char*)frame->start, frame->length,
                detected_faces.data(), detected_faces.size()
            );
        }

        // 5. 获取识别结果并更新追踪器信息
        RecognitionResult* rec_results_ptr = nullptr;
        int num_results = face_recognizer_get_results(&rec_results_ptr);
        if (num_results > 0) {
            for (int i = 0; i < num_results; ++i) {
                const auto& result = rec_results_ptr[i];
                float best_iou = 0.0f;
                FaceTracker* best_tracker = nullptr;
                for (auto& tracker : m_trackers) {
                    if (tracker.active) {
                        float iou = calculate_iou(tracker.rect, result.rect);
                        if (iou > best_iou) {
                            best_iou = iou;
                            best_tracker = &tracker;
                        }
                    }
                }

                if (best_iou > IOU_MATCH_THRESHOLD && strcmp(result.name, "Unknown") != 0) {
                    strncpy(best_tracker->name, result.name, 63);
                    best_tracker->score = result.score;
                } else if (strcmp(result.name, "Unknown") != 0) { // 未匹配到，但识别是已知人脸，创建新追踪器
                     for (auto& tracker : m_trackers) {
                        if (!tracker.active) {
                            initKalmanFilter(tracker.kf, result.rect);
                            tracker.active = 1;
                            tracker.rect = result.rect;
                            strncpy(tracker.name, result.name, 63);
                            tracker.score = result.score;
                            tracker.lifespan = TRACKER_LIFESPAN;
                            tracker.id = m_nextTrackerId++;
                            qDebug() << ">>> Created new tracker" << tracker.id << "for" << tracker.name;
                            break;
                        }
                    }
                }
            }
            free(rec_results_ptr);
        }

        // 6. 准备最终要发送给主线程的结果
        QList<RecognitionResult> final_results;
        QString currentStatus = "正在监控...";
        for (auto& tracker : m_trackers) {
            if (tracker.active && tracker.lifespan > 0) {
                RecognitionResult res;
                res.rect = tracker.rect;
                strncpy(res.name, tracker.name, 63);
                res.name[63] = '\0';
                res.score = tracker.score;
                final_results.append(res);

                if (strcmp(tracker.name, "Tracking...") != 0 && strcmp(tracker.name, "Unknown") != 0) {
                     currentStatus = QString("检测到: %1").arg(tracker.name);
                }
            } else {
                 if(tracker.active) {
                     qDebug() << ">>> Tracker" << tracker.id << "lost.";
                 }
                 tracker.active = 0;
            }
        }

        // 7. 发射包含原始数据和结果的信号
        QByteArray jpegData((const char*)frame->start, frame->length);
        emit frameProcessed(jpegData, final_results);
        emit statusMessage(currentStatus);

        video_capture_release_frame(m_cam, frame);
        frame_count++;
        QThread::msleep(20); // 稍微增加延时，给CPU一点喘息时间
    }
    qDebug() << "DEBUG: Main loop exited.";
}

void VideoProcessor::stop()
{
    m_stopped = true;
}

void VideoProcessor::setBrightness(int value)
{
    if (!m_cam) return;

    struct v4l2_control ctl;
    ctl.id = V4L2_CID_BRIGHTNESS;
    ctl.value = value;

    if (ioctl(m_cam->fd, VIDIOC_S_CTRL, &ctl) < 0) {
        qWarning("设置亮度失败");
    } else {
        qDebug() << "亮度设置为" << value;
    }
}

// --- 卡尔曼滤波器辅助函数 ---
void VideoProcessor::initKalmanFilter(cv::KalmanFilter& kf, const FaceRect& initial_rect) {
    kf.init(8, 4, 0, CV_32F);

    kf.transitionMatrix = (cv::Mat_<float>(8, 8) <<
        1, 0, 0, 0, 1, 0, 0, 0,
        0, 1, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 0, 0, 0, 1, 0,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 1);

    kf.measurementMatrix = cv::Mat::eye(4, 8, CV_32F);
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));

    kf.statePost.at<float>(0) = initial_rect.x + initial_rect.width / 2.0f;
    kf.statePost.at<float>(1) = initial_rect.y + initial_rect.height / 2.0f;
    kf.statePost.at<float>(2) = initial_rect.width;
    kf.statePost.at<float>(3) = initial_rect.height;
    kf.statePost.at<float>(4) = 0;
    kf.statePost.at<float>(5) = 0;
    kf.statePost.at<float>(6) = 0;
    kf.statePost.at<float>(7) = 0;
}

float VideoProcessor::calculate_iou(const FaceRect& r1, const FaceRect& r2) {
    int x1 = std::max(r1.x, r2.x);
    int y1 = std::max(r1.y, r2.y);
    int x2 = std::min(r1.x + r1.width, r2.x + r2.width);
    int y2 = std::min(r1.y + r1.height, r2.y + r2.height);
    int intersection_w = std::max(0, x2 - x1);
    int intersection_h = std::max(0, y2 - y1);
    int intersection_area = intersection_w * intersection_h;
    int union_area = r1.width * r1.height + r2.width * r2.height - intersection_area;
    return union_area > 0 ? (float)intersection_area / union_area : 0.0f;
}
